#!/usr/bin/env python3

import asyncio
import logging
import math
import datetime
import json
import sys
from urllib.parse import urlparse

import cbor2
import aiocoap
from aiocoap.resource import Resource, ObservableResource
from aiocoap.credentials import CredentialsMap
from influxdb_client.domain.write_precision import WritePrecision
from influxdb_client import Point

import consts
import fcm_sender

write_api = None
fcm_last_timestamps = {}


def uri_first_path(uri):
    return urlparse(uri).path.split("/")[1]


def uri_to_topic(uri):
    return urlparse(uri).path[1:]


def fcm_q_message(topic, payload_dict):
    global fcm_last_timestamps

    if topic in fcm_last_timestamps:
        if payload_dict["timestamp"] <= fcm_last_timestamps[topic]:
            logging.warning(f"Skipping {topic}")
            return

    # delete all entries that are nan or -1 in that dict
    new_payload_dict = {}
    for key, value in payload_dict.items():
        if key != "audioFft" and value != -1 and not math.isnan(value):
            new_payload_dict[key] = value
    payload_dict = new_payload_dict

    fcm_last_timestamps[topic] = payload_dict["timestamp"]

    fcm_sender.enqueue(
        "widget", {"topic": topic, "payload": json.dumps(payload_dict)}, False
    )


class ReadingsResource(Resource):
    def __init__(self):
        super().__init__()

    def reconstruct_fft(self, unique_values, sample_rate=48000, fft_n=2048):
        # Recalculate logarithmic bin center frequencies.
        log_bins = []
        i = 0
        while True:
            freq = (sample_rate * 2 / fft_n) * (2 ** (i / 12.0))
            if freq > (sample_rate / 2):
                bin_count = i
                break
            log_bins.append(freq)
            i += 1

        full_resampled = []
        prev_bin_start = -1
        unique_idx = 0

        # Loop over each bin in the same way as the C code.
        # I am not considering the last bin, to make the number aligned and a multiple of 4, so subtract 1
        # the answer turns out to be 84 bins at 48000 Hz and 2048 FFT size
        for i in range(bin_count - 1 - 1):
            bin_start = int(log_bins[i] / (sample_rate / fft_n))
            bin_end = int(log_bins[i + 1] / (sample_rate / fft_n))

            while bin_start == bin_end:
                i += 1
                bin_end = int(log_bins[i + 1] / (sample_rate / fft_n))

            # If this bin's key is new, pick the next unique value.
            if bin_start != prev_bin_start:
                value = unique_values[unique_idx]
                unique_idx += 1
                prev_bin_start = bin_start
            # Append the value for this bin.
            full_resampled.append(value)

        return full_resampled

    async def render_put(self, request: aiocoap.Message):
        global fcm_q_tasks

        data = cbor2.loads(request.payload)
        uri = request.get_request_uri()
        mid_hex = hex(request.mid)
        topic = uri_to_topic(uri)

        logging.info(f"Received {mid_hex}")

        point = (
            Point(uri_first_path(uri))
            .tag("topic", uri_to_topic(uri))
            .time(data["timestamp"], WritePrecision.S)
        )

        for key, value in data.items():
            if key != "timestamp" and key != "audioFft":
                if value and value != -1 and not math.isnan(value):
                    val = round(value, 6)
                    point.field(key, val)
        logging.info(point)
        await write_api.write(
            bucket=consts.influx_bucket, record=point, write_precision=WritePrecision.S
        )

        fcm_q_message(topic, data)

        audio_fft_bytes = data.get("audioFft")

        if audio_fft_bytes is not None:
            audio_fft_reconstructed = self.reconstruct_fft(audio_fft_bytes)

            point = (
                Point(uri_first_path(uri))
                .tag("topic", uri_first_path(uri) + "/audioFft")
                .time(data["timestamp"], WritePrecision.S)
            )

            for i, val in enumerate(audio_fft_reconstructed):
                point.field(f"bin{i:03}", float(val))

            logging.info(point)
            await write_api.write(
                bucket=consts.influx_bucket,
                record=point,
                write_precision=WritePrecision.S,
            )

        return aiocoap.Message(
            payload=mid_hex.encode("UTF-8"), code=aiocoap.message.Code.CREATED
        )


class TimeResource(ObservableResource):
    """Example resource that can be observed. The `notify` method keeps
    scheduling itself, and calles `update_state` to trigger sending
    notifications."""

    def __init__(self):
        super().__init__()

        self.handle = None

    def notify(self):
        self.updated_state()
        self.reschedule()

    def reschedule(self):
        self.handle = asyncio.get_event_loop().call_later(5, self.notify)

    def update_observation_count(self, count):
        if count and self.handle is None:
            logging.info("Starting the clock")
            self.reschedule()
        if count == 0 and self.handle:
            logging.info("Stopping the clock")
            self.handle.cancel()
            self.handle = None

    async def render_get(self, request):
        payload = datetime.datetime.now().strftime("%Y-%m-%d %H:%M").encode("ascii")
        return aiocoap.Message(payload=payload)


async def main(write_api_p):
    global write_api

    write_api = write_api_p

    root = aiocoap.resource.Site()

    for device_name in consts.device_names:
        root.add_resource([device_name, "data"], ReadingsResource())

    root.add_resource(["time"], TimeResource())

    server_credentials = None
    transports = None

    if consts.dtls_client_identity and consts.dtls_psk:
        server_credentials = CredentialsMap()
        server_credentials.load_from_dict(
            {
                ":client": {
                    "dtls": {
                        "client-identity": {"ascii": consts.dtls_client_identity},
                        "psk": {"ascii": consts.dtls_psk},
                    }
                }
            }
        )
        transports = ["tinydtls_server"]
    else:
        if sys.platform != "linux":
            transports = ["simple6"]
        else:
            transports = ["udp6"]

    await aiocoap.Context.create_server_context(
        root,
        bind=(consts.coap_bind_ip, consts.coap_bind_port),  # dtls runs on port + 1
        server_credentials=server_credentials,
        transports=transports,
    )
