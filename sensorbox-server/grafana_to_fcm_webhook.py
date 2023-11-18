from aiohttp import web
import consts
import fcm_sender


async def handle(request):
    data = await request.json()
    print(data)
    if data["state"] == "alerting":
        fcm_sender.enqueue("alerts", data, True)
    return web.Response(text="ok")


async def main():
    app = web.Application()
    app.add_routes([web.post("/alert-endpoint", handle)])

    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, host="127.0.0.1", port=consts.webhook_bind_port)
    await site.start()
