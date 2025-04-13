#pragma once

#include <Arduino.h>
#include <my_utils.h>

#ifdef THE_BOX
#define READINGS_NUM_FIELDS 16
#define READINGS_BUFFER_SIZE 61

#define LOG_RESAMPLED_SIZE_ORIG 108
#define LOG_RESAMPLED_SIZE_COMPRESSED 84

#else
#define READINGS_BUFFER_SIZE 400
#define READINGS_NUM_FIELDS 5
#endif
#define PQ_SIZE 6

#define DIS_COMPANY_ID_PREFIX 0xF0

struct Readings
{
  uint timestampS; // seconds since epoch

#ifdef THE_BOX
  short ir;
  short visible;
  float pressure;
  float luminosity;
  short pm25x10;
  short pm10x10;
  float soundDbA;
  float soundDbZ;
  float voltageAvgS;
  uint8_t audioFft[LOG_RESAMPLED_SIZE_COMPRESSED];
  short co2;
#endif

  short awakeTime;
  float temperature;
  float humidity;
  // float freeHeap;
  float voltageAvg;
};

typedef struct Readings Readings;

Readings invalidReadings = {
    .timestampS = 0,

#ifdef THE_BOX
    .ir = -1,
    .visible = -1,
    .pressure = NAN,
    .luminosity = NAN,
    .pm25x10 = -1,
    .pm10x10 = -1,
    .soundDbA = NAN,
    .soundDbZ = NAN,
    .voltageAvgS = NAN,
    .audioFft = {0},
    .co2 = -1,
#endif
    .awakeTime = -1,
    .temperature = NAN,
    .humidity = NAN,
    // .freeHeap = NAN,
    .voltageAvg = NAN,
};

RTC_DATA_ATTR short oobLastPm25x10 = -1;
RTC_DATA_ATTR short oobLastPm10x10 = -1;
RTC_DATA_ATTR short lastCo2 = -1;
RTC_DATA_ATTR short lastPressure = -1;
RTC_DATA_ATTR bool oobValuesUsed = false;

struct ReadingsBuffer
{
  Readings buffer[READINGS_BUFFER_SIZE];
  uint8_t head;
  uint8_t tail;
  bool full;
};
typedef struct ReadingsBuffer ReadingsBuffer;

RTC_DATA_ATTR ReadingsBuffer readingsBuffer;

struct __attribute__((packed)) WakeupTask
{
  uint8_t wakeupReasonsBitset;
  uint64_t timestamp;
};
typedef struct WakeupTask WakeupTask;
RTC_DATA_ATTR WakeupTask wakeupTasksQ[PQ_SIZE];

bool readingsBufferIsEmpty(ReadingsBuffer *cb)
{
  return !cb->full && cb->tail == cb->head;
}

Readings *readingsBufferPop(ReadingsBuffer *cb)
{
  if (readingsBufferIsEmpty(cb))
    return NULL;

  Readings *data = &cb->buffer[cb->tail];
  cb->tail = (cb->tail + 1) % READINGS_BUFFER_SIZE;
  cb->full = false;
  return data;
}

WakeupTask *priorityQueuePop(WakeupTask *q)
{
  size_t length = PQ_SIZE;
  uint64_t minTimestamp = UINT64_MAX;
  size_t minIdx = 0;
  for (size_t i = 0; i < length; i++)
  {
    if (q[i].timestamp != 0 && q[i].timestamp < minTimestamp)
    {
      minTimestamp = q[i].timestamp;
      minIdx = i;
    }
  }

  if (minTimestamp != UINT64_MAX)
  {
    return &q[minIdx];
  }
  else
  {
    return NULL;
  }
}

void readingsBufferPush(ReadingsBuffer *cb, Readings data)
{
  cb->buffer[cb->head] = data;
  cb->head = (cb->head + 1) % READINGS_BUFFER_SIZE;
  if (cb->full)
  {
    cb->tail = (cb->tail + 1) % READINGS_BUFFER_SIZE;
  }
  else if (cb->head == cb->tail)
    cb->full = true;
}

void priorityQueueWrite(WakeupTask *q, WakeupTask data)
{
  // size_t length = sizeof(*q) / sizeof(WakeupTask);
  size_t length = PQ_SIZE; // todo generalize
  size_t minIdx = 0;
  for (size_t i = 0; i < length; i++)
  {
    if (q[i].timestamp == 0)
    {
      minIdx = i;
      break;
    }
  }
  q[minIdx] = data;
}

int readingsBufferCount(ReadingsBuffer *cb)
{
  if (cb->full)
  {
    return READINGS_BUFFER_SIZE; // buffer is full
  }
  else if (cb->head >= cb->tail)
  {
    return cb->head - cb->tail; // normal case
  }
  else
  {
    return READINGS_BUFFER_SIZE - (cb->head - cb->tail); // wrap-around case
  }
}

void circularBufferIterate(struct ReadingsBuffer *cb, void (*callback)(Readings *))
{
  int count = readingsBufferCount(cb);
  for (int i = 0; i < count; i++)
  {
    callback(&cb->buffer[(cb->tail + i) % READINGS_BUFFER_SIZE]);
  }
}

void readingsBufferClear(ReadingsBuffer *cb)
{
  cb->head = 0;
  cb->tail = 0;
  cb->full = false;
}

void pqPrint(WakeupTask *tasks)
{
  for (int i = 0; i < PQ_SIZE; i++)
  {
    Serial.print(i);
    Serial.print(". ");
    Serial.print(tasks[i].timestamp);
    Serial.print(" ");
    Serial.println(tasks[i].wakeupReasonsBitset, BIN);
  }
}

uint16_t scaleReading(float reading, uint8_t factor)
{
  if (isnanf(reading) || reading < 0 || reading > UINT16_MAX)
    return 0;

  return static_cast<uint16_t>(roundf(reading * factor));
}

uint8_t readingsToAdvData(Readings &readings, char initials, byte *data)
{
  uint8_t offset = 0;

  data[offset++] = initials;
  data[offset++] = DIS_COMPANY_ID_PREFIX;

  // put the timestamp in the first 4 bytes
  memcpy(data + offset, &readings.timestampS, sizeof(readings.timestampS));
  offset += sizeof(readings.timestampS);

  // put the voltageAvg in the next 2 bytes
  uint16_t voltageAvg = scaleReading(readings.voltageAvg, 100);
  memcpy(data + offset, &voltageAvg, sizeof(voltageAvg));
  offset += sizeof(voltageAvg);

  // put the temperature in the next 2 bytes
  uint16_t temperature = scaleReading(readings.temperature, 100);
  memcpy(data + offset, &temperature, sizeof(temperature));
  offset += sizeof(temperature);

  // put the humidity in the next 2 bytes
  uint16_t humidity = scaleReading(readings.humidity, 100);
  memcpy(data + offset, &humidity, sizeof(humidity));
  offset += sizeof(humidity);

#ifdef THE_BOX
  // put the pressure in the next 2 bytes
  uint16_t pressure = scaleReading(readings.pressure, 10);
  memcpy(data + offset, &pressure, sizeof(pressure));
  offset += sizeof(pressure);

  // put the luminosity in the next 2 bytes
  uint16_t luminosity = scaleReading(readings.luminosity, 1);
  memcpy(data + offset, &luminosity, sizeof(luminosity));
  offset += sizeof(luminosity);

  // put the soundDbA in the next 2 bytes
  uint16_t soundDbA = scaleReading(readings.soundDbA, 100);
  memcpy(data + offset, &soundDbA, sizeof(soundDbA));
  offset += sizeof(soundDbA);

  // put the soundDbZ in the next 2 bytes
  uint16_t soundDbZ = scaleReading(readings.soundDbZ, 100);
  memcpy(data + offset, &soundDbZ, sizeof(soundDbZ));
  offset += sizeof(soundDbZ);

  // put the pm25 in the next 2 bytes
  uint16_t pm25 = scaleReading(oobLastPm25x10, 1);
  memcpy(data + offset, &pm25, sizeof(pm25));
  offset += sizeof(pm25);

  // put the pm10 in the next 2 bytes
  uint16_t pm10 = scaleReading(oobLastPm10x10, 1);
  memcpy(data + offset, &pm10, sizeof(pm10));
  offset += sizeof(pm10);

  // put the co2 in the next 2 bytes
  uint16_t co2 = scaleReading(readings.co2, 1);
  memcpy(data + offset, &co2, sizeof(co2));
  offset += sizeof(co2);
#endif

  return offset;
}

// ------------------------------------- fix timestamps before NTP ---------------

void fixReadingsTimestamps(ReadingsBuffer *cb, unsigned long old_time_s)
{
  for (int i = 0; i < READINGS_BUFFER_SIZE; i++)
  {
    if (cb->buffer[i].timestampS != 0 && cb->buffer[i].timestampS < APR_20_2023_S) // 0s are nulls
    {
      int diff = static_cast<int64_t>(rtcSecs()) - static_cast<int64_t>(old_time_s);
      cb->buffer[i].timestampS += diff;
    }
  }
}

void fixPqTimestamps(WakeupTask *q, uint64_t old_time_ms)
{
  for (int i = 0; i < PQ_SIZE; i++)
  {
    if (q[i].timestamp != 0) // 0s are nulls
    {
      int64_t diff = static_cast<int64_t>(rtcMillis()) - static_cast<int64_t>(old_time_ms);
      q[i].timestamp += diff;
    }
  }
}