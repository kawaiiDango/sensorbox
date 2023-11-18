#pragma once

#include <Preferences.h>
#include <my_utils.h>
#include "cbor.h"

#define PREF_WIFI_SSID "wifiSsid"
#define PREF_WIFI_PASSWORD "wifiPassword"
#define PREF_STATIC_IP "staticIp"
#define PREF_STATIC_GATEWAY "staticGateway"
#define PREF_STATIC_SUBNET "staticSubnet"
#define PREF_COAP_HOST "coapHost"
#define PREF_COAP_PORT "coapPort"
#define PREF_COAP_DTLS_ID "coapDtlsId"
#define PREF_COAP_DTLS_PSK "coapDtlsPsk"
#define PREF_URI_PREFIX "uriPrefix"
#define PREF_NTP_SERVER "ntpServer"
#define PREF_ALTITUDE_M "altitudeM"
#define PREF_REPORTING_INTERVAL "reportIntvlMs"
#define PREF_COLLECTING_INTERVAL "collectIntvlMs"
#define PREF_PM_SENSOR_EVERY "pmSensorEvery"
#define PREF_LAST_RESET_REASON "lastResetReason"
#define PREF_LAST_CHANGED_S "lastChangedS"
#define PREF_TIMEZONE_OFFSET_S "timezoneOffsetS"
#define NAME_TIMESTAMP "timestamp"
#define NUM_PREFS 18

#ifdef THE_BOX
#define DEFAULT_URI_PREFIX "sensorBox"
#else
#define DEFAULT_URI_PREFIX "roomSensors"
#endif
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#define DEFAULT_REPORTING_INTERVAL 30 * 60 * 1000
#define DEFAULT_COLLECTING_INTERVAL 2 * 60 * 1000

const char *TAG_PREFS = "prefs";

struct MyPreferences
{
    const char *wifiSsid;
    const char *wifiPassword;
    const char *staticIp;
    const char *staticGateway;
    const char *staticSubnet;
    const char *coapHost;
    uint coapPort;
    const char *coapDtlsId;
    const char *coapDtlsPsk;
    const char *uriPrefix;
    const char *ntpServer;
    uint altitudeM;
    uint reportIntvlMs;
    uint collectIntvlMs;
    uint pmSensorEvery;
    uint lastResetReason;
    int timezoneOffsetS;
    uint lastChangedS;
};

struct MyPreferences prefs;

// fns

const char *pGetStrOrDefault(Preferences preferences, const char *key, const char *def = "")
{
    char *buf = (char *)malloc(16);
    size_t len = preferences.getString(key, buf, 20);
    if (len == 0)
        return def;
    return buf;
}

void initFromPrefs()
{
    Preferences preferences;

    preferences.begin("preferences");
    prefs = {
        .wifiSsid = pGetStrOrDefault(preferences, PREF_WIFI_SSID),
        .wifiPassword = pGetStrOrDefault(preferences, PREF_WIFI_PASSWORD),
        .staticIp = pGetStrOrDefault(preferences, PREF_STATIC_IP),
        .staticGateway = pGetStrOrDefault(preferences, PREF_STATIC_GATEWAY),
        .staticSubnet = pGetStrOrDefault(preferences, PREF_STATIC_SUBNET),
        .coapHost = pGetStrOrDefault(preferences, PREF_COAP_HOST),
        .coapPort = preferences.getUInt(PREF_COAP_PORT, 5683), // default coap port
        .coapDtlsId = pGetStrOrDefault(preferences, PREF_COAP_DTLS_ID),
        .coapDtlsPsk = pGetStrOrDefault(preferences, PREF_COAP_DTLS_PSK),
        .uriPrefix = pGetStrOrDefault(preferences, PREF_URI_PREFIX, DEFAULT_URI_PREFIX),
        .ntpServer = pGetStrOrDefault(preferences, PREF_NTP_SERVER, DEFAULT_NTP_SERVER),
        .altitudeM = preferences.getUInt(PREF_ALTITUDE_M, 158), // measured using gps status app on 2023-01-07
        .reportIntvlMs = preferences.getUInt(PREF_REPORTING_INTERVAL, DEFAULT_REPORTING_INTERVAL),
        .collectIntvlMs = preferences.getUInt(PREF_COLLECTING_INTERVAL, DEFAULT_COLLECTING_INTERVAL),
        .pmSensorEvery = preferences.getUInt(PREF_PM_SENSOR_EVERY, 3),
        .lastResetReason = preferences.getUInt(PREF_LAST_RESET_REASON, 0),
        .timezoneOffsetS = preferences.getInt(PREF_TIMEZONE_OFFSET_S, (5 * 60 + 30) * 60), // IST
        .lastChangedS = preferences.getUInt(PREF_LAST_CHANGED_S, 0),
    };
    preferences.end();
}

bool isSetupCompleted()
{
    if (!prefs.wifiSsid || !prefs.coapHost || !prefs.ntpServer || !prefs.uriPrefix ||
        strlen(prefs.wifiSsid) == 0 || strlen(prefs.coapHost) == 0 || strlen(prefs.ntpServer) == 0 ||
        strlen(prefs.uriPrefix) == 0)
    {
        ESP_LOGE(TAG_PREFS, "Setup not complete");
        return false;
    }
    return true;
}

void savePrefs()
{
    Serial.println("Saving preferences");
    Preferences preferences;

    preferences.begin("preferences");
    preferences.putString(PREF_WIFI_SSID, prefs.wifiSsid);
    preferences.putString(PREF_WIFI_PASSWORD, prefs.wifiPassword);
    preferences.putString(PREF_STATIC_IP, prefs.staticIp);
    preferences.putString(PREF_STATIC_GATEWAY, prefs.staticGateway);
    preferences.putString(PREF_STATIC_SUBNET, prefs.staticSubnet);
    preferences.putString(PREF_COAP_HOST, prefs.coapHost);
    preferences.putUInt(PREF_COAP_PORT, prefs.coapPort);
    preferences.putString(PREF_COAP_DTLS_ID, prefs.coapDtlsId);
    preferences.putString(PREF_COAP_DTLS_PSK, prefs.coapDtlsPsk);
    preferences.putString(PREF_URI_PREFIX, prefs.uriPrefix);
    preferences.putString(PREF_NTP_SERVER, prefs.ntpServer);
    preferences.putUInt(PREF_ALTITUDE_M, prefs.altitudeM);
    preferences.putUInt(PREF_REPORTING_INTERVAL, prefs.reportIntvlMs);
    preferences.putUInt(PREF_COLLECTING_INTERVAL, prefs.collectIntvlMs);
    preferences.putUInt(PREF_PM_SENSOR_EVERY, prefs.pmSensorEvery);
    preferences.putInt(PREF_TIMEZONE_OFFSET_S, prefs.timezoneOffsetS);

    if (rtcSecs() > APR_20_2023_S)
        preferences.putUInt(PREF_LAST_CHANGED_S, rtcSecs());
    preferences.end();
}

size_t prefsToCbor(uint8_t *buf)
{
    CborEncoder encoder;
    CborEncoder map_encoder;
    int error = CborNoError;

    cbor_encoder_init(&encoder, buf, 512, 0);

    error |= cbor_encoder_create_map(&encoder, &map_encoder, NUM_PREFS);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_WIFI_SSID);
    error |= cbor_encode_text_stringz(&map_encoder, prefs.wifiSsid);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_WIFI_PASSWORD);
    error |= cbor_encode_text_stringz(&map_encoder, prefs.wifiPassword);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_STATIC_IP);
    error |= cbor_encode_text_stringz(&map_encoder, prefs.staticIp);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_STATIC_GATEWAY);
    error |= cbor_encode_text_stringz(&map_encoder, prefs.staticGateway);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_STATIC_SUBNET);
    error |= cbor_encode_text_stringz(&map_encoder, prefs.staticSubnet);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_COAP_HOST);
    error |= cbor_encode_text_stringz(&map_encoder, prefs.coapHost);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_COAP_PORT);
    error |= cbor_encode_uint(&map_encoder, prefs.coapPort);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_COAP_DTLS_ID);
    error |= cbor_encode_text_stringz(&map_encoder, prefs.coapDtlsId);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_COAP_DTLS_PSK);
    error |= cbor_encode_text_stringz(&map_encoder, prefs.coapDtlsPsk);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_URI_PREFIX);
    error |= cbor_encode_text_stringz(&map_encoder, prefs.uriPrefix);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_NTP_SERVER);
    error |= cbor_encode_text_stringz(&map_encoder, prefs.ntpServer);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_ALTITUDE_M);
    error |= cbor_encode_uint(&map_encoder, prefs.altitudeM);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_REPORTING_INTERVAL);
    error |= cbor_encode_uint(&map_encoder, prefs.reportIntvlMs);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_COLLECTING_INTERVAL);
    error |= cbor_encode_uint(&map_encoder, prefs.collectIntvlMs);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_PM_SENSOR_EVERY);
    error |= cbor_encode_uint(&map_encoder, prefs.pmSensorEvery);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_LAST_RESET_REASON);
    error |= cbor_encode_uint(&map_encoder, prefs.lastResetReason);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_TIMEZONE_OFFSET_S);
    error |= cbor_encode_int(&map_encoder, prefs.timezoneOffsetS);

    error |= cbor_encode_text_stringz(&map_encoder, PREF_LAST_CHANGED_S);
    error |= cbor_encode_uint(&map_encoder, prefs.lastChangedS);

    error |= cbor_encoder_close_container(&encoder, &map_encoder);

    if (error != CborNoError)
    {
        if (error == CborErrorInternalError)
            printf("CborErrorInternalError");
        else if (error == CborErrorOutOfMemory)
            printf("CborErrorOutOfMemory");

        printf("Error encoding CBOR: %d\n", error);
        return 0;
    }

    size_t encoded_size = cbor_encoder_get_buffer_size(&encoder, buf);

    if (encoded_size > 500)
        ESP_LOGE(TAG_PREFS, "prefs encoded size: %zu", encoded_size);

#if PRINT_CBOR
    printCbor(buf, encoded_size);
#endif
    return encoded_size;
}

void cborToPrefsSave(const uint8_t *buf, size_t len)
{
    CborParser parser;
    CborValue value;
    CborValue map;
    int err = CborNoError;

    cbor_parser_init(buf, len, 0, &parser, &value);

    if (!cbor_value_is_map(&value))
        return;

    err |= cbor_value_enter_container(&value, &map);

    Preferences preferences;

    preferences.begin("preferences");

    while (!cbor_value_at_end(&map))
    {
        char *keyStr;
        size_t keyLen;
        err |= cbor_value_dup_text_string(&map, &keyStr, &keyLen, NULL);
        err |= cbor_value_advance(&map);

        if (cbor_value_is_text_string(&map) &&
            (strncmp(keyStr, PREF_WIFI_SSID, keyLen) == 0 ||
             strncmp(keyStr, PREF_WIFI_PASSWORD, keyLen) == 0 ||
             strncmp(keyStr, PREF_STATIC_IP, keyLen) == 0 ||
             strncmp(keyStr, PREF_STATIC_GATEWAY, keyLen) == 0 ||
             strncmp(keyStr, PREF_STATIC_SUBNET, keyLen) == 0 ||
             strncmp(keyStr, PREF_COAP_HOST, keyLen) == 0 ||
             strncmp(keyStr, PREF_COAP_DTLS_ID, keyLen) == 0 ||
             strncmp(keyStr, PREF_COAP_DTLS_PSK, keyLen) == 0 ||
             strncmp(keyStr, PREF_URI_PREFIX, keyLen) == 0 ||
             strncmp(keyStr, PREF_NTP_SERVER, keyLen) == 0))
        {
            char *valStr;
            size_t valLen;
            err |= cbor_value_dup_text_string(&map, &valStr, &valLen, NULL);
            preferences.putString(keyStr, valStr);
        }
        else if (cbor_value_is_unsigned_integer(&map) &&
                 (strncmp(keyStr, PREF_ALTITUDE_M, keyLen) == 0 ||
                  strncmp(keyStr, PREF_COAP_PORT, keyLen) == 0 ||
                  strncmp(keyStr, PREF_REPORTING_INTERVAL, keyLen) == 0 ||
                  strncmp(keyStr, PREF_COLLECTING_INTERVAL, keyLen) == 0 ||
                  strncmp(keyStr, PREF_PM_SENSOR_EVERY, keyLen) == 0 ||
                  strncmp(keyStr, PREF_LAST_CHANGED_S, keyLen) == 0))
        {
            uint64_t val;
            err |= cbor_value_get_uint64(&map, &val);
            preferences.putUInt(keyStr, val);
        }
        else if (cbor_value_is_integer(&map) &&
                 (strncmp(keyStr, PREF_TIMEZONE_OFFSET_S, keyLen) == 0))
        {
            int val;
            err |= cbor_value_get_int(&map, &val);
            preferences.putInt(keyStr, val);
        }
        else
        {
            ESP_LOGE(TAG_PREFS, "Unknown key: %s", keyStr);
        }

        if (err != CborNoError)
        {
            printf("Error decoding CBOR: %d\n", err);
            preferences.end();

            return;
        }
        err |= cbor_value_advance(&map);
    }

    err |= cbor_value_leave_container(&value, &map);

    preferences.end();
    initFromPrefs();
}
