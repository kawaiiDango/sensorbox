import asyncio
import datetime
import logging
from influxdb_client.client.query_api_async import QueryApiAsync
from influxdb_client.client.flux_table import FluxRecord
import fcm_sender
import consts

query_api: QueryApiAsync = None
duration = "-24h"


def get_index_or_none(lst, n):
    try:
        return lst[n]
    except IndexError:
        return None


first_device_name = consts.device_names[0]
second_device_name = get_index_or_none(consts.device_names, 1)

queries_partial = {
    "temp": f"""from(bucket:"{consts.influx_bucket}")
    |> range(start: {duration})
    |> filter(fn: (r) => r["topic"] =~ /^{first_device_name}.+/)
    |> filter(fn: (r) => r["_field"] == "temperature")
    |> keep(columns: ["_value", "_field", "_time"])
    """,
    "humidity": f"""from(bucket:"{consts.influx_bucket}")
    |> range(start: {duration})
    |> filter(fn: (r) => r["topic"] =~ /^{first_device_name}.+/)
    |> filter(fn: (r) => r["_field"] == "humidity")
    |> keep(columns: ["_value", "_field", "_time"])
    """,
    "pressure": f"""import "math"
    seaPressure = (t,p) => p * (math.exp(x: 9.80665 * 0.0289644 * {consts.altitude_m}.0 / (8.31447 * (t + 273.15))))
    temperature = from(bucket:"{consts.influx_bucket}")
    |> range(start: {duration})
    |> filter(fn: (r) => r["topic"] =~ /^{first_device_name}.+/)
    |> filter(fn: (r) => r["_field"] == "temperature")
    |> keep(columns: ["_value", "_field", "_time"])
    |> aggregateWindow(every: 10m, fn: mean, createEmpty: false)
    pressure = from(bucket: "{consts.influx_bucket}")
    |> range(start: {duration})
    |> filter(fn: (r) => r["topic"] =~ /^{first_device_name}.+/)
    |> filter(fn: (r) => r["_field"] == "pressure")
    |> keep(columns: ["_value", "_field", "_time"])
    |> aggregateWindow(every: 10m, fn: mean, createEmpty: false)
    join(tables: {{temperature, pressure}}, on: ["_time"])
    |> map(fn: (r) => ({{r with _value: seaPressure(t: r._value_temperature, p: r._value_pressure)}}))
    |> keep(columns: ["_value", "_time"])
    |> set(key: "_field", value: "pressure")
    """,
    "pm25": f"""from(bucket:"{consts.influx_bucket}")
    |> range(start: {duration})
    |> filter(fn: (r) => r["topic"] =~ /^{first_device_name}.+/)
    |> filter(fn: (r) => r["_field"] == "pm25")
    |> drop(columns: ["topic", "_measurement", "bucket"])
    """,
    "pm10": f"""from(bucket:"{consts.influx_bucket}")
    |> range(start: {duration})
    |> filter(fn: (r) => r["topic"] =~ /^{first_device_name}.+/)
    |> filter(fn: (r) => r["_field"] == "pm10")
    |> drop(columns: ["topic", "_measurement", "bucket"])
    """,
    "soundDbA": f"""from(bucket:"{consts.influx_bucket}")
    |> range(start: {duration})
    |> filter(fn: (r) => r["topic"] =~ /^{first_device_name}.+/)
    |> filter(fn: (r) => r["_field"] == "soundDbA")
    |> drop(columns: ["topic", "_measurement", "bucket"])
    """,
}

if second_device_name is not None:
    queries_partial["rTemp"] = f"""from(bucket:"{consts.influx_bucket}")
    |> range(start: {duration})
    |> filter(fn: (r) => r["topic"] =~ /^{second_device_name}.+/)
    |> filter(fn: (r) => r["_field"] == "temperature")
    |> keep(columns: ["_value", "_field", "_time"])
    """

    queries_partial["rHumidity"] = f"""from(bucket:"{consts.influx_bucket}")
    |> range(start: {duration})
    |> filter(fn: (r) => r["topic"] =~ /^{second_device_name}.+/)
    |> filter(fn: (r) => r["_field"] == "humidity")
    |> keep(columns: ["_value", "_field", "_time"])
    """


async def do_query(query_api, q):
    results = []

    try:
        result = await query_api.query(query=q)
    except Exception as e:
        logging.error("Exception when querying")
        logging.error(q)
        logging.error(e)
        return [0]

    for table in result:
        # print(table)
        record: FluxRecord = table.records[0]
        results.append(record.get_value())
    return results


async def make_digest() -> None:
    noti_msgs = []
    for name, query_partial in queries_partial.items():
        min_result = (await do_query(query_api, query_partial + "|> min()"))[-1]
        max_result = (await do_query(query_api, query_partial + "|> max()"))[-1]
        mean_result = (await do_query(query_api, query_partial + "|> mean()"))[-1]
        noti_msgs.append(
            f"{name}: 🔻{min_result:.2f} 🔺{max_result:.2f} 🔹{mean_result:.2f}"
        )

    fcm_sender.enqueue(
        "digests", {"title": "🔻Min🔺Max🔹Mean", "message": "\n".join(noti_msgs)}, True
    )
    logging.info(noti_msgs)


async def run_at(time, coro):
    now = datetime.datetime.now()
    delay = ((time - now) % datetime.timedelta(days=1)).total_seconds()
    await asyncio.sleep(delay)
    return await coro


async def main(query_api_p: QueryApiAsync):
    global query_api
    query_api = query_api_p

    # test immediately
    # await make_digest()

    time_to_run = datetime.datetime.combine(
        datetime.date.today(),
        datetime.time(consts.daily_digest_hr, consts.daily_digest_min),
    )
    while True:
        await run_at(time_to_run, make_digest())
        # Increment the day for the next run
        time_to_run += datetime.timedelta(days=1)
