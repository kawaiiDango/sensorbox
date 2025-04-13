package com.arn.sensorbox

import android.util.Log
import com.arn.sensorbox.widget.ListDataUtils
import com.arn.sensorbox.widget.ReadingsUtils
import com.arn.sensorbox.widget.SensorBoxData
import com.google.firebase.messaging.FirebaseMessagingService
import com.google.firebase.messaging.RemoteMessage
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.runBlocking

class IncomingMessagesService : FirebaseMessagingService() {

    override fun onMessageReceived(message: RemoteMessage) {
        super.onMessageReceived(message)

        if (message.from != "/topics/widget")
            return

        Log.d(this::class.simpleName, "onMessageReceived: ${message.data}")

        val coapTopic = message.data["topic"] ?: return
        val deviceName = coapTopic.substringBefore("/")
        val coapPayload = message.data["payload"] ?: return

        val newData = App.json.decodeFromString<SensorBoxData>(coapPayload)

        ReadingsUtils.updateWidgetData(
            deviceName = deviceName,
            newData = newData
        )
    }

}