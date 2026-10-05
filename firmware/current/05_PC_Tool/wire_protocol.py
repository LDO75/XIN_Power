"""Protocol 5: bounded binary framing, little endian integers, no C struct padding."""
from __future__ import annotations
from collections import deque
from dataclasses import dataclass
import binascii
import secrets
import struct
import time
from protocol import ParsedFrame, parse_line

MAGIC = b'\xa5\x5a'
VERSION = 5
MAX_PAYLOAD = 448
FAULTS = {0:'NONE', 1:'MON_OFFLINE', 2:'VIN_UV', 4:'VOUT_OV',
          5:'IIN_OC', 6:'IOUT_OC', 10:'OTP'}
OPS = {'INFO':1, 'STATUS':2, 'SET V':3, 'SET I':4, 'OUT':5, 'CLEAR':6,
       'CAL GET':7, 'CAL RAW':8, 'CAL SET V':9, 'CAL SET I':10,
       'CAL SAVE':11, 'CAL RESET':12, 'CAL POINT GET':13,
       'CAL POINT START':15, 'CAL POINT STEP':16, 'CAL POINT CAPTURE':17,
       'CAL POINT STOP':18, 'CAL POINT STATUS':19, 'LOG':20, 'LOG RATE':21,
       'DIAG':22, 'HELP':23, 'PING':24, 'FW STATUS':30, 'FW BEGIN':31,
       'FW DATA':32, 'FW END':33, 'FW APPLY':34, 'FW ABORT':35, 'REBOOT':36}
ACK_NAMES = {v:k.replace(' ', '_') for k,v in OPS.items()}
ACK_NAMES.update({1:'INFO', 14:'CAL_POINT_GET', 13:'CAL_POINT_GET'})

@dataclass(frozen=True)
class Packet:
    type: int
    sequence: int
    payload: bytes

def encode(type_: int, sequence: int, payload: bytes) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError('数据包超长')
    body = struct.pack('<BBHH', VERSION, type_, len(payload), sequence) + payload
    return MAGIC + body + struct.pack('<H', binascii.crc_hqx(body, 0xffff))

class Parser:
    def __init__(self):
        self.buffer = bytearray()
        self.bad_frames = 0
        self.last_time = None

    def feed(self, chunk: bytes, now=None) -> list[Packet]:
        now = time.monotonic() if now is None else now
        if self.buffer and self.last_time is not None and now-self.last_time > .2:
            self.buffer.clear()
            self.bad_frames += 1
        self.last_time = now
        self.buffer.extend(chunk)
        result = []
        while self.buffer:
            start = self.buffer.find(MAGIC)
            if start < 0:
                self.buffer[:] = self.buffer[-1:] if self.buffer[-1] == 0xa5 else b''
                break
            del self.buffer[:start]
            if len(self.buffer) < 8:
                break
            version, type_, length, seq = struct.unpack_from('<BBHH', self.buffer, 2)
            if version != VERSION or length > MAX_PAYLOAD:
                del self.buffer[0]
                self.bad_frames += 1
                continue
            size = length+10
            if len(self.buffer) < size:
                break
            if binascii.crc_hqx(self.buffer[2:size-2], 0xffff) != struct.unpack_from('<H', self.buffer, size-2)[0]:
                del self.buffer[0]
                self.bad_frames += 1
                continue
            result.append(Packet(type_, seq, bytes(self.buffer[8:size-2])))
            del self.buffer[:size]
        return result

def command_payload(command: str, nonce: int) -> bytes:
    words = command.upper().split()
    normalized = ' '.join(words)
    if normalized == 'CAL POINT GET':
        return bytes([13])
    if normalized.startswith('CAL POINT GET '):
        return bytes([14]) + struct.pack('<H', int(words[-1]))
    name = next((k for k in sorted(OPS, key=len, reverse=True)
                 if normalized == k or normalized.startswith(k+' ')), None)
    if name is None:
        raise ValueError('6.2.0 不支持此命令')
    op = OPS[name]
    args = words[len(name.split()):]
    if op == 1:
        if args: raise ValueError('INFO 无参数')
        tail = struct.pack('<I', nonce)
    elif op in (3,4,15,17,21):
        if len(args)!=1: raise ValueError('参数数量错误')
        tail = struct.pack('<H', int(args[0]))
    elif op == 16:
        if len(args)!=1: raise ValueError('参数数量错误')
        tail = struct.pack('<h', int(args[0]))
    elif op in (5,20):
        if args not in (['ON'], ['OFF']): raise ValueError('必须选择 ON/OFF')
        tail = bytes([args == ['ON']])
    elif op in (9,10):
        if len(args)!=2: raise ValueError('参数数量错误')
        tail = struct.pack('<ii', *map(int,args))
    elif op == 31:
        if len(args)!=3: raise ValueError('参数数量错误')
        tail = struct.pack('<III', int(args[0]), int(args[1],16), int(args[2]))
    elif op == 32:
        if len(args)!=2: raise ValueError('参数数量错误')
        data = bytes.fromhex(args[1])
        if not 1<=len(data)<=192: raise ValueError('固件块长度错误')
        tail = struct.pack('<I', int(args[0])) + data
    else:
        if args: raise ValueError('命令不接受参数')
        tail = b''
    return bytes([op]) + tail

class Fields:
    def __init__(self, data): self.data, self.pos, self.values = data, 0, {}
    def read(self, key, fmt):
        fmt = '<'+fmt
        value = struct.unpack_from(fmt, self.data, self.pos)[0]
        self.pos += struct.calcsize(fmt)
        self.values[key] = str(value)
        return value
    def flags(self, flags, names):
        self.values.update({name:str((flags>>i)&1) for i,name in enumerate(names)})
    def finish(self, kind):
        if self.pos != len(self.data): raise ValueError('数据包字段长度不匹配')
        prefix = {'telemetry':'TEL','info':'INFO','fault':'FAULT','curve':'CURVE',
                  'curve_point':'CPOINT','curve_state':'CSTATE','calibration':'CAL'}[kind]
        return ParsedFrame(kind, self.values, '@'+prefix+','+','.join(k+'='+v for k,v in self.values.items()))

def decode(packet: Packet) -> ParsedFrame:
    f = Fields(packet.payload)
    r = f.read
    if packet.type in (2,3):
        if not packet.payload: raise ValueError('缺少确认命令')
        values = {'cmd':ACK_NAMES.get(packet.payload[0], str(packet.payload[0])),
                  'detail' if packet.type==2 else 'reason':packet.payload[1:].decode('ascii')}
        kind = 'ack' if packet.type==2 else 'error'
        return ParsedFrame(kind,values, ('@ACK,' if packet.type==2 else '@ERR,')+','.join(k+'='+v for k,v in values.items()))
    if packet.type == 4:
        major,minor,patch = r('major','B'),r('minor','B'),r('patch','B')
        r('hardware_version','B')
        for k in ('vmin','vmax','imin','imax','sample_ms','screen_ms','telemetry_ms','rinj','hardware_tag'): r(k,'H')
        r('points','B'); flags=r('flags','B');r('baud','I');ota=r('ota_code','B')
        f.values.update(fw=f'{major}.{minor}.{patch}', proto='5', hw='PY32F403_V2', control='DAC_PI' if (major,minor,patch)>=(6,2,3) else 'DAC',
                        flashok=str(flags&1), bl=str((flags>>1)&1), src='BYPASS' if (major,minor,patch)>=(6,2,3) else 'INPUT', rup='100000', rgnd='7620',
                        ota={0:'NONE',1:'STAGED',2:'COPYING',3:'TRIAL',4:'CONFIRMED',5:'ROLLBACK',
                             6:'CANCELLED',7:'VERIFIED',8:'SETTINGS_BUSY'}.get(ota,'UNKNOWN'))
        return f.finish('info')
    if packet.type == 5:
        for k,fmt in [('t','I'),('seq','I'),('samplet','I'),('vin','H'),('iin','i'),('pin','I'),
                      ('vout','H'),('iout','i'),('pout','I'),('temp','h'),('setv','H'),('seti','H'),
                      ('ilim','H'),('dac','H'),('vraw','H'),('iraw','i'),('vfast','H')]: r(k,fmt)
        flags=r('flags','H');fault=r('fault_code','B')
        for k,fmt in [('vpp','H'),('hz','H'),('maxgap','H'),('dw','I'),('dwt','I'),
                      ('rxdrop','H'),('txdrop','H'),('in_errors','H'),('out_errors','H'),('ntcraw','H')]: r(k,fmt)
        f.flags(flags,('req','out','valid','cegpio','input_online','output_online','vppvalid','tempvalid'))
        f.values['fault']=FAULTS.get(fault,'UNKNOWN')
        f.values['pid']=str((flags>>8)&1); f.values['trim_available']=str((flags>>9)&1)
        f.values['pe']=str(int(f.values['setv'])-int(f.values['vfast']))
        return f.finish('telemetry')
    if packet.type == 6:
        for k,fmt in [('t','I'),('fault_code','B'),('phase','B'),('set','H'),('dac','H'),
                      ('vout','H'),('vuncal','H'),('vin','H'),('iin','i'),('iout_uncal','i'),
                      ('pout','I'),('temp','h'),('flags','B'),('age','I'),('seq','I'),('ilim','H')]:r(k,fmt)
        f.values['reason']=FAULTS.get(int(f.values['fault_code']),'UNKNOWN')
        f.flags(int(f.values['flags']),('valid','input_online','output_online'))
        return f.finish('fault')
    if packet.type == 7:
        for k,fmt in [('schema','B'),('mask','I'),('saved','B'),('total','B')]:r(k,fmt)
        return f.finish('curve')
    if packet.type == 8:
        for k,fmt in [('id','B'),('target','H'),('valid','B'),('actual','H'),('dac','H'),('saved','B')]:r(k,fmt)
        return f.finish('curve_point')
    if packet.type == 9:
        for k,fmt in [('active','B'),('id','B'),('target','H'),('dac','H'),('vout','H'),('ready','B'),('fault_code','B')]:r(k,fmt)
        f.values['fault']=FAULTS.get(int(f.values['fault_code']),'UNKNOWN')
        return f.finish('curve_state')
    if packet.type == 10:
        flags=r('flags','B')
        for k in ('vgain','voff','igain','ioff'):r(k,'i')
        for k in ('v1','c1','v2','c2','vraw'):r(k,'H')
        r('iraw','i');r('dac','H');f.flags(flags,('valid','vvalid','ivalid','dacvalid','saved'))
        return f.finish('calibration')
    if packet.type == 11:
        return parse_line(packet.payload.decode('utf-8',errors='replace').strip())
    raise ValueError('未知数据包类型')

def expected_type(op):
    return {1:4,2:5,7:10,8:10,13:7,14:8,19:9,22:11,23:11}.get(op,2)

class SerialSession:
    """Single outstanding request. Retry identical bytes/sequence; never replay stale replies."""
    def __init__(self, port, binary=False):
        self.port=port;self.binary=binary;self.parser=Parser();self.nonce=secrets.randbits(32)
        self.sequence=0;self.pending=None;self.completed=deque(maxlen=16);self.lines=bytearray()
        self.frames=deque();self.last_packets=[]

    def send(self, command):
        if not self.binary:
            self.port.write((command+'\r\n').encode('ascii'));return
        payload=command_payload(command,self.nonce)
        self.sequence=(self.sequence+1)&0xffff
        wire=encode(1,self.sequence,payload)
        delay=90 if payload[0]==31 else 45 if payload[0]==33 else 1.2
        self.pending=[self.sequence,payload[0],wire,time.monotonic()+delay,0]
        self.port.write(wire)

    def poll(self):
        chunk=self.port.read(max(1,min(int(getattr(self.port,'in_waiting',0)),4096)))
        result=[]
        if self.binary:
            for packet in self.parser.feed(chunk) if chunk else []:
                try:frame=decode(packet)
                except (ValueError,UnicodeError,struct.error):continue
                key=(packet.sequence,packet.type,packet.payload)
                if packet.type not in (5,6) and key in self.completed:
                    continue
                # Events have a separate sequence domain from request responses.
                matched=(self.pending is not None and packet.sequence==self.pending[0] and
                         packet.type in (expected_type(self.pending[1]),3))
                if matched:
                    self.pending=None
                if packet.type not in (5,6):self.completed.append(key)
                result.append(frame)
            if self.pending and time.monotonic()>=self.pending[3]:
                if self.pending[4]>=2:
                    raise TimeoutError('设备未确认命令；操作结果未知，请读取状态后确认')
                self.pending[4]+=1;self.pending[3]=time.monotonic()+1.2
                self.port.write(self.pending[2])
        else:
            self.lines.extend(chunk)
            if len(self.lines)>8192:self.lines.clear()
            while b'\n' in self.lines:
                raw,_,rest=self.lines.partition(b'\n');self.lines=bytearray(rest)
                result.append(parse_line(raw.decode('utf-8',errors='replace').strip()))
        return result

    def handshake(self, cancelled=lambda:False):
        self.binary=True;self.send('INFO');deadline=time.monotonic()+1.8
        while time.monotonic()<deadline and not cancelled():
            for frame in self.poll():
                if frame.kind=='info':return frame
        if cancelled():raise InterruptedError('连接已取消')
        # Legacy applications treat arbitrary binary as a partial text line. Clear
        # that line, then query again; no output-changing command is sent here.
        self.binary=False;self.pending=None;self.port.write(b'\r\n');self.lines.clear()
        self.send('INFO');deadline=time.monotonic()+3
        while time.monotonic()<deadline and not cancelled():
            for frame in self.poll():
                if frame.kind=='info':return frame
        raise TimeoutError('端口已打开，但电源未回应身份查询')

    # Borrowed-port adapter used by the existing OTA state machine.
    def write(self, data):
        self.send(data.decode('ascii').strip());return len(data)
    def readline(self):
        if not self.frames:self.frames.extend(self.poll())
        return (self.frames.popleft().raw+'\r\n').encode('ascii',errors='replace') if self.frames else b''
    def flush(self):self.port.flush()
    def reset_input_buffer(self):
        self.port.reset_input_buffer();self.parser=Parser();self.lines.clear();self.frames.clear();self.pending=None
    def close(self):self.port.close()
    @property
    def is_open(self):return self.port.is_open
