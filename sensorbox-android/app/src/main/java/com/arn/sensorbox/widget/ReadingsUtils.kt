package com.arn.sensorbox.widget


import android.util.Log
import com.arn.sensorbox.App
import com.arn.sensorbox.BuildConfig
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.runBlocking
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.roundToInt
import kotlin.math.sqrt

object ReadingsUtils {
    private data class Breakpoint(
        val cLow: Float,
        val cHigh: Float,
        val iLow: Float,
        val iHigh: Float,
    )

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


    // us aqi breakpoints
    private val pm25UsAqiBreakpoints = listOf(
        Breakpoint(0f, 12f, 0f, 50f),
        Breakpoint(12.1f, 35.4f, 51f, 100f),
        Breakpoint(35.5f, 55.4f, 101f, 150f),
        Breakpoint(55.5f, 150.4f, 151f, 200f),
        Breakpoint(150.5f, 250.4f, 201f, 300f),
        Breakpoint(250.5f, 350.4f, 301f, 400f),
        Breakpoint(350.5f, 500.4f, 401f, 500f)
    )

    private val pm10UsAqiBreakpoints = listOf(
        Breakpoint(0f, 54f, 0f, 50f),
        Breakpoint(55f, 154f, 51f, 100f),
        Breakpoint(155f, 254f, 101f, 150f),
        Breakpoint(255f, 354f, 151f, 200f),
        Breakpoint(355f, 424f, 201f, 300f),
        Breakpoint(425f, 504f, 301f, 400f),
        Breakpoint(505f, 604f, 401f, 500f)
    )

    fun usAqi(pm25: Float, pm10: Float): Float {
        val pm25Aqi = calculateUsAqi((pm25 * 10).roundToInt() / 10f, pm25UsAqiBreakpoints)
        val pm10Aqi = calculateUsAqi(pm10.roundToInt().toFloat(), pm10UsAqiBreakpoints)
        return maxOf(pm25Aqi, pm10Aqi)
    }

    fun inAqi(pm25: Float, pm10: Float): Float {
        val pm25Aqi = calculateInAqiPm25(pm25)
        val pm10Aqi = calculateInAqiPm10(pm10)
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

    fun inAqiColorEmoji(aqi: Float): String {
        return when {
            aqi <= 50 -> "🟢"
            aqi <= 100 -> "🍏"
            aqi <= 200 -> "🟡"
            aqi <= 300 -> "🟠"
            aqi <= 400 -> "🔴"
            else -> "🟤"
        }
    }


    // indian aqi
    /*
    For PM2.5, the excel formula is as follows:

=IF(ISTEXT(C10),0,IF(C10<=30,C10*50/30,IF(AND(C10>30,C10<=60),50+(C10-30)*50/30,IF(AND(C10>60,C10<=90),100+(C10-60)*100/30,IF(AND(C10>90,C10<=120),200+(C10-90)*(100/30),IF(AND(C10>120,C10<=250),300+(C10-120)*(100/130),IF(C10>250,400+(C10-250)*(100/130))))))))

     */
    private fun calculateInAqiPm25(conc: Float): Float {
        return when {
            conc <= 30 -> conc * 50 / 30
            conc <= 60 -> 50 + (conc - 30) * 50 / 30
            conc <= 90 -> 100 + (conc - 60) * 100 / 30
            conc <= 120 -> 200 + (conc - 90) * 100 / 30
            conc <= 250 -> 300 + (conc - 120) * 100 / 130
            else -> 400 + (conc - 250) * 100 / 130
        }
    }

    /*
    For PM10, the excel formula is as follows:
    =IF(ISTEXT(C8),0,IF(C8<=50,C8,IF(AND(C8>50,C8<=100),C8,IF(AND(C8>100,C8<=250),100+(C8-100)*100/150,IF(AND(C8>250,C8<=350),200+(C8-250),IF(AND(C8>350,C8<=430),300+(C8-350)*(100/80),IF(C8>430,400+(C8-430)*(100/80))))))))

     */
    private fun calculateInAqiPm10(conc: Float): Float {
        return when {
            conc <= 50 -> conc
            conc <= 100 -> conc
            conc <= 250 -> 100 + (conc - 100) * 100 / 150
            conc <= 350 -> 200 + (conc - 250)
            conc <= 430 -> 300 + (conc - 350) * 100 / 80
            else -> 400 + (conc - 430) * 100 / 80
        }
    }


    private fun calculateUsAqi(concentration: Float, breakpoints: List<Breakpoint>): Float {
        for (breakpoint in breakpoints) {
            if (concentration <= breakpoint.cHigh) {
                return ((breakpoint.iHigh - breakpoint.iLow) / (breakpoint.cHigh - breakpoint.cLow) * (concentration - breakpoint.cLow) + breakpoint.iLow)
            }
        }
        return -1f // Invalid concentration
    }

    fun updateWidgetData(
        deviceName: String,
        newData: SensorBoxData,
        forceUpdate: Boolean = false,
    ) {
        fun withFallback(
            newValue: Float,
            oldValue: Float,
        ): Float {
            return if (newValue != 0f) {
                newValue
            } else {
                oldValue
            }
        }

        val cachedData = runBlocking { App.prefs.data.map { it.cachedData }.first() }.toMutableMap()

        val firstDevice = BuildConfig.DEVICE_NAMES[0]
        val secondDevice = BuildConfig.DEVICE_NAMES.getOrNull(1)

        val savedTimestamp = cachedData[deviceName]?.timestamp ?: 0L

        if (newData.timestamp < savedTimestamp && !forceUpdate) {
            Log.d(
                this::class.simpleName,
                "onMessageReceived: ignoring old data with timestamp ${newData.timestamp} < $savedTimestamp"
            )
            return
        }

        if (secondDevice != null && deviceName == secondDevice) {
            val oldData = cachedData[deviceName]

            cachedData[deviceName] = oldData?.copy(
                temperature = withFallback(newData.temperature, oldData.temperature),
                humidity = withFallback(newData.humidity, oldData.humidity),
                voltageAvg = withFallback(newData.voltageAvg, oldData.voltageAvg),
                timestamp = newData.timestamp,
                isFromBle = newData.isFromBle,
            ) ?: newData
        } else if (deviceName == firstDevice) {
            val oldData = cachedData[deviceName]

            cachedData[deviceName] = oldData?.copy(
                temperature = withFallback(newData.temperature, oldData.temperature),
                humidity = withFallback(newData.humidity, oldData.humidity),
                pressure = withFallback(newData.pressure, oldData.pressure),
                luminosity = withFallback(newData.luminosity, oldData.luminosity),
                visible = withFallback(newData.visible, oldData.visible),
                ir = withFallback(newData.ir, oldData.ir),
                soundDbA = withFallback(newData.soundDbA, oldData.soundDbA),
                soundDbZ = withFallback(newData.soundDbZ, oldData.soundDbZ),
                pm25 = withFallback(newData.pm25, oldData.pm25),
                pm10 = withFallback(newData.pm10, oldData.pm10),
                co2 = withFallback(newData.co2, oldData.co2),
                voltageAvg = withFallback(newData.voltageAvg, oldData.voltageAvg),
                voltageAvgS = withFallback(newData.voltageAvgS, oldData.voltageAvgS),
                timestamp = newData.timestamp,
                isFromBle = newData.isFromBle,
            ) ?: newData
        }

        runBlocking {
            App.prefs.updateData { it.copy(cachedData = cachedData) }
        }
        // update all appwidgets
        ListDataUtils.updateWidgets()
    }

    private fun ByteArray.parseFloat(startIdx: Int): Float {
        if (this.size < startIdx + 4) {
            return 0f
        }

        val intBits = this[startIdx + 3].toInt() shl 24 or
                (this[startIdx + 2].toInt() and 0xFF shl 16) or
                (this[startIdx + 1].toInt() and 0xFF shl 8) or
                (this[startIdx].toInt() and 0xFF)
        val floatVal = Float.fromBits(intBits)
        if (floatVal.isNaN()) {
            return 0f
        }

        return floatVal
    }


    private fun ByteArray.parseShortAsFloat(startIdx: Int, factor: Int): Float {
        if (this.size < startIdx + 2) {
            return 0f
        }

        val uIntVal = this[startIdx + 1].toUInt() shl 8 or
                (this[startIdx].toUInt() and 0xffu)

        return uIntVal.toFloat() / factor
    }

    private fun ByteArray.parseLong(startIdx: Int): Long {
        if (this.size < startIdx + 4) {
            return 0L
        }

        val longVal = this[startIdx + 3].toUInt() shl 24 or
                (this[startIdx + 2].toUInt() and 0xffu shl 16) or
                (this[startIdx + 1].toUInt() and 0xffu shl 8) or
                (this[startIdx].toUInt() and 0xffu)
        return longVal.toLong()
    }

    fun parseBleData(advData: ByteArray): SensorBoxData {
        var advOffset = 0

        fun pIncAdv(toAdd: Int): Int {
            val existingOffset = advOffset
            advOffset += toAdd
            return existingOffset
        }

        val readings = SensorBoxData(
            // parse advData
            timestamp = advData.parseLong(pIncAdv(4)),
            voltageAvg = advData.parseShortAsFloat(pIncAdv(2), 100),
            temperature = advData.parseShortAsFloat(pIncAdv(2), 100),
            humidity = advData.parseShortAsFloat(pIncAdv(2), 100),
            pressure = advData.parseShortAsFloat(pIncAdv(2), 10),
            luminosity = advData.parseShortAsFloat(pIncAdv(2), 1),
            soundDbA = advData.parseShortAsFloat(pIncAdv(2), 100),
            soundDbZ = advData.parseShortAsFloat(pIncAdv(2), 100),
            pm25 = advData.parseShortAsFloat(pIncAdv(2), 10),
            pm10 = advData.parseShortAsFloat(pIncAdv(2), 10),
            co2 = advData.parseShortAsFloat(pIncAdv(2), 1),
            isFromBle = true,
        )

        Log.d(this::class.simpleName, "parseBleData: $readings")

        return readings
    }
}