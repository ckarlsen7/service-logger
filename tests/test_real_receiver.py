"""Compatibility checks against the actual uploaded receiver source."""
import importlib.util,json,tempfile,threading,unittest,http.client,os
from pathlib import Path
from http.server import ThreadingHTTPServer
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'receiver'))
from companion_api import handler_class
SOURCE=Path(os.environ.get('RECEIVER_SOURCE', 'server.py'))

@unittest.skipUnless(SOURCE.exists(),'Uploaded original receiver source is not available in this checkout')
class RealReceiverTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();path=Path(self.tmp.name)/'server.py';path.write_bytes(SOURCE.read_bytes())
        spec=importlib.util.spec_from_file_location('receiver_under_test',path);self.base=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.base);self.base.TOKEN='test-token'
        cls=handler_class(self.base);cls.log_message=lambda *a:None
        self.http=ThreadingHTTPServer(('127.0.0.1',0),cls);threading.Thread(target=self.http.serve_forever,daemon=True).start()
    def tearDown(self):self.http.shutdown();self.http.server_close();self.tmp.cleanup()
    def req(self,method,path,body=None,auth=True):
        c=http.client.HTTPConnection('127.0.0.1',self.http.server_port);c.request(method,path,body=body,headers={'X-Device-Token':'test-token'} if auth else {});r=c.getresponse();code=r.status;data=r.read();c.close();return code,data
    def test_original_upload_and_visit_still_work_with_phone_api(self):
        sid='job_test002';name='note_20261106T010713_test.wav';payload=b'RIFF'+(52).to_bytes(4,'little')+b'WAVE'+b'\0'*48
        self.assertEqual(200,self.req('POST','/upload/'+sid+'/'+name,payload)[0])
        self.assertEqual((200,b'waiting'),self.req('GET','/status/'+sid))
        self.assertEqual(202,self.req('POST','/visit/'+sid,b'')[0])
        code,data=self.req('GET','/v1/sessions/'+sid);self.assertEqual(200,code);item=json.loads(data);self.assertEqual('queued',item['status']);self.assertEqual(name,item['recordings'][0]['name'])
        self.assertEqual(409,self.req('DELETE','/v1/audio/'+sid+'/'+name)[0])
    def test_real_auth_applies_to_phone_routes(self):
        self.assertEqual(401,self.req('GET','/v1/svos',auth=False)[0]);self.assertEqual(200,self.req('GET','/v1/health')[0])

if __name__=='__main__':unittest.main()
