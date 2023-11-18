package com.arn.sensorbox.widget

import kotlinx.serialization.Serializable

@Serializable
data class SensorBoxData(
    var temperature: Float = 0f,
    var humidity: Float = 0f,
    var roomTemperature: Float = 0f,
    var roomHumidity: Float = 0f,
    var pressure: Float = 0f,
    var luminosity: Float = 0f,
    var soundDbA: Float = 0f,
    var pm25: Float = 0f,
    var pm10: Float = 0f,
    var voltageAvg: Float = 0f,
    var voc: Float = 0f,

    var timestamp: Long
) {
    fun updateFrom(other: SensorBoxData): SensorBoxData {
        if (other.temperature != 0f) temperature = other.temperature
        if (other.humidity != 0f) humidity = other.humidity
        if (other.pressure != 0f) pressure = other.pressure
        if (other.luminosity != 0f) luminosity = other.luminosity
        if (other.soundDbA != 0f) soundDbA = other.soundDbA
        if (other.pm25 != 0f) pm25 = other.pm25
        if (other.pm10 != 0f) pm10 = other.pm10
        if (other.voltageAvg != 0f) voltageAvg = other.voltageAvg
        if (other.voc != 0f) voc = other.voc
        timestamp = other.timestamp
        return this
    }
}