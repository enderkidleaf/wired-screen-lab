"""Download build/runtime tools into this project; no global installations."""
import concurrent.futures, hashlib, json, pathlib, urllib.request, xml.etree.ElementTree as ET, zipfile
ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / '.tools'
TOOLS.mkdir(exist_ok=True)
def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent':'WiredScreenLab/0.2'}), timeout=90) as r:
        return r.read()
def install(name, url, destination, digest=None, algorithm='sha256'):
    target=TOOLS/destination
    marker=target/'.complete'
    if marker.exists(): return name+' already installed'
    archive=TOOLS/(name+'.zip')
    if not archive.exists():
        print('Downloading '+name, flush=True)
        with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'WiredScreenLab/0.2'}),timeout=120) as r, archive.with_suffix('.part').open('wb') as out:
            while chunk:=r.read(1024*1024):out.write(chunk)
        archive.with_suffix('.part').replace(archive)
    if digest:
        with archive.open('rb') as f:actual=hashlib.file_digest(f,algorithm).hexdigest()
        if actual.lower()!=digest.lower():
            archive.replace(archive.with_suffix('.invalid'))
            raise ValueError(name+' checksum mismatch; quarantined as .invalid; rerun to download again')
    target.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            if not (target/entry.filename).resolve().is_relative_to(target.resolve()):raise ValueError('Unsafe archive path')
        z.extractall(target)
    marker.write_text(json.dumps({'url':url,'digest':digest}),encoding='utf8')
    return name+' installed'
def main():
    xml=ET.fromstring(get('https://dl.google.com/android/repository/repository2-1.xml'))
    def package(wanted):
        p=next(p for p in xml if p.tag.endswith('remotePackage') and p.attrib.get('path')==wanted)
        for a in p.findall('./archives/archive'):
            if a.findtext('host-os') not in (None,'windows'):continue
            complete=a.find('complete')
            return 'https://dl.google.com/android/repository/'+complete.findtext('url'),complete.findtext('checksum')
        raise ValueError('No Windows archive: '+wanted)
    jdk=json.loads(get('https://api.adoptium.net/v3/assets/latest/17/hotspot?architecture=x64&image_type=jdk&os=windows'))[0]['binary']['package']
    tasks=[('jdk',jdk['link'],'jdk',jdk['checksum'],'sha256')]
    for name,path,dest in [('platform-tools','platform-tools','android-platform-tools'),('android-platform','platforms;android-35','android-platform'),('android-build-tools','build-tools;35.0.0','android-build-tools')]:
        url,checksum=package(path);tasks.append((name,url,dest,checksum,'sha1'))
    ffurl='https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.1-essentials_build.zip'
    checksum=get(ffurl+'.sha256').decode().split()[0]
    tasks.append(('ffmpeg',ffurl,'ffmpeg',checksum,'sha256'))
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        futures=[pool.submit(install,*t) for t in tasks]
        for f in concurrent.futures.as_completed(futures):print(f.result(),flush=True)
    print('Tools ready in '+str(TOOLS),flush=True)
if __name__=='__main__': main()
