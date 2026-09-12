"""Run the real Android startup parser on the local JVM, without a device."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
jdk=next((ROOT/'.tools/jdk').rglob('javac.exe')).parent
out=ROOT/'native/build/avc-tests'
out.mkdir(parents=True,exist_ok=True)
subprocess.run([str(jdk/'javac.exe'),'-encoding','UTF-8','--release','8','-d',str(out),
 str(ROOT/'native/android/src/com/wiredscreen/usb/AvcInitialization.java'),
 str(ROOT/'native/android/tests/AvcInitializationTest.java')],check=True)
subprocess.run([str(jdk/'java.exe'),'-cp',str(out),'com.wiredscreen.usb.AvcInitializationTest'],check=True)
