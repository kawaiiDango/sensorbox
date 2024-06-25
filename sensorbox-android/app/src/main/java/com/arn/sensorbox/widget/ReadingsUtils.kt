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

}