from pathlib import Path
import urllib.request, xml.etree.ElementTree as E, hashlib, zipfile
root=Path(__file__).resolve().parent
for name,url,sha in [('platform','platform-35_r02.zip','0bb560a90a7a2cbd0dd8348224d518b638fe7949'),('build-tools','build-tools_r35_linux.zip','2cfaa0bbb2336e9ec18ed3ecea84fa2e2af607bc')]:
 p=root/url
 if not p.exists(): urllib.request.urlretrieve('https://dl.google.com/android/repository/'+url,p)
 if hashlib.sha1(p.read_bytes()).hexdigest()!=sha: raise SystemExit('Official SDK checksum mismatch: '+url)
 dest=root/name;dest.mkdir(exist_ok=True)
 with zipfile.ZipFile(p) as z:
  for i in z.infolist():
   target=dest/i.filename
   if not target.resolve().is_relative_to(dest.resolve()): raise SystemExit('Unsafe archive path')
   z.extract(i,dest)
   mode=i.external_attr>>16
   if mode and target.is_file(): target.chmod(mode & 0o777)
 print('Verified and extracted',name,flush=True)
