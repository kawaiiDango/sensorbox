#include <Arduino.h>
#include "driver/adc.h"
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

#ifdef OTA_ENABLED
#include <ArduinoOTA.h>
#endif

#define NTP_SYNC_INTERVAL_S 60 * 60 * 2 // 2 hours
#define SERIAL_ENABLED true

const char *TAG_MAIN = "main";

// sleep stuff
RTC_DATA_ATTR uint bootCount = 0;
const int collectingIntervalActive = 6000;
RTC_DATA_ATTR int64_t collectingTime = 0;
RTC_DATA_ATTR int64_t reportingTime = 0;
int64_t timeItTime = 0;
RTC_DATA_ATTR uint16_t touchThreshold = 0;

const long forceWakeupTimeout = 20 * 1000;
const long vocInterval = 30 * 60 * 1000;

int page = 0;
bool touchInterruptProcessed = true;
long touchInterruptTime = 0;
bool buttonPressedToStartApBool = false;

const uint32_t wifiSecureConnectTimeoutMs = 3000;
const uint32_t wifiConnectTimeoutMs = 500;
RTC_DATA_ATTR uint8_t lastConnectedWifiChannel = 0;
RTC_DATA_ATTR uint8_t lastBssid[6] = {0, 0, 0, 0, 0, 0};
TaskHandle_t arduino_task_handle;

// wakeup reasons
const uint8_t WAKEUP_FIRST_BOOT = 1 << 0;
const uint8_t WAKEUP_MEASURE = 1 << 1;
const uint8_t WAKEUP_SUBMIT = 1 << 2;
const uint8_t WAKEUP_MEASURE_PM = 1 << 3;
const uint8_t WAKEUP_MEASURE_VOC = 1 << 4;
const uint8_t WAKEUP_AP_MODE = 1 << 5;

RTC_DATA_ATTR uint8_t wakeupReasonsBitset = WAKEUP_FIRST_BOOT | WAKEUP_MEASURE;

SemaphoreHandle_t gotIpSemaphore = NULL;
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
IRAM_ATTR void touchCallback();
void pollAllSensors();
void calibrateTouchTask(void *arg);

void timeit(const char *msg = "timeit")
{
  Serial.print(msg);
  Serial.print(" ");
  Serial.println(rtcMillis() - timeItTime);
  timeItTime = rtcMillis();
}

void print_wakeup_reason()
{
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  Serial.println();

  switch (wakeup_reason)
  {
  case ESP_SLEEP_WAKEUP_EXT0:
    Serial.print("RTC_IO ");
    break;
  case ESP_SLEEP_WAKEUP_EXT1:
    Serial.print("RTC_CNTL");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.print("Timer");
    break;
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    Serial.print("Touchpad");
    break;
  case ESP_SLEEP_WAKEUP_UNDEFINED:
    Serial.print("Fresh");
    break;
  default:
    Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
    break;
  }
}

void runApMode()
{
#ifdef THE_BOX
  updateLcdStatus(true);
  enableBacklight(true);
  lcd.setTextColor(0xFFFF, 0x0000);
  printToLcdAndSerial("Setup Mode");
  setCpuFreqIfNeeded(80);
#endif

  bool success = frb_save_from_rtc(true);

  if (!success)
    Serial.println("Failed to save from rtc");

  apModeLoop();

  lastConnectedWifiChannel = 0;
  // set lastBssid to all 0s
  memset(lastBssid, 0, 6);
}

#ifdef THE_BOX

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
    lcd.printf("%.1f %.1f ug", oobLastPm25, oobLastPm10);
    lcd.setCursor(8, 5 * 8);
    lcd.printf("%.2f %s %c%c", readings.soundDbA, units.sound, motion, conn);

    // lcd.println(strbuf);
  }
  else if (page == 1)
  {
    float irPercent = ((float)readings.ir / readings.visible) * 100;
    sprintf(strbuf,
            "%.1f %s\n%d %s\n%.1f %%IR\n%d/%d touch\n%.2f V\nv%s",
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
#ifdef THE_BOX
  enableBacklight(true);

  // lcd
  updateLcdStatus(true);
  lcd.display(); // show splashscreen
  lcd.setTextColor(0xFFFF, 0x0000);

  printToLcdAndSerial(version_str);
  String tmp2 = "lastReset: " + String(prefs.lastResetReason);
  printToLcdAndSerial(tmp2);
  printToLcdAndSerial("setup wifi");

#else
  digitalWrite(LED_PIN, LOW);
  delay(3000);
  digitalWrite(LED_PIN, HIGH);
#endif

  // wifi setup

  Serial.println("connecting");
  connectToWiFiIfNeeded();

  stayAwakeUntilTime = millis() + forceWakeupTimeout;
}

void doOnTouchpadWakeup()
{
  touchInterruptTime = millis();
  stayAwakeUntilTime = millis() + forceWakeupTimeout;

#ifdef THE_BOX
  updateLcdStatus(true);
  printSensorDataToLcd();
#endif
  connectToWiFiIfNeeded();
}

void doOnEveryBoot()
{
  arduino_task_handle = xTaskGetCurrentTaskHandle();
  bootCount++;
  btStop();
  initFromPrefs();

  if (SERIAL_ENABLED)
    Serial.begin(DEBUG_BAUD_RATE);
  print_wakeup_reason();
  Serial.print(" boot: ");
  Serial.print(bootCount);
  Serial.print(" ");
  Serial.print("timeTaken");
  Serial.print(" ");
  Serial.print(millis());
  Serial.println(wakeupReasonsBitset, BIN);

  const esp_app_desc_t *appDesc = esp_ota_get_app_description();

  sprintf(version_str, "%s-%sv%s", appDesc->date, appDesc->time, appDesc->version);
  Serial.print("Build: ");
  Serial.println(version_str);

  if (esp_reset_reason() == ESP_RST_BROWNOUT && prefs.lastResetReason == ESP_RST_BROWNOUT)
  {
    ESP_LOGE(TAG_MAIN, "Repeated brownouts, going to sleep");
    Serial.flush();
    esp_deep_sleep(1000000ULL * 60 * 60 * 24);
  }

  xTaskCreate(
      calibrateTouchTask,
      "calibrateTouchTask",
      configMINIMAL_STACK_SIZE * 2,
      NULL,
      1,
      NULL);

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
#ifdef THE_BOX
  if (!isIdle())
    printSensorDataToLcd();
#endif

  while (!isIdle())
  {
    if (rtcMillis() - collectingTime > collectingIntervalActive)
    {
      pollAllSensors();
#ifdef THE_BOX
      updateLcdStatus(true);
      printSensorDataToLcd();
#endif
    }

#ifdef OTA_ENABLED
    ArduinoOTA.handle();
#endif

    if (!touchInterruptProcessed)
    {
      touchInterruptProcessed = true;
      touchAttachInterrupt(TOUCH_PIN, touchCallback, touchThreshold);
#ifdef THE_BOX
      printSensorDataToLcd();
#endif
    }

    if (buttonPressedToStartApBool)
      prepareBootIntoApMode();

    delay(200);
  }

#ifdef THE_BOX
  if (lcdStarted)
    lcd.powerSaving(true);
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

void gotIpTask(void *args)
{
  // ntp
  if (lastNtpSyncTimeS == 0 || rtcSecs() - lastNtpSyncTimeS > NTP_SYNC_INTERVAL_S)
  {
    struct tm timeinfo;
    int64_t oldTime = rtcMillis();
    unsigned long timeTaken = millis();

    configTime(0, 0, prefs.ntpServer);
    bool gotNetworkTime = getLocalTime(&timeinfo);

    timeTaken = millis() - timeTaken;

    if (!gotNetworkTime)
    {
      ESP_LOGE(TAG_MAIN, "Failed to obtain time");
    }
    else
    {
      lastNtpSyncTimeS = rtcSecs();
      printRtcMillis(prefs.timezoneOffsetS);

      if (oldTime / 1000 < APR_20_2023_S)
      {
        if (prefs.lastChangedS == 0)
          savePrefs(); // save the time to preferences
        fixTimestampsBeforeNtp(&readingsBuffer, (oldTime + timeTaken) / 1000);
      }
    }
  }

  if (rtcSecs() > APR_20_2023_S)
  {
    coap_loop_running = true;

    xTaskCreate(
        coap_io_loop,
        "coap_io_loop",
        1024 * 4,
        NULL,
        1,
        NULL);
    xSemaphoreTake(coap_prepare_semaphore, portMAX_DELAY);
    xTaskCreate(
        coap_readings_report_loop,
        "coap_readings_report_loop",
        1024 * 4,
        NULL,
        1,
        NULL);

    reportingTime = rtcMillis();
  }

  xSemaphoreGive(gotIpSemaphore);

  vTaskDelete(NULL);
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.print("Got IP address: ");
  Serial.print(WiFi.localIP());
  Serial.print(" in ");
  Serial.println(millis() - timeItTime);

  setCpuFreqIfNeeded(80);

  gotIpSemaphore = xSemaphoreCreateBinary();
  xTaskCreate(
      gotIpTask,
      "gotIpTask",
      2048,
      NULL,
      1,
      NULL);

#ifdef OTA_ENABLED
  if (!isIdle())
  {
    ArduinoOTA.onError([](ota_error_t error)
                       {
                         Serial.print("OTA Error: ");
                         if (error == OTA_AUTH_ERROR)
                           Serial.print("Auth Failed");
                         else if (error == OTA_BEGIN_ERROR)
                           Serial.print("Begin Failed");
                         else if (error == OTA_CONNECT_ERROR)
                           Serial.print("Connect Failed");
                         else if (error == OTA_RECEIVE_ERROR)
                           Serial.print("Receive Failed");
                         else if (error == OTA_END_ERROR)
                           Serial.print("End Failed");
                         Serial.println();
                         Serial.flush();
                         //  ESP.restart();
                       });
    ArduinoOTA.begin();
  }
#endif
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.println("Disconnected");
}

void connectToWiFiIfNeeded()
{
  if (WiFi.status() == WL_CONNECTED)
    return;
  timeItTime = millis();

  if (strlen(prefs.wifiPassword) > 0)
    setCpuFreqIfNeeded(160);

  WiFi.setSleep(true);
  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_FAST_SCAN);
  // WiFi.setTxPower(WIFI_POWER_2dBm);

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
}

IRAM_ATTR void touchCallback()
{
  if (rtcMillis() - touchInterruptTime > 500 && !isIdle()) // debounce
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

  createPollingTask(pollI2cSensors, "pollI2cSensors");
  createPollingTask(pollBoardStats, "pollBoardStats");

#ifdef THE_BOX
  createPollingTask(pollBatteryVoltage, "pollBatteryVoltage");
  createPollingTask(pollPir, "pollPir");
  createPollingTask(pollAudio, "pollAudio", configMINIMAL_STACK_SIZE * 5, 3);

  // start and schedule sds
  if (rtcMillis() - sdsStartTime > prefs.collectIntvlMs * prefs.pmSensorEvery)
  {
    createPollingTask(startSds, "startSds");
    priorityQueueWrite(wakeupTasksQ, WakeupTask{WAKEUP_MEASURE_PM, rtcMillis() + PM_SENSOR_RUNTIME_SECS * 1000});
  }
#endif

  EventBits_t pollingCompleteBitset = (1 << pollingCtr) - 1;
  xEventGroupWaitBits(pollingEventGroup, pollingCompleteBitset, pdTRUE, pdTRUE, portMAX_DELAY);

  if (lastAwakeDuration > 0)
    readings.awakeTime = lastAwakeDuration;

#ifdef THE_BOX
  if (!oobPmValueUsed)
  {
    readings.pm25 = oobLastPm25;
    readings.pm10 = oobLastPm10;

    oobPmValueUsed = true;
  }
#endif

  collectingTime = rtcMillis();
  readings.timestamp = collectingTime / 1000;

  Readings *readingsCopy = new Readings(readings);
  enqueueReadings(readingsCopy);
}

void calibrateTouchTask(void *arg)
{
  uint8_t count;
  uint16_t totalTouchValue;
  uint16_t touch;

  if (wasTouchpadWakeup)
    delay(2000);

  // calculate the avarage value
  while (true)
  {
    count = 0;
    totalTouchValue = 0;
    touch = 0;

    while (count < 3)
    {
      touch = touchRead(TOUCH_PIN);
      if (touch < 1)
        continue;
      count++;
      totalTouchValue += touch;
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

    // wakeup
    esp_sleep_enable_touchpad_wakeup();

    delay(10 * 1000);
  }
}

void setup()
{

  doOnEveryBoot();

  uint8_t nextWakeupReasonsBitset = 0;

  if (bitsetContains(wakeupReasonsBitset, WAKEUP_FIRST_BOOT))
  {
    doOnFreshBoot();
    bitsetAdd(nextWakeupReasonsBitset, WAKEUP_MEASURE);
  }

  // // turn on wifi before measure to save tine and get board stats
  // if (bitsetContains(wakeupReasonsBitset, WAKEUP_SUBMIT))
  // {
  //   wifiSetup();
  //   connectToWiFiIfNeeded();
  // }
  if (bitsetContains(wakeupReasonsBitset, WAKEUP_MEASURE))
  {
    pollAllSensors();
    Serial.println("pollAllSensors");

    if (isIdle() &&
        !bitsetContains(wakeupReasonsBitset, WAKEUP_SUBMIT) &&
        rtcMillis() - reportingTime > prefs.reportIntvlMs)
      bitsetAdd(nextWakeupReasonsBitset, WAKEUP_SUBMIT);

    bitsetAdd(nextWakeupReasonsBitset, WAKEUP_MEASURE);
  }

  if (!isIdle())
  {
#ifdef THE_BOX
    pinMode(BUTTON_PIN, INPUT_PULLUP);
#endif
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonPressedToStartAp, FALLING);
  }
  doWhileAwakeLoop();

  // do the rest
  if (bitsetContains(wakeupReasonsBitset, WAKEUP_SUBMIT))
  {
    connectToWiFiIfNeeded();
    WiFi.waitForConnectResult(strlen(prefs.wifiPassword) > 0 ? wifiSecureConnectTimeoutMs
                                                             : wifiConnectTimeoutMs);
  }

// out of band wakeup
#ifdef THE_BOX
  if (bitsetContains(wakeupReasonsBitset, WAKEUP_MEASURE_PM))
    pollSds();
#endif

  if (bitsetContains(wakeupReasonsBitset, WAKEUP_AP_MODE))
    runApMode();

  if (nextWakeupReasonsBitset != 0)
  {
    priorityQueueWrite(wakeupTasksQ, WakeupTask{nextWakeupReasonsBitset, rtcMillis() + prefs.collectIntvlMs});
  }

  // pqPrint(wakeupTasksQ);
  // end

  if (gotIpSemaphore != NULL)
    xSemaphoreTake(gotIpSemaphore, portMAX_DELAY);

  if (coap_loop_running)
  {
    coap_loop_running = false;
    xSemaphoreTake(coap_loop_semaphore, portMAX_DELAY);
  }

  coapClientCleanup();
  esp_wifi_stop();
  LittleFS.end();

  WakeupTask *wt = priorityQueuePop(wakeupTasksQ);
  wakeupReasonsBitset = wt->wakeupReasonsBitset;
  // make times in the past to 1 sec
  int64_t willWakeInMs = max(500LL, (wt->timestamp - rtcMillis()));
  Serial.print("\nAwakeFor: ");
  Serial.print(millis());
  Serial.print(" willWakeInMs: ");
  Serial.print(willWakeInMs);
  Serial.print(" with ");
  Serial.println(nextWakeupReasonsBitset, BIN);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(willWakeInMs * 1000);

  // mark as consumed
  wt->timestamp = 0;

  lastAwakeDuration = millis();
  esp_deep_sleep_start();
}

void loop() {}