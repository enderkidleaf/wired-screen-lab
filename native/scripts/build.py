import pathlib, subprocess, zipfile, shutil, os, filecmp
ROOT=pathlib.Path(__file__).resolve().parents[2]
NATIVE=ROOT/'native';TOOLS=ROOT/'.tools';BUILD=NATIVE/'build';DIST=NATIVE/'dist'
def find(root,name):
    paths=list(root.rglob(name))
    if not paths:raise RuntimeError('Missing '+name+'; run native/scripts/bootstrap.py first')
    return paths[0]
def run(args):
    def argument(x):
        if isinstance(x,pathlib.Path) and x.is_relative_to(ROOT):return str(x.relative_to(ROOT))
        return str(x)
    subprocess.run([argument(x) for x in args],check=True,cwd=ROOT)
def main():
    BUILD.mkdir(exist_ok=True);DIST.mkdir(exist_ok=True)
    run(['cmd.exe','/c',NATIVE/'scripts'/'build_bridge.cmd'])
    jdk=find(TOOLS/'jdk','javac.exe').parent
    android=find(TOOLS/'android-platform','android.jar')
    bt=find(TOOLS/'android-build-tools','aapt2.exe').parent
    classes=BUILD/'classes';dex=BUILD/'dex';classes.mkdir(exist_ok=True);dex.mkdir(exist_ok=True)
    sources=list((NATIVE/'android'/'src').rglob('*.java'))
    run([jdk/'javac.exe','-encoding','UTF-8','--release','8','-classpath',android,'-d',classes,*sources])
    run([jdk/'java.exe','-cp',bt/'lib'/'d8.jar','com.android.tools.r8.D8','--lib',android,'--min-api','26','--output',dex,*classes.rglob('*.class')])
    unsigned=BUILD/'unsigned.apk';aligned=BUILD/'aligned.apk'
    run([bt/'aapt2.exe','link','-I',android,'--manifest',NATIVE/'android'/'AndroidManifest.xml','-o',unsigned])
    with zipfile.ZipFile(unsigned,'a') as z:
        for f in dex.glob('*.dex'):z.write(f,f.name)
    run([bt/'zipalign.exe','-f','4',unsigned,aligned])
    keystore=TOOLS/'native-debug.jks'
    if not keystore.exists():run([jdk/'keytool.exe','-genkeypair','-keystore',keystore,'-storepass','android','-keypass','android','-alias','androiddebugkey','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=Android Debug,O=WiredScreenLab,C=US'])
    apk=DIST/'WiredScreen.apk'
    run([jdk/'java.exe','-jar',bt/'lib'/'apksigner.jar','sign','--ks',keystore,'--ks-pass','pass:android','--key-pass','pass:android','--out',apk,aligned])
    run([jdk/'java.exe','-jar',bt/'lib'/'apksigner.jar','verify',apk])
    csc=pathlib.Path(os.environ.get('WINDIR','C:/Windows'))/'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    run([csc,'/nologo','/target:exe','/platform:x64','/optimize+','/out:'+str(DIST/'WiredScreen.exe'),'/r:System.Windows.Forms.dll','/r:System.Drawing.dll','/r:System.Web.Extensions.dll',*sorted((NATIVE/'pc').glob('*.cs'))])
    for name in ['adb.exe','AdbWinApi.dll','AdbWinUsbApi.dll']:
        source=find(TOOLS/'android-platform-tools',name)
        if not (DIST/name).exists() or not filecmp.cmp(source,DIST/name,shallow=False):shutil.copy2(source,DIST/name)
    shutil.copy2(find(TOOLS/'ffmpeg','ffmpeg.exe'),DIST/'ffmpeg.exe')
    print('Built '+str(apk)+' and WiredScreen.exe')
if __name__=='__main__':main()
