#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ -x "$PWD/tools/jdk/jdk-21.0.9+10/bin/javac" ]]; then export PATH="$PWD/tools/jdk/jdk-21.0.9+10/bin:$PATH"; fi
SDK_JAR="$PWD/tools/platform/android-35/android.jar"
ANDROID_BT="$PWD/tools/build-tools/android-15"
mkdir -p build/classes build/dex dist
"$ANDROID_BT/aapt2" compile --dir android/res -o build/resources.zip
"$ANDROID_BT/aapt2" link -o build/base.apk -I "$SDK_JAR" --manifest android/AndroidManifest.xml --java build/generated --min-sdk-version 26 --target-sdk-version 35 build/resources.zip
find android/src build/generated -name '*.java' -print > build/sources.txt
javac --release 8 -classpath "$SDK_JAR" -d build/classes @build/sources.txt
jar cf build/classes.jar -C build/classes .
"$ANDROID_BT/d8" --lib "$SDK_JAR" --min-api 26 --output build/dex build/classes.jar
python3 - <<'PY'
from zipfile import ZipFile,ZIP_DEFLATED
from pathlib import Path
import shutil
shutil.copyfile('build/base.apk','build/unsigned.apk')
with ZipFile('build/unsigned.apk','a',ZIP_DEFLATED) as z:
    for p in Path('build/dex').glob('*.dex'):z.write(p,p.name)
PY
"$ANDROID_BT/zipalign" -f -p 4 build/unsigned.apk build/aligned.apk
if [[ ! -f tools/debug.keystore ]]; then
  keytool -genkeypair -keystore tools/debug.keystore -storepass android -keypass android -alias androiddebugkey -dname 'CN=Forklift Companion Development' -keyalg RSA -keysize 2048 -validity 3650 >/dev/null 2>&1
fi
"$ANDROID_BT/apksigner" sign --ks tools/debug.keystore --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out dist/ForkliftCompanion.apk build/aligned.apk
"$ANDROID_BT/apksigner" verify --verbose dist/ForkliftCompanion.apk
