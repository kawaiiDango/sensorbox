#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <prefs.h>
#include <my_utils.h>
#include "esp_http_server.h"
#include <file_ring_buffer.h>

#define AP_MODE_TIMEOUT 300000 // 5 minutes

bool apModeDone = false;
httpd_handle_t http_server = NULL;
const char *TAG_APMODE = "apmode";

void send_input_field(httpd_req_t *req, const char *name, const char *type, const char *value, bool required = false)
{
    char strbuf[512];
    sprintf(strbuf, "<label for='%s'>%s%s</label><br>", name, name, required ? "*" : "");
    httpd_resp_sendstr_chunk(req, strbuf);
    sprintf(strbuf, "<input type='%s' id='%s' name='%s' value='%s' %s><br>", type, name, name, value, required ? "required" : "");
    httpd_resp_sendstr_chunk(req, strbuf);
}

esp_err_t root_get_handler(httpd_req_t *req)
{
    char num_buf[10];
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>");
    httpd_resp_sendstr_chunk(req, prefs.uriPrefix);
    httpd_resp_sendstr_chunk(req, " config</title></head><body>");
    httpd_resp_sendstr_chunk(req, "<h1>");
    httpd_resp_sendstr_chunk(req, prefs.uriPrefix);
    httpd_resp_sendstr_chunk(req, " config</h1>");

    httpd_resp_sendstr_chunk(req, "<h3>");
    sprintf(num_buf, "%d", readingsBufferCount(&readingsBuffer));
    httpd_resp_sendstr_chunk(req, num_buf);
    httpd_resp_sendstr_chunk(req, " + ");
    sprintf(num_buf, "%d", frb.size());
    httpd_resp_sendstr_chunk(req, num_buf);
    httpd_resp_sendstr_chunk(req, " readings saved</h3>");
    httpd_resp_sendstr_chunk(req, "<form name='delete_readings' method='post' action='/delete_readings'>");
    httpd_resp_sendstr_chunk(req, "<input name='delete_readings' type='submit' value='Delete readings'>");
    httpd_resp_sendstr_chunk(req, "</form><br>");

    httpd_resp_sendstr_chunk(req, "<form name='main' method='post' action='/' onsubmit='setTime()'>");
    send_input_field(req, PREF_WIFI_SSID, "text", prefs.wifiSsid, true);
    send_input_field(req, PREF_WIFI_PASSWORD, "password", prefs.wifiPassword, false);
    httpd_resp_sendstr_chunk(req, "<br>");
    send_input_field(req, PREF_STATIC_IP, "text", prefs.staticIp);
    send_input_field(req, PREF_STATIC_GATEWAY, "text", prefs.staticGateway);
    send_input_field(req, PREF_STATIC_SUBNET, "text", prefs.staticSubnet);
    httpd_resp_sendstr_chunk(req, "<br>");
    send_input_field(req, PREF_COAP_HOST, "text", prefs.coapHost, true);
    sprintf(num_buf, "%u", prefs.coapPort);
    send_input_field(req, PREF_COAP_PORT, "number", num_buf, true);
    send_input_field(req, PREF_COAP_DTLS_ID, "text", prefs.coapDtlsId, false);
    send_input_field(req, PREF_COAP_DTLS_PSK, "password", prefs.coapDtlsPsk, false);
    httpd_resp_sendstr_chunk(req, "<br>");
    send_input_field(req, PREF_URI_PREFIX, "text", prefs.uriPrefix, true);
    send_input_field(req, PREF_NTP_SERVER, "text", prefs.ntpServer, true);
    httpd_resp_sendstr_chunk(req, "<br>");
    sprintf(num_buf, "%u", prefs.altitudeM);
    send_input_field(req, PREF_ALTITUDE_M, "number", num_buf, true);
    httpd_resp_sendstr_chunk(req, "<br>");
    sprintf(num_buf, "%u", prefs.reportIntvlMs);
    send_input_field(req, PREF_REPORTING_INTERVAL, "number", num_buf, true);
    sprintf(num_buf, "%u", prefs.collectIntvlMs);
    send_input_field(req, PREF_COLLECTING_INTERVAL, "number", num_buf, true);
    sprintf(num_buf, "%u", prefs.pmSensorEvery);
    send_input_field(req, PREF_PM_SENSOR_EVERY, "number", num_buf, true);
    httpd_resp_sendstr_chunk(req, "<br>");
    sprintf(num_buf, "%d", prefs.timezoneOffsetS);
    send_input_field(req, PREF_TIMEZONE_OFFSET_S, "number", num_buf, true);
    send_input_field(req, NAME_TIMESTAMP, "number", "0", true);
    httpd_resp_sendstr_chunk(req, "<br>");
    httpd_resp_sendstr_chunk(req, "<input type='submit' value='Save'>");
    httpd_resp_sendstr_chunk(req, "</form>");
    httpd_resp_sendstr_chunk(req, "<script>"
                                  "function setTime() {"
                                  "document.getElementById('" NAME_TIMESTAMP
                                  "').value = (new Date()).getTime();"
                                  "};"
                                  "setInterval(setTime, 1000);</script>");
    httpd_resp_sendstr_chunk(req, "</body></html>");

    /* Send empty chunk to signal HTTP response completion */
    return httpd_resp_sendstr_chunk(req, NULL);
}

esp_err_t delete_readings_post_handler(httpd_req_t *req)
{
    char *postdata = (char *)malloc(req->content_len + 1);
    size_t off = 0;
    while (off < req->content_len)
    {
        /* Read data received in the request */
        int ret = httpd_req_recv(req, postdata + off, req->content_len - off);
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                httpd_resp_send_408(req);
            }
            free(postdata);
            return ESP_FAIL;
        }
        off += ret;
    }
    postdata[off] = '\0';

    if (strlen(postdata) == 0)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (strcmp(postdata, "delete_readings=Delete+readings") == 0)
    {
        readingsBufferClear(&readingsBuffer);
        frb.clear();
        apModeDone = true;
    }

    return httpd_resp_sendstr(req, "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body><h1>Deleted!</h1></body></html>");
}

esp_err_t root_post_handler(httpd_req_t *req)
{
    char *postdata = (char *)malloc(req->content_len + 1);
    size_t off = 0;
    while (off < req->content_len)
    {
        /* Read data received in the request */
        int ret = httpd_req_recv(req, postdata + off, req->content_len - off);
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                httpd_resp_send_408(req);
            }
            free(postdata);
            return ESP_FAIL;
        }
        off += ret;
    }
    postdata[off] = '\0';

    if (strlen(postdata) == 0)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // parse form encoded post data
    char *parameter = postdata;
    char *key;
    char *value;

    Preferences preferences;
    preferences.begin("preferences");

    while (parameter != NULL)
    {
        char *equals = strstr(parameter, "=");
        if (equals == NULL)
        {
            break;
        }
        int keyLen = equals - parameter;
        key = strndup(parameter, keyLen);
        char *next = strstr(equals + 1, "&");
        if (next != NULL)
        {
            int valueLen = next - equals - 1;
            value = strndup(equals + 1, valueLen);
            parameter = next + 1;
        }
        else
        {
            int valueLen = strlen(equals + 1);
            value = strndup(equals + 1, valueLen);
            // value[valueLen] = '\0';
            parameter = NULL;
        }

        // urldecode the value
        char *p = value;
        char *q = value;
        while (*p != '\0')
        {
            if (*p == '%')
            {
                char hex[3];
                hex[0] = *(p + 1);
                hex[1] = *(p + 2);
                hex[2] = '\0';
                *q = strtol(hex, NULL, 16);
                p += 3;
                q++;
            }
            else
            {
                *q = *p;
                p++;
                q++;
            }
        }

        *q = '\0';

        if (strcmp(key, PREF_WIFI_SSID) == 0 || strcmp(key, PREF_WIFI_PASSWORD) == 0 || strcmp(key, PREF_STATIC_IP) == 0 ||
            strcmp(key, PREF_STATIC_GATEWAY) == 0 || strcmp(key, PREF_STATIC_SUBNET) == 0 || strcmp(key, PREF_COAP_HOST) == 0 ||
            strcmp(key, PREF_COAP_DTLS_ID) == 0 || strcmp(key, PREF_COAP_DTLS_PSK) == 0 || strcmp(key, PREF_URI_PREFIX) == 0 ||
            strcmp(key, PREF_NTP_SERVER) == 0)
        {
            preferences.putString(key, value);
        }
        else if (strcmp(key, PREF_ALTITUDE_M) == 0 || strcmp(key, PREF_COAP_PORT) == 0 || strcmp(key, PREF_REPORTING_INTERVAL) == 0 ||
                 strcmp(key, PREF_COLLECTING_INTERVAL) == 0 || strcmp(key, PREF_PM_SENSOR_EVERY) == 0)
        {

            preferences.putUInt(key, atoi(value));
        }
        else if (strcmp(key, PREF_TIMEZONE_OFFSET_S) == 0)
        {
            preferences.putInt(key, atoi(value));
        }

        else if (strcmp(key, NAME_TIMESTAMP) == 0)
        {
            long long timestamp = atoll(value);
            if (timestamp / 1000 > APR_20_2023_S)
            {
                int64_t oldTime = rtcMillis();

                // set the system time
                struct timeval tv = {(time_t)(timestamp / 1000), (suseconds_t)((timestamp % 1000) * 1000)};
                settimeofday(&tv, NULL);
                printRtcMillis(prefs.timezoneOffsetS);

                lastNtpSyncTimeS = rtcSecs();

                if (oldTime / 1000 < APR_20_2023_S)
                    fixTimestampsBeforeNtp(&readingsBuffer, oldTime / 1000);

                preferences.putUInt(PREF_LAST_CHANGED_S, tv.tv_sec);
            }
            else
                preferences.putUInt(PREF_LAST_CHANGED_S, 0);
        }

        free(key);
        free(value);
    }

    preferences.end();

    apModeDone = true;

    return httpd_resp_sendstr(req, "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body><h1>Saved!</h1></body></html>");
}

esp_err_t catch_all_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Moved Temporarily");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "Redirecting to /");

    return ESP_OK;
}

esp_err_t start_server(int port)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;

    /* Use the URI wildcard matching function in order to
     * allow the same handler to respond to multiple different
     * target URIs which match the wildcard scheme */
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&http_server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG_APMODE, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t _root_get_handler = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &_root_get_handler);

    httpd_uri_t _root_post_handler = {
        .uri = "/",
        .method = HTTP_POST,
        .handler = root_post_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &_root_post_handler);

    httpd_uri_t _delete_readings_post_handler = {
        .uri = "/delete_readings",
        .method = HTTP_POST,
        .handler = delete_readings_post_handler,
        .user_ctx = NULL,
    };

    httpd_register_uri_handler(http_server, &_delete_readings_post_handler);

    // 302 handler
    httpd_uri_t _catch_all_handler = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = catch_all_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(http_server, &_catch_all_handler);

    return ESP_OK;
}

IRAM_ATTR void buttonPressed()
{
    apModeDone = true;
}

void apModeLoop()
{
    DNSServer dnsServer;

    frb.begin();

    WiFi.mode(WIFI_AP);

    boolean result = WiFi.softAP(prefs.uriPrefix, "sensorBox321");

    if (!result)
    {
        Serial.println("AP mode failed to start");
        return;
    }

    WiFi.setTxPower(WIFI_POWER_2dBm);

    result = dnsServer.start(53, "*", WiFi.softAPIP());

    if (!result)
    {
        Serial.println("DNS server failed to start");
        return;
    }

    MDNS.begin(prefs.uriPrefix);

    start_server(80);

    Serial.println("AP mode started");
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonPressed, FALLING);

    while (millis() < AP_MODE_TIMEOUT && !apModeDone)
    {
        dnsServer.processNextRequest();
        delay(100);
    }
    delay(1000); // wait for the response to be sent
    dnsServer.stop();
    // httpd_stop(&http_server);
    WiFi.softAPdisconnect(true);
    Serial.println("AP mode stopped");
    Serial.flush();
    lastAwakeDuration = millis();
}