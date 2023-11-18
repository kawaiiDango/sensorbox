#!/usr/bin/env python

import asyncio
import logging
import threading

from influxdb_client.client.influxdb_client_async import InfluxDBClientAsync
import consts
import daily_digest
import grafana_to_fcm_webhook
import coap_server
import fcm_sender


async def main():
    logging.basicConfig(
        format="%(asctime)s %(levelname)s %(message)s",
        level=logging.WARNING,
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    influx_client = InfluxDBClientAsync(
        url=consts.influx_url, token=consts.influx_token, org=consts.influx_org
    )
    write_api = influx_client.write_api()

    query_api = influx_client.query_api()

    fcm_thread = threading.Thread(target=fcm_sender.fcm_send)
    fcm_thread.daemon = True
    fcm_thread.start()

    await asyncio.gather(
        coap_server.main(write_api),
        daily_digest.main(query_api),
        grafana_to_fcm_webhook.main(),
    )


if __name__ == "__main__":
    asyncio.run(main())
