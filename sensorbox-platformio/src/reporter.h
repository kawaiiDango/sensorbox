#pragma once

#include <map>
#include <stdarg.h>
#include <my_buffers.h>
#include <Arduino.h>
#include <prefs.h>
#include <my_utils.h>
#include "coap3/coap.h"
#include <sys/socket.h>
#include <netdb.h>
#include <sensors_poll.h>
#include <cbor.h>
#include <arpa/inet.h>
#include <file_ring_buffer.h>

#define NUM_FFT_BINS 106
#define COAP_TIMEOUT 750

const static char *TAG_REPORTER = "reporter";

struct coap_meta
{
    coap_pdu_t *pdu;
    Readings *readings;
};

int64_t coap_last_active_time = 0;
bool coapClientInitialized = false;
std::map<coap_mid_t, Readings *> coapMessagesSent;
coap_context_t *coap_ctx = NULL;
coap_session_t *coap_session = NULL;
bool coap_loop_running = false;
QueueHandle_t coap_pdu_queue = xQueueCreate(5, sizeof(struct coap_meta));
SemaphoreHandle_t coap_loop_semaphore = xSemaphoreCreateBinary();
SemaphoreHandle_t coap_prepare_semaphore = xSemaphoreCreateBinary();

bool frb_save_from_rtc(bool force = false)
{
    // if ntp time is not set, or is awake and is reading the buffer file, dont save
    if (rtcSecs() > APR_20_2023_S && (readingsBuffer.full || force))
    {
        frb.begin();
        frb.pushRtcBuffer(&readingsBuffer);
        readingsBufferClear(&readingsBuffer);
        Serial.println("saved readings from rtc");
        return true;
    }
    return false;
}

void enqueueReadings(Readings *readings)
{
    if (readings == NULL)
        return;

    if (readingsBuffer.full)
    {
        bool success = frb_save_from_rtc();
        if (!success)
        {
            ESP_LOGE(TAG_REPORTER, "frb_save_from_rtc failed");
        }
    }

    readingsBufferPush(&readingsBuffer, *readings);
}

inline void set_coap_is_active()
{
    coap_last_active_time = millis();
}

inline bool coap_is_active()
{
    return millis() - coap_last_active_time < COAP_TIMEOUT;
}

static coap_response_t message_handler(coap_session_t *session,
                                       const coap_pdu_t *sent,
                                       const coap_pdu_t *received,
                                       const coap_mid_t mid)
{
    const unsigned char *data = NULL;
    size_t data_len;
    size_t offset;
    size_t total;
    coap_pdu_code_t rcvd_code = coap_pdu_get_code(received);

    if (rcvd_code == COAP_RESPONSE_CODE_CREATED || rcvd_code == COAP_RESPONSE_CODE_CHANGED) // measurement created
    {
        if (coap_get_data_large(received, &data_len, &data, &offset, &total))
        {
            if (data_len != total)
                printf("Unexpected partial data received offset %u, length %u\n", offset, data_len);

            coap_mid_t sent_mid = strtol((const char *)data, NULL, 16);

            if (sent_mid != 0)
            {
                set_coap_is_active();
                coapMessagesSent.erase(sent_mid);
            }
        }
    }
    else if (rcvd_code == COAP_RESPONSE_CODE_CONTENT) // prefs received
    {
        if (coap_get_data_large(received, &data_len, &data, &offset, &total))
        {
            if (data_len != total)
                printf("Unexpected partial data received offset %u, length %u\n", offset, data_len);

            if (data_len > 100)
            {
                Serial.println("saving prefs");
                cborToPrefsSave(data, data_len);
            }
        }
    }

    return COAP_RESPONSE_OK;
}

static coap_address_t *coap_get_address(coap_uri_t *uri)
{
    static coap_address_t dst_addr;
    char *phostname = NULL;
    struct addrinfo hints;
    struct addrinfo *addrres;
    int error;
    char tmpbuf[INET6_ADDRSTRLEN];
    struct sockaddr_in host_addr;
    struct sockaddr_in6 host_addr6;

    phostname = (char *)calloc(1, uri->host.length + 1);
    if (phostname == NULL)
    {
        ESP_LOGE(TAG_REPORTER, "calloc failed");
        return NULL;
    }
    memcpy(phostname, uri->host.s, uri->host.length);
    phostname[uri->host.length] = '\0';

    // check if host is IP address
    if (inet_pton(AF_INET, phostname, &host_addr.sin_addr) == 1)
    {
        coap_address_init(&dst_addr);

        host_addr.sin_family = AF_INET;
        host_addr.sin_port = htons(uri->port);
        memcpy(&dst_addr.addr.sin, &host_addr, sizeof(host_addr));
        inet_ntop(AF_INET, &dst_addr.addr.sin.sin_addr, tmpbuf, sizeof(tmpbuf));
        ESP_LOGI(TAG_REPORTER, "Host is IPv4. IP=%s", tmpbuf);
        free(phostname);
        return &dst_addr;
    }
    else if (inet_pton(AF_INET6, phostname, &host_addr6.sin6_addr) == 1)
    {
        coap_address_init(&dst_addr);

        host_addr6.sin6_family = AF_INET6;
        host_addr6.sin6_port = htons(uri->port);
        memcpy(&dst_addr.addr.sin6, &host_addr6, sizeof(host_addr6));
        inet_ntop(AF_INET6, &dst_addr.addr.sin6.sin6_addr, tmpbuf, sizeof(tmpbuf));
        ESP_LOGI(TAG_REPORTER, "Host is IPv6. IP=%s", tmpbuf);
        free(phostname);
        return &dst_addr;
    }

    memset((char *)&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;

    error = getaddrinfo(phostname, NULL, &hints, &addrres);
    if (error != 0)
    {
        ESP_LOGE(TAG_REPORTER, "DNS lookup failed for destination address %s. error: %d", phostname, error);
        free(phostname);
        return NULL;
    }
    if (addrres == NULL)
    {
        ESP_LOGE(TAG_REPORTER, "DNS lookup %s did not return any addresses", phostname);
        free(phostname);
        return NULL;
    }
    free(phostname);
    coap_address_init(&dst_addr);
    switch (addrres->ai_family)
    {
    case AF_INET:
        memcpy(&dst_addr.addr.sin, addrres->ai_addr, sizeof(dst_addr.addr.sin));
        dst_addr.addr.sin.sin_port = htons(uri->port);
        inet_ntop(AF_INET, &dst_addr.addr.sin.sin_addr, tmpbuf, sizeof(tmpbuf));
        ESP_LOGI(TAG_REPORTER, "DNS lookup succeeded. IP=%s", tmpbuf);
        break;
    case AF_INET6:
        memcpy(&dst_addr.addr.sin6, addrres->ai_addr, sizeof(dst_addr.addr.sin6));
        dst_addr.addr.sin6.sin6_port = htons(uri->port);
        inet_ntop(AF_INET6, &dst_addr.addr.sin6.sin6_addr, tmpbuf, sizeof(tmpbuf));
        ESP_LOGI(TAG_REPORTER, "DNS lookup succeeded. IP=%s", tmpbuf);
        break;
    default:
        ESP_LOGE(TAG_REPORTER, "DNS lookup response failed");
        return NULL;
    }
    freeaddrinfo(addrres);

    return &dst_addr;
}

int coap_build_optlist(coap_uri_t *uri, coap_optlist_t **optlist)
{
#define BUFSIZE 40
    unsigned char _buf[40];
    unsigned char *buf;
    size_t buflen;
    int res;

    if (uri->scheme == COAP_URI_SCHEME_COAPS && !coap_dtls_is_supported())
    {
        ESP_LOGE(TAG_REPORTER, "MbedTLS DTLS Client Mode not configured");
        return 0;
    }
    if (uri->scheme == COAP_URI_SCHEME_COAPS_TCP && !coap_tls_is_supported())
    {
        ESP_LOGE(TAG_REPORTER, "MbedTLS TLS Client Mode not configured");
        return 0;
    }
    if (uri->scheme == COAP_URI_SCHEME_COAP_TCP && !coap_tcp_is_supported())
    {
        ESP_LOGE(TAG_REPORTER, "TCP Client Mode not configured");
        return 0;
    }

    if (uri->path.length)
    {
        buflen = BUFSIZE;
        buf = _buf;
        res = coap_split_path(uri->path.s, uri->path.length, buf, &buflen);

        while (res--)
        {
            coap_insert_optlist(optlist,
                                coap_new_optlist(COAP_OPTION_URI_PATH,
                                                 coap_opt_length(buf),
                                                 coap_opt_value(buf)));

            buf += coap_opt_size(buf);
        }
    }

    if (uri->query.length)
    {
        buflen = BUFSIZE;
        buf = _buf;
        res = coap_split_query(uri->query.s, uri->query.length, buf, &buflen);

        while (res--)
        {
            coap_insert_optlist(optlist,
                                coap_new_optlist(COAP_OPTION_URI_QUERY,
                                                 coap_opt_length(buf),
                                                 coap_opt_value(buf)));

            buf += coap_opt_size(buf);
        }
    }
    return 1;
}
#ifdef CONFIG_COAP_MBEDTLS_PSK
static coap_session_t *
coap_start_psk_session(coap_context_t *ctx, coap_address_t *dst_addr, coap_uri_t *uri)
{
    static coap_dtls_cpsk_t dtls_psk;
    static char client_sni[256];

    memset(client_sni, 0, sizeof(client_sni));
    memset(&dtls_psk, 0, sizeof(dtls_psk));
    dtls_psk.version = COAP_DTLS_CPSK_SETUP_VERSION;
    dtls_psk.validate_ih_call_back = NULL;
    dtls_psk.ih_call_back_arg = NULL;
    if (uri->host.length)
        memcpy(client_sni, uri->host.s, min(uri->host.length, sizeof(client_sni) - 1));
    else
        memcpy(client_sni, "localhost", 9);
    dtls_psk.client_sni = client_sni;
    dtls_psk.psk_info.identity.s = (const uint8_t *)prefs.coapDtlsId;
    dtls_psk.psk_info.identity.length = strlen(prefs.coapDtlsId);
    dtls_psk.psk_info.key.s = (const uint8_t *)prefs.coapDtlsPsk;
    dtls_psk.psk_info.key.length = strlen(prefs.coapDtlsPsk);
    return coap_new_client_session_psk2(ctx, NULL, dst_addr,
                                        uri->scheme == COAP_URI_SCHEME_COAPS ? COAP_PROTO_DTLS : COAP_PROTO_TLS,
                                        &dtls_psk);
}
#endif /* CONFIG_COAP_MBEDTLS_PSK */

void coap_create_uri(const char *path, coap_uri_t *uri, coap_optlist_t **optlist)
{
    String uri_str = String("coap") + ((strlen(prefs.coapDtlsId) == 0 || strlen(prefs.coapDtlsPsk) == 0) ? "" : "s") + "://" + prefs.coapHost + ":" + prefs.coapPort + "/" + path;

    if (coap_split_uri((const uint8_t *)uri_str.c_str(), uri_str.length(), uri) == -1)
    {
        ESP_LOGE(TAG_REPORTER, "CoAP server uri error");
        return;
    }

    if (optlist != NULL && !coap_build_optlist(uri, optlist))
    {
        ESP_LOGE(TAG_REPORTER, "coap_build_optlist failed");
    }
}

size_t createReadingsCbor(Readings *readings, uint8_t *buffer)
{
    CborEncoder root_encoder;
    CborEncoder map_encoder;
    int error = CborNoError;
    size_t buffer_size = 512;

    cbor_encoder_init(&root_encoder, buffer, buffer_size, 0);

    error |= cbor_encoder_create_map(&root_encoder, &map_encoder, READINGS_NUM_FIELDS);

    error |= cbor_encode_text_stringz(&map_encoder, "timestamp");
    error |= cbor_encode_uint(&map_encoder, readings->timestamp);

#ifdef THE_BOX
    error |= cbor_encode_text_stringz(&map_encoder, "motion");
    error |= cbor_encode_int(&map_encoder, readings->motion);

    error |= cbor_encode_text_stringz(&map_encoder, "ir");
    error |= cbor_encode_int(&map_encoder, readings->ir);

    error |= cbor_encode_text_stringz(&map_encoder, "visible");
    error |= cbor_encode_int(&map_encoder, readings->visible);

    error |= cbor_encode_text_stringz(&map_encoder, "pressure");
    error |= cbor_encode_float(&map_encoder, readings->pressure);

    error |= cbor_encode_text_stringz(&map_encoder, "luminosity");
    error |= cbor_encode_float(&map_encoder, readings->luminosity);

    error |= cbor_encode_text_stringz(&map_encoder, "pm25");
    error |= cbor_encode_float(&map_encoder, readings->pm25);

    error |= cbor_encode_text_stringz(&map_encoder, "pm10");
    error |= cbor_encode_float(&map_encoder, readings->pm10);

    error |= cbor_encode_text_stringz(&map_encoder, "voltageAvg");
    error |= cbor_encode_float(&map_encoder, readings->voltageAvg);

    error |= cbor_encode_text_stringz(&map_encoder, "soundDbA");
    error |= cbor_encode_float(&map_encoder, readings->soundDbA);

    error |= cbor_encode_text_stringz(&map_encoder, "audioFft");
    error |= cbor_encode_byte_string(&map_encoder, readings->audioFft, NUM_FFT_BINS);

#else
    error |= cbor_encode_text_stringz(&map_encoder, "voc");
    error |= cbor_encode_float(&map_encoder, readings->voc);

    error |= cbor_encode_text_stringz(&map_encoder, "boardTemperature");
    error |= cbor_encode_float(&map_encoder, readings->boardTemperature);
#endif

    error |= cbor_encode_text_stringz(&map_encoder, "temperature");
    error |= cbor_encode_float(&map_encoder, readings->temperature);

    error |= cbor_encode_text_stringz(&map_encoder, "humidity");
    error |= cbor_encode_float(&map_encoder, readings->humidity);

    error |= cbor_encode_text_stringz(&map_encoder, "freeHeap");
    error |= cbor_encode_float(&map_encoder, readings->freeHeap);

    error |= cbor_encode_text_stringz(&map_encoder, "awakeTime");
    error |= cbor_encode_float(&map_encoder, readings->awakeTime);

    error |= cbor_encoder_close_container(&root_encoder, &map_encoder);

    if (error != CborNoError)
    {
        if (error == CborErrorInternalError)
            printf("CborErrorInternalError");
        else if (error == CborErrorOutOfMemory)
            printf("CborErrorOutOfMemory");

        printf("Error encoding CBOR: %d\n", error);
        return 0;
    }

    size_t encoded_size = cbor_encoder_get_buffer_size(&root_encoder, buffer);

    if (encoded_size > 500)
        printf("Encoded size: %zu\n", encoded_size);

#if PRINT_CBOR
    printCbor(buffer, encoded_size);
#endif

    return encoded_size;
}

void coapPrepareClient()
{
    coap_address_t *dst_addr = NULL;
    coap_uri_t uri = {};

    /* Set up the CoAP context */
    coap_ctx = coap_new_context(NULL);
    if (!coap_ctx)
    {
        ESP_LOGE(TAG_REPORTER, "coap_new_context() failed");
        goto finish;
    }
    coap_context_set_block_mode(coap_ctx,
                                COAP_BLOCK_USE_LIBCOAP | COAP_BLOCK_SINGLE_BODY);

    coap_register_response_handler(coap_ctx, message_handler);

    coap_create_uri("test", &uri, NULL);

    dst_addr = coap_get_address(&uri);
    if (!dst_addr)
    {
        goto finish;
    }

    /*
     * Note that if the URI starts with just coap:// (not coaps://) the
     * session will still be plain text.
     */
    if (uri.scheme == COAP_URI_SCHEME_COAPS || uri.scheme == COAP_URI_SCHEME_COAPS_TCP)
    {
#ifdef CONFIG_COAP_MBEDTLS_PSK
        coap_session = coap_start_psk_session(coap_ctx, dst_addr, &uri);
#endif /* CONFIG_COAP_MBEDTLS_PSK */
    }
    else
    {
        coap_session = coap_new_client_session(coap_ctx, NULL, dst_addr,
                                               uri.scheme == COAP_URI_SCHEME_COAP_TCP ? COAP_PROTO_TCP : COAP_PROTO_UDP);
    }
    if (!coap_session)
    {
        ESP_LOGE(TAG_REPORTER, "coap_new_client_session() failed");
        goto finish;
    }
    coapClientInitialized = true;

finish:
    xSemaphoreGive(coap_prepare_semaphore);
}

void coapClientCleanup()
{
    if (!coapClientInitialized || !coap_ctx)
        return;

    bool done = coapMessagesSent.size() == 0;

    if (!done)
    {
        for (auto const &entry : coapMessagesSent)
        {
            Serial.print(entry.first, HEX);
            Serial.println(" not ACKed");
            enqueueReadings(entry.second);
        }

        struct coap_meta meta;
        while (xQueueReceive(coap_pdu_queue, &meta, 0) == pdTRUE)
        {
            if (meta.readings == NULL)
                continue;
            Serial.print(meta.readings->timestamp);
            Serial.println(" not sent");
            enqueueReadings(meta.readings);
        }
    }

    if (coap_session)
    {
        coap_session_release(coap_session);
    }
    if (coap_ctx)
    {
        coap_free_context(coap_ctx);
    }

    coapClientInitialized = false;
}

coap_pdu_t *coap_create_my_pdu(coap_pdu_code_t req_code, coap_optlist_t *optlist, coap_pdu_type_t pdu_type, bool observe = false,
                               uint8_t *data = NULL, size_t data_len = 0, unsigned int mime = COAP_MEDIATYPE_APPLICATION_CBOR)
{
    coap_pdu_t *request = NULL;
    size_t tokenlength;
    unsigned char token[8];
    unsigned char buf[4];

    coap_insert_optlist(&optlist,
                        coap_new_optlist(COAP_OPTION_CONTENT_FORMAT,
                                         coap_encode_var_safe(buf, sizeof(buf), mime), buf));

    request = coap_new_pdu(pdu_type, req_code, coap_session);
    if (!request)
    {
        return NULL;
    }
    /* Add in an unique token */
    coap_session_new_token(coap_session, &tokenlength, token);
    coap_add_token(request, tokenlength, token);

    if (req_code == COAP_REQUEST_CODE_GET && observe)
    {
        if (!coap_insert_optlist(&optlist, coap_new_optlist(COAP_OPTION_OBSERVE, COAP_OBSERVE_ESTABLISH, NULL)))
            return NULL;
    }

    if (data_len > 0 && data != NULL)
        coap_add_data_large_request(coap_session, request, data_len, data, NULL, NULL);

    coap_add_optlist_pdu(request, &optlist);

    return request;
}

void coap_io_loop(void *arg)
{
    if (!coapClientInitialized)
    {
        coapPrepareClient();
        if (!coapClientInitialized)
        {
            ESP_LOGE(TAG_REPORTER, "coapPrepareClient failed");
            coap_loop_running = false;
            goto finish;
        }
    }
    set_coap_is_active();
    while (coap_loop_running || coap_is_active() || !isIdle())
    {
        struct coap_meta meta;
        bool q_received = xQueueReceive(coap_pdu_queue, &meta, 0);
        if (q_received == pdTRUE && meta.pdu != NULL)
        {
            coap_mid_t mid = coap_send(coap_session, meta.pdu);

            if (mid == COAP_INVALID_MID)
            {
                ESP_LOGE(TAG_REPORTER, "coap_send failed");
                goto finish;
            }
            else if (meta.readings != NULL)
            {
                coapMessagesSent[mid] = meta.readings;
            }
        }

        int time_taken = coap_io_process(coap_ctx, 50);
        if (time_taken < 0)
        {
            ESP_LOGE(TAG_REPORTER, "coap_io_process failed");
            break;
        }
    }

finish:
    coapClientCleanup();
    xSemaphoreGive(coap_loop_semaphore);
    vTaskDelete(NULL);
}

bool coap_client_sync_prefs()
{
    coap_uri_t uri;
    coap_pdu_t *request = NULL;
    coap_optlist_t *optlist = NULL;
    char pathbuf_small[50];
    uint8_t databuf_big[512];

    sprintf(pathbuf_small, "%s/prefs", prefs.uriPrefix);
    return true;

    coap_create_uri(pathbuf_small, &uri, &optlist);

    size_t data_len = prefsToCbor(databuf_big);
    request = coap_create_my_pdu(COAP_REQUEST_CODE_POST, optlist, COAP_MESSAGE_CON, false, databuf_big, data_len);

    if (!request)
    {
        ESP_LOGE(TAG_REPORTER, "coap_create_my_pdu failed");
        return false;
    }

    struct coap_meta meta = {request, NULL};

    xQueueSend(coap_pdu_queue, &meta, portMAX_DELAY);

    return true;
}

void coap_readings_report_loop(void *arg)
{
    coap_optlist_t *optlist = NULL;
    size_t data_len;
    Readings *readings = NULL;
    coap_pdu_t *request = NULL;
    coap_uri_t uri;
    Readings *entries = NULL;
    char pathbuf_small[50];
    uint8_t databuf_big[512];

    if (coapClientInitialized && wasTouchpadWakeup && prefs.lastChangedS > APR_20_2023_S)
        coap_client_sync_prefs();

    frb.beginPrefs();

    bool frb_inited = false;

    if (frb.size() > 0)
    {
        frb.begin();
        frb_inited = true;
    }

    set_coap_is_active();

    while (coapClientInitialized && coap_is_active() && (coap_loop_running || readings != NULL || (frb_inited && frb.size() > 0)))
    {
        optlist = NULL;
        readings = readingsBufferPop(&readingsBuffer);
        if (readings == NULL)
        {
            if (frb_inited && frb.size() > 0)
            {
                entries = new Readings[frb.blockSize / sizeof(Readings)];
                size_t num_entries = frb.popFile(entries);
                if (num_entries == 0)
                {
                    ESP_LOGE(TAG_REPORTER, "frb.popFile failed");
                    frb.listFilesAndMeta();
                    frb.clear();
                    frb_inited = false;
                }
                else
                {
                    for (int i = 0; i < num_entries; i++)
                        enqueueReadings(&entries[i]);

                    Serial.printf("%d readings loaded from frb\n", num_entries);
                }

                delete[] entries;
                continue;
            }
        }

        if (readings == NULL)
        {
            delay(100);
            continue;
        }

        data_len = createReadingsCbor(readings, databuf_big);

        sprintf(pathbuf_small, "%s/data", prefs.uriPrefix);

        coap_create_uri(pathbuf_small, &uri, &optlist);
        request = coap_create_my_pdu(COAP_REQUEST_CODE_PUT, optlist, COAP_MESSAGE_NON, false, databuf_big, data_len);
        if (!request)
        {
            ESP_LOGE(TAG_REPORTER, "coap_create_my_pdu failed");
            enqueueReadings(readings);
            break;
        }

        struct coap_meta meta = {request, readings};
        xQueueSend(coap_pdu_queue, &meta, portMAX_DELAY);
    }

    vTaskDelete(NULL);
}