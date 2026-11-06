# Forklift Companion 0.1.0

**Downloads:** [Android APK](https://github.com/ckarlsen7/service-logger/raw/refs/heads/main/dist/ForkliftCompanion.apk) · [Windows receiver add-on](https://github.com/ckarlsen7/service-logger/raw/refs/heads/main/dist/receiver-companion.zip) · [Arduino firmware](https://github.com/ckarlsen7/service-logger/raw/refs/heads/main/dist/logger-companion-firmware.zip) · [All source](https://github.com/ckarlsen7/service-logger/raw/refs/heads/main/dist/ForkliftCompanion-source.zip)

Start with the Windows receiver add-on and APK. Direct SD browsing also needs the firmware update described below.

An Android companion for the ESP32-S3 ePaper V2 forklift logger and existing Windows receiver. Built for Android 8+ and targeting Android 15; suitable for a Samsung S23. This is a development-signed sideload APK, not a Play Store release. No subscription or cloud AI service is required by the app.

## Included

- `dist/ForkliftCompanion.apk`: installable Android app.
- `receiver/companion_api.py`, `receiver/start_companion.py`, and `receiver/start_companion.cmd`: additive Windows receiver integration.
- `firmware/ForkliftLogger/`: complete companion-enabled Arduino sketch. Configuration contains placeholders only.
- Android source and `build-apk.sh`: reproducible local build.

## What it does

- Receiver: select an SVO, see the entire combined transcript across its visits, open individual visit transcripts and technician reports, and list/play/save/remove recordings.
- All visits: includes unassigned SVOs and processing/error states.
- Logger SD: discover the logger on local Wi-Fi, browse SD recordings, play or save original WAVs, and move unwanted files to SD trash.
- Original IMA ADPCM WAVs are decoded to PCM for playback. Saving a WAV exports the original, unchanged file through Android's file picker.
- Confirmed removal is reversible: receiver audio moves to `data/companion_trash`; SD audio moves to `/trash`. Transcripts are retained. Removing one copy does not remove the other.
- Battery bar on the logger and in the app’s Logger SD view, with voltage and an explicitly estimated percentage. V2 ADC1 channel 3 / GPIO4, 2:1 divider and 3.0–4.12 V endpoints match the manufacturer’s factory code. USB/charging and load affect readings; no charging status or runtime is inferred.
- Device token stored encrypted using Android Keystore. No passwords or tokens are embedded in the APK.
- Editable receiver URL in phone Settings; a changing Quick Tunnel address does not require reinstalling the app.

## 1. Enable the Windows receiver API

1. Keep your existing `server.py`, `.env`, data, and launcher backed up.
2. Copy `receiver/companion_api.py`, `receiver/start_companion.py`, and `receiver/start_companion.cmd` into the SAME folder as the existing `server.py` and `.env`.
3. Stop the currently running Python receiver with Ctrl+C. Leave the Cloudflare tunnel running.
4. Double-click `start_companion.cmd`, or run `python start_companion.py` from that folder instead of `python server.py`.
5. It uses the same port (8765), device token, transcription tools, SMTP configuration, and existing data. Existing ESP32 routes are retained. Do not run both Python launchers simultaneously.

The app cannot browse the receiver until this API launcher is active. Restoring the old launcher is sufficient to remove the phone API; no edits to the original server file are required.

## 2. Install the APK on the S23

Transfer `ForkliftCompanion.apk` to the phone, open it in Samsung My Files, and allow installation from that specific app if Android requests it. Samsung Auto Blocker may prevent sideload installation; use device settings appropriate to your own phone and organization policy. This app requests network access only; it does not need microphone, contacts, or broad storage permissions.

Open the app and tap Settings:

- Receiver address: the current `https://...trycloudflare.com` base URL (no path).
- Device token: EXACTLY the existing `DEVICE_TOKEN` from your receiver/logger configuration. Enter it on the phone, not in chat.
- Logger local address: optional until firmware is installed, e.g. `http://10.0.0.57`.

Tap Save, then SVOs or Visits. A browser showing Unauthorized is normal; the app supplies the authentication header.

## 3. Enable direct logger SD access

This requires ONE Arduino firmware update. It is not Bluetooth.

1. Open `firmware/ForkliftLogger/ForkliftLogger.ino` in Arduino IDE.
2. Keep `CompanionApi.h` and `trusted_roots.h` in that sketch folder.
3. Fill the placeholder `config.h` with your existing home Wi-Fi, hotspot, receiver URL, token, and timezone values. Do not upload the placeholder values.
4. The sketch includes the microphone LEFT-slot correction, PWR GPIO18 / battery hold GPIO17, home/hotspot connection support, and USB `URL` commands.
5. Keep the ESP32-S3 board settings that worked on your board (including USB CDC and OPI PSRAM). Verify, then Upload. Firmware compilation could not be run here because the environment blocks downloads.arduino.cc.
6. Open Serial Monitor at 115200. It prints `PHONE APP LOGGER ADDRESS: http://...` once connected and idle.
7. On the phone, connect to the SAME Wi-Fi as the logger or turn on the hotspot the logger uses. Tap Settings > Find logger on this Wi-Fi > Save. If multicast discovery is blocked, type the printed local address manually.
8. Open Logger SD. Requests are handled only while recording/uploads are idle. Network isolation on some Wi-Fi/hotspots can prevent direct device access.

The logger's local API is authenticated HTTP over your trusted home/hotspot LAN; it is not end-to-end TLS. Never expose logger port 80 to the internet, port-forward it, or publish it through a tunnel. Receiver access remains certificate-verified HTTPS through Cloudflare. The phone does not send the token to public cleartext HTTP destinations.

SD-only recordings have no authoritative SVO or transcript until the Windows receiver processes them. Use Receiver SVOs for those views. The app does not guess an SVO from filenames or invent transcripts.

## Removal and recovery

- Phone confirmation clearly identifies which copy is being removed.
- Receiver rejects removal while queued/processing or before a visit has reached `sent`/`error`, to protect the reporting workflow.
- Logger refuses changes to the active visit. To remove an unuploaded, queued SD file, acknowledge that it will no longer upload.
- To recover receiver audio, move it from `data/companion_trash/<timestamp>/<job>/` back into `data/sessions/<job>/` on the PC.
- To recover SD audio, move it from `/trash/<job>/` back to `/jobs/<job>/` using a computer/card reader. Do not overwrite a different file with the same name.
- Saved phone documents are independent copies. Settings > Clear downloaded audio cache removes only the app's temporary files.
- SD listing is bounded to 128 visits and 256 recordings per visit; the app displays a truncation notice if these limits are reached.

## Build and validation

Linux build requires JDK 21, Python 3 and the official Android SDK platform 35 / build tools 35. `python3 tools/get_sdk.py` downloads and checksum-verifies the pinned SDK archives; then run `./build-apk.sh`. A development signing key is retained privately in the build workspace; it is excluded from the repository and archives. Keep the same key to install updates over the existing app. A fresh source build generates a different key, so it requires uninstalling the previous app unless the original key is reused. Uninstalling removes phone connection settings, not receiver or SD data. For release distribution, generate and protect your own signing key.

Verified in this environment:

- Android Java/resources compiled, APK aligned, v2/v3 signature verified.
- 10 loopback HTTP integration tests cover authentication, SVO transcripts, exact WAV bytes, traversal protections, reversible removal, processing protection, and existing receiver upload/report routes.
- Java ADPCM decoder produces exactly the same PCM samples as FFmpeg for all four supplied SD recordings, while respecting the WAV fact sample count.

Not yet verified: installation/UI/playback on an actual S23; Android local-network discovery on your hotspot; compiling/flashing the ESP32 companion firmware; physical SD browsing/deletion, microphone behavior, and battery/PWR behavior with that firmware. No test sends email or modifies the user's real receiver data. A real device smoke test is still required.

## First device smoke test

1. Receiver: confirm existing SVO and visit transcripts load.
2. Play an existing WAV and Save WAV to Downloads. Listen for the original voice; the app does not repair recordings made with the previous channel bug.
3. Use one disposable processed test visit to exercise Remove; verify its transcript remains and the WAV is recoverable from receiver trash.
4. After compiling/uploading firmware, discover the logger, play a short disposable SD recording, then move that test recording to SD trash.
5. Test home Wi-Fi and hotspot separately. Pending/processing/report emails remain the existing logger/receiver workflow.

Suggested later enhancements: battery calibration and discharge-curve tuning, manually assigning an SVO before recording, firmware settings from the phone, push notifications for processing failures, and an in-app trash/restore screen. These are not represented as completed features in this build.
