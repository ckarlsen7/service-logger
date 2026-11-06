#pragma once
#include <Arduino.h>
#include <atomic>
// Waveshare ESP32-S3-ePaper-1.54 V2 factory firmware uses
// ADC1 channel 3 (GPIO4) and a 2:1 divider; full=4.12 V, empty=3.0 V.
// Percentage is a voltage estimate, not a calibrated fuel-gauge reading.
static constexpr int BATTERY_ADC_PIN = 4;
static std::atomic<int> batteryMv{-1};
static uint32_t batteryLastRead = 0;
static bool batteryHasRead = false;
static int batteryPercentFromMv(int mv) {
  if (mv < 2000 || mv > 4500) return -1;
  if (mv <= 3000) return 0;
  if (mv >= 4120) return 100;
  return (mv - 3000) * 100 / 1120;
}
static int batteryPercent() { return batteryPercentFromMv(batteryMv.load()); }
static void batteryPoll(bool force = false) {
  if (!force && batteryHasRead && millis() - batteryLastRead < 15000UL) return;
  uint32_t sum = 0;
  for (int i=0; i<16; i++) sum += analogReadMilliVolts(BATTERY_ADC_PIN);
  int measured = (int)((sum * 2U + 8U) / 16U);
  int previous = batteryMv.load();
  if (batteryPercentFromMv(measured) < 0) batteryMv.store(-1);
  else batteryMv.store(previous < 0 ? measured : (previous * 3 + measured) / 4);
  batteryLastRead = millis(); batteryHasRead = true;
}
static void batteryBegin() {
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  batteryPoll(true);
}
static String batteryJson() {
  return "{\"millivolts\":" + String(batteryMv.load()) +
    ",\"percent\":" + String(batteryPercent()) + ",\"estimated\":true}";
}
static void drawBatteryBar() {
  int percent = batteryPercent();
  display.drawRect(8, 65, 42, 12, GxEPD_BLACK);
  display.fillRect(50, 68, 3, 6, GxEPD_BLACK);
  if (percent >= 0) {
    int bars = (percent + 19) / 20;
    for (int i=0; i<bars; i++) display.fillRect(10+i*8, 67, 6, 8, GxEPD_BLACK);
  }
  display.setTextSize(1); display.setTextColor(GxEPD_BLACK);
  display.setCursor(60, 67);
  display.print(percent < 0 ? "BAT ?" : "~" + String(percent) + "%");
  if (percent >= 0) {
    display.setCursor(128, 67);
    display.print(String(batteryMv.load() / 1000.0f, 2) + "V");
  }
}
