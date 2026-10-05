"""Local API client for V6.2.0; serial ownership remains with the GUI."""
import argparse
import json
from pathlib import Path
import time
import urllib.request

class Client:
    def __init__(self,api_file=None):
        self.endpoint=json.loads(Path(api_file or Path(__file__).with_name('live_api.json')).read_text(encoding='utf-8'))
    def request(self,path,body=None):
        request=urllib.request.Request(self.endpoint['url']+'/v1/'+path,
            data=None if body is None else json.dumps(body).encode('utf-8'),
            headers={'Authorization':'Bearer '+self.endpoint['token'],'Content-Type':'application/json'})
        return json.loads(urllib.request.urlopen(request,timeout=5).read())
    def state(self):return self.request('state')
    def events(self,after):return self.request(f'events?after={after}&limit=20000')
    def command(self,path,body):
        state=self.state()
        if not state['connected']:raise RuntimeError('Device disconnected')
        if path!='output' or body.get('on'):
            if state['firmware']!='6.2.0' or state['device_info'].get('control')!='DAC':
                raise RuntimeError('Confirm V6.2.0 DAC firmware before changing voltage/output')
            if state['telemetry_age_ms'] is None or state['telemetry_age_ms']>1500:
                raise RuntimeError('No fresh telemetry')
        result=self.request(path,dict(body,epoch=state['epoch']))
        expected={'voltage':'SET_V','output':'OUT','telemetry-rate':'LOG_RATE'}[path]
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            events=self.events(result['after_event'])['events']
            if any(e['kind']=='error' for e in events):raise RuntimeError('Device rejected command; read events')
            ack=any(e['kind']=='ack' and e['values'].get('cmd')==expected and
                (path!='output' or e['values'].get('detail')==('ON' if body['on'] else 'OFF')) for e in events)
            if ack:
                current=self.state()
                if current['epoch']!=state['epoch']:raise RuntimeError('Device rebooted')
                return {'acknowledged':True,'state':current,'after_event':result['after_event']}
            time.sleep(.05)
        raise RuntimeError('No device acknowledgement; do not assume HTTP202 means applied')

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('action',choices=('state','events','voltage','on','off','rate'))
    parser.add_argument('value',nargs='?');parser.add_argument('--api-file')
    args=parser.parse_args();client=Client(args.api_file)
    if args.action=='state':result=client.state()
    elif args.action=='events':result=client.events(int(args.value or '0'))
    elif args.action=='voltage':result=client.command('voltage',{'mv':int(args.value)})
    elif args.action=='rate':result=client.command('telemetry-rate',{'ms':int(args.value)})
    else:result=client.command('output',{'on':args.action=='on'})
    print(json.dumps(result,ensure_ascii=False))
