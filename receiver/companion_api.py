"""Authenticated phone API, layered onto the existing receiver without editing it."""
import json
import re
import shutil
from pathlib import Path
from datetime import datetime, timezone
from urllib.parse import urlsplit, unquote

ID = re.compile(r'^[A-Za-z0-9_-]{5,90}$')
NOTE = re.compile(r'^note_?[A-Za-z0-9_.-]+\.wav$')
SVO = re.compile(r'^SVO[0-9]{6}$')

def read(path, default):
    try:
        return json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return default

def text(path):
    try:
        return path.read_text(encoding='utf-8-sig')
    except OSError:
        return ''

def safe_session(base, sid):
    if not ID.fullmatch(sid):
        raise ValueError('Invalid visit ID')
    path = (base.SESSIONS / sid).resolve()
    if path.parent != base.SESSIONS.resolve() or not path.is_dir():
        raise FileNotFoundError('Visit not found')
    return path

def describe(base, folder, detail=False):
    state = read(folder/'state.json', {})
    req = read(folder/'request.json', {})
    audio = []
    for p in sorted(folder.glob('note*.wav')):
        if NOTE.fullmatch(p.name) and p.is_file() and not p.is_symlink():
            audio.append({'name':p.name,'bytes':p.stat().st_size,
                'url':'/v1/audio/'+folder.name+'/'+p.name})
    result = {'id':folder.name,'svo':state.get('svo',''),
        'status':state.get('status','waiting'),'message':state.get('message',''),
        'action':req.get('action',''),'sent_at':state.get('sent_at',''),
        'recordings':audio,'source':'receiver'}
    if detail:
        result['transcript'] = text(folder/'transcript.txt')
        result['customer_report'] = text(folder/'customer_report.txt')
        result['technician_report'] = text(folder/'technician_report.txt')
    return result

def handler_class(base):
    class CompanionHandler(base.Handler):
        def json_reply(self, code, obj):
            body=json.dumps(obj,ensure_ascii=False).encode('utf-8')
            self.send_response(code)
            self.send_header('Content-Type','application/json; charset=utf-8')
            self.send_header('Content-Length',str(len(body)))
            self.send_header('Cache-Control','no-store')
            self.end_headers()
            self.wfile.write(body)

        def parts_v1(self):
            return [unquote(x) for x in urlsplit(self.path).path.split('/') if x]

        def do_GET(self):
            if not urlsplit(self.path).path.startswith('/v1/'):
                return super().do_GET()
            if not self.auth(): return
            p=self.parts_v1()
            try:
                if p==['v1','health']:
                    return self.json_reply(200,{'name':'Forklift receiver','api':1})
                if p==['v1','sessions']:
                    folders=[f for f in base.SESSIONS.iterdir() if f.is_dir() and not f.is_symlink() and ID.fullmatch(f.name)]
                    return self.json_reply(200,{'items':[describe(base,f) for f in sorted(folders,key=lambda x:x.name,reverse=True)]})
                if len(p)==3 and p[:2]==['v1','sessions']:
                    return self.json_reply(200,describe(base,safe_session(base,p[2]),True))
                if p==['v1','svos']:
                    items=[]
                    for folder in sorted(base.SVOS.iterdir(),reverse=True):
                        if folder.is_dir() and SVO.fullmatch(folder.name):
                            index=read(folder/'index.json',{})
                            items.append({'id':folder.name,'model':index.get('model',''),
                                'closed':bool(index.get('closed',False)),'visits':len(index.get('visits',[]))})
                    return self.json_reply(200,{'items':items})
                if len(p)==3 and p[:2]==['v1','svos'] and SVO.fullmatch(p[2]):
                    index=read(base.SVOS/p[2]/'index.json',None)
                    if index is None: raise FileNotFoundError('SVO not found')
                    ids=[]
                    for v in index.get('visits',[]):
                        if isinstance(v.get('session'),str) and v['session'] not in ids: ids.append(v['session'])
                    visits=[]
                    for sid in ids:
                        try: visits.append(describe(base,safe_session(base,sid),True))
                        except FileNotFoundError: continue
                    return self.json_reply(200,{'id':p[2],'model':index.get('model',''),
                        'closed':bool(index.get('closed',False)),'items':visits,
                        'transcript':'\n\n'.join('===== '+v['id']+' =====\n'+v['transcript'] for v in visits)})
                if len(p)==4 and p[:2]==['v1','audio'] and NOTE.fullmatch(p[3]):
                    folder=safe_session(base,p[2]); path=folder/p[3]
                    if path.is_symlink() or not path.is_file(): raise FileNotFoundError('Recording not found')
                    with path.open('rb') as audio:
                        self.send_response(200)
                        self.send_header('Content-Type','audio/wav')
                        self.send_header('Content-Length',str(path.stat().st_size))
                        self.send_header('Cache-Control','no-store')
                        self.end_headers(); shutil.copyfileobj(audio,self.wfile)
                    return
                return self.json_reply(404,{'error':'Not found'})
            except ValueError as exc: return self.json_reply(400,{'error':str(exc)})
            except FileNotFoundError as exc: return self.json_reply(404,{'error':str(exc)})
            except (BrokenPipeError,ConnectionResetError): return

        def do_DELETE(self):
            if not self.auth(): return
            p=self.parts_v1()
            try:
                if len(p)!=4 or p[:2]!=['v1','audio'] or not NOTE.fullmatch(p[3]):
                    return self.json_reply(404,{'error':'Not found'})
                folder=safe_session(base,p[2]); path=folder/p[3]
                with base.queue_lock:
                    state=read(folder/'state.json',{}).get('status','waiting')
                    if p[2] in base.in_queue or state in ('queued','processing'):
                        return self.json_reply(409,{'error':'Report queued or processing. Try again after it finishes.'})
                    if state not in ('sent','error'):
                        return self.json_reply(409,{'error':'This visit has not been processed. Finish its report before removing receiver audio.'})
                    if path.is_symlink() or not path.is_file(): raise FileNotFoundError('Recording not found')
                    trash=base.DATA/'companion_trash'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')/p[2]
                    trash.mkdir(parents=True,exist_ok=True); path.replace(trash/p[3])
                return self.json_reply(200,{'ok':True,'message':'Moved to receiver trash. Transcripts retained. SD original unchanged.'})
            except ValueError as exc: return self.json_reply(400,{'error':str(exc)})
            except FileNotFoundError as exc: return self.json_reply(404,{'error':str(exc)})
    return CompanionHandler
