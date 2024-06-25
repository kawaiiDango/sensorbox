package com.arn.sensorbox.widget

data class DataItem(
    val name: String,
    val value: Float,
    val unit: String,
    val emoji: String,
)

data class DataItems(
    val suffix: String,
    val timestamp: Long,
    val items: List<DataItem>,
) {
    companion object {

        fun SensorBoxData.toDataItems() = listOf(
            DataItems(
                suffix = "",
                timestamp = timestampFirst,
                items = listOf(
                    DataItem(::temperature.name, temperature, "°C", "🌡️"),
                    DataItem(
                        ::temperature.name,
                        ReadingsUtils.heatIndex(temperature, humidity),
                        "°C",
                        "🌡️f"
                    ),
                    DataItem(::humidity.name, humidity, "%", "💧"),
                    DataItem(
                        ::pressure.name,
                        ReadingsUtils.seaPressure(pressure, temperature),
                        "hPa",
                        "⛅s"
                    ),
                    DataItem(::luminosity.name, luminosity, "lux", "☀"),
                    DataItem(::soundDbA.name, soundDbA, "dB", "📢"),
                    DataItem(::pm25.name, pm25, "μg/m³", "🌫️₂.₅"),
                    DataItem(::pm10.name, pm10, "μg/m³", "🌫️₁₀"),
                    DataItem(::co2.name, co2, "ppm", "💨"),
                    DataItem(::voltageAvg.name, voltageAvg, "V", "🔋"),

                    )
            ),
            DataItems(
                suffix = "r",
                timestamp = timestampSecond,
                items = listOf(
                    DataItem(::roomTemperature.name, roomTemperature, "°C", "🌡️"),
                    DataItem(
                        ::roomTemperature.name,
                        ReadingsUtils.heatIndex(roomTemperature, roomHumidity),
                        "°C",
                        "🌡️f"
                    ),
                    DataItem(::roomHumidity.name, roomHumidity, "%", "💧"),
                    DataItem(::roomVoltageAvg.name, roomVoltageAvg, "V", "🔋"),
                )
            )
        )
    }
}