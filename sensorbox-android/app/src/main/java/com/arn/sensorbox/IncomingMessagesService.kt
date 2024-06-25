package com.arn.sensorbox

import android.util.Log
import com.arn.sensorbox.widget.ListDataUtils
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
        val coapPayload = message.data["payload"] ?: return

        val cachedData = runBlocking { App.prefs.data.map { it.cachedData }.first() }

        val newData = App.json.decodeFromString<SensorBoxData>(coapPayload)
        val firstDevice = BuildConfig.DEVICE_NAMES[0]
        val secondDevice = BuildConfig.DEVICE_NAMES.getOrNull(1)

        val savedTimestamp = when {
            secondDevice != null && coapTopic == "$secondDevice/data" -> cachedData.timestampSecond
            else -> cachedData.timestampFirst
        }

        if (newData.timestampFirst <= savedTimestamp) {
            Log.d(this::class.simpleName, "onMessageReceived: ignoring old data")
            return
        }

        var newCachedData = cachedData

        if (secondDevice != null && coapTopic == "$secondDevice/data") {
            newCachedData = newCachedData.copy(
                roomTemperature = newData.temperature,
                roomHumidity = newData.humidity,
                roomVoltageAvg = newData.voltageAvg,
                timestampSecond = newData.timestampFirst
            )
        } else if (coapTopic == "$firstDevice/data") {

            if (newData.pm25 != 0f && newData.pm10 != 0f && newData.co2 != 0f)
                newCachedData = newCachedData.copy(
                    pm25 = newData.pm25,
                    pm10 = newData.pm10,
                    co2 = newData.co2
                )

            if (newData.temperature != 0f && newData.humidity != 0f)
                newCachedData = newCachedData.copy(
                    temperature = newData.temperature,
                    humidity = newData.humidity,
                    pressure = newData.pressure,
                    luminosity = newData.luminosity,
                    soundDbA = newData.soundDbA,
                    voltageAvg = newData.voltageAvg,
                    timestampFirst = newData.timestampFirst
                )
        }

        runBlocking {
            App.prefs.updateData { it.copy(cachedData = newCachedData) }
        }
        // update all appwidgets
        ListDataUtils.updateWidgets(newCachedData)
    }

}