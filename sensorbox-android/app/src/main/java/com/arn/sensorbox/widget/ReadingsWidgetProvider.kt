package com.arn.sensorbox.widget

import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.Context
import android.util.Log
import android.widget.RemoteViews
import com.arn.sensorbox.App
import com.arn.sensorbox.R
import com.arn.sensorbox.widget.DataItems.Companion.toDataItems
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
    sensorBoxData: SensorBoxData,
) {
    val rv = RemoteViews(context.packageName, R.layout.appwidget_readings)

    val items = RemoteViews.RemoteCollectionItems.Builder().apply {
        setHasStableIds(true)
        setViewTypeCount(2)

        sensorBoxData.toDataItems().forEach { dataItems ->
            addItem(
                dataItems.suffix.hashCode().toLong(),
                ListDataUtils.createHeader(dataItems.timestamp)
            )
            dataItems.items.forEach { item ->
                addItem(
                    item.hashCode().toLong(),
                    ListDataUtils.createDataItem(dataItems.suffix, item)
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