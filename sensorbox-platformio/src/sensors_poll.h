#pragma once

#include <my_buffers.h>
#include <my_utils.h>

#ifdef HAS_DISPLAY
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#endif

#ifdef THE_BOX
#include <SdsDustSensor.h>
#include <Adafruit_TSL2591.h>
#include <Adafruit_BMP280.h>
#include <SensirionI2cSht4x.h>
#include <SensirionI2CScd4x.h>
#include "audio_read.h"
#define PM_SENSOR_RUNTIME_SECS (31)

#else

#include <DFRobot_DHT20.h>
// #include <DFRobot_AGS01DB.h>

#endif

#ifdef HAS_DISPLAY
Adafruit_PCD8544 lcd = Adafruit_PCD8544(LCD_DC_PIN, LCD_CS_PIN, LCD_RST_PIN);
bool lcdStarted = false;
bool backlightOn = false;
bool lcdSleeping = false;
#endif

#ifdef THE_BOX

RTC_DATA_ATTR tsl2591Gain_t prevTslGain = TSL2591_GAIN_MED;
bool tslConfigured = false;
RTC_DATA_ATTR bool sdsRunning = false;
RTC_DATA_ATTR bool scd41Inited = false;

#else
RTC_DATA_ATTR int64_t vocStartTime = 0;
#endif

const char *TAG_SENSORS_POLL = "sensors_poll";
Readings readings;
RTC_DATA_ATTR uint8_t measureCountModPm = 0;
RTC_DATA_ATTR uint8_t measureCountModSubmit = 0;
RTC_DATA_ATTR float lastBatteryVoltage = -1;
bool isBatterySpike = false;

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

  if (bmp.begin(0x76)) // this sets mode to normal again
  {
    // take measurement
    // go to sleep
    // I am getting occational downward spikes with this setup, so commenting it out
    // bmp.setSampling(Adafruit_BMP280::MODE_SLEEP,       /* Operating Mode. */
    //                 Adafruit_BMP280::SAMPLING_X1,      /* Temp. oversampling */
    //                 Adafruit_BMP280::SAMPLING_X1,      /* Pressure oversampling */
    //                 Adafruit_BMP280::FILTER_OFF,       /* Filtering. */
    //                 Adafruit_BMP280::STANDBY_MS_4000); /* Standby time. */

    bmp.setSampling(Adafruit_BMP280::MODE_FORCED,      /* Operating Mode. */
                    Adafruit_BMP280::SAMPLING_X16,     /* Temp. oversampling */
                    Adafruit_BMP280::SAMPLING_X16,     /* Pressure oversampling */
                    Adafruit_BMP280::FILTER_X4,        /* Filtering. */
                    Adafruit_BMP280::STANDBY_MS_4000); /* Standby time. */

    if (bmp.takeForcedMeasurement())
    {
      bmp_pressure->getEvent(&pressureEvent);
      readings.pressure = pressureEvent.pressure;
      lastPressure = readings.pressure;
    }
    else
    {
      ESP_LOGE(TAG_SENSORS_POLL, "bmp.takeForcedMeasurement() failed");
    }
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

#ifdef HAS_DISPLAY
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

#endif

void scd41PrintError(uint16_t error)
{
  char errorMessage[256];
  errorToString(error, errorMessage, 256);
  ESP_LOGE(TAG_SENSORS_POLL, "scd41 error: %s", errorMessage);
}

uint16_t SensirionI2CScd4x_measureSingleShot()
{
  uint16_t error;
  uint8_t buffer[2];
  SensirionI2CTxFrame txFrame(buffer, 2);

  error = txFrame.addCommand(0x219D);
  if (error)
  {
    return error;
  }

  error = SensirionI2CCommunication::sendFrame(0x62, txFrame, Wire);
  return error;
}

void initScd41()
{
  SensirionI2cScd4x scd41;
  uint16_t co2 = 0;
  float temperature = 0.0f;
  float humidity = 0.0f;

  float measurementIntervalSeconds = prefs.collectIntvlMs / 1000.0;

  /*
  The initial period represents the number of readings after powering up the sensor for the very first time to trigger the
first automatic self-calibration. The standard period represents the number of subsequent readings periodically
triggering ASC after completion of the initial period. Sensirion recommends adjusting the number of samples
comprising initial and standard period to 2 and 7 days at the average intended sampling rate, respectively.
The number of single shots for initial and standard period can be adjusted onboard the sensor using the commands
detailed in Table 4 below. Note that the parameter value represents twelve times the number of single shots defining
the length of either period. Furthermore, this parameter must be an integer and a multiple of four.

Example: Average sampling rate: 3 minutes
Standard period: 7 days = 3360 single shots → Standard period parameter value = 280
Initial period: 2 days = 960 single shots → Initial period parameter value = 80

Example: Average sampling rate: 2 minutes
Standard period: 7 days = 5040 single shots → Standard period parameter value = 420
Initial period: 2 days = 1440 single shots → Initial period parameter value = 120
  */

  uint16_t ascInitialPeriod = ceil(2.0 * 24 * 60 * 60 / measurementIntervalSeconds / 12 / 4) * 4;
  uint16_t ascStandardPeriod = ceil(7.0 * 24 * 60 * 60 / measurementIntervalSeconds / 12 / 4) * 4;

  uint16_t storedAscInitialPeriod = 0;
  uint16_t storedAscStandardPeriod = 0;
  uint16_t storedAscEnabled = false;
  float storedTemperatureOffset = 0;

  Wire.begin();
  scd41.begin(Wire, SCD41_I2C_ADDR_62);

  scd41.wakeUp(); // always returns NO_ERROR

  uint16_t error = scd41.stopPeriodicMeasurement();
  if (error)
  {
    scd41PrintError(error);
    return;
  }

  // reset the sensor if asked

  if (scd41NeedsReset)
  {
    ESP_LOGW(TAG_SENSORS_POLL, "Resetting the scd41");

    error = scd41.performFactoryReset();
    if (error)
    {
      scd41PrintError(error);
      return;
    }
    scd41NeedsReset = false;
  }

  // get the params stored in eeprom

  // error = scd41.getAutomaticSelfCalibrationEnabled(storedAscEnabled);

  // if (error)
  // {
  //   scd41PrintError(error);
  //   return;
  // }

  // error = scd41.getAutomaticSelfCalibrationInitialPeriod(storedAscInitialPeriod); // is 44 in factory settings

  // if (error)
  // {
  //   scd41PrintError(error);
  //   return;
  // }

  // error = scd41.getAutomaticSelfCalibrationStandardPeriod(storedAscStandardPeriod); // is 156 in factory settings

  // if (error)
  // {
  //   scd41PrintError(error);
  //   return;
  // }

  // error = scd41.getTemperatureOffset(storedTemperatureOffset); // is 4 in factory settings

  // if (error)
  // {
  //   scd41PrintError(error);
  //   return;
  // }

  // ESP_LOGW(TAG_SENSORS_POLL, "ASC initial period: %u, standard period: %u, stored: %u, %u", ascInitialPeriod, ascStandardPeriod, storedAscInitialPeriod, storedAscStandardPeriod);

  // write the new params if they are different

  // if (storedAscInitialPeriod == 0 || storedAscInitialPeriod != ascInitialPeriod ||
  //     storedAscStandardPeriod == 0 || storedAscStandardPeriod != ascStandardPeriod ||
  //     storedAscEnabled == false || storedTemperatureOffset != 0)
  // {
  //   ESP_LOGW(TAG_SENSORS_POLL, "ASC initial period: %u, standard period: %u, stored: %u, %u", ascInitialPeriod, ascStandardPeriod, storedAscInitialPeriod, storedAscStandardPeriod);
  //   error = scd41.setAutomaticSelfCalibrationEnabled(true);
  //   if (error)
  //   {
  //     scd41PrintError(error);
  //     return;
  //   }

  //   error = scd41.setAutomaticSelfCalibrationInitialPeriod(ascInitialPeriod);
  //   if (error)
  //   {
  //     scd41PrintError(error);
  //     return;
  //   }

  //   error = scd41.setAutomaticSelfCalibrationStandardPeriod(ascStandardPeriod);
  //   if (error)
  //   {
  //     scd41PrintError(error);
  //     return;
  //   }

  //   error = scd41.setTemperatureOffset(0);
  //   if (error)
  //   {
  //     scd41PrintError(error);
  //     return;
  //   }

  //   if (SCD41_WRITE_EEPROM_ENABLED)
  //   {
  //     error = scd41.persistSettings();
  //     if (error)
  //     {
  //       scd41PrintError(error);
  //       return;
  //     }

  //     ESP_LOGW(TAG_SENSORS_POLL, "SCD41 settings persisted");
  //   }
  // }

  if (storedTemperatureOffset != 0)
  {
    error = scd41.setTemperatureOffset(0);
    if (error)
    {
      scd41PrintError(error);
      return;
    }

    if (SCD41_WRITE_EEPROM_ENABLED)
    {
      error = scd41.persistSettings();
      if (error)
      {
        scd41PrintError(error);
        return;
      }

      ESP_LOGW(TAG_SENSORS_POLL, "SCD41 settings persisted");
    }
  }

  // throw away the first reading

  // error = scd41.measureSingleShot();
  // if (error)
  // {
  //   scd41PrintError(error);
  //   return;
  // }

  // scd41.readMeasurement(co2, temperature, humidity);

  scd41.startLowPowerPeriodicMeasurement();
  delay(5000);

  // throw away the first reading
  error = scd41.readMeasurement(co2, temperature, humidity);
  if (error)
  {
    scd41PrintError(error);
    return;
  }

  scd41Inited = true;

  ESP_LOGW(TAG_SENSORS_POLL, "SCD41 inited");
}

// to be run after reading pressure
void pollScd41()
{
  SensirionI2cScd4x scd41;
  uint16_t error;
  uint16_t co2 = 0;
  float temperature = 0.0f;
  float humidity = 0.0f;

  scd41.begin(Wire, SCD41_I2C_ADDR_62);

  if (!scd41Inited || scd41NeedsReset)
  {
    initScd41();
    return;
  }

  // read prev measurement
  error = scd41.readMeasurement(co2, temperature, humidity);
  if (error)
  {
    scd41PrintError(error);
  }
  else if (co2 == 0)
  {
    ESP_LOGE(TAG_SENSORS_POLL, "Invalid sample detected, skipping.");
  }
  else
  {
    readings.co2 = co2;
    lastCo2 = co2;
    ESP_LOGW(TAG_SENSORS_POLL, "CO2: %d, Temperature: %.2f / %.2f, Humidity: %.2f / %.2f",
             co2, temperature, readings.temperature, humidity, readings.humidity);
  }

  // set params for the next measurement

  if (lastPressure > 0)
  {
    // convert hPa to Pa
    error = scd41.setAmbientPressure(100 * lastPressure);

    if (error)
    {
      scd41PrintError(error);
      return;
    }
  }

  // error = scd41.measureSingleShot();
  // error = SensirionI2CScd4x_measureSingleShot();
  // if (error)
  // {
  //   scd41PrintError(error);
  //   return;
  // }
}

void powerDownScd41()
{
  SensirionI2cScd4x scd41;
  uint16_t error;

  if (!scd41Inited)
    return;

  scd41.begin(Wire, SCD41_I2C_ADDR_62);
  error = scd41.stopPeriodicMeasurement();
  if (error)
  {
    scd41PrintError(error);
    return;
  }

  error = scd41.powerDown();
  if (error)
  {
    scd41PrintError(error);
    return;
  }

  scd41Inited = false;
}

void startSds()
{
  // stopScd41();

  pinMode(SDS_POWER_PIN, OUTPUT);
  rtc_gpio_hold_dis((gpio_num_t)SDS_POWER_PIN);
  digitalWrite(SDS_POWER_PIN, HIGH);
  rtc_gpio_hold_en((gpio_num_t)SDS_POWER_PIN);

  sdsRunning = true;

  Serial2.begin(9600, SERIAL_8N1, SDS_TX_PIN, SDS_RX_PIN);
  delay(1400);
  // SdsDustSensor sds(Serial2, RETRY_DELAY_MS_DEFAULT, 5);
  SdsDustSensor sds(Serial2);

  auto wps = sds.setCustomWorkingPeriod(10);

  if (!wps.isOk())
  {
    ESP_LOGE(TAG_SENSORS_POLL, "Could not setCustomWorkingPeriod: %s", wps.statusToString().c_str());
  }

  auto rms = sds.setQueryReportingMode();

  if (!rms.isOk())
  {
    ESP_LOGE(TAG_SENSORS_POLL, "Could not setQueryReportingMode: %s", rms.statusToString().c_str());
  }

  Serial.println("sds started");
}

void pollSds()
{
  Serial2.begin(9600, SERIAL_8N1, SDS_TX_PIN, SDS_RX_PIN);
  SdsDustSensor sds(Serial2, RETRY_DELAY_MS_DEFAULT, 5);

  delay(700);

  auto pm = sds.queryPm();
  auto wsr = sds.sleep();

  pinMode(SDS_POWER_PIN, OUTPUT);
  rtc_gpio_hold_dis((gpio_num_t)SDS_POWER_PIN);
  digitalWrite(SDS_POWER_PIN, LOW);
  rtc_gpio_hold_en((gpio_num_t)SDS_POWER_PIN);

  sdsRunning = false;

  if (!pm.isOk())
  {
    ESP_LOGE(TAG_SENSORS_POLL, "Could not queryPm: %s", pm.statusToString().c_str());
    return;
  }

  if (!wsr.isOk())
    ESP_LOGE(TAG_SENSORS_POLL, "Could not sleep: %s", wsr.statusToString().c_str());

  oobLastPm25x10 = pm.pm25 * 10;
  oobLastPm10x10 = pm.pm10 * 10;
  oobValuesUsed = false;

  ESP_LOGW(TAG_SENSORS_POLL, "PM2.5: %.2f, PM10: %.2f", pm.pm25, pm.pm10);
}

void pollAudio(void *arg)
{
  pinMode(MIC_POWER_PIN, OUTPUT);
  digitalWrite(MIC_POWER_PIN, HIGH);
  delay(50);
  audio_read(&readings.soundDbA, &readings.soundDbZ, readings.audioFft);

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

  // calibration constants for y = ax + b
  // calibrated against the more accurate sht41 using excel
  float at = 1.056740402;
  float bt = -0.953464085;

  float ah = 0.994045558;
  float bh = 1.768696989;

  if (dht20.begin() == 0)
  {
    // 0 is successful
    readings.temperature = dht20.getTemperature();
    readings.humidity = dht20.getHumidity() * 100;

    readings.temperature = at * readings.temperature + bt;
    readings.humidity = ah * readings.humidity + bh;
  }
}

#endif

float pollBatteryVoltage(uint8_t pin)
{
  float sum = 0;
  int rounds = 10;
  float vReadings[rounds];
  float mean;
  int filteredCount = 0;

  pinMode(pin, INPUT);

  // First Pass: Collect readings
  for (int i = 0; i < rounds; i++)
  {
    vReadings[i] = (float)analogReadMilliVolts(pin) * 2 / 1000;
    sum += vReadings[i];
  }

  // prelininary mean
  mean = sum / rounds;

  // Outlier Filtering: Define a threshold for what you consider an outlier
  float threshold = 0.05;

  sum = 0; // Reset sum for the second pass

  for (int i = 0; i < rounds; i++)
  {
    if (fabs(vReadings[i] - mean) <= threshold)
    {
      filteredCount++;
      sum += vReadings[i];
    }
  }

  mean = sum / filteredCount;

#ifdef THE_BOX
  mean = mapf(mean, 3.94, 4.20, 3.85, 4.10); // for the firebeetle 2
  // mean = mapf(mean, 3.71, 4.25, 3.60, 4.15); // for the firebeetle 2, old calibration
#else
  mean = mapf(mean, 3.88, 4.15, 3.84, 4.10); // for the lolin clone
#endif

  return mean;
}

void pollAllBatteryVoltages()
{
  float threshold = 0.05;
  // poll the main battery voltage
  float mean = pollBatteryVoltage(BATTERY_VOLTAGE_PIN);

  isBatterySpike = lastBatteryVoltage > 0 && fabs(lastBatteryVoltage - mean) > threshold;

  // prevent spikes from shutting down the device
  if (!isBatterySpike)
  {
#ifdef ENABLE_LOW_BATTERY_SHUTDOWN
    if (mean > 0.6 && mean < 3.5)
    {

#ifdef THE_BOX
      rtc_gpio_hold_dis((gpio_num_t)SDS_POWER_PIN);
      powerDownScd41();
#endif

      ESP_LOGE(TAG_SENSORS_POLL, "Battery voltage too low: %f, going to sleep", mean);
      Serial.flush();
      esp_deep_sleep_start();
    }
#endif
  }

  readings.voltageAvg = mean;
  lastBatteryVoltage = mean;

#ifdef THE_BOX
  // poll the second battery voltage
  mean = pollBatteryVoltage(BATTERY_B_VOLTAGE_PIN);

#ifdef ENABLE_LOW_BATTERY_SHUTDOWN
  if (mean > 0.6 && mean < 3.5)
  {
    ESP_LOGE(TAG_SENSORS_POLL, "BatteryS voltage too low: %f, powering down scd41", mean);
    Serial.flush();
    powerDownScd41();
  }
#endif

  readings.voltageAvgS = mean;
#endif
}

void pollMainSensors(void *arg)
{
  pollAllBatteryVoltages();
  Wire.begin();
#ifdef THE_BOX
  pollSht41();
  pollBmp280();
  pollTSL2591();

  // if (measureCountModPm != 0 && measureCountModSubmit != 0 &&
  //     !sdsRunning && isIdle() && !isBatterySpike)

  if (isIdle())
    pollScd41();

#else
  pollDht20();
  // pollVocContinuous();
#endif
  COMPLETE_TASK
}

void pollBoardStats()
{
  float freeHeap = ESP.getFreeHeap();

  Serial.printf("Free heap: %.2fK\n", freeHeap / 1024);
}

void createPollingTask(TaskFunction_t taskFn,
                       const char *taskName,
                       uint32_t stackSize = configMINIMAL_STACK_SIZE * 3,
                       UBaseType_t priority = 1)
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