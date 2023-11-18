import asyncio
import logging
import math
import datetime
import json
from urllib.parse import urlparse

import cbor2
import aiocoap
from aiocoap.resource import Resource, ObservableResource
from aiocoap.numbers.contentformat import ContentFormat
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


class PrefsResource(ObservableResource):
    def __init__(self):
        super().__init__()

    def read_prefs(self, topic_prefix):
        try:
            with open(f"prefs/{topic_prefix}.cbor", "rb") as f:
                return cbor2.load(f)
        except (cbor2.CBORDecodeEOF, cbor2.CBORDecodeError, FileNotFoundError):
            return None

    def write_prefs(self, topic_prefix, data):
        try:
            with open(f"prefs/{topic_prefix}.cbor", "wb") as f:
                cbor2.dump(data, f)
        except cbor2.CBORDecodeError:
            logging.error("Invalid CBOR payload")

    async def render_get(self, request: aiocoap.Message):
        uri = request.get_request_uri()
        existing_data = self.read_prefs(uri_first_path(uri))

        if existing_data is None:
            existing_prefs_bytes = b""
        else:
            existing_prefs_bytes = cbor2.dumps(existing_data)

        return aiocoap.Message(
            payload=existing_prefs_bytes,
            content_format=ContentFormat.CBOR,
            code=aiocoap.message.Code.CONTENT,
        )

    async def render_post(self, request: aiocoap.Message):
        uri = request.get_request_uri()

        new_payload = cbor2.loads(request.payload)
        new_timestamp = new_payload["lastChangedS"]

        existing_data = self.read_prefs(uri_first_path(uri))

        if existing_data is None:
            existing_timestamp = 0
        else:
            existing_timestamp = existing_data["lastChangedS"]

        logging.info(
            f"existing_timestamp {datetime.datetime.fromtimestamp(existing_timestamp)}"
        )
        logging.info(f"new_timestamp {datetime.datetime.fromtimestamp(new_timestamp)}")

        if new_timestamp > existing_timestamp:
            self.write_prefs(uri_first_path(uri), new_payload)
            self.updated_state()
            return aiocoap.Message(
                payload="Server prefs changed".encode("UTF-8"),
                code=aiocoap.message.Code.CHANGED,
            )
        else:
            return aiocoap.Message(
                payload=cbor2.dumps(existing_data),
                content_format=ContentFormat.CBOR,
                code=aiocoap.message.Code.CONTENT,
            )


class ReadingsResource(Resource):
    def __init__(self):
        super().__init__()

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
            .time(data["timestamp"])
        )

        for key, value in data.items():
            if key != "timestamp" and key != "audioFft":
                if value != -1 and not math.isnan(value):
                    val = round(value, 6)
                    point.field(key, val)
        logging.info(point)
        await write_api.write(
            bucket=consts.influx_bucket, record=point, write_precision=WritePrecision.S
        )

        audio_fft_bytes = data.get("audioFft")

        fcm_q_message(topic, data)

        if audio_fft_bytes is not None:
            point = (
                Point(uri_first_path(uri))
                .tag("topic", uri_first_path(uri) + "/audioFft")
                .time(data["timestamp"])
            )

            for i in range(0, len(audio_fft_bytes)):
                val = float(audio_fft_bytes[i])
                point.field(f"bin{i:03}", val)

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
        root.add_resource([device_name, "prefs"], PrefsResource())
        root.add_resource([device_name, "data"], ReadingsResource())

    root.add_resource(["time"], TimeResource())

    await aiocoap.Context.create_server_context(
        root, bind=(consts.coap_bind_ip, consts.coap_bind_port)
    )
