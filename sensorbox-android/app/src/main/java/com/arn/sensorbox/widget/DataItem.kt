package com.arn.sensorbox.widget

data class DataItem(
    val value: Float,
    val unit: String,
    val emoji: String,
) {
    companion object {
        fun SensorBoxData.toDataItems(): List<DataItem> {
            val dataItems = mutableListOf<DataItem>()

            if (temperature != 0f) {
                dataItems.add(DataItem(temperature, "°C", "🌡️"))
                dataItems.add(DataItem(ReadingsUtils.heatIndex(temperature, humidity), "°C", "🌡️f"))
            }

            if (humidity != 0f) {
                dataItems.add(DataItem(humidity, "%", "💧"))
            }

            if (pressure != 0f) {
                dataItems.add(
                    DataItem(
                        ReadingsUtils.seaPressure(pressure, temperature),
                        "hPa",
                        "⛅s"
                    )
                )
            }

            if (luminosity != 0f) {
                dataItems.add(DataItem(luminosity, "lux", "☀"))
            }

            if (visible != 0f && ir != 0f) {
                dataItems.add(DataItem(ir * 100 / visible, "%", "⭕"))
            }

            if (soundDbA != 0f) {
                dataItems.add(DataItem(soundDbA, "dB", "📢a"))
            }

            if (soundDbZ != 0f) {
                dataItems.add(DataItem(soundDbZ, "dB", "📢z"))
            }

            if (pm25 != 0f) {
                dataItems.add(DataItem(pm25, "μg/m³", "🌫️₂.₅"))
            }

            if (pm10 != 0f) {
                dataItems.add(DataItem(pm10, "μg/m³", "🌫️₁₀"))
            }

            if (co2 != 0f) {
                dataItems.add(DataItem(co2, "ppm", "💨"))
            }

            if (voltageAvg != 0f) {
                dataItems.add(DataItem(voltageAvg, "V", "🔋"))
            }

            return dataItems
        }
    }
}