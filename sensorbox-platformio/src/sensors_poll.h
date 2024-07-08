#pragma once

#include <my_buffers.h>
#include <my_utils.h>
#include <DFRobot_DHT20.h>

#ifdef THE_BOX

#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <SdsDustSensor.h>
#include <Adafruit_TSL2591.h>
#include <Adafruit_BMP280.h>
#include <SensirionI2cSht4x.h>
#include <SensirionI2CScd4x.h>
#include "audio_read.h"

#define PM_SENSOR_RUNTIME_SECS (31)

#define PIR_PIN 36
#define TOUCH_PIN 0
#define SDS_POWER_PIN 25
#define SDS_TX_PIN 16
#define SDS_RX_PIN 17
#define LCD_RST_PIN 26
#define LCD_CS_PIN 12
#define LCD_DC_PIN 15
#define LCD_DIN_PIN 23
#define LCD_CLK_PIN 18
#define LCD_BACKLIGHT_PIN 2
#define LED_PIN 2
#define LCD_INVERSE false    // set to true to invert display pixel color
#define MY_LCD_CONTRAST 0xB1 // default is 0xBF set in LCDinit, Try 0xB1 <-> 0xBF if your display is too dark/dim
#define MY_LCD_BIAS 0x12     // LCD bias mode 1:48: Try 0x12 or 0x13 or 0x14

#else

// #include <DFRobot_AGS01DB.h>

// #define VOC_PREHEAT_TIMEOUT (3 * 60 * 1000)
#define TOUCH_PIN 32
#define LED_PIN 5

#endif

#define EXTERNAL_BATTERY_VOLTAGE_PIN 34

const char *TAG_SENSORS_POLL = "sensors_poll";

#ifdef THE_BOX

RTC_DATA_ATTR tsl2591Gain_t prevTslGain = TSL2591_GAIN_MED;
bool tslConfigured = false;
Adafruit_PCD8544 lcd = Adafruit_PCD8544(LCD_DC_PIN, LCD_CS_PIN, LCD_RST_PIN);
bool lcdStarted = false;

bool backlightOn = false;
bool lcdSleeping = false;

RTC_DATA_ATTR float oobLastPm25 = NAN;
RTC_DATA_ATTR float oobLastPm10 = NAN;
RTC_DATA_ATTR short oobLastCo2 = -1;
RTC_DATA_ATTR bool oobValuesUsed = false;
RTC_DATA_ATTR bool scd41Inited = false;
RTC_DATA_ATTR bool scd41FirstReadingDiscarded = false;

#else
RTC_DATA_ATTR int64_t vocStartTime = 0;
#endif
Readings readings;

uint32_t pollingCtr = 0;
EventGroupHandle_t pollingEventGroup = xEventGroupCreate();

#ifdef THE_BOX

void pollSht41()
{
  int16_t error = 0;
  char errorMessage[64];
  SensirionI2cSht4x sht41;

  sht41.begin(Wire, SHT41_I2C_ADDR_44);
  error = sht41.measureHighPrecision(readings.temperature, readings.humidity);
  if (error != 0)
  {
    // does not write to readings.temperature, readings.humidity on error
    errorToString(error, errorMessage, sizeof errorMessage);
    ESP_LOGE(TAG_SENSORS_POLL, "measureHighPrecision() error: %s", errorMessage);
  }
}

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
                    Adafruit_BMP280::FILTER_OFF,       /* Filtering. */
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
  uint16_t ir, full;
  uint32_t lum = tsl->getFullLuminosity();

  if (lum == 0)
  {
    ESP_LOGE(TAG_SENSORS_POLL, "TSL2591 lum == 0");
    return;
  }
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
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PIN, enabled);
}

void startLcd()
{
  lcd.begin();
  lcd.setContrast(42);
  lcdStarted = true;
}

void lcdPowerSaving(boolean i)
{
  if (!i)
  {
    lcd.command(PCD8544_FUNCTIONSET);
  }
  else
  {
    lcd.command(PCD8544_FUNCTIONSET | PCD8544_POWERDOWN);
  }
}

void updateLcdStatus(bool enable)
{
  if (!lcdStarted)
    startLcd();

  if (enable == lcdSleeping)
  {
    lcdSleeping = !enable;
    lcdPowerSaving(!enable);
  }
  enableBacklight(enable);
}

void startSds()
{
  Serial2.begin(9600, SERIAL_8N1, SDS_TX_PIN, SDS_RX_PIN);
  SdsDustSensor sds(Serial2, RETRY_DELAY_MS_DEFAULT, 5);

  pinMode(SDS_POWER_PIN, OUTPUT);
  rtc_gpio_hold_dis((gpio_num_t)SDS_POWER_PIN);
  digitalWrite(SDS_POWER_PIN, HIGH);
  rtc_gpio_hold_en((gpio_num_t)SDS_POWER_PIN);

  delay(100);

  auto rms = sds.setQueryReportingMode();

  if (!rms.isOk())
    ESP_LOGE(TAG_SENSORS_POLL, "Could not setQueryReportingMode: %s", rms.statusToString().c_str());

  auto wps = sds.setCustomWorkingPeriod(10);

  if (!wps.isOk())
    ESP_LOGE(TAG_SENSORS_POLL, "Could not setCustomWorkingPeriod: %s", wps.statusToString().c_str());

  Serial.println("startSds");
}

void pollSds()
{
  Serial2.begin(9600, SERIAL_8N1, SDS_TX_PIN, SDS_RX_PIN);
  SdsDustSensor sds(Serial2, RETRY_DELAY_MS_DEFAULT, 5);

  auto pm = sds.queryPm();
  auto wsr = sds.sleep();

  pinMode(SDS_POWER_PIN, OUTPUT);
  rtc_gpio_hold_dis((gpio_num_t)SDS_POWER_PIN);
  digitalWrite(SDS_POWER_PIN, LOW);
  rtc_gpio_hold_en((gpio_num_t)SDS_POWER_PIN);

  if (!pm.isOk())
  {
    ESP_LOGE(TAG_SENSORS_POLL, "Could not queryPm: %s", pm.statusToString().c_str());
    return;
  }

  if (!wsr.isOk())
    ESP_LOGE(TAG_SENSORS_POLL, "Could not sleep: %s", wsr.statusToString().c_str());

  oobLastPm25 = pm.pm25;
  oobLastPm10 = pm.pm10;
  oobValuesUsed = false;
}

uint16_t scd41_measureSingleShotNoWait()
{
  uint16_t error;
  uint8_t buffer[2];
  SensirionI2CTxFrame txFrame(buffer, 2);

  error = txFrame.addCommand(0x219D);
  if (error)
  {
    return error;
  }

  // #define SCD4X_I2C_ADDRESS 0x62
  error = SensirionI2CCommunication::sendFrame(0x62, txFrame, Wire);
  // delay(5000);
  return error;
}

void scd41PrintError(uint16_t error)
{
  char errorMessage[256];
  errorToString(error, errorMessage, 256);
  ESP_LOGE(TAG_SENSORS_POLL, "scd41 error: %s", errorMessage);
}

// unused for now, manually run when required
void printScd41Settings()
{
  SensirionI2CScd4x scd41;
  Wire.begin();
  scd41.begin(Wire);
  uint16_t data = 0;
  uint16_t error = 0;

  error = scd41.stopPeriodicMeasurement();
  if (error)
  {
    scd41PrintError(error);
    return;
  }

  ESP_LOGW(TAG_SENSORS_POLL, "Stopped periodic measurement");

  error = scd41.getAutomaticSelfCalibration(data);

  if (error)
  {
    scd41PrintError(error);
    return;
  }

  ESP_LOGW(TAG_SENSORS_POLL, "Automatic self calibration: %d", data);

  error = scd41.getAutomaticSelfCalibrationInitialPeriod(data); // is 44

  if (error)
  {
    scd41PrintError(error);
    return;
  }

  ESP_LOGW(TAG_SENSORS_POLL, "Automatic self calibration initial period: %d", data);

  error = scd41.getAutomaticSelfCalibrationStandardPeriod(data); // is 156

  if (error)
  {
    scd41PrintError(error);
    return;
  }

  ESP_LOGW(TAG_SENSORS_POLL, "Automatic self calibration standard period: %d", data);
}

// unused for now, manually run when required
void changeScd41Settings()
{
  SensirionI2CScd4x scd41;
  Wire.begin();
  scd41.begin(Wire);
  uint16_t ascEnabled = 0;
  uint16_t error = 0;

  error = scd41.stopPeriodicMeasurement();
  if (error)
  {
    scd41PrintError(error);
    return;
  }

  error = scd41.getAutomaticSelfCalibration(ascEnabled);

  if (error)
  {
    scd41PrintError(error);
    return;
  }

  if (ascEnabled)
  {
    error = scd41.setAutomaticSelfCalibration(false);
    if (error)
    {
      scd41PrintError(error);
      return;
    }
    error = scd41.setTemperatureOffset(0);
    if (error)
    {
      scd41PrintError(error);
      return;
    }

    error = scd41.persistSettings();
    if (error)
    {
      scd41PrintError(error);
      return;
    }
  }

  ESP_LOGW(TAG_SENSORS_POLL, "scd41 settings modified and persisted");
}

// to be run after reading pressure
void pollScd41()
{
  SensirionI2CScd4x scd41;
  uint16_t error;
  uint16_t co2 = 0;
  float temperature = 0.0f;
  float humidity = 0.0f;

  scd41.begin(Wire);

  // setup
  if (!scd41Inited)
  {
    scd41.stopPeriodicMeasurement();

    error = scd41.startLowPowerPeriodicMeasurement();
    if (error)
    {
      scd41PrintError(error);
      return;
    }

    scd41Inited = true;
  }

  // read prev measurement
  error = scd41.readMeasurement(co2, temperature, humidity);
  if (error)
  {
    scd41PrintError(error);
  }
  else if (co2 == 0)
  {
    ESP_LOGW(TAG_SENSORS_POLL, "Invalid sample detected, skipping.");
  }
  else
  {
    if (scd41FirstReadingDiscarded)
    {
      readings.co2 = co2;
      oobLastCo2 = co2;
    }
    else
    {
      scd41FirstReadingDiscarded = true;
    }
    ESP_LOGW(TAG_SENSORS_POLL, "CO2: %d, Temperature: %.2f / %.2f, Humidity: %.2f / %.2f", co2, temperature, readings.temperature, humidity, readings.humidity);
  }

  // set params for the next measurement
  error = scd41.setAmbientPressure((uint16_t)readings.pressure);

  if (error)
  {
    scd41PrintError(error);
    return;
  }

  // error = scd41.measureSingleShot();
  // if (error)
  // {
  //   scd41PrintError(error);
  //   return;
  // }
}

void pollAudio(void *arg)
{
  pinMode(MIC_POWER_PIN, OUTPUT);
  digitalWrite(MIC_POWER_PIN, HIGH);
  delay(50);
  audio_read(&readings.soundDbA, readings.audioFft);

  pinMode(MIC_POWER_PIN, OUTPUT);
  digitalWrite(MIC_POWER_PIN, LOW);

  COMPLETE_TASK
}

#else

// void pollVocContinuous()
// {
//   DFRobot_AGS01DB voc;

//   if (voc.begin() == 0)
//   {
//     if (vocStartTime == 0)
//       vocStartTime = rtcMillis();

//     if (rtcMillis() - vocStartTime > VOC_PREHEAT_TIMEOUT)
//     {
//       float reading = voc.readVocPPM();
//       if (reading >= 0 && reading <= 100)
//         readings.voc = reading;
//     }
//   }
// }

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

#endif

void pollBatteryVoltage(void *arg)
{
  float v = 0;
  int rounds = 10;

  pinMode(EXTERNAL_BATTERY_VOLTAGE_PIN, INPUT);

  for (int i = 0; i < rounds; i++)
  {
    v += (float)analogReadMilliVolts(EXTERNAL_BATTERY_VOLTAGE_PIN) * 2 / 1000;
  }

  v /= rounds;

#ifdef THE_BOX
  v = mapf(v, 3.94, 4.20, 3.85, 4.10); // for the firebeetle 2
  // v = mapf(v, 3.71, 4.25, 3.60, 4.15); // for the firebeetle 2, old calibration
#else
  v = mapf(v, 3.88, 4.15, 3.84, 4.10); // for the lolin clone
#endif

#ifdef ENABLE_LOW_BATTERY_SHUTDOWN
  if (v > 0.6 && v < 3.5)
  {
#ifdef THE_BOX

    rtc_gpio_hold_dis((gpio_num_t)SDS_POWER_PIN);
#endif

    ESP_LOGE(TAG_SENSORS_POLL, "Battery voltage too low: %f, going to sleep", v);
    Serial.flush();
    esp_deep_sleep_start();
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

void pollI2cSensors(void *arg)
{
  Wire.begin();
#ifdef THE_BOX
  pollSht41();
  pollBmp280();
  pollTSL2591();
  pollScd41();

#else
  pollDht20();
  // pollVocContinuous();
#endif
  COMPLETE_TASK
}

void pollBoardStats()
{
  readings.freeHeap = ESP.getFreeHeap();

  Serial.printf("Free heap: %.2fK\n", readings.freeHeap / 1024);
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