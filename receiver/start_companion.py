"""Put beside the existing server.py and .env; run this instead of server.py."""
import logging
import os
from pathlib import Path
from http.server import ThreadingHTTPServer
import server as receiver
from companion_api import handler_class

def main():
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(levelname)s %(message)s')
    if not receiver.TOKEN or receiver.TOKEN=='REPLACE_WITH_LONG_RANDOM_TOKEN':
        raise SystemExit('Set DEVICE_TOKEN in the existing .env')
    for key in ('WHISPER_EXE','WHISPER_MODEL'):
        if not Path(receiver.exe(key)).is_file(): raise SystemExit('Check '+key+' in .env')
    if not all(os.getenv(k) for k in ('SMTP_HOST','SMTP_USERNAME','SMTP_PASSWORD')):
        raise SystemExit('Check SMTP settings in .env')
    receiver.start_worker()
    host=os.getenv('LISTEN_HOST','127.0.0.1'); port=int(os.getenv('LISTEN_PORT','8765'))
    with ThreadingHTTPServer((host,port),handler_class(receiver)) as http:
        print(f'Receiver + companion API listening on http://{host}:{port}',flush=True)
        http.serve_forever()

if __name__=='__main__': main()
