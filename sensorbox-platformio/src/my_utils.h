#pragma once

#include <Arduino.h>
#include "cbor.h"

#define THE_BOX
#define ENABLE_LOW_BATTERY_SHUTDOWN
// #define PRINT_CBOR

#define BUTTON_PIN 27
#define DEBUG_BAUD_RATE 115200
#define APR_20_2023_S 1681948800
#define COMPLETE_TASK                                          \
    xEventGroupSetBits(pollingEventGroup, 1 << (uint32_t)arg); \
    vTaskDelete(NULL);

uint64_t stayAwakeUntilTime = 0;
bool wasTouchpadWakeup = false;
RTC_DATA_ATTR unsigned long lastAwakeDuration = 0;

struct MyUnits {
    const char *dust;
    const char *light;
    const char *pressure;
    const char *humidity;
    const char *temperature;
    const char *gas;
    const char *rssi;
    const char *sound;
    const char *data;
    const char *boolean;
    const char *none;
} units = {
    .dust = "ug/m3",
    .light = "lux",
    .pressure = "hPa",
    .humidity = "%RH",
    .temperature = "°C",
    .gas = "ppm",
    .rssi = "dBm",
    .sound = "dBA",
    .data = "K",
    .boolean = "b",
    .none = "",
};

float seaPressure(float p, float t, float altitude)
{
    // sea_level_pressure = pressure * math.exp(g * M * altitude / (R * temp)) # Sea level pressure in Pa
    return p * (exp(9.80665 * 0.0289644 * altitude / (8.31447 * (t + 273.15))));
    // return p * pow(1 - 0.0065 * height / (t + 0.0065 * height + 273.15), -5.275);
};

float heatIndex(float TC, float RH)
{
    float T = 1.8 * TC + 32;
    float F = 0.5 * (T + 61.0 + (T - 68.0) * 1.2 + RH * 0.094);

    if (F >= 80)
    {
        F = -42.379 + 2.04901523 * T + 10.14333127 * RH - .22475541 * T * RH - .00683783 * T * T - .05481717 * RH * RH + .00122874 * T * T * RH + .00085282 * T * RH * RH - .00000199 * T * T * RH * RH;
        if (RH < 13.0 and T > 80.0 and T < 112.0)
            F -= ((13.0 - RH) / 4.0) * sqrt((17.0 - abs(T - 95.0)) / 17.0);
        else if (RH > 85.0 and T > 80.0 and T < 87.0)
            F += ((RH - 85.0) / 10.0) * ((87.0 - T) / 5.0);
    }
    float C = (F - 32) * 5 / 9;
    return C;
}

inline bool bitsetContains(uint8_t bs, uint8_t element)
{
    return bs & element;
}

inline void bitsetAdd(uint8_t &bs, uint8_t element)
{
    bs |= element;
}

float mapf(float x, float in_min, float in_max, float out_min, float out_max)
{
    const float run = in_max - in_min;
    if (run == 0)
    {
        log_e("map(): Invalid input range, min == max");
        return -1; // AVR returns -1, SAM returns 0
    }
    const float rise = out_max - out_min;
    const float delta = x - in_min;
    return (delta * rise) / run + out_min;
}

uint64_t rtcMillis()
{
    timeval currentTime;
    gettimeofday(&currentTime, NULL);
    uint64_t milliseconds = (uint64_t)currentTime.tv_sec * 1000L + (uint64_t)currentTime.tv_usec / 1000L;
    return milliseconds;
}

uint rtcSecs()
{
    timeval currentTime;
    gettimeofday(&currentTime, NULL);
    return currentTime.tv_sec;
}

void printRtcMillis(time_t timezoneOffsetS)
{
    struct tm timeinfo;
    timeval currentTime;

    gettimeofday(&currentTime, NULL);
    currentTime.tv_sec += timezoneOffsetS;
    localtime_r(&currentTime.tv_sec, &timeinfo);

    Serial.println(&timeinfo, "System time: %A, %B %d %Y %H:%M:%S");
}

void setCpuFreqIfNeeded(int mhz)
{
    if (ESP.getCpuFreqMHz() != mhz)
    {
        setCpuFrequencyMhz(mhz);
        Serial.updateBaudRate(DEBUG_BAUD_RATE);
#ifdef THE_BOX
        Serial2.updateBaudRate(9600);
#endif
    }
}

void printCbor(uint8_t *buf, size_t encoded_size)
{
    CborParser root_parser;
    CborValue it;
    printf("JSON: ");
    cbor_parser_init(buf, encoded_size, 0, &root_parser, &it);

    // Dump the values in JSON format
    cbor_value_to_json(stdout, &it, 0);
    printf("\n");
}

inline bool isIdle()
{
    return stayAwakeUntilTime < millis();
}