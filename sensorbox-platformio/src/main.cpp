#include <Arduino.h>
#include "driver/rtc_io.h"
#include "esp_ota_ops.h"
#include <stdarg.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Reporter.h>
#include <Preferences.h>
#include <prefs.h>
#include <my_utils.h>
#include <sensors_poll.h>
#include <apmode.h>
#include <file_ring_buffer.h>
#include <ble.h>

const char *TAG_MAIN = "main";

// sleep stuff
// RTC_DATA_ATTR uint8_t measureCountModPm = 0;
// RTC_DATA_ATTR uint8_t measureCountModSubmit = 0;
// RTC_DATA_ATTR uint8_t measureCountModNtp = 0;
const int collectingIntervalActive = 6000;
uint64_t timeItTime = 0;
RTC_DATA_ATTR uint16_t touchThreshold = 0;

const long forceWakeupTimeout = 20 * 1000;
const long vocInterval = 30 * 60 * 1000;

int page = 0;
bool touchInterruptProcessed = true;
long touchInterruptTime = 0;
bool buttonPressedToStartApBool = false;

const uint32_t wifiSecureConnectTimeoutMs = 1600;
const uint32_t wifiConnectTimeoutMs = 500;
RTC_DATA_ATTR uint8_t lastConnectedWifiChannel = 0;
RTC_DATA_ATTR uint8_t lastBssid[6] = {0};

// wakeup reasons
const uint8_t WAKEUP_FIRST_BOOT = 1 << 0;
const uint8_t WAKEUP_MEASURE = 1 << 1;
const uint8_t WAKEUP_SUBMIT = 1 << 2;
const uint8_t WAKEUP_MEASURE_PM = 1 << 3;
const uint8_t WAKEUP_AP_MODE = 1 << 5;

RTC_DATA_ATTR uint8_t wakeupReasonsBitset = WAKEUP_FIRST_BOOT | WAKEUP_MEASURE;

SemaphoreHandle_t gotIpTaskSemaphore = xSemaphoreCreateBinary();
char version_str[64];

#ifdef THE_BOX
void printToLcdAndSerial(const char *text, bool clear = false);
void printToLcdAndSerial(String &text, bool clear = false);
void printSensorDataToLcd();
#endif

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info);
void WiFiLostIP(WiFiEvent_t event, WiFiEventInfo_t info);
void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info);
void connectToWiFiIfNeeded();
void touchCallback();
void pollAllSensors();
void calibrateTouchTask(void *arg);

void timeit(const char *msg = "timeit")
{
  Serial.print(msg);
  Serial.print(" ");
  Serial.println(millis() - timeItTime);
  timeItTime = millis();
}

const char *get_wakeup_reason_str()
{
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch (wakeup_reason)
  {
  case ESP_SLEEP_WAKEUP_EXT0:
    return "RTC_IO";
  case ESP_SLEEP_WAKEUP_EXT1:
    return "RTC_CNTL";
  case ESP_SLEEP_WAKEUP_TIMER:
    return "Timer";
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    return "Touchpad";
  case ESP_SLEEP_WAKEUP_UNDEFINED:
    return "Fresh";
  default:
    return "Wakeup was not caused by deep sleep";
  }
}

void runApMode()
{
#ifdef HAS_DISPLAY
  updateLcdStatus(true);
  enableBacklight(true);
  lcd.setTextColor(0xFFFF, 0x0000);
  printToLcdAndSerial("Setup Mode");
#else
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ON_STATE);
#endif

  bool success = frb_save_from_rtc(true);

  if (!success)
    Serial.println("Failed to save from rtc");

  apModeLoop();

  lastConnectedWifiChannel = 0;
  // set lastBssid to all 0s
  memset(lastBssid, 0, sizeof(lastBssid));
}

#ifdef HAS_DISPLAY

void printToLcdAndSerial(const char *text, bool clear)
{
  Serial.println(text);
  if (clear)
    lcd.clearDisplay();
  lcd.println(text);
  lcd.display();
}

void printToLcdAndSerial(String &text, bool clear)
{
  printToLcdAndSerial(text.c_str(), clear);
}

void printSensorDataToLcd()
{
  char strbuf[128];
  if (page == 0)
  {
    char motion = ' ';
    char conn = ' ';

    if (readings.motion)
    {
      // lcd.setCursor(12 * 4, 5);
      motion = 'm';
    }

    switch (WiFi.status())
    {
    case WL_CONNECTED:
      conn = 'i';
      break;

    case WL_DISCONNECTED:
      conn = 'x';
      break;
    case WL_CONNECTION_LOST:
      conn = 'l';
      break;
    case WL_CONNECT_FAILED:
      conn = 'f';
      break;
    case WL_NO_SSID_AVAIL:
      conn = 'n';
      break;
    case WL_IDLE_STATUS:
      conn = 's';
      break;
    default:
      conn = '.';
      break;
    }
    lcd.clearDisplay();
    lcd.setTextWrap(false);
    lcd.drawFastVLine(0, 0, 48, 0xFFFF);
    lcd.setTextColor(0x0000, 0xFFFF);
    lcd.setCursor(1, 0 * 8);
    lcd.print("T");
    lcd.setCursor(1, 1 * 8);
    lcd.print("H");
    lcd.setCursor(1, 2 * 8);
    lcd.print("P");
    lcd.setCursor(1, 3 * 8);
    lcd.print("L");
    lcd.setCursor(1, 4 * 8);
    lcd.print("D");
    lcd.setCursor(1, 5 * 8);
    lcd.print("S");

    lcd.setTextColor(0xFFFF, 0xFFFF);

    lcd.setCursor(8, 0 * 8);
    lcd.printf("%.2f %.2f C", readings.temperature, heatIndex(readings.temperature, readings.humidity));
    lcd.setCursor(8, 1 * 8);
    lcd.printf("%.2f %s", readings.humidity, units.humidity);
    lcd.setCursor(8, 2 * 8);
    lcd.printf("%.1f %.1f", readings.pressure, seaPressure(readings.pressure, readings.temperature, prefs.altitudeM));
    lcd.setCursor(8, 3 * 8);
    lcd.printf("%.2f %s", readings.luminosity, units.light);
    lcd.setCursor(8, 4 * 8);
    lcd.printf("%.1f %.1f ug", oobLastPm25x10 / 10.0f, oobLastPm10x10 / 10.0f);
    lcd.setCursor(8, 5 * 8);
    lcd.printf("%.2f %s %c%c", readings.soundDbA, units.sound, motion, conn);

    // lcd.println(strbuf);
  }
  else if (page == 1)
  {
    float irPercent = ((float)readings.ir / readings.visible) * 100;
    sprintf(strbuf,
            "%hd ppm\n%.1f%s %d%s\n%.1f %%IR\n%d/%d touch\n%.2f V\nv%s",
            lastCo2,
            temperatureRead(), "C",
            WiFi.RSSI(), units.rssi,
            irPercent,
            touchRead(TOUCH_PIN),
            touchThreshold,
            readings.voltageAvg,
            version_str);
    lcd.clearDisplay();
    lcd.setCursor(0, 0);
    lcd.setTextWrap(true);
    lcd.println(strbuf);
  }
  lcd.display();
}

#endif

void doOnFreshBoot()
{
  esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason != prefs.lastResetReason && resetReason != ESP_RST_DEEPSLEEP
      // resetReason != ESP_RST_POWERON &&
      // && resetReason != ESP_RST_SW
  )
  {
    Preferences preferences;
    preferences.begin("preferences");
    preferences.putUInt(PREF_LAST_RESET_REASON, esp_reset_reason());
    preferences.end();
  }
#ifdef HAS_DISPLAY
  enableBacklight(true);

  // lcd
  updateLcdStatus(true);
  lcd.display(); // show splashscreen
  lcd.setTextColor(0xFFFF, 0x0000);

  printToLcdAndSerial(version_str);
  String tmp2 = "lastReset: " + String(prefs.lastResetReason);
  printToLcdAndSerial(tmp2);
  printToLcdAndSerial("init scd41");
#else
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ON_STATE);
#endif

  stayAwakeUntilTime = millis() + forceWakeupTimeout;
}

void doOnTouchpadWakeup()
{
  touchInterruptTime = millis();
  stayAwakeUntilTime = millis() + forceWakeupTimeout;

#ifdef HAS_DISPLAY
  updateLcdStatus(true);
  printSensorDataToLcd();
#else
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ON_STATE);
#endif
  connectToWiFiIfNeeded();
}

void doOnEveryBoot()
{
  // btStop();
  Serial.begin(DEBUG_BAUD_RATE);

  ESP_LOGW(TAG_MAIN, "Wakeup: %s, mPm: %u, mSubmit: %u, bootTime: %lu", get_wakeup_reason_str(), measureCountModPm, measureCountModSubmit, millis());

  initFromPrefs();

  const esp_app_desc_t *appDesc = esp_app_get_description();

  sprintf(version_str, "%s-%sv%s", prefs.uriPrefix, appDesc->date, appDesc->version);
  Serial.print("Build: ");
  Serial.println(version_str);

  if (esp_reset_reason() == ESP_RST_BROWNOUT && prefs.lastResetReason == ESP_RST_BROWNOUT)
  {
    ESP_LOGE(TAG_MAIN, "Repeated brownouts, going to sleep");
    Serial.flush();
    esp_deep_sleep_start();
  }

#ifdef SUPPORTS_TOUCH
  xTaskCreate(
      calibrateTouchTask,
      "calibrateTouchTask",
      configMINIMAL_STACK_SIZE * 2,
      NULL,
      1,
      NULL);
#endif

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TOUCHPAD)
  {
    wasTouchpadWakeup = true;
    doOnTouchpadWakeup();
  }
  else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1 || !isSetupCompleted())
  {
    runApMode();
    esp_deep_sleep(1000000ULL * 1);
  }
}

void prepareBootIntoApMode()
{
  Serial.println("prepareBootIntoApMode");
  detachInterrupt(digitalPinToInterrupt(BUTTON_PIN));
  buttonPressedToStartApBool = false;
  priorityQueueWrite(wakeupTasksQ, WakeupTask{WAKEUP_AP_MODE, rtcMillis() + 1 * 1000});
  stayAwakeUntilTime = millis() + 1000; // todo fix
  // esp_sleep_enable_timer_wakeup(1 * 1000 * 1000);
  // esp_deep_sleep_start();
}

void doWhileAwakeLoop()
{
  uint64_t collectingTime = millis();

#ifdef HAS_DISPLAY
  if (!isIdle())
    printSensorDataToLcd();
#endif

  if (bitsetContains(wakeupReasonsBitset, WAKEUP_FIRST_BOOT))
  {
    connectToWiFiIfNeeded();
  }

  while (!isIdle())
  {
    if (millis() - collectingTime > collectingIntervalActive)
    {
      pollAllSensors();
      collectingTime = millis();

#ifdef HAS_DISPLAY
      updateLcdStatus(true);
      printSensorDataToLcd();
#endif
    }

    if (!touchInterruptProcessed)
    {
      touchInterruptProcessed = true;
#ifdef SUPPORTS_TOUCH
      touchAttachInterrupt(TOUCH_PIN, touchCallback, touchThreshold);
#endif
#ifdef HAS_DISPLAY
      printSensorDataToLcd();
#endif
    }

    if (buttonPressedToStartApBool)
      prepareBootIntoApMode();

    delay(200);
  }

#ifdef HAS_DISPLAY
  if (lcdStarted)
    lcdPowerSaving(true);
#endif
}

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  uint8_t *bssid = WiFi.BSSID();

  if (bssid != NULL)
  {
    lastConnectedWifiChannel = WiFi.channel();
    memcpy(lastBssid, bssid, 6);
  }
}

void WiFiLostIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
  ESP_LOGW(TAG_MAIN, "Lost IP");
}

void syncTime()
{
  struct tm timeinfo;
  uint64_t oldTime = rtcMillis();
  unsigned long timeTaken = millis();

  configTime(0, 0, prefs.ntpServer);
  bool gotNetworkTime = false;

  delay(200);
  gotNetworkTime = getLocalTime(&timeinfo);

  timeTaken = millis() - timeTaken;

  if (!gotNetworkTime)
  {
    ESP_LOGE(TAG_MAIN, "Failed to obtain time");
  }
  else
  {
    printRtcMillis(prefs.timezoneOffsetS);

    // fix readings timestamps after the first NTP sync
    if (oldTime / 1000 < APR_20_2023_S)
    {
      if (prefs.lastChangedS == 0)
        savePrefs(); // save the time to preferences
      fixReadingsTimestamps(&readingsBuffer, (oldTime + timeTaken) / 1000);
    }

    // fix wakeup tasks timestamps, they all should be in the future
    fixPqTimestamps(wakeupTasksQ, oldTime + timeTaken);
  }
}

void gotIpTask(void *args)
{
  bool timeSyncDone = false;

  if (rtcSecs() <= APR_20_2023_S)
  {
    syncTime();

    if (rtcSecs() > APR_20_2023_S)
    {
      timeSyncDone = true;
    }
  }

  if (rtcSecs() > APR_20_2023_S)
  {
    xTaskCreate(
        coap_io_loop,
        "coap_io_loop",
        1024 * 6,
        NULL,
        1,
        NULL);

    xTaskCreate(
        coap_readings_report_loop,
        "coap_readings_report_loop",
        1024 * 4,
        NULL,
        1,
        NULL);

    if (!timeSyncDone)
    {
      syncTime();
    }
  }

  xSemaphoreGive(gotIpTaskSemaphore);

  vTaskDelete(NULL);
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.print("Got IP address: ");
  Serial.print(WiFi.localIP());
  Serial.print(" in ");
  Serial.println(millis() - timeItTime);

  xTaskCreate(
      gotIpTask,
      "gotIpTask",
      2048,
      NULL,
      1,
      NULL);
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  if (info.wifi_sta_disconnected.reason == WIFI_REASON_NO_AP_FOUND)
  {
    // forget wifi channel and bssid, they may have changed now
    lastConnectedWifiChannel = 0;
    memset(lastBssid, 0, sizeof(lastBssid));
  }

  Serial.println("Disconnected");
}

void connectToWiFiIfNeeded()
{
  if (WiFi.status() == WL_CONNECTED)
    return;
  timeItTime = millis();

  WiFi.setSleep(true);
  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_FAST_SCAN);

  IPAddress primaryDNS(1, 1, 1, 1);
  IPAddress secondaryDNS(1, 0, 0, 1);

  if (strlen(prefs.staticIp) > 0 && strlen(prefs.staticGateway) > 0 && strlen(prefs.staticSubnet) > 0)
  {
    IPAddress gatewayIP;
    gatewayIP.fromString(prefs.staticGateway);
    IPAddress subnetMask;
    subnetMask.fromString(prefs.staticSubnet);
    IPAddress myIP;
    myIP.fromString(prefs.staticIp);

    WiFi.config(myIP, gatewayIP, subnetMask, primaryDNS, secondaryDNS);
  }

  uint8_t *bssid;
  if (lastBssid[0] == 0 && lastBssid[1] == 0 && lastBssid[2] == 0 && lastBssid[3] == 0 && lastBssid[4] == 0 && lastBssid[5] == 0)
    bssid = NULL;
  else
    bssid = lastBssid;

  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiLostIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_LOST_IP);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  const char *wifiPassword = strlen(prefs.wifiPassword) > 0 ? prefs.wifiPassword : NULL;

  WiFi.begin(prefs.wifiSsid, wifiPassword, lastConnectedWifiChannel, bssid);

#ifndef THE_BOX
  // this esp32 is kept very close to the router
  WiFi.setTxPower(WIFI_POWER_2dBm);
#endif
}

IRAM_ATTR void touchCallback()
{
  if (millis() - touchInterruptTime > 500 && !isIdle()) // debounce
  {
    page = (page + 1) % 2;

    touchInterruptProcessed = false;
    touchInterruptTime = millis();
    stayAwakeUntilTime = millis() + forceWakeupTimeout;
  }
}

IRAM_ATTR void buttonPressedToStartAp()
{
  buttonPressedToStartApBool = true;
}

void pollAllSensors()
{
  readings = invalidReadings;
  pollingCtr = 0;

  createPollingTask(pollMainSensors, "pollMainSensors");

#ifdef THE_BOX
  createPollingTask(pollAudio, "pollAudio");

  // if (rtcMillis() - sdsStartTime > prefs.collectIntvlMs * prefs.pmSensorEvery)
  // {
  //   createPollingTask(startSds, "startSds");
  //   priorityQueueWrite(wakeupTasksQ, WakeupTask{WAKEUP_MEASURE_PM, rtcMillis() + PM_SENSOR_RUNTIME_SECS * 1000});
  // }
#endif

  EventBits_t pollingCompleteBitset = (1 << pollingCtr) - 1;
  xEventGroupWaitBits(pollingEventGroup, pollingCompleteBitset, pdTRUE, pdTRUE, portMAX_DELAY);

  // poll heap stats towards the end
  pollBoardStats();

  if (lastAwakeDuration > 0)
    readings.awakeTime = lastAwakeDuration;

#ifdef THE_BOX
  // start and schedule sds after all other sensors have been polled to avoid voltage ripple while reading
  if (measureCountModPm == 0 && !sdsRunning)
  {
    startSds();
    priorityQueueWrite(wakeupTasksQ, WakeupTask{WAKEUP_MEASURE_PM, rtcMillis() + PM_SENSOR_RUNTIME_SECS * 1000});
  }

  if (!oobValuesUsed)
  {
    readings.pm25x10 = oobLastPm25x10;
    readings.pm10x10 = oobLastPm10x10;
    oobValuesUsed = true;
  }
#endif

  readings.timestampS = rtcSecs();

  Readings *readingsCopy = new Readings(readings);
  enqueueReadings(readingsCopy);
}

#ifdef SUPPORTS_TOUCH
void calibrateTouchTask(void *arg)
{
  uint8_t count;
  uint16_t totalTouchValue;
  uint16_t touch;

  touchAttachInterrupt(TOUCH_PIN, touchCallback, touchThreshold);

  if (wasTouchpadWakeup)
    delay(10000);

  // calculate the avarage value
  while (true)
  {
    count = 0;
    totalTouchValue = 0;
    touch = 0;

    while (count < 3)
    {
      touch = touchRead(TOUCH_PIN);
      if (touch > 1)
      {
        count++;
        totalTouchValue += touch;
      }
      delay(5);
    }

    if (touch > 0 && count > 0)
    {
      touchThreshold = totalTouchValue / count - 6;
      Serial.print("touchThreshold: ");
      Serial.println(touchThreshold);
    }
    // touch
    touchAttachInterrupt(TOUCH_PIN, touchCallback, touchThreshold);

    delay(10 * 1000);
  }
}
#endif

void setup()
{
  doOnEveryBoot();

  uint8_t nextWakeupReasonsBitset = 0;

  if (bitsetContains(wakeupReasonsBitset, WAKEUP_FIRST_BOOT))
  {
    doOnFreshBoot();
    bitsetAdd(nextWakeupReasonsBitset, WAKEUP_MEASURE);
  }

  if (bitsetContains(wakeupReasonsBitset, WAKEUP_MEASURE))
  {
    pollAllSensors();
    Serial.println("pollAllSensors");

    bitsetAdd(nextWakeupReasonsBitset, WAKEUP_MEASURE);

    // if (prefs.collectIntvlMs > 60000)
    //   priorityQueueWrite(wakeupTasksQ, WakeupTask{WAKEUP_MEASURE_CO2_ONLY, rtcMillis() + prefs.collectIntvlMs / 2});

    measureCountModPm = (measureCountModPm + 1) % prefs.pmSensorEvery;
    measureCountModSubmit = (measureCountModSubmit + 1) % (prefs.reportIntvlMs / prefs.collectIntvlMs);

    // do this after incrementing
    if (isIdle() && !bitsetContains(wakeupReasonsBitset, WAKEUP_SUBMIT) && measureCountModSubmit == 0)
      bitsetAdd(nextWakeupReasonsBitset, WAKEUP_SUBMIT);
  }

  if (!isIdle())
  {
#ifdef THE_BOX
    pinMode(BUTTON_PIN, INPUT_PULLUP);
#endif
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonPressedToStartAp, FALLING);
    doWhileAwakeLoop();
  }

// out of band wakeup
#ifdef THE_BOX
  if (bitsetContains(wakeupReasonsBitset, WAKEUP_MEASURE_PM) && sdsRunning)
  {
    pollSds();
  }
#endif

  if (bitsetContains(wakeupReasonsBitset, WAKEUP_AP_MODE))
    runApMode();

  // do the rest
  if (bitsetContains(wakeupReasonsBitset, WAKEUP_SUBMIT))
  {
    connectToWiFiIfNeeded();
    WiFi.waitForConnectResult(strlen(prefs.wifiPassword) > 0 ? wifiSecureConnectTimeoutMs
                                                             : wifiConnectTimeoutMs);
  }

  if (nextWakeupReasonsBitset != 0)
  {
    priorityQueueWrite(wakeupTasksQ, WakeupTask{nextWakeupReasonsBitset, rtcMillis() + prefs.collectIntvlMs});
  }

  // pqPrint(wakeupTasksQ);
  // end

  if (WiFi.status() == WL_CONNECTED)
  {
    xSemaphoreTake(gotIpTaskSemaphore, portMAX_DELAY);

    if (rtcSecs() > APR_20_2023_S)
      xSemaphoreTake(coap_loop_semaphore, portMAX_DELAY);
  }

  if (WiFi.status() != WL_STOPPED)
  {
    WiFi.disconnect(true);
  }
  LittleFS.end();

  // send a BLE adv

  uint8_t advData[31] = {0};
  uint8_t advDataLen = readingsToAdvData(readings, prefs.uriPrefix[0], advData);

  uint64_t t1 = millis();
  do_ble_adv(advData, advDataLen);
  uint64_t t2 = millis() - t1;
  Serial.printf("BLE adv took: %llu ms\n", t2);

  WakeupTask *wt = priorityQueuePop(wakeupTasksQ);
  // make times in the past to 1 sec
  int64_t diff = static_cast<int64_t>(wt->timestamp) - static_cast<int64_t>(rtcMillis());
  uint64_t willWakeInMs = max(500LL, diff);

  // if (!bitsetContains(wakeupReasonsBitset, WAKEUP_MEASURE_CO2_ONLY) || sdsRunning)
  lastAwakeDuration = millis();

  wakeupReasonsBitset = wt->wakeupReasonsBitset;

  Serial.print("\nAwakeFor: ");
  Serial.print(millis());
  Serial.print(" willWakeInMs: ");
  Serial.print(willWakeInMs);
  Serial.print(" with ");
  Serial.println(wakeupReasonsBitset, BIN);
  Serial.flush();

#ifdef SUPPORTS_TOUCH
  touchAttachInterrupt(TOUCH_PIN, touchCallback, touchThreshold);
  esp_sleep_enable_touchpad_wakeup();
#endif

  esp_sleep_enable_timer_wakeup(willWakeInMs * 1000ULL);
  // mark as consumed
  wt->timestamp = 0;

  esp_deep_sleep_start();
}

void loop()
{
  delay(1000);
}