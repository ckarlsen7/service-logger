import sys,json,tempfile,threading,unittest,http.client,hmac
from pathlib import Path
from types import SimpleNamespace
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'receiver'))
from companion_api import handler_class

class BaseHandler(BaseHTTPRequestHandler):
    def auth(self):
        if hmac.compare_digest(self.headers.get('X-Device-Token',''),'test-token'):return True
        self.send_response(401);self.end_headers();return False
    def do_GET(self):
        if not self.auth():return
        self.send_response(200);self.end_headers();self.wfile.write(b'legacy route retained')
    def log_message(self,*args):pass

class PhoneApiTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();data=Path(self.tmp.name)
        self.base=SimpleNamespace(DATA=data,SESSIONS=data/'sessions',SVOS=data/'svos',queue_lock=threading.Lock(),in_queue=set(),Handler=BaseHandler)
        self.base.SESSIONS.mkdir();self.base.SVOS.mkdir();self.folder=self.base.SESSIONS/'job_test001';self.folder.mkdir()
        (self.folder/'state.json').write_text(json.dumps({'status':'sent','svo':'SVO123456'}))
        (self.folder/'transcript.txt').write_text('SVO123456. Inspected hydraulic hose; no leak found.')
        self.audio=self.folder/'note_20261105T212339_test.wav';self.audio.write_bytes(b'RIFF'+b'\x00'*4+b'WAVEtestaudio')
        svo=self.base.SVOS/'SVO123456';svo.mkdir();(svo/'index.json').write_text(json.dumps({'model':'RC5700','visits':[{'session':self.folder.name}]}))
        self.http=ThreadingHTTPServer(('127.0.0.1',0),handler_class(self.base));threading.Thread(target=self.http.serve_forever,daemon=True).start()
    def tearDown(self):self.http.shutdown();self.http.server_close();self.tmp.cleanup()
    def req(self,path,method='GET',auth=True):
        c=http.client.HTTPConnection('127.0.0.1',self.http.server_port);c.request(method,path,headers={'X-Device-Token':'test-token'} if auth else {});r=c.getresponse();code=r.status;body=r.read();c.close();return code,body
    def test_auth_for_read_and_delete(self):
        for path,method in [('/v1/svos','GET'),('/v1/audio/'+self.folder.name+'/'+self.audio.name,'DELETE')]:
            self.assertEqual(401,self.req(path,method,False)[0])
        self.assertTrue(self.audio.exists())
    def test_svo_and_entire_transcript(self):
        code,b=self.req('/v1/svos');self.assertEqual(200,code);self.assertEqual('SVO123456',json.loads(b)['items'][0]['id'])
        code,b=self.req('/v1/svos/SVO123456');self.assertEqual(200,code);self.assertIn('Inspected hydraulic hose',json.loads(b)['transcript'])
    def test_audio_bytes_exact(self):
        self.assertEqual((200,self.audio.read_bytes()),self.req('/v1/audio/'+self.folder.name+'/'+self.audio.name))
    def test_delete_moves_only_audio_and_preserves_transcript(self):
        code,b=self.req('/v1/audio/'+self.folder.name+'/'+self.audio.name,'DELETE');self.assertEqual(200,code)
        self.assertFalse(self.audio.exists());self.assertEqual(1,len(list((self.base.DATA/'companion_trash').rglob('*.wav'))));self.assertTrue((self.folder/'transcript.txt').exists())
    def test_inflight_and_unsubmitted_deletes_rejected(self):
        path='/v1/audio/'+self.folder.name+'/'+self.audio.name
        self.base.in_queue.add(self.folder.name);self.assertEqual(409,self.req(path,'DELETE')[0]);self.base.in_queue.clear()
        (self.folder/'state.json').write_text('{"status":"waiting"}');self.assertEqual(409,self.req(path,'DELETE')[0]);self.assertTrue(self.audio.exists())
    def test_traversal_and_symlink_rejected(self):
        self.assertNotEqual(200,self.req('/v1/audio/%2e%2e/'+self.audio.name)[0])
        external=Path(self.tmp.name)/'outside.wav';external.write_text('private')
        link=self.folder/'note_link.wav';link.symlink_to(external)
        self.assertEqual(404,self.req('/v1/audio/'+self.folder.name+'/'+link.name)[0])
    def test_existing_receiver_routes_retained(self):
        self.assertEqual((200,b'legacy route retained'),self.req('/status/job_test001'))
    def test_unassigned_visits_listed(self):
        (self.folder/'state.json').write_text('{"status":"error","message":"No speech"}')
        code,b=self.req('/v1/sessions');v=json.loads(b)['items'][0];self.assertEqual('',v['svo']);self.assertEqual('error',v['status']);self.assertEqual('No speech',v['message'])

if __name__=='__main__':unittest.main()
