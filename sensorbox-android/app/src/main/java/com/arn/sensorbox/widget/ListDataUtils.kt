package com.arn.sensorbox.widget


import android.appwidget.AppWidgetManager
import android.content.ComponentName
import android.content.Intent
import android.net.Uri
import android.widget.RemoteViews
import com.arn.sensorbox.App
import com.arn.sensorbox.R
import java.text.DateFormat
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.sqrt
import com.arn.sensorbox.BuildConfig

object ListDataUtils {

    fun createHeader(timestamp: Long): RemoteViews {
        val headerView = RemoteViews(App.context.packageName, R.layout.appwidget_list_header)
        val formatter = DateFormat.getTimeInstance()
        headerView.setTextViewText(R.id.appwidget_time, formatter.format(timestamp * 1000))
        return headerView
    }

    fun createDataItem(dataItem: DataItem): RemoteViews {
        return RemoteViews(App.context.packageName, R.layout.appwidget_list_item).apply {
            setTextViewText(R.id.appwidget_emoji, dataItem.emoji)
            setTextViewText(
                R.id.appwidget_text,
                "${"%.2f".format(dataItem.value)} ${dataItem.unit}"
            )
        }
    }

    fun updateWidgets() {
        val appWidgetManager = App.context.getSystemService(AppWidgetManager::class.java)
        val appWidgetIds = appWidgetManager.getAppWidgetIds(
            ComponentName(App.context, ReadingsWidgetProvider::class.java)
        )
        val i = Intent(App.context, ReadingsWidgetProvider::class.java).apply {
            action = AppWidgetManager.ACTION_APPWIDGET_UPDATE
            putExtra(AppWidgetManager.EXTRA_APPWIDGET_IDS, appWidgetIds)
            data = Uri.parse(toUri(Intent.URI_INTENT_SCHEME))
        }
        App.context.sendBroadcast(i)
    }

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