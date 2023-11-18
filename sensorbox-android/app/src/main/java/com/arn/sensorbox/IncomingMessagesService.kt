package com.arn.sensorbox

import android.util.Log
import com.arn.sensorbox.widget.ListDataUtils
import com.arn.sensorbox.widget.SensorBoxData
import com.google.firebase.messaging.FirebaseMessagingService
import com.google.firebase.messaging.RemoteMessage

class IncomingMessagesService : FirebaseMessagingService() {

    override fun onNewToken(token: String) {
        super.onNewToken(token)
        // TODO: send token to server
    }

    override fun onMessageReceived(message: RemoteMessage) {
        super.onMessageReceived(message)

        if (message.from != "/topics/widget")
            return

        val coapTopic = message.data["topic"] ?: return
        val coapPayload = message.data["payload"] ?: return

        val prefs = MyPrefs()
        val cachedData = prefs.cachedData
        val newData = App.json.decodeFromString<SensorBoxData>(coapPayload)
        val firstDevice = BuildConfig.DEVICE_NAMES[0]
        val secondDevice = BuildConfig.DEVICE_NAMES.getOrNull(1)

        if (secondDevice != null && coapTopic == "$secondDevice/data") {
            cachedData.roomTemperature = newData.temperature
            cachedData.roomHumidity = newData.humidity
            cachedData.voc = newData.voc
        } else if (coapTopic == "$firstDevice/data") {
            cachedData.updateFrom(newData)
        }

        prefs.cachedData = cachedData


        // update all appwidgets
        ListDataUtils.updateWidgets()

        Log.d(TAG, "onMessageReceived: ${message.data}")
    }

}