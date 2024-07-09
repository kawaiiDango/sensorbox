package com.arn.sensorbox.widget

import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.Context
import android.widget.RemoteViews
import com.arn.sensorbox.App
import com.arn.sensorbox.BuildConfig
import com.arn.sensorbox.R
import com.arn.sensorbox.widget.DataItem.Companion.toDataItems
import com.google.android.gms.common.internal.Objects
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.runBlocking

// glance uses workmanager and is subject to its background limits, so do not migrate

class ReadingsWidgetProvider : AppWidgetProvider() {
    override fun onUpdate(
        context: Context,
        appWidgetManager: AppWidgetManager,
        appWidgetIds: IntArray,
    ) {
        // There may be multiple widgets active, so update all of them
        val sensorBoxData = runBlocking { App.prefs.data.map { it.cachedData }.first() }

        appWidgetIds.forEach { appWidgetId ->
            updateAppWidget(context, appWidgetManager, appWidgetId, sensorBoxData)
        }
    }

    override fun onEnabled(context: Context) {
        // Enter relevant functionality for when the first widget is created
    }
}

internal fun updateAppWidget(
    context: Context,
    appWidgetManager: AppWidgetManager,
    appWidgetId: Int,
    cachedData: Map<String, SensorBoxData>,
) {
    val rv = RemoteViews(context.packageName, R.layout.appwidget_readings)

    val items = RemoteViews.RemoteCollectionItems.Builder().apply {
        setHasStableIds(true)
        setViewTypeCount(2)

        cachedData.forEach { (deviceName, sensorBoxData) ->
            addItem(
                deviceName.hashCode().toLong(),
                ListDataUtils.createHeader(sensorBoxData.timestamp)
            )

            val suffix =
                if (deviceName == BuildConfig.DEVICE_NAMES[0]) "" else deviceName.first().toString()

            sensorBoxData.toDataItems().forEach { dataItem ->
                addItem(
                    Objects.hashCode(deviceName, dataItem.emoji, dataItem.unit).toLong(),
                    ListDataUtils.createDataItem(suffix, dataItem)
                )
            }

        }
    }.build()

    // The empty view is displayed when the collection has no items. It should be a sibling
    // of the collection view.
    rv.setEmptyView(R.id.data_list, R.id.no_data)
    rv.setRemoteAdapter(R.id.data_list, items)
    // Instruct the widget manager to update the widget
    appWidgetManager.updateAppWidget(appWidgetId, rv)
}