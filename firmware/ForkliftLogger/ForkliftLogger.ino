// Forklift Logger: home + hotspot, physical PWR, persistent USB URL settings.
#include <Arduino.h>
#include <atomic>
#include <esp_sleep.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include "trusted_roots.h"
#include <Wire.h>
#include <ESP_I2S.h>
#include <SD_MMC.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <time.h>
#include <esp_system.h>
#include <sys/time.h>
#include "config.h"
#include <Arduino.h>
#include <Preferences.h>

static String savedServerURL;
static char serverCommand[192];
static size_t serverCommandLength = 0;
static bool serverCommandOverflow = false;

static bool validTunnelURL(const String &url) {
  const String prefix = "https://";
  const String suffix = ".trycloudflare.com";
  if (!url.startsWith(prefix) || !url.endsWith(suffix)) return false;
  String label = url.substring(prefix.length(), url.length() - suffix.length());
  if (label.length() == 0 || label.length() > 63) return false;
  if (label[0] == '-' || label[label.length() - 1] == '-') return false;
  for (size_t i = 0; i < label.length(); ++i) {
    char c = label[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}

static void beginServerSettings() {
  savedServerURL = String(SERVER_URL);
  savedServerURL.trim();
  while (savedServerURL.endsWith("/")) savedServerURL.remove(savedServerURL.length() - 1);
  Preferences prefs;
  if (prefs.begin("forklift", true)) {
    String stored = prefs.getString("server_url", "");
    prefs.end();
    if (validTunnelURL(stored)) savedServerURL = stored;
  }
  Serial.println("SERVER: " + savedServerURL);
  Serial.println("USB commands: URL? | URL https://name.trycloudflare.com | URL RESET");
}

static void handleServerCommand(String command) {
  command.trim();
  if (command.length() == 0) return;
  if (command == "URL?") {
    Serial.println("SERVER: " + savedServerURL);
    return;
  }
  if (command == "URL RESET") {
    Preferences prefs;
    if (!prefs.begin("forklift", false)) {
      Serial.println("URL ERROR: could not open persistent settings");
      return;
    }
    bool ok = !prefs.isKey("server_url") || prefs.remove("server_url");
    prefs.end();
    if (!ok) {
      Serial.println("URL ERROR: reset failed");
      return;
    }
    savedServerURL = String(SERVER_URL);
    savedServerURL.trim();
    while (savedServerURL.endsWith("/")) savedServerURL.remove(savedServerURL.length() - 1);
    Serial.println("URL RESET: " + savedServerURL);
    return;
  }
  if (!command.startsWith("URL ")) {
    Serial.println("Use: URL? or URL https://name.trycloudflare.com");
    return;
  }
  String candidate = command.substring(4);
  candidate.trim();
  candidate.toLowerCase();
  while (candidate.endsWith("/")) candidate.remove(candidate.length() - 1);
  if (!validTunnelURL(candidate)) {
    Serial.println("URL ERROR: enter an HTTPS trycloudflare.com address with no path");
    return;
  }
  Preferences prefs;
  if (!prefs.begin("forklift", false)) {
    Serial.println("URL ERROR: could not open persistent settings");
    return;
  }
  size_t written = prefs.putString("server_url", candidate);
  String verified = prefs.getString("server_url", "");
  prefs.end();
  if (written != candidate.length() || verified != candidate) {
    Serial.println("URL ERROR: could not verify saved address");
    return;
  }
  savedServerURL = candidate;
  Serial.println("URL SAVED: " + savedServerURL);
  Serial.println("The next upload attempt will use this address. No reupload needed.");
}

static void pollServerSettings() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (serverCommandOverflow) {
        Serial.println("URL ERROR: command too long");
      } else if (serverCommandLength) {
        serverCommand[serverCommandLength] = '\0';
        handleServerCommand(String(serverCommand));
      }
      serverCommandLength = 0;
      serverCommandOverflow = false;
    } else if (c == '\b' || c == 127) {
      if (serverCommandLength) --serverCommandLength;
    } else if (serverCommandLength < sizeof(serverCommand) - 1) {
      serverCommand[serverCommandLength++] = c;
    } else {
      serverCommandOverflow = true;
    }
  }
}

#ifndef HOTSPOT_SSID
#define HOTSPOT_SSID ""
#endif
#ifndef HOTSPOT_PASSWORD
#define HOTSPOT_PASSWORD ""
#endif
WiFiMulti savedNetworks;
bool savedNetworksReady = false;
constexpr int PWR_KEY = 18;
std::atomic<bool> shutdownRequested{false};
constexpr int BOOT_KEY = 0; constexpr int BAT_HOLD = 17;
constexpr int EPD_PWR = 6, EPD_BUSY = 8, EPD_RST = 9, EPD_DC = 10;
constexpr int EPD_CS = 11, EPD_SCK = 12, EPD_MOSI = 13;
constexpr int AUDIO_PWR = 42; constexpr int AUDIO_PA = 46;
constexpr int I2S_MCLK = 14, I2S_BCLK = 15, I2S_DIN = 16, I2S_WS = 38, I2S_DOUT = 45;
constexpr int SD_CLK = 39, SD_CMD = 41, SD_D0 = 40;
constexpr int I2C_SDA = 47, I2C_SCL = 48;
constexpr uint8_t CODEC = 0x18, RTC = 0x51;
constexpr uint32_t SAMPLE_RATE = 16000, MAX_NOTE_SECONDS = 240, SYNC_INTERVAL_MS = 20000;
constexpr uint16_t ADPCM_BLOCK_SIZE = 256, ADPCM_SAMPLES_PER_BLOCK = 505;
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
#include "BatteryMeter.h"
I2SClass mic; File wav; String activeJob, wavFinalPath, wavPartialPath, screenStatus = "STARTING";
SemaphoreHandle_t statusMutex = nullptr; SemaphoreHandle_t displayGate = nullptr; bool displayRefreshPending = true;
bool sdOK = false, audioOK = false, recording = false, writeFailure = false;
uint32_t bytesRecorded = 0, noteStartMs = 0, noteSerial = 0, samplesRecorded = 0, adpcmBlocks = 0;
uint8_t adpcmBlock[ADPCM_BLOCK_SIZE]; int blockSamples = 0, blockIndex = 0, predictor = 0, lastSample = 0; uint32_t lastSync = 0;
bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) { Wire.beginTransmission(addr); Wire.write(reg); Wire.write(val); return Wire.endTransmission() == 0; }
bool i2cRead(uint8_t addr, uint8_t reg, uint8_t &v) { Wire.beginTransmission(addr); Wire.write(reg); if (Wire.endTransmission(false) != 0 || Wire.requestFrom(addr, (uint8_t)1) != 1) return false; v = Wire.read(); return true; }
uint8_t bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); } uint8_t unbcd(uint8_t v) { return ((v >> 4) * 10) + (v & 15); }
bool rtcRead(time_t &epoch) {
uint8_t v[7]; Wire.beginTransmission(RTC); Wire.write((uint8_t)0x04); if (Wire.endTransmission(false) != 0 || Wire.requestFrom(RTC, (uint8_t)7) != 7) return false;
for (int i = 0; i < 7; i++) v[i] = Wire.read(); if (v[0] & 0x80) return false;
tm t = {}; t.tm_sec = unbcd(v[0] & 0x7f); t.tm_min = unbcd(v[1] & 0x7f); t.tm_hour = unbcd(v[2] & 0x3f); t.tm_mday = unbcd(v[3] & 0x3f); t.tm_mon = unbcd(v[5] & 0x1f) - 1; t.tm_year = unbcd(v[6]) + 100;
if (t.tm_year < 124 || t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31) return false;
setenv("TZ", "UTC0", 1); tzset(); epoch = mktime(&t); setenv("TZ", TIMEZONE_TZ, 1); tzset(); return epoch > 1700000000;
}
void rtcWrite(time_t epoch) {
tm t = {}; gmtime_r(&epoch, &t); i2cWrite(RTC, 0x00, 0x20); Wire.beginTransmission(RTC); Wire.write((uint8_t)0x04);
Wire.write(bcd(t.tm_sec)); Wire.write(bcd(t.tm_min)); Wire.write(bcd(t.tm_hour)); Wire.write(bcd(t.tm_mday)); Wire.write((uint8_t)t.tm_wday); Wire.write(bcd(t.tm_mon + 1)); Wire.write(bcd((t.tm_year + 1900) % 100));
Wire.endTransmission(); i2cWrite(RTC, 0x00, 0x00);
}
void setStatus(const String &s) { Serial.println("STATUS: " + s); if (statusMutex && xSemaphoreTake(statusMutex, pdMS_TO_TICKS(100)) == pdTRUE) { if (screenStatus != s) { screenStatus = s; displayRefreshPending = true; } xSemaphoreGive(statusMutex); } }
void drawHeartbeat(bool leftOn) { display.fillRect(168, 123, 32, 14, GxEPD_WHITE); display.fillRect(leftOn ? 177 : 189, 127, 5, 5, GxEPD_BLACK); }
void drawRecoveryScreen(const String &label, bool wifiOnline, bool leftOn, const String &clockText) {
display.fillScreen(GxEPD_WHITE); display.setTextColor(GxEPD_BLACK); display.setTextWrap(false); display.setTextSize(2); display.setCursor(8, 10); display.print("SERVICE LOGGER"); display.drawLine(8, 34, 192, 34, GxEPD_BLACK);
display.setCursor(8, 43); display.print(wifiOnline ? "Wi-Fi: ONLINE" : "Wi-Fi: OFFLINE"); display.setTextSize(1); display.setCursor(8, 82); display.print("STATUS"); display.setTextSize(label.length() <= 16 ? 2 : 1);
display.setCursor(8, 97); display.print(label); display.setTextSize(1); display.setCursor(8, 127); display.print(clockText); display.setCursor(8, 145); display.print("BOOT: note / stop"); display.setCursor(8, 158); display.print("2.5s: VISIT email");
display.setCursor(8, 171); display.print("6s: CLOSE SVO"); display.setCursor(8, 184); display.print("PWR hold: turn off"); drawHeartbeat(leftOn); drawBatteryBar();
}
void displayWorker(void*) {
int previousBattery = -999;
bool firstDraw = true, previousWifi = false, previousBeat = false; String previousLabel, previousClock;
for (;;) {
time_t epoch = time(nullptr); char clockBuf[22] = {}; if (epoch > 1700000000) { tm localClock = {}; localtime_r(&epoch, &localClock); strftime(clockBuf, sizeof(clockBuf), "%m/%d %I:%M%p", &localClock); } else { strncpy(clockBuf, "CLOCK UNSYNCED", sizeof(clockBuf) - 1); }
String clockText(clockBuf); const bool wifiOnline = (WiFi.status() == WL_CONNECTED); const bool beatLeft = ((millis() / 2000UL) & 1U) == 0; String label; bool requested = false;
if (statusMutex && xSemaphoreTake(statusMutex, pdMS_TO_TICKS(25)) == pdTRUE) { label = screenStatus; requested = displayRefreshPending; xSemaphoreGive(statusMutex); } else { vTaskDelay(pdMS_TO_TICKS(180)); continue; }
int currentBattery = batteryPercent();
bool redrawStatus = firstDraw || requested || currentBattery != previousBattery || (wifiOnline != previousWifi) || (label != previousLabel) || (clockText != previousClock); bool redrawDots = (!firstDraw && !redrawStatus && beatLeft != previousBeat);
if (redrawStatus || redrawDots) {
if (displayGate && xSemaphoreTake(displayGate, pdMS_TO_TICKS(10)) == pdTRUE) {
display.setRotation(0); if (firstDraw) display.setFullWindow(); else if (redrawStatus) display.setPartialWindow(0, 38, 200, 103); else display.setPartialWindow(168, 123, 32, 14);
display.firstPage(); do { if (redrawStatus) drawRecoveryScreen(label, wifiOnline, beatLeft, clockText); else drawHeartbeat(beatLeft); } while (display.nextPage()); xSemaphoreGive(displayGate);
if (redrawStatus) { previousBattery = currentBattery; firstDraw = false; previousLabel = label; previousWifi = wifiOnline; previousClock = clockText; if (statusMutex && xSemaphoreTake(statusMutex, pdMS_TO_TICKS(50)) == pdTRUE) { if (screenStatus == label) displayRefreshPending = false; xSemaphoreGive(statusMutex); } }
previousBeat = beatLeft;
}
}
vTaskDelay(pdMS_TO_TICKS(180));
}
}
enum : uint8_t { FB_START, FB_SAVED, FB_VISIT, FB_CLOSE_QUEUED, FB_SENT, FB_CLOSED_SENT, FB_ERROR };
void playTone(uint16_t freqHz, uint16_t durationMs) {
if (!audioOK || recording || freqHz == 0) return;
constexpr int BLOCK = 128; int16_t samples[BLOCK]; const uint32_t total = (SAMPLE_RATE * (uint32_t)durationMs) / 1000U;
const uint32_t ramp = SAMPLE_RATE / 200U; uint32_t phase = 0; const uint32_t phaseStep = ((uint64_t)freqHz << 32) / SAMPLE_RATE;
for (uint32_t offset = 0; offset < total;) {
uint32_t n = min((uint32_t)BLOCK, total - offset);
for (uint32_t i = 0; i < n; ++i) {
uint32_t p = phase >> 16; int32_t tri = (p < 32768U) ? ((int32_t)p * 2 - 32768) : (98304 - (int32_t)p * 2);
uint32_t position = offset + i; uint32_t tail = total - position - 1; uint32_t envelope = min((uint32_t)ramp, min(position, tail));
samples[i] = (int16_t)((tri * 4000LL * envelope) / (32768LL * ramp)); phase += phaseStep;
}
mic.write((const uint8_t*)samples, n * sizeof(int16_t)); offset += n;
}
}
void playSilence(uint16_t durationMs) {
if (!audioOK || recording) return; constexpr int BLOCK = 128; static const int16_t silence[BLOCK] = {}; uint32_t remaining = (SAMPLE_RATE * (uint32_t)durationMs) / 1000U;
while (remaining) { uint32_t n = min((uint32_t)BLOCK, remaining); mic.write((const uint8_t*)silence, n * sizeof(int16_t)); remaining -= n; }
}
void chirp(uint16_t hz, uint16_t ms, uint16_t gap = 50) { playTone(hz, ms); playSilence(gap); }
void playFeedback(uint8_t sound) {
if (!audioOK || recording) return;
switch (sound) {
case FB_START: chirp(1000, 90, 30); break; case FB_SAVED: chirp(820, 80); chirp(820, 80, 30); break; case FB_VISIT: chirp(750, 80); chirp(900, 80); chirp(1080, 110, 30); break;
case FB_CLOSE_QUEUED: chirp(660, 85); chirp(820, 85); chirp(990, 85); chirp(1180, 140, 30); break; case FB_SENT: chirp(1050, 100); chirp(1400, 140, 30); break;
case FB_CLOSED_SENT: chirp(880, 80); chirp(1100, 80); chirp(1480, 190, 30); break; case FB_ERROR: chirp(380, 180); chirp(310, 220, 30); break;
}
}
bool initCodec() {
pinMode(AUDIO_PWR, OUTPUT); digitalWrite(AUDIO_PWR, HIGH); pinMode(AUDIO_PA, OUTPUT); digitalWrite(AUDIO_PA, HIGH); delay(500);
Wire.beginTransmission(CODEC); if (Wire.endTransmission() != 0) { Serial.println("ES8311 not found"); return false; }
mic.setPins(I2S_BCLK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCLK); if (!mic.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) return false;
const uint8_t regs[][2] = {
{0x44,0x08},{0x44,0x08},{0x01,0x30},{0x02,0x00},{0x03,0x10},{0x16,0x24},{0x04,0x10},{0x05,0x00},{0x0B,0x00},{0x0C,0x00},
{0x10,0x1F},{0x11,0x7F},{0x00,0x80},{0x01,0x3F},{0x02,0x00},{0x05,0x00},{0x03,0x10},{0x04,0x20},{0x07,0x00},{0x08,0xFF},
{0x06,0x03},{0x13,0x10},{0x1B,0x0A},{0x1C,0x6A},{0x44,0x58},{0x00,0x80},{0x01,0x3F},{0x09,0x0C},{0x0A,0x0C},{0x17,0xBF},
{0x0E,0x02},{0x12,0x00},{0x14,0x1A},{0x0D,0x01},{0x15,0x40},{0x32,0xA0},{0x37,0x08},{0x45,0x00}
};
for (const auto &r : regs) { if (!i2cWrite(CODEC, r[0], r[1])) return false; }
Serial.println("Audio initialized."); return true;
}
void writeLE16(File &f, uint16_t n) { f.write((uint8_t)(n&255)); f.write((uint8_t)(n>>8)); }
void writeLE32(File &f, uint32_t n) { for (int i=0;i<4;i++) f.write((uint8_t)(n >> (8*i))); }
static const int IMA_STEP[89] = {
7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,
253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,
3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767
};
static const int8_t IMA_INDEX[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
void wavHeader(File &f, uint32_t blocks, uint32_t actualSamples) {
uint32_t dataBytes = blocks * ADPCM_BLOCK_SIZE; f.seek(0); f.write((const uint8_t*)"RIFF", 4); writeLE32(f, 52 + dataBytes);
f.write((const uint8_t*)"WAVEfmt ", 8); writeLE32(f, 20); writeLE16(f, 0x0011); writeLE16(f, 1); writeLE32(f, SAMPLE_RATE);
writeLE32(f, (SAMPLE_RATE * ADPCM_BLOCK_SIZE) / ADPCM_SAMPLES_PER_BLOCK); writeLE16(f, ADPCM_BLOCK_SIZE); writeLE16(f, 4); writeLE16(f, 2); writeLE16(f, ADPCM_SAMPLES_PER_BLOCK);
f.write((const uint8_t*)"fact", 4); writeLE32(f, 4); writeLE32(f, actualSamples); f.write((const uint8_t*)"data", 4); writeLE32(f, dataBytes);
}
int16_t pcmSample(const uint8_t *p) { return (int16_t)(uint16_t(p[0]) | (uint16_t(p[1])<<8)); }
void addAdpcmSample(int sample) {
if (blockSamples == 0) { predictor = sample; adpcmBlock[0] = uint8_t(predictor); adpcmBlock[1] = uint8_t(predictor >> 8); adpcmBlock[2] = uint8_t(blockIndex); adpcmBlock[3] = 0; memset(adpcmBlock + 4, 0, 252); blockSamples = 1; lastSample = sample; return; }
int step = IMA_STEP[blockIndex]; int diff = sample - predictor; uint8_t nib = 0; if (diff < 0) { nib = 8; diff = -diff; }
if (diff >= step) { nib |= 4; diff -= step; } if (diff >= step / 2) { nib |= 2; diff -= step / 2; } if (diff >= step / 4) nib |= 1;
int change = step >> 3; if (nib & 4) change += step; if (nib & 2) change += (step >> 1); if (nib & 1) change += (step >> 2);
predictor += (nib & 8) ? -change : change; predictor = constrain(predictor, -32768, 32767); blockIndex = constrain(blockIndex + IMA_INDEX[nib], 0, 88);
int coded = blockSamples - 1; if ((coded & 1) == 0) adpcmBlock[4 + coded / 2] = nib; else adpcmBlock[4 + coded / 2] |= uint8_t(nib << 4);
blockSamples++; lastSample = sample;
if (blockSamples == ADPCM_SAMPLES_PER_BLOCK) { size_t n = wav.write(adpcmBlock, ADPCM_BLOCK_SIZE); if (n == ADPCM_BLOCK_SIZE) { adpcmBlocks++; bytesRecorded += n; } else { writeFailure = true; } blockSamples = 0; }
}
void flushAdpcmBlock() { if (!blockSamples) return; while (blockSamples && blockSamples < ADPCM_SAMPLES_PER_BLOCK) addAdpcmSample(lastSample); }
String stamp() { time_t now = time(nullptr); if (now < 1700000000) return "UNSYNCED" + String((uint32_t)millis()); tm utc = {}; gmtime_r(&now, &utc); char b[24]; strftime(b, sizeof(b), "%Y%m%dT%H%M%S", &utc); return String(b); }
String basename(String s) { int i = s.lastIndexOf('/'); return i >= 0 ? s.substring(i + 1) : s; }
void saveCurrentJob() { SD_MMC.remove("/current.txt"); if (activeJob.isEmpty()) return; File f = SD_MMC.open("/current.txt", FILE_WRITE); if (f) { f.print(activeJob); f.close(); } }
void ensureJob() { if (!activeJob.isEmpty()) return; activeJob = "job_" + stamp() + "" + String((uint32_t)esp_random(), HEX); if (!SD_MMC.mkdir("/jobs/" + activeJob)) { Serial.println("Could not create job folder"); activeJob = ""; return; } saveCurrentJob(); }
void startNote() {
if (!sdOK || !audioOK) { setStatus("SD/AUDIO ERROR"); return; } ensureJob(); if (activeJob.isEmpty()) return;
String path = "/jobs/" + activeJob + "/note_" + stamp() + "_" + String((uint32_t)esp_random(), HEX); wavFinalPath = path + ".wav"; wavPartialPath = path + ".part";
wav = SD_MMC.open(wavPartialPath, FILE_WRITE); if (!wav) { setStatus("WRITE ERROR"); playFeedback(FB_ERROR); return; }
wavHeader(wav, 0, 0); bytesRecorded = 0; samplesRecorded = 0; adpcmBlocks = 0; blockSamples = 0; blockIndex = 0; writeFailure = false; playFeedback(FB_START); noteStartMs = millis(); recording = true; setStatus("RECORDING");
}
void stopNote(bool safe = true, bool announce = true) {
if (!recording) return; recording = false; if (wav) { flushAdpcmBlock(); wavHeader(wav, adpcmBlocks, samplesRecorded); wav.flush(); wav.close(); }
if (safe && !writeFailure && bytesRecorded >= 256 && SD_MMC.rename(wavPartialPath, wavFinalPath)) { Serial.printf("Saved %s\n", wavFinalPath.c_str()); setStatus("NOTE SAVED"); if (announce) playFeedback(FB_SAVED); }
else { Serial.println("Incomplete note"); setStatus("CHECK SD CARD"); if (announce) playFeedback(FB_ERROR); }
}
void endSession(bool closeSvo) {
if (recording) stopNote(true, false); if (activeJob.isEmpty()) { setStatus("NO ACTIVE VISIT"); playFeedback(FB_ERROR); return; }
const String folder = "/jobs/" + activeJob; bool hasNote = false; File check = SD_MMC.open(folder);
if (check) { File n = check.openNextFile(); while (n) { String fn = basename(String(n.name())); if (!n.isDirectory() && fn.startsWith("note") && fn.endsWith(".wav")) hasNote = true; n.close(); n = check.openNextFile(); } check.close(); }
if (!hasNote) { setStatus("NO SAVED NOTES"); playFeedback(FB_ERROR); return; }
File f = SD_MMC.open(folder + (closeSvo ? "/close.req" : "/visit.req"), FILE_WRITE); if (!f) { setStatus("SESSION ERROR"); playFeedback(FB_ERROR); return; }
f.println(stamp()); f.close(); activeJob = ""; saveCurrentJob(); lastSync = millis() - SYNC_INTERVAL_MS; setStatus(closeSvo ? "SVO CLOSE QUEUED" : "VISIT QUEUED"); playFeedback(closeSvo ? FB_CLOSE_QUEUED : FB_VISIT);
}
int queuedVisitCount() {
if (!sdOK) return -1; File root = SD_MMC.open("/jobs"); if (!root || !root.isDirectory()) return -1; int q = 0; File job = root.openNextFile();
while (job) { if (job.isDirectory()) { String folder = "/jobs/" + basename(String(job.name())); if (!SD_MMC.exists(folder + "/finished.ok") && (SD_MMC.exists(folder + "/visit.req") || SD_MMC.exists(folder + "/close.req"))) ++q; } job.close(); job = root.openNextFile(); }
root.close(); return q;
}
bool uploadFile(String jobId, String folder, String filename) {
String wavPath = folder + "/" + filename; File f = SD_MMC.open(wavPath, FILE_READ); if (!f) return false; String url = String(savedServerURL) + "/upload/" + jobId + "/" + filename;
HTTPClient http; WiFiClient plain; WiFiClientSecure secure; bool started = false; if (url.startsWith("https://")) { secure.setCACert(TRUSTED_ROOTS); started = http.begin(secure, url); } else { started = http.begin(plain, url); }
if (!started) { f.close(); setStatus("UPLOAD RETRY"); return false; }
http.setTimeout(20000); http.addHeader("X-Device-Token", DEVICE_TOKEN); http.addHeader("Content-Type", "audio/wav"); http.addHeader("ngrok-skip-browser-warning", "true");
const size_t uploadSize = f.size();
int code = http.sendRequest("POST", &f, uploadSize);
if (code != 200) {
  Serial.printf("Upload failed: HTTP %d\n", code);
  if (code > 0) Serial.println(http.getString());
  else Serial.println(HTTPClient::errorToString(code));
}
http.end(); f.close(); if (code != 200) setStatus("UPLOAD RETRY"); return code == 200;
}
bool finishRemote(const String &jobId) {
HTTPClient http; WiFiClient plain; WiFiClientSecure secure; String action = SD_MMC.exists("/jobs/" + jobId + "/close.req") ? "close" : "visit"; String url = String(savedServerURL) + "/" + action + "/" + jobId;
if (url.startsWith("https://")) { secure.setCACert(TRUSTED_ROOTS); http.begin(secure, url); } else http.begin(plain, url);
http.setTimeout(20000); http.addHeader("X-Device-Token", DEVICE_TOKEN); http.addHeader("ngrok-skip-browser-warning", "true"); int code = http.POST((uint8_t*)"", 0); http.end(); return code == 200 || code == 202;
}
void uploadPending() {
if (!sdOK || recording || shutdownRequested.load() || WiFi.status() != WL_CONNECTED) return; File dir = SD_MMC.open("/jobs"); if (!dir || !dir.isDirectory()) return; File job = dir.openNextFile();
while (job && !shutdownRequested.load()) {
if (job.isDirectory()) {
String jid = basename(String(job.name())); String folder = "/jobs/" + jid;
if ((SD_MMC.exists(folder + "/visit.req") || SD_MMC.exists(folder + "/close.req")) && !SD_MMC.exists(folder + "/finished.ok")) {
bool allSent = true; bool foundNote = false; File child = job.openNextFile();
while (child && !shutdownRequested.load()) { String name = basename(String(child.name())); if (!child.isDirectory() && name.startsWith("note") && name.endsWith(".wav")) { foundNote = true; if (!name.startsWith("note_")) { allSent = false; Serial.println("OLD NOTE NAME: " + name); child.close(); child = job.openNextFile(); continue; } String sent = folder + "/" + name + ".sent"; if (!SD_MMC.exists(sent)) { if (uploadFile(jid, folder, name)) { File marker = SD_MMC.open(sent, FILE_WRITE); if (marker) { marker.print("OK"); marker.close(); } else allSent = false; } else allSent = false; } } child.close(); child = job.openNextFile(); }
if (allSent && foundNote && !shutdownRequested.load()) {
HTTPClient check; WiFiClient plain; WiFiClientSecure secure; String sUrl = String(savedServerURL) + "/status/" + jid; if (sUrl.startsWith("https://")) { secure.setCACert(TRUSTED_ROOTS); check.begin(secure, sUrl); } else check.begin(plain, sUrl);
check.setTimeout(12000); check.addHeader("X-Device-Token", DEVICE_TOKEN); check.addHeader("ngrok-skip-browser-warning", "true"); int sCode = check.GET(); String remoteState = (sCode == 200) ? check.getString() : "offline"; check.end(); remoteState.trim(); Serial.printf("STATUS HTTP: %d\n", sCode); Serial.println("REMOTE: " + remoteState);
if (remoteState == "sent") { File marker = SD_MMC.open(folder + "/finished.ok", FILE_WRITE); if (marker) { marker.print("SENT"); marker.close(); setStatus("REPORT SENT"); playFeedback(SD_MMC.exists(folder + "/close.req") ? FB_CLOSED_SENT : FB_SENT); } }
else if (remoteState == "error") { setStatus("SERVER ERROR"); Serial.println("Check Windows receiver logs"); }
else if (remoteState == "waiting") { if (finishRemote(jid)) setStatus("REPORT QUEUED"); } else if (remoteState == "queued" || remoteState == "processing") setStatus("REPORT QUEUED");
}
}
}
job.close(); job = dir.openNextFile();
}
dir.close();
}
bool syncClockFromHttpDate() {
const char *urls[] = {"http://google.com", "http://gstatic.com"};
for (const char *url : urls) {
WiFiClient client; HTTPClient http; http.setConnectTimeout(5000); http.setTimeout(5000); const char *headers[] = {"Date"}; if (!http.begin(client, url)) continue; http.collectHeaders(headers, 1); int result = http.GET(); String date = http.header("Date"); http.end();
if (result < 200 || result >= 400 || date.isEmpty()) continue;
tm utc = {}; char *end = strptime(date.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &utc); if (!end || *end != '\0' || utc.tm_year < 125) continue;
setenv("TZ", "UTC0", 1); tzset(); time_t epoch = mktime(&utc); setenv("TZ", TIMEZONE_TZ, 1); tzset(); if (epoch < 1735689600) continue; timeval tv = {epoch, 0}; settimeofday(&tv, nullptr); rtcWrite(epoch); return true;
}
return false;
}
bool ensureValidClock() {
  static bool ntpStarted = false;
  static bool clockSaved = false;
  time_t now = time(nullptr);
  if (now >= 1735689600) {
    if (!clockSaved) { rtcWrite(now); clockSaved = true; }
    return true;
  }
  if (!ntpStarted) {
    configTzTime(TIMEZONE_TZ, "time.google.com", "time.cloudflare.com", "pool.ntp.org");
    ntpStarted = true;
  }
  unsigned long began = millis();
  while (time(nullptr) < 1735689600 && millis() - began < 6000UL) delay(100);
  now = time(nullptr);
  if (now >= 1735689600) {
    rtcWrite(now); clockSaved = true;
    return true;
  }
  return syncClockFromHttpDate();
}

void connectAndSync() {
  if (recording) return;
  Serial.println("SYNC: checking connection");
  if (!savedNetworksReady) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    if (strlen(WIFI_SSID)) savedNetworks.addAP(WIFI_SSID, WIFI_PASSWORD);
    if (strlen(HOTSPOT_SSID)) savedNetworks.addAP(HOTSPOT_SSID, HOTSPOT_PASSWORD);
    savedNetworksReady = true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WIFI: searching home and hotspot");
    if (savedNetworks.run(8000) != WL_CONNECTED) {
      setStatus("OFFLINE - SAVED");
      Serial.println("WIFI: offline; recordings remain on SD");
      return;
    }
  }
  Serial.println("WIFI: connected to " + WiFi.SSID());
  if (strlen(DEVICE_TOKEN) == 0) {
    setStatus("SET DEVICE TOKEN");
    return;
  }
  if (!ensureValidClock()) {
    setStatus("CLOCK SYNC RETRY");
    Serial.println("CLOCK: synchronization failed");
    return;
  }
  Serial.println("CLOCK: valid");
  int pending = queuedVisitCount();
  Serial.printf("SYNC: pending visits=%d\n", pending);
  if (!shutdownRequested.load() && pending > 0 && displayGate &&
      xSemaphoreTake(displayGate, pdMS_TO_TICKS(5000)) == pdTRUE) {
    uploadPending();
    xSemaphoreGive(displayGate);
  }
}
void powerButtonWorker(void *) {
  bool armed = false, down = false, stable = digitalRead(PWR_KEY) == LOW;
  bool previousRaw = stable;
  uint32_t changed = millis(), pressed = 0;
  for (;;) {
    bool raw = digitalRead(PWR_KEY) == LOW;
    uint32_t now = millis();
    if (raw != previousRaw) { previousRaw = raw; changed = now; }
    if (now - changed >= 40) {
      stable = raw;
      if (!armed) { if (!stable) armed = true; }
      else if (stable && !down) { down = true; pressed = now; }
      else if (!stable && down) {
        down = false;
        if (now - pressed >= 1500) shutdownRequested.store(true);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
void shutdownUnit() {
  Serial.println("POWER: saving and shutting down");
  if (recording) stopNote(true, false);
  if (sdOK) { saveCurrentJob(); SD_MMC.end(); sdOK = false; }
  if (audioOK) { mic.end(); audioOK = false; }
  digitalWrite(AUDIO_PA, LOW);
  WiFi.disconnect(true, false);
  bool locked = displayGate && xSemaphoreTake(displayGate, pdMS_TO_TICKS(10000)) == pdTRUE;
  if (locked) {
    display.setFullWindow(); display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE); display.setTextColor(GxEPD_BLACK);
      display.setTextSize(2); display.setCursor(25, 85); display.print("POWER OFF");
    } while (display.nextPage());
    display.hibernate();
  }
  digitalWrite(EPD_PWR, HIGH);
  Serial.println("POWER: battery hold released; USB may keep power present");
  Serial.flush();
  digitalWrite(BAT_HOLD, LOW);
  delay(300);
  // If USB keeps the processor powered, sleep until PWR is pressed again.
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PWR_KEY, 0);
  esp_deep_sleep_start();
}
void buttonPoll() {
static bool isDown = false; static unsigned long downMs = 0; bool down = (digitalRead(BOOT_KEY) == LOW); if (down && !isDown) { isDown = true; downMs = millis(); }
if (!down && isDown) { isDown = false; unsigned long length = millis() - downMs; if (length < 50) return; if (length >= 6000) endSession(true); else if (length >= 2500) endSession(false); else if (recording) stopNote(); else startNote(); }
}
#include "CompanionApi.h"

void setup() {
pinMode(BAT_HOLD, OUTPUT); digitalWrite(BAT_HOLD, HIGH); batteryBegin(); Serial.begin(115200); delay(300); Serial.println("FORKLIFT LOGGER: HOME + HOTSPOT + PWR + USB URL"); beginServerSettings(); pinMode(BOOT_KEY, INPUT_PULLUP); pinMode(PWR_KEY, INPUT_PULLUP); xTaskCreate(powerButtonWorker, "powerkey", 2048, nullptr, 1, nullptr); setenv("TZ", TIMEZONE_TZ, 1); tzset(); Wire.begin(I2C_SDA, I2C_SCL, 100000);
audioOK = initCodec();
time_t cached; if (rtcRead(cached)) { timeval tv = {cached, 0}; settimeofday(&tv, nullptr); } pinMode(EPD_PWR, OUTPUT); digitalWrite(EPD_PWR, LOW); delay(50); SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS); display.init(0);
statusMutex = xSemaphoreCreateMutex(); displayGate = xSemaphoreCreateMutex(); xTaskCreatePinnedToCore(displayWorker, "epaper", 6144, nullptr, 1, nullptr, 0);
if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0)) Serial.println("SD pin setup failed"); sdOK = SD_MMC.begin("/sdcard", true) && SD_MMC.cardType() != CARD_NONE;
if (sdOK) { SD_MMC.mkdir("/jobs"); File f = SD_MMC.open("/current.txt", FILE_READ); if (f) { activeJob = f.readStringUntil('\n'); activeJob.trim(); f.close(); } }
setStatus(!sdOK ? "SD ERROR" : (!audioOK ? "AUDIO ERROR" : (queuedVisitCount() > 0 ? "VISITS PENDING" : "READY")));
if (audioOK) playFeedback(FB_START);
connectAndSync();
}
void loop() {
if (!recording && !shutdownRequested.load()) batteryPoll();
if (!recording && !shutdownRequested.load()) companionPoll();
if (!recording && !shutdownRequested.load()) pollServerSettings();
if (shutdownRequested.load()) shutdownUnit();
buttonPoll();
if (recording) {
uint8_t buffer[1024]; size_t n = mic.readBytes((char*)buffer, sizeof(buffer));
if (n > 0) { for (size_t i = 0; i + 1 < n; i += 2) { if (writeFailure) break; addAdpcmSample(pcmSample(buffer + i)); ++samplesRecorded; } if (writeFailure) { stopNote(false); setStatus("SD WRITE ERROR"); } }
if (millis() - noteStartMs >= MAX_NOTE_SECONDS * 1000UL) stopNote();
} else { if (millis() - lastSync > SYNC_INTERVAL_MS) { lastSync = millis(); connectAndSync(); } delay(15); }
}