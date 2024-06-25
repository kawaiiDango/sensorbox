package com.arn.sensorbox.widget

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class SensorBoxData(
    val temperature: Float = 0f,
    val humidity: Float = 0f,
    val roomTemperature: Float = 0f,
    val roomHumidity: Float = 0f,
    val pressure: Float = 0f,
    val luminosity: Float = 0f,
    val soundDbA: Float = 0f,
    val pm25: Float = 0f,
    val pm10: Float = 0f,
    val co2: Float = 0f,
    val voltageAvg: Float = 0f,
    val roomVoltageAvg: Float = 0f,

    @SerialName("timestamp")
    val timestampFirst: Long = 0L,
    val timestampSecond: Long = 0L
)