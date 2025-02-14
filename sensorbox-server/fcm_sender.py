import logging
import datetime
import queue
import os

from firebase_admin import messaging, credentials
import firebase_admin
import consts

firebase_cred = credentials.Certificate(
    os.path.join(os.path.dirname(__file__), consts.firebase_certificate_file)
)
firebase_app = firebase_admin.initialize_app(firebase_cred)

fcm_queue = queue.Queue(maxsize=100)


def enqueue(topic: str, payload_dict: str, notification: bool):
    fcm_queue.put((topic, payload_dict, notification))


def fcm_send():
    while True:
        (topic, payload_dict, notification) = fcm_queue.get(block=True)

        if "timestamp" in payload_dict:
            logging.warning(datetime.datetime.fromtimestamp(payload_dict["timestamp"]))

        if notification:
            message = messaging.Message(
                notification=messaging.Notification(
                    title=payload_dict["title"], body=payload_dict["message"]
                ),
                topic=topic,
                android=messaging.AndroidConfig(
                    ttl=datetime.timedelta(hours=24),
                    priority="normal",
                    notification=messaging.AndroidNotification(
                        color="#ffaaff",
                        channel_id=topic,
                        priority="min",
                    ),
                ),
            )
        else:
            message = messaging.Message(
                data=payload_dict,
                topic=topic,
                android=messaging.AndroidConfig(
                    ttl=datetime.timedelta(hours=24),
                    priority="normal",
                ),
            )

        try:
            result = messaging.send(message)
            logging.info(result)
        except Exception as e:
            logging.error(e)
