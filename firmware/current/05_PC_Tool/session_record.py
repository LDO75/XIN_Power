"""Buffered disk writes on a dedicated thread; exports use a flushed snapshot."""
from datetime import datetime
from pathlib import Path
from queue import Queue, Empty
import tempfile
import threading
import time
import json

class SessionRecord:
    def __init__(self, directory=None):
        base=Path(directory) if directory else Path(tempfile.gettempdir())/'XINPowerStudio'
        base.mkdir(parents=True,exist_ok=True)
        self.path=base/('session_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.jsonl')
        self.queue=Queue();self.error='';self.closed=False
        self.thread=threading.Thread(target=self._run,name='XIN-session-log',daemon=True);self.thread.start()
    def write(self, item):
        if not self.closed:self.queue.put(item)
    def flush(self):
        event=threading.Event();self.queue.put(event)
        if not event.wait(5):raise OSError('日志写入未完成')
        if self.error:raise OSError(self.error)
    def _run(self):
        try:
            with self.path.open('w',encoding='utf-8',buffering=65536) as stream:
                last=time.monotonic()
                while True:
                    try:item=self.queue.get(timeout=.5)
                    except Empty:item='flush'
                    if item is None:break
                    if isinstance(item,threading.Event):stream.flush();item.set()
                    elif item!='flush':stream.write(json.dumps(item,ensure_ascii=False)+'\n')
                    if time.monotonic()-last>=1:stream.flush();last=time.monotonic()
        except Exception as exc:
            self.error=str(exc)
            while True:
                item=self.queue.get()
                if item is None:break
                if isinstance(item,threading.Event):item.set()
    def close(self):
        if not self.closed:self.closed=True;self.queue.put(None);self.thread.join(5)
