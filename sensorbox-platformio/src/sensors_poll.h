#pragma once

#include <my_buffers.h>
#include <my_utils.h>
#include "driver/adc.h"
#include <DFRobot_DHT20.h>

#ifdef THE_BOX

#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <SdsDustSensor.h>
#include <Adafruit_TSL2591.h>
#include <Adafruit_BMP280.h>
#include "audio_read.h"
#include "MedianFilterLib2.h"

#define PM_SENSOR_RUNTIME_SECS (31)

#define PIR_PIN 36
#define TOUCH_PIN 0
#define SDS_POWER_PIN 25
#define EXTERNAL_BATTERY_VOLTAGE_PIN 34
#define LCD_RST_PIN 26
#define LCD_CS_PIN 12
#define LCD_DC_PIN 15
#define LCD_DIN_PIN 23
#define LCD_CLK_PIN 18
#define LCD_BACKLIGHT_PWM_PIN 2
#define LED_PIN 2
#define LCD_INVERSE false    // set to true to invert display pixel color
#define MY_LCD_CONTRAST 0xB1 // default is 0xBF set in LCDinit, Try 0xB1 <-> 0xBF if your display is too dark/dim
#define MY_LCD_BIAS 0x12     // LCD bias mode 1:48: Try 0x12 or 0x13 or 0x14

#else

#include <DFRobot_AGS01DB.h>
#define VOC_PREHEAT_TIMEOUT (3 * 60 * 1000)
#define DHT_PIN 23
#define TOUCH_PIN 32
#define LED_PIN 0

#endif

const char *TAG_SENSORS_POLL = "sensors_poll";

#ifdef THE_BOX

RTC_DATA_ATTR tsl2591Gain_t prevTslGain = TSL2591_GAIN_MED;
bool tslConfigured = false;

// ESP32 hardware-SPI
// pin 17 - Data/Command select (D/C) 4
// pin 5 - LCD chip select (CS)
// pin 16 - LCD reset (RST) 27
// Adafruit_PCD8544 display = Adafruit_PCD8544(17, 5, 16);
Adafruit_PCD8544 lcd = Adafruit_PCD8544(LCD_DC_PIN, LCD_CS_PIN, LCD_RST_PIN);
bool lcdStarted = false;

bool backlightOn = false;
bool lcdSleeping = false;

RTC_DATA_ATTR int64_t sdsStartTime = 0;

RTC_DATA_ATTR float oobLastPm25 = NAN;
RTC_DATA_ATTR float oobLastPm10 = NAN;
RTC_DATA_ATTR bool oobPmValueUsed = false;

#else
RTC_DATA_ATTR int64_t vocStartTime = 0;
#endif
// FSEDHT dht(DHT_PIN);
Readings readings;

uint32_t pollingCtr = 0;
EventGroupHandle_t pollingEventGroup = xEventGroupCreate();
// void dhtError(FSEDHT_error_t error)
// {
//   ESP_LOGE(TAG_SENSORS_POLL, "DHT11 Failed: %d, Message: %s", error.errorNum, error.errorMsg);
// }

// void pollDht11()
// {
//   dht.onError(dhtError);
//   dht.read();

//   readings.temperature = dht.toCelsius();
//   readings.temperature = mapf(readings.temperature, 28.5, 31.3, 29.8, 32.2);
//   readings.humidity = dht.getHumidity();
//   readings.humidity = mapf(readings.humidity, 63, 57, 52.4, 46.4);
// }

#ifdef THE_BOX

void pollBmp280()
{
  Adafruit_BMP280 bmp;
  Adafruit_Sensor *bmp_pressure = bmp.getPressureSensor();
  sensors_event_t pressureEvent;

  if (bmp.begin(0x76))
  {
    bmp.setSampling(Adafruit_BMP280::MODE_FORCED,      /* Operating Mode. */
                    Adafruit_BMP280::SAMPLING_X1,      /* Temp. oversampling */
                    Adafruit_BMP280::SAMPLING_X8,      /* Pressure oversampling */
                    Adafruit_BMP280::FILTER_OFF,        /* Filtering. */
                    Adafruit_BMP280::STANDBY_MS_4000); /* Standby time. */

    bmp_pressure->getEvent(&pressureEvent);

    readings.pressure = pressureEvent.pressure;
  }
  else
  {
    ESP_LOGE(TAG_SENSORS_POLL, "bmp.begin() failed");
  }
}

void configureTSL2591(Adafruit_TSL2591 *tsl)
{
  // printToLcdAndSerial("TSL2591");
  // configureTSL2591();

  // You can change the gain on the fly, to adapt to brighter/dimmer light situations
  // tsl.setGain(TSL2591_GAIN_LOW);    // 1x gain (bright light)
  // tsl.setGain(TSL2591_GAIN_MED); // 25x gain
  // tsl.setGain(TSL2591_GAIN_HIGH);   // 428x gain

  // Changing the integration time gives you a longer time over which to sense light
  // longer timelines are slower, but are good in very low light situtations!
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);  // shortest integration time (bright light)
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_200MS);
  tsl->setTiming(TSL2591_INTEGRATIONTIME_300MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_400MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_500MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_600MS);  // longest integration time (dim light)

  tsl->enableAutoRange(false); // this doesn't work properly, disable it
  /* Display the gain and integration time for reference sake */
  Serial.print("Gain:");
  tsl2591Gain_t gain = tsl->getGain();
  if (prevTslGain != gain)
  {
    tsl->setGain(prevTslGain);
    gain = prevTslGain;
  }

  switch (gain)
  {
  case TSL2591_GAIN_LOW:
    Serial.println("1x (Low)");
    break;
  case TSL2591_GAIN_MED:
    Serial.println("25x (Medium)");
    break;
  case TSL2591_GAIN_HIGH:
    Serial.println("428x (High)");
    break;
  case TSL2591_GAIN_MAX:
    Serial.println("9876x (Max)");
    break;
  }
}

void calcTslReadings(Adafruit_TSL2591 *tsl)
{
  uint32_t lum = tsl->getFullLuminosity();
  uint16_t ir, full;
  ir = lum >> 16;
  full = lum & 0xFFFF;

  readings.ir = (float)ir;
  readings.visible = (float)(full - ir);
  readings.luminosity = tsl->calculateLux(full, ir);
}

void pollTSL2591()
{
  Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591);

  if (!tsl.begin())
  {
    ESP_LOGE(TAG_SENSORS_POLL, "No TSL2591 detected");
    return;
  }

  if (!tslConfigured)
  {
    configureTSL2591(&tsl);
    tslConfigured = true;
  }

  calcTslReadings(&tsl);
  // auto gain

  if (readings.luminosity > 0 && readings.luminosity < 15 && tsl.getGain() < TSL2591_GAIN_HIGH)
    tsl.setGain(TSL2591_GAIN_HIGH);
  else if ((readings.luminosity > 350 || readings.luminosity == -1) && tsl.getGain() > TSL2591_GAIN_LOW)
  {
    tsl.setGain(TSL2591_GAIN_LOW);
  }
  else if (readings.luminosity <= 350 && readings.luminosity >= 15 && tsl.getGain() != TSL2591_GAIN_MED)
    tsl.setGain(TSL2591_GAIN_MED);

  if ((readings.luminosity == -1 || prevTslGain != tsl.getGain()))
  { // do it once again
    prevTslGain = tsl.getGain();
    calcTslReadings(&tsl);
  }

  tsl.disable();
}

void pollPir(void *arg)
{
  pinMode(PIR_PIN, INPUT);
  readings.motion = digitalRead(PIR_PIN) == HIGH;
  COMPLETE_TASK
}

void enableBacklight(bool enabled)
{
  if (enabled == backlightOn)
    return;

  backlightOn = enabled;
  pinMode(LCD_BACKLIGHT_PWM_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PWM_PIN, enabled);
}

void startLcd()
{
  lcd.begin();
  lcd.setContrast(42);
  lcdStarted = true;
}

void updateLcdStatus(bool enable)
{
  if (!lcdStarted)
    startLcd();

  // bool idle = isIdle();
  bool bl = readings.luminosity > 0 &&
            readings.luminosity <= 40 &&
            enable;

  if (enable == lcdSleeping)
  {
    lcdSleeping = !enable;
    lcd.powerSaving(!enable);
  }
  enableBacklight(bl);
}

void startSds(void *arg)
{
  SdsDustSensor sds(Serial2); // pin 16, 17

  pinMode(SDS_POWER_PIN, OUTPUT);
  rtc_gpio_hold_dis((gpio_num_t)SDS_POWER_PIN);
  digitalWrite(SDS_POWER_PIN, HIGH);
  rtc_gpio_hold_en((gpio_num_t)SDS_POWER_PIN);

  delay(100);
  sds.begin();
  sds.setQueryReportingMode();
  sds.setCustomWorkingPeriod(10);
  sdsStartTime = rtcMillis();
  Serial.println("startSds");

  COMPLETE_TASK
}

void pollSds()
{
  SdsDustSensor sds(Serial2); // pin 16, 17

  sds.begin();

  auto pm = sds.queryPm();
  sds.sleep();

  pinMode(SDS_POWER_PIN, OUTPUT);
  rtc_gpio_hold_dis((gpio_num_t)SDS_POWER_PIN);
  digitalWrite(SDS_POWER_PIN, LOW);
  rtc_gpio_hold_en((gpio_num_t)SDS_POWER_PIN);

  if (!pm.isOk())
  {
    ESP_LOGE(TAG_SENSORS_POLL, "Could not read pm sensor: %s", pm.statusToString());
    return;
  }

  oobLastPm25 = pm.pm25;
  oobLastPm10 = pm.pm10;
  oobPmValueUsed = false;
}

void pollBatteryVoltage(void *arg)
{
  int rounds = 20;
  float v;
  MedianFilter2<float> medianFilter(rounds);

  // max 1.5 min 1.2
  // reading / 4096 * 3.3 * 2 (2 100ks) / 4 (4 batteries)
  adc_power_acquire();
  pinMode(EXTERNAL_BATTERY_VOLTAGE_PIN, INPUT);

  for (int i = 0; i < rounds; i++)
  {
    v = (float)analogReadMilliVolts(EXTERNAL_BATTERY_VOLTAGE_PIN) * 2 / 1 / 1000;
    medianFilter.AddValue(v);
  }
  adc_power_release();

  v = medianFilter.GetFiltered();

  // v = mapf(v, 3.71, 4.22, 3.61, 4.09); // for the lolin clone
  v = mapf(v, 3.71, 4.25, 3.60, 4.15); // for the firebeetle 2

#ifdef ENABLE_LOW_BATTERY_SHUTDOWN
  if (v > 0.6 && v < 3.3)
  {
    rtc_gpio_hold_dis((gpio_num_t)SDS_POWER_PIN);

    ESP_LOGE(TAG_SENSORS_POLL, "Battery voltage too low: %f, going to sleep", v);
    Serial.flush();
    esp_deep_sleep(1000000ULL * 60 * 60 * 24);
  }
#endif
  readings.voltageAvg = v;

  COMPLETE_TASK
}

float pollBatteryVoltage()
{
  float v = 0;
  pollBatteryVoltage(&v);
  return v;
}

void pollAudio(void *arg)
{
  pinMode(MIC_POWER_PIN, OUTPUT);
  digitalWrite(MIC_POWER_PIN, HIGH);
  delay(100);
  audio_measurements am = audio_read();
  readings.soundDbA = am.dbA;

  for (int i = 0; i < LOG_RESAMPLED_SIZE; i++)
  {
    if (am.fft[i] > 0)
      readings.audioFft[i] = static_cast<uint8_t>(min(255.0f, am.fft[i]));
    else
      readings.audioFft[i] = 0;
  }

  digitalWrite(MIC_POWER_PIN, LOW);

  COMPLETE_TASK
}

#else

void pollVocContinuous()
{
  DFRobot_AGS01DB voc;

  if (voc.begin() == 0)
  {
    if (vocStartTime == 0)
      vocStartTime = rtcMillis();

    if (rtcMillis() - vocStartTime > VOC_PREHEAT_TIMEOUT)
    {
      float reading = voc.readVocPPM();
      if (reading >= 0 && reading <= 100)
        readings.voc = reading;
    }
  }
}

#endif

void pollDht20()
{
  DFRobot_DHT20 dht20;

  if (dht20.begin() == 0)
  {
    // 0 is successful
    readings.temperature = dht20.getTemperature();
    readings.humidity = dht20.getHumidity() * 100;
  }
}

void pollI2cSensors(void *arg)
{
  pollDht20();
#ifdef THE_BOX
  pollBmp280();
  pollTSL2591();
#else
  pollVocContinuous();
#endif
  COMPLETE_TASK
}

void pollBoardStats(void *arg)
{
  readings.freeHeap = ESP.getFreeHeap();

#ifndef THE_BOX
  if (WiFi.status() == WL_CONNECTED)
    readings.boardTemperature = temperatureRead();
#endif

  COMPLETE_TASK
}

void createPollingTask(TaskFunction_t taskFn, const char *taskName, uint32_t stackSize = configMINIMAL_STACK_SIZE * 3, UBaseType_t priority = 1)
{
  xTaskCreate(
      taskFn,
      taskName,
      stackSize,
      (void *)pollingCtr,
      priority,
      NULL);
  pollingCtr++;
}