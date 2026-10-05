"""Opt-in localhost control API. Serial I/O stays in the existing Qt worker."""
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import secrets
import threading
import time
from urllib.parse import urlparse, parse_qs
from PySide6.QtCore import QObject, Signal, Slot, QTimer, Qt
from pid_tuning import KEYS, encode_config
from session_record import SessionRecord


class LiveControl(QObject):
    requested = Signal(object)

    def __init__(self, bridge, discovery, record, port=0):
        super().__init__(bridge)
        self.bridge = bridge
        self.token = secrets.token_urlsafe(32)
        self.lock = threading.Lock()
        self.events = deque(maxlen=20000)
        self.counter = 0
        self.epoch = 0
        self.last_device_t = None
        self.latest = None
        self.device_info = {}
        self.latest_time = None
        self.snapshot = {}
        self.discovery = Path(discovery)
        self.record = Path(record)
        self.record.parent.mkdir(parents=True, exist_ok=True)
        self.writer = SessionRecord(self.record.parent)
        self.record=self.writer.path
        self.requested.connect(self.execute, Qt.ConnectionType.QueuedConnection)
        api = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_): pass
            def reply(self, code, value):
                payload = json.dumps(value, ensure_ascii=False, allow_nan=False).encode('utf-8')
                self.send_response(code)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.send_header('Cache-Control', 'no-store')
                self.send_header('Content-Length', str(len(payload)))
                self.end_headers()
                try: self.wfile.write(payload)
                except (BrokenPipeError, ConnectionResetError): pass
            def authorized(self):
                # No browser cross-origin mutations; token never appears in URLs.
                return (not self.headers.get('Origin') and
                        secrets.compare_digest(self.headers.get('Authorization', ''), 'Bearer '+api.token))
            def do_GET(self):
                if not self.authorized(): return self.reply(401, {'error':'unauthorized'})
                path=urlparse(self.path)
                if path.path == '/v1/state':
                    with api.lock:
                        result=dict(api.snapshot)
                        result['telemetry_age_ms'] = (round((time.monotonic()-api.latest_time)*1000)
                                                      if api.latest_time is not None else None)
                    return self.reply(200, result)
                if path.path == '/v1/events':
                    try:
                        args=parse_qs(path.query)
                        after=int(args.get('after',['0'])[0])
                        limit=int(args.get('limit',['1000'])[0])
                        if after<0 or not 1<=limit<=20000: raise ValueError()
                    except ValueError: return self.reply(400,{'error':'invalid cursor/limit'})
                    with api.lock:
                        first=api.events[0]['id'] if api.events else api.counter+1
                        result=[e for e in api.events if e['id']>after][:limit]
                    return self.reply(200,{'events':result,'next':result[-1]['id'] if result else after,
                                           'gap':bool(after and after<first-1)})
                return self.reply(404,{'error':'unknown endpoint'})
            def do_POST(self):
                if not self.authorized(): return self.reply(401, {'error':'unauthorized'})
                try:
                    size=int(self.headers.get('Content-Length','0'))
                    if not 0<size<=4096: raise ValueError('invalid body length')
                    body=json.loads(self.rfile.read(size))
                    if not isinstance(body,dict): raise ValueError('JSON object required')
                except (ValueError,UnicodeError) as exc: return self.reply(400,{'error':str(exc)})
                item={'path':urlparse(self.path).path,'body':body,'done':threading.Event(),
                      'cancelled':False,'guard':threading.Lock()}
                api.requested.emit(item)
                if not item['done'].wait(3):
                    with item['guard']: item['cancelled']=True
                    return self.reply(504,{'error':'Qt dispatch timeout; read state before retry'})
                return self.reply(item['code'],item['result'])

        self.server=ThreadingHTTPServer(('127.0.0.1',port),Handler)
        self.server.daemon_threads=True
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True)
        self.refresh()
        self.timer=QTimer(self);self.timer.setInterval(100);self.timer.timeout.connect(self.refresh);self.timer.start()
        self.thread.start()
        self.discovery.parent.mkdir(parents=True,exist_ok=True)
        self.discovery.write_text(json.dumps({'url':f'http://127.0.0.1:{self.server.server_port}',
                                              'token':self.token,'record':str(self.record.resolve())}),encoding='utf-8')
        bridge._log(f'实时调参接口已开启：127.0.0.1:{self.server.server_port}；记录 {self.record.name}')

    def refresh(self):
        b=self.bridge
        with self.lock:
            self.snapshot={'connected':b._connected,'port':b._selected_port,'firmware':b._firmware,
                           'device_info':dict(self.device_info),
                           'epoch':self.epoch,'telemetry':self.latest,'cursor':self.counter,
                           'pid_revision':b.pid._revision,'pid_scaled':dict(b.pid._params),
                           'pid_status':b.pid.status,'updating':b._updating}

    def capture(self,frame,line):
        now=time.monotonic()
        if frame.kind=='info':
            self.device_info=dict(frame.values)
            self.epoch+=1
            self.latest=None;self.latest_time=None;self.last_device_t=None
            # Parameter revision numbers are reused after reboot; old config is invalid.
            self.bridge.pid._revision=None
            if frame.values.get('control') != 'DAC' and frame.values.get('fw') not in ('6.2.0',):
                if self.bridge.pid.supported: self.bridge.sendCommand('PID GET')
        if frame.kind=='telemetry':
            try: tick=int(frame.values['t'])
            except (KeyError,ValueError): tick=None
            if tick is not None and self.last_device_t is not None and tick<self.last_device_t:
                self.epoch+=1
                self.bridge.pid._revision=None
                if self.bridge.pid.supported: self.bridge.sendCommand('PID GET')
            self.last_device_t=tick
            self.latest=dict(frame.values);self.latest_time=now
        with self.lock:
            self.counter+=1
            event={'id':self.counter,'epoch':self.epoch,'host_monotonic':now,
                   'kind':frame.kind,'values':dict(frame.values),'line':line}
            self.events.append(event)
        self.writer.write(event)
        self.refresh()

    @Slot(object)
    def execute(self,item):
        with item['guard']:
            if item['cancelled']: return
            try:
                b=self.bridge;data=item['body'];path=item['path']
                if path not in ('/v1/pid','/v1/voltage','/v1/output','/v1/telemetry-rate','/v1/pid-read','/v1/connection'):
                    raise ValueError('unknown endpoint')
                if b._updating or (path!='/v1/connection' and (not b._connected or not b._worker)):
                    raise ValueError('device not connected or updating')
                if data.get('epoch')!=self.epoch: raise ValueError('device epoch changed; read state')
                if path=='/v1/connection':
                    if type(data.get('connected')) is not bool: raise ValueError('connected must be boolean')
                    if data['connected']:
                        if not b._connected: b.connectDevice()
                    else: b.disconnectDevice()
                    item['code']=202
                    item['result']={'queued':'CONNECT' if data['connected'] else 'DISCONNECT',
                                    'epoch':self.epoch,'after_event':self.counter,'confirmed':False}
                    return
                if path=='/v1/pid':
                    if not b.pid.supported: raise ValueError('firmware does not support PID tuning')
                    if data.get('revision')!=b.pid._revision or b.pid._revision is None:
                        raise ValueError('PID revision changed; read state')
                    if set(data.get('parameters',{}))!=set(KEYS): raise ValueError('complete eight parameters required')
                    command=encode_config([data['parameters'][key] for key in KEYS])
                elif path=='/v1/voltage':
                    value=data.get('mv')
                    if type(value) is not int or not 3200<=value<=32000: raise ValueError('voltage range 3200..32000mV')
                    if b.curve.requiresStop: raise ValueError('manual calibration active')
                    command=f'SET V {value}'
                elif path=='/v1/output':
                    if type(data.get('on')) is not bool: raise ValueError('on must be boolean')
                    if b.curve.requiresStop: raise ValueError('manual calibration active')
                    command='OUT ON' if data['on'] else 'OUT OFF'
                elif path=='/v1/telemetry-rate':
                    value=data.get('ms')
                    if type(value) is not int or value!=20: raise ValueError('telemetry fixed at 20ms')
                    command=f'LOG RATE {value}'
                else:
                    if not b.pid.supported: raise ValueError('PID unavailable in DAC mode')
                    command='PID GET'
                b.sendCommand(command)
                item['code']=202
                item['result']={'queued':command,'epoch':self.epoch,'after_event':self.counter,
                                'confirmed':False,'note':'Wait for device ACK/config/telemetry via events.'}
            except (ValueError,TypeError,KeyError) as exc:
                item['code']=409;item['result']={'error':str(exc)}
            finally: item['done'].set()

    def close(self):
        self.timer.stop();self.server.shutdown();self.server.server_close();self.thread.join(2)
        self.writer.close()
        try: self.discovery.unlink()
        except FileNotFoundError: pass
