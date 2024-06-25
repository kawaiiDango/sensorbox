#pragma once

#include <Arduino.h>
#include <my_utils.h>

#ifdef THE_BOX
#define READINGS_NUM_FIELDS 16
#else
// #define READINGS_BUFFER_SIZE 170
#define READINGS_NUM_FIELDS 7
#endif
#define PQ_SIZE 6
#define READINGS_BUFFER_SIZE 48

struct Readings
{
  uint timestamp; // seconds since epoch

#ifdef THE_BOX
  signed char motion;
  short ir;
  short visible;
  float pressure;
  float luminosity;
  float pm25;
  float pm10;
  short co2;
  float soundDbA;
  uint8_t audioFft[106];
#else
  float voc;
#endif

  float temperature;
  float humidity;
  float freeHeap;
  short awakeTime;
  float voltageAvg;
};

typedef struct Readings Readings;

Readings invalidReadings = {
    .timestamp = 0,

#ifdef THE_BOX
    .motion = -1,
    .ir = -1,
    .visible = -1,
    .pressure = NAN,
    .luminosity = NAN,
    .pm25 = NAN,
    .pm10 = NAN,
    .co2 = -1,
    .soundDbA = NAN,
    .audioFft = {0},
#else
    .voc = NAN,
    .boardTemperature = NAN,
#endif
    .temperature = NAN,
    .humidity = NAN,
    .freeHeap = NAN,
    .awakeTime = -1,
    .voltageAvg = NAN,
};

struct ReadingsBuffer
{
  Readings buffer[READINGS_BUFFER_SIZE];
  uint8_t head;
  uint8_t tail;
  bool full;
};
typedef struct ReadingsBuffer ReadingsBuffer;

RTC_DATA_ATTR ReadingsBuffer readingsBuffer;

struct WakeupTask
{
  uint8_t wakeupReasonsBitset;
  int64_t timestamp;
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
  int64_t minTimestamp = INT64_MAX;
  size_t minIdx = 0;
  for (size_t i = 0; i < length; i++)
  {
    if (q[i].timestamp != 0 && q[i].timestamp < minTimestamp)
    {
      minTimestamp = q[i].timestamp;
      minIdx = i;
    }
  }

  if (minTimestamp != INT64_MAX)
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

// ------------------------------------- fix timestamps before NTP ---------------

void fixTimestampsBeforeNtp(ReadingsBuffer *cb, unsigned long offset_s)
{
  for (int i = 0; i < READINGS_BUFFER_SIZE; i++)
  {
    if (cb->buffer[i].timestamp != 0 && cb->buffer[i].timestamp < APR_20_2023_S) // 0s are nulls
      cb->buffer[i].timestamp += rtcSecs() - offset_s;
  }
}