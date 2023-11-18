package com.arn.sensorbox.widget

import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.Context
import android.widget.RemoteViews
import androidx.core.widget.RemoteViewsCompat
import com.arn.sensorbox.MyPrefs
import com.arn.sensorbox.R


class ReadingsWidgetProvider : AppWidgetProvider() {
    override fun onUpdate(
        context: Context,
        appWidgetManager: AppWidgetManager,
        appWidgetIds: IntArray
    ) {
        // There may be multiple widgets active, so update all of them
        appWidgetIds.forEach { appWidgetId ->
            updateAppWidget(context, appWidgetManager, appWidgetId)
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
) {
    val prefs = MyPrefs()
    val cachedData = prefs.cachedData
    val rv = RemoteViews(context.packageName, R.layout.appwidget_readings)

    val items = RemoteViewsCompat.RemoteCollectionItems.Builder().apply {
        setHasStableIds(true)
        setViewTypeCount(2)
        addItem(0, ListDataUtils.createHeader(cachedData.timestamp))

        DataItem.createFrom(cachedData)
            .forEachIndexed { i, item ->
                addItem(item.hashCode().toLong(), ListDataUtils.createDataItem(item))
            }
    }.build()
    RemoteViewsCompat.setRemoteAdapter(context, rv, appWidgetId, R.id.data_list, items)

    // The empty view is displayed when the collection has no items. It should be a sibling
    // of the collection view.
    rv.setEmptyView(R.id.data_list, R.id.no_data)

    // Instruct the widget manager to update the widget
    appWidgetManager.updateAppWidget(appWidgetId, rv)
}