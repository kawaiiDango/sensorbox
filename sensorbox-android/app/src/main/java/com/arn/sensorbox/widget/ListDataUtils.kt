package com.arn.sensorbox.widget


import android.appwidget.AppWidgetManager
import android.content.ComponentName
import android.widget.RemoteViews
import com.arn.sensorbox.App
import com.arn.sensorbox.R
import java.text.DateFormat

object ListDataUtils {

    fun createHeader(timestamp: Long): RemoteViews {
        val headerView = RemoteViews(App.context.packageName, R.layout.appwidget_list_header)
        val formatter = DateFormat.getTimeInstance()
        headerView.setTextViewText(R.id.appwidget_time, formatter.format(timestamp * 1000))
        return headerView
    }

    fun createDataItem(suffix: String, dataItem: DataItem): RemoteViews {
        return RemoteViews(App.context.packageName, R.layout.appwidget_list_item).apply {
            setTextViewText(R.id.appwidget_emoji, dataItem.emoji + suffix)
            setTextViewText(
                R.id.appwidget_text,
                "${"%.2f".format(dataItem.value)} ${dataItem.unit}"
            )
        }
    }

    fun updateWidgets(cachedData: Map<String, SensorBoxData>) {
        val appWidgetManager = AppWidgetManager.getInstance(App.context)
        val appWidgetIds = appWidgetManager.getAppWidgetIds(
            ComponentName(App.context, ReadingsWidgetProvider::class.java)
        )
        appWidgetIds.forEach { appWidgetId ->
            updateAppWidget(App.context, appWidgetManager, appWidgetId, cachedData)
        }
    }

}