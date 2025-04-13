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

            if (pm25 != 0f && pm10 != 0f) {
                val usAqi = ReadingsUtils.usAqi(pm25, pm10)
                val inAqi = ReadingsUtils.inAqi(pm25, pm10)
                val usColorEmoji = ReadingsUtils.usAqiColorEmoji(usAqi)
                val inColorEmoji = ReadingsUtils.inAqiColorEmoji(inAqi)
                dataItems.add(DataItem(inAqi, "INaqi", inColorEmoji))
                dataItems.add(DataItem(usAqi, "USaqi", usColorEmoji))
            }

            if (voltageAvg != 0f) {
                dataItems.add(DataItem(voltageAvg, "V", "🔋"))
            }

            if (voltageAvgS != 0f) {
                dataItems.add(DataItem(voltageAvgS, "V", "🔋s"))
            }

            return dataItems
        }
    }
}