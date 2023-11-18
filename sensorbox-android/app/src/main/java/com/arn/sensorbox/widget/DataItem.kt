package com.arn.sensorbox.widget

data class DataItem(
    val name: String,
    val value: Float,
    val unit: String,
    val emoji: String,
) {

    companion object {
        fun createFrom(sensorBoxData: SensorBoxData): List<DataItem> {
            val dataItems = mutableListOf<DataItem>()

            dataItems +=  DataItem(sensorBoxData::temperature.name, sensorBoxData.temperature, "°C", "🌡️")
            dataItems +=  DataItem(sensorBoxData::temperature.name, ListDataUtils.heatIndex(sensorBoxData.temperature, sensorBoxData.humidity), "°C", "🌡️f")
            dataItems +=  DataItem(sensorBoxData::humidity.name, sensorBoxData.humidity, "%", "💧")
            dataItems +=  DataItem(sensorBoxData::pressure.name, ListDataUtils.seaPressure(sensorBoxData.pressure, sensorBoxData.temperature), "hPa", "⛅s")
            dataItems +=  DataItem(sensorBoxData::luminosity.name, sensorBoxData.luminosity, "lux",  "☀")
            dataItems +=  DataItem(sensorBoxData::soundDbA.name, sensorBoxData.soundDbA, "dB",  "📢")
            dataItems +=  DataItem(sensorBoxData::pm25.name, sensorBoxData.pm25, "μg/m³",  "💨₂.₅")
            dataItems +=  DataItem(sensorBoxData::pm10.name, sensorBoxData.pm10, "μg/m³",  "💨₁₀")
            dataItems +=  DataItem(sensorBoxData::roomTemperature.name, sensorBoxData.roomTemperature, "°C", "🌡️r")
            dataItems +=  DataItem(sensorBoxData::roomTemperature.name, ListDataUtils.heatIndex(sensorBoxData.roomTemperature, sensorBoxData.roomHumidity), "°C", "🌡️rf")
            dataItems +=  DataItem(sensorBoxData::roomHumidity.name, sensorBoxData.roomHumidity, "%", "💧r")
            dataItems +=  DataItem(sensorBoxData::voc.name, sensorBoxData.voc, "ppm",  "☣️r")
            dataItems +=  DataItem(sensorBoxData::voltageAvg.name, sensorBoxData.voltageAvg, "V",  "🔋")

            return dataItems
        }

    }
}