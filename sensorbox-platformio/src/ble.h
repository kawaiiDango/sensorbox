#pragma once

#include "esp_log.h"
/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include <esp_bt.h>

#define BLE_GAP_ADV_ITVL 32 // 20ms
#define BLE_GAP_ADV_DURATION_MS 50
#define CONFIG_EXAMPLE_RANDOM_ADDR 1

void bleprph_advertise(void);

#if CONFIG_EXAMPLE_RANDOM_ADDR
static uint8_t own_addr_type = BLE_OWN_ADDR_RANDOM;
#else
static uint8_t own_addr_type;
#endif

SemaphoreHandle_t bleAdvDone = xSemaphoreCreateBinary();
uint8_t *bleAdvData;
uint8_t bleAdvDataLen;

static void adv_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

#if CONFIG_EXAMPLE_RANDOM_ADDR
static void adv_set_addr(void)
{
    ble_addr_t addr;
    int rc;

    /* generate new non-resolvable private address */
    rc = ble_hs_id_gen_rnd(0, &addr);
    assert(rc == 0);

    /* set generated address */
    rc = ble_hs_id_set_rnd(addr.val);

    assert(rc == 0);
}
#endif

static void adv_on_sync(void)
{
    int rc;

#if CONFIG_EXAMPLE_RANDOM_ADDR
    /* Generate a non-resolvable private address. */
    adv_set_addr();
    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(1);
#else
    rc = ble_hs_util_ensure_addr(0);
#endif
    assert(rc == 0);

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    bleprph_advertise();
}

void adv_host_task(void *param)
{
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
    int ret;
    // MODLOG_DFLT(INFO, "GAP event: %d", event->type);

    switch (event->type)
    {
    case BLE_GAP_EVENT_ADV_COMPLETE:
        xSemaphoreGive(bleAdvDone);
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "Device disconnected");
        break;

    default:
        break;
    }

    return 0;
}

void bleprph_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields adv_fields;
    // struct ble_hs_adv_fields rsp_fields;
    const char *name;

    int rc;

    memset(&adv_fields, 0, sizeof adv_fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN |
                       BLE_HS_ADV_F_BREDR_UNSUP;


    adv_fields.mfg_data = bleAdvData;
    adv_fields.mfg_data_len = bleAdvDataLen;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
        return;
    }

    // // set scan response data
    // memset(&rsp_fields, 0, sizeof rsp_fields);

    // rsp_fields.mfg_data = bleRespData;
    // rsp_fields.mfg_data_len = bleRespDataLen;

    // rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    // if (rc != 0)
    // {
    //     MODLOG_DFLT(ERROR, "failed to set scan response data, error code: %d", rc);
    //     return;
    // }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    adv_params.itvl_min = BLE_GAP_ADV_ITVL;
    adv_params.itvl_max = BLE_GAP_ADV_ITVL;

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_GAP_ADV_DURATION_MS,
                           &adv_params, bleprph_gap_event, NULL);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
        return;
    }
}

void do_ble_adv(uint8_t *advData, uint8_t advDataLen)
{
    int ret;

    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE("BLE", "Failed to init nimble %d ", ret);
        return;
    }

    bleAdvData = advData;
    bleAdvDataLen = advDataLen;

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = adv_on_reset;
    ble_hs_cfg.sync_cb = adv_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    nimble_port_freertos_init(adv_host_task);

    // wait on the semaphore

    if (xSemaphoreTake(bleAdvDone, 5000 / portTICK_PERIOD_MS) == pdTRUE)
    {
        ESP_ERROR_CHECK(nimble_port_stop());
        ESP_ERROR_CHECK(nimble_port_deinit());
    }
    else
    {
        ESP_LOGE("BLE", "BLE adv task failed to finish");
    }
}
