"""Client for the opt-in PC API; no serial port is opened here."""
import json, time, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
class Client:
    def __init__(self,api_file=None):
        self.discovery=json.loads(Path(api_file or Path(__file__).parent/'live_api.json').read_text(encoding='utf-8'))
        self.out=Path(self.discovery['record']).parent
    def request(self,path,body=None):
        req=urllib.request.Request(self.discovery['url']+'/v1/'+path,
            data=None if body is None else json.dumps(body).encode(),
            headers={'Authorization':'Bearer '+self.discovery['token'],'Content-Type':'application/json'})
        return json.loads(urllib.request.urlopen(req,timeout=5).read())
    def state(self): return self.request('state')
    def events(self,after): return self.request(f'events?after={after}&limit=20000')['events']
    def command(self,path,body):
        s=self.state()
        if not s['connected'] or s['telemetry_age_ms'] is None or s['telemetry_age_ms']>1500:
            raise RuntimeError('No fresh telemetry')
        body=dict(body,epoch=s['epoch'])
        if path=='pid': body['revision']=s['pid_revision']
        response=self.request(path,body)
        expected={'pid':'PID_SET','voltage':'SET_V','output':'OUT',
                  'telemetry-rate':'LOG_RATE'}.get(path)
        deadline=time.monotonic()+4
        while time.monotonic()<deadline:
            events=self.events(response['after_event'])
            if any(e['kind']=='error' for e in events): raise RuntimeError(events)
            if any(e['kind']=='ack' and e['values'].get('cmd')==expected and
                   (path!='output' or e['values'].get('detail')==('ON' if body['on'] else 'OFF')) for e in events):
                state=self.state()
                if state['epoch']!=s['epoch']: raise RuntimeError('Device rebooted')
                if path=='pid' and state['pid_revision']==s['pid_revision']:
                    time.sleep(.1); continue
                if path=='pid':
                    expected_params=dict(zip(('kp','ki','kd','period','deadband','nearstep','farstep','nearband'),
                        (round(float(body['parameters'][k])*(1000 if k in ('kp','ki','kd') else 1)) for k in
                         ('kp','ki','kd','period','deadband','nearstep','farstep','nearband'))))
                    if state['pid_scaled']!=expected_params: raise RuntimeError('PID echo mismatch')
                break
            time.sleep(.05)
        else: raise RuntimeError(f'No confirmed ACK for {response}')
        with (self.out/'actions.jsonl').open('a',encoding='utf-8') as f:
            f.write(json.dumps({'host_monotonic':time.monotonic(),'path':path,'body':body,'response':response})+'\n')
        return self.state()
    def config(self,kp=.15,ki=1,kd=.001,farstep=1):
        return self.command('pid',{'parameters':dict(kp=kp,ki=ki,kd=kd,period=20,deadband=5,
                              nearstep=1,farstep=farstep,nearband=500)})
    def measure(self,label,seconds):
        s=self.state(); start=s['cursor']; epoch=s['epoch']; t0=time.monotonic();interrupted=None;seen_on=False
        while time.monotonic()-t0<seconds:
            s=self.state()
            if s['epoch']!=epoch or s['telemetry_age_ms']>1500:
                interrupted='Measurement interrupted';break
            if int(s['telemetry']['vin'])<27000:
                interrupted='28V input lost';break
            seen_on=seen_on or s['telemetry']['out']=='1'
            if seen_on and s['telemetry']['out']!='1':
                interrupted='Output turned off';break
            time.sleep(.2)
        events=self.events(start)
        rows=[dict(e['values'],host_monotonic=e['host_monotonic']) for e in events if e['kind']=='telemetry']
        out=self.out
        (out/(label+'.json')).write_text(json.dumps(rows,indent=2),encoding='utf-8')
        if not rows: raise RuntimeError('No samples')
        goal=int(rows[-1]['setv']); vv=[int(r['vfb']) for r in rows]
        tt=[(int(r['t'])-int(rows[0]['t']))/1000 for r in rows]
        tail=[v for t,v in zip(tt,vv) if t>tt[-1]-3]
        settling={}
        for band in (10,20):
            outside=[i for i,v in enumerate(vv) if abs(v-goal)>band]
            settling[str(band)]=tt[outside[-1]+1] if outside and outside[-1]+1<len(tt) else (0 if not outside else None)
        valid=all(r['out']=='1' and r['pid']=='1' and r['fault']=='NONE' and int(r['vin'])>=27000 for r in rows)
        stats=dict(label=label,valid=valid,goal=goal,n=len(rows),duration=tt[-1],minimum=min(vv),maximum=max(vv),
                   tail_min=min(tail),tail_max=max(tail),tail_mean=sum(tail)/len(tail),settling=settling,
                   revision=rows[-1]['pidrev'],dac_min=min(int(r['dac']) for r in rows),dac_max=max(int(r['dac']) for r in rows))
        with (out/'metrics.jsonl').open('a') as f: f.write(json.dumps(stats)+'\n')
        print(json.dumps(stats),flush=True)
        if interrupted: raise RuntimeError(interrupted)
        return stats

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('action',choices=['state','config','voltage','on','off','rate','measure'])
    p.add_argument('value',nargs='?');p.add_argument('--api-file');p.add_argument('--kp',type=float,default=.15);p.add_argument('--ki',type=float,default=1)
    p.add_argument('--kd',type=float,default=.001);p.add_argument('--farstep',type=int,default=1);p.add_argument('--seconds',type=float,default=20)
    a=p.parse_args();c=Client(a.api_file)
    if a.action=='state': print(json.dumps(c.state()))
    elif a.action=='config': print(json.dumps(c.config(a.kp,a.ki,a.kd,a.farstep)))
    elif a.action=='measure': c.measure(a.value,a.seconds)
    else: print(json.dumps(c.command({'voltage':'voltage','on':'output','off':'output','rate':'telemetry-rate'}[a.action],
        {'mv':int(a.value)} if a.action=='voltage' else {'ms':int(a.value)} if a.action=='rate' else {'on':a.action=='on'})))
