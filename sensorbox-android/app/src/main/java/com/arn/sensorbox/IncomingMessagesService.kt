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
        val deviceName = coapTopic.substringBefore("/")
        val coapPayload = message.data["payload"] ?: return

        val cachedData = runBlocking { App.prefs.data.map { it.cachedData }.first() }.toMutableMap()

        val newData = App.json.decodeFromString<SensorBoxData>(coapPayload)
        val firstDevice = BuildConfig.DEVICE_NAMES[0]
        val secondDevice = BuildConfig.DEVICE_NAMES.getOrNull(1)

        val savedTimestamp = cachedData[deviceName]?.timestamp ?: 0L

        if (newData.timestamp <= savedTimestamp) {
            Log.d(this::class.simpleName, "onMessageReceived: ignoring old data")
            return
        }

        if (secondDevice != null && deviceName == secondDevice) {
            cachedData[deviceName] = cachedData[deviceName]?.copy(
                temperature = newData.temperature,
                humidity = newData.humidity,
                voltageAvg = newData.voltageAvg,
                timestamp = newData.timestamp
            ) ?: newData
        } else if (deviceName == firstDevice) {
                cachedData[deviceName] = cachedData[deviceName]?.copy(
                    temperature = newData.temperature,
                    humidity = newData.humidity,
                    pressure = newData.pressure,
                    luminosity = newData.luminosity,
                    visible = newData.visible,
                    ir = newData.ir,
                    soundDbA = newData.soundDbA,
                    soundDbZ = newData.soundDbZ,
                    voltageAvg = newData.voltageAvg,
                    timestamp = newData.timestamp,
                )?.let {
                    if (newData.pm25 != 0f && newData.pm10 != 0f && newData.co2 != 0f)
                        it.copy(
                            pm25 = newData.pm25,
                            pm10 = newData.pm10,
                            co2 = newData.co2
                        )
                    else it
                } ?: newData
        }

        runBlocking {
            App.prefs.updateData { it.copy(cachedData = cachedData) }
        }
        // update all appwidgets
        ListDataUtils.updateWidgets()
    }

}