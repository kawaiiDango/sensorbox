package com.arn.sensorbox.widget


import com.arn.sensorbox.BuildConfig
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.sqrt

object ReadingsUtils {

    fun heatIndex(TC: Float, RH: Float): Float {
        val T = 1.8 * TC + 32
        var F = 0.5 * (T + 61.0 + (T - 68.0) * 1.2 + RH * 0.094)

        if (F >= 80) {
            F =
                -42.379 + 2.04901523 * T + 10.14333127 * RH - 0.22475541 * T * RH - 0.00683783 * T * T - 0.05481717 * RH * RH + 0.00122874 * T * T * RH + 0.00085282 * T * RH * RH - 0.00000199 * T * T * RH * RH
            if (RH < 13.0 && T > 80.0 && T < 112.0)
                F -= (13.0 - RH) / 4.0 * sqrt((17.0 - abs(T - 95.0)) / 17.0)
            else if (RH > 85.0 && T > 80.0 && T < 87.0)
                F += (RH - 85.0) / 10.0 * (87.0 - T) / 5.0
        }
        val C = (F - 32) * 5 / 9
        return C.toFloat()
    }

    fun seaPressure(p: Float, t: Float): Float {
        return p * (exp(9.80665f * 0.0289644f * BuildConfig.ALTITUDE_M / (8.31447f * (t + 273.15f))))
    }

    private data class Breakpoint(val cLow: Float, val cHigh: Float, val iLow: Float, val iHigh: Float)

    private val pm25Breakpoints = listOf(
        Breakpoint(0f, 12f, 0f, 50f),
        Breakpoint(12.1f, 35.4f, 51f, 100f),
        Breakpoint(35.5f, 55.4f, 101f, 150f),
        Breakpoint(55.5f, 150.4f, 151f, 200f),
        Breakpoint(150.5f, 250.4f, 201f, 300f),
        Breakpoint(250.5f, 350.4f, 301f, 400f),
        Breakpoint(350.5f, 500.4f, 401f, 500f)
    )

    private val pm10Breakpoints = listOf(
        Breakpoint(0f, 54f, 0f, 50f),
        Breakpoint(55f, 154f, 51f, 100f),
        Breakpoint(155f, 254f, 101f, 150f),
        Breakpoint(255f, 354f, 151f, 200f),
        Breakpoint(355f, 424f, 201f, 300f),
        Breakpoint(425f, 504f, 301f, 400f),
        Breakpoint(505f, 604f, 401f, 500f)
    )

    fun usAqi(pm25: Float, pm10: Float): Float {
        val pm25Aqi = calculateAqi(pm25, pm25Breakpoints)
        val pm10Aqi = calculateAqi(pm10, pm10Breakpoints)
        return maxOf(pm25Aqi, pm10Aqi)
    }

    fun usAqiColorEmoji(aqi: Float): String {
        return when {
            aqi <= 50 -> "🟢"
            aqi <= 100 -> "🟡"
            aqi <= 150 -> "🟠"
            aqi <= 200 -> "🔴"
            aqi <= 300 -> "🟣"
            else -> "🟤"
        }
    }

    private fun calculateAqi(concentration: Float, breakpoints: List<Breakpoint>): Float {
        for (breakpoint in breakpoints) {
            if (concentration <= breakpoint.cHigh) {
                return ((breakpoint.iHigh - breakpoint.iLow) / (breakpoint.cHigh - breakpoint.cLow) * (concentration - breakpoint.cLow) + breakpoint.iLow)
            }
        }
        return -1f // Invalid concentration
    }
}