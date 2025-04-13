package com.arn.sensorbox.widget

import kotlinx.serialization.Serializable

@Serializable
data class SensorBoxData(
    val temperature: Float = 0f,
    val humidity: Float = 0f,
    val pressure: Float = 0f,
    val luminosity: Float = 0f,
    val visible: Float = 0f,
    val ir: Float = 0f,
    val soundDbA: Float = 0f,
    val soundDbZ: Float = 0f,
    val pm25: Float = 0f,
    val pm10: Float = 0f,
    val co2: Float = 0f,
    val usAqi: Float = 0f,
    val voltageAvg: Float = 0f,
    val voltageAvgS: Float = 0f,
    val timestamp: Long = 0L,
    val isFromBle: Boolean = false,
)