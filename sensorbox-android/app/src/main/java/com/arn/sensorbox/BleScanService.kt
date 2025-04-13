package com.arn.sensorbox

import android.app.NotificationManager
import android.app.Service
import android.bluetooth.BluetoothManager
import android.content.Intent
import androidx.core.app.NotificationCompat
import androidx.core.util.size
import com.arn.sensorbox.widget.ReadingsUtils
import com.arn.sensorbox.widget.ReadingsUtils.updateWidgetData
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.runBlocking

class BleScanService : Service() {
    private val notificationManager by lazy { getSystemService(NotificationManager::class.java) }

    private val bleScanManager by lazy {
        BleScanManager(
            btManager = getSystemService(BluetoothManager::class.java),
            scanPeriod = runBlocking { App.prefs.data.map { it.scanDurationSecs * 1000L }.first() },
            onMissingPermissions = {
                postErrorNotification(getString(R.string.ble_missing_permissions))
            },
            onScanResultAction = { scanResult ->
                scanResult ?: return@BleScanManager

                if (scanResult.scanRecord?.manufacturerSpecificData?.size != 1)
                    return@BleScanManager

                val manufacturerId = scanResult.scanRecord!!.manufacturerSpecificData!!.keyAt(0)
                val initials = (manufacturerId and 0xff).toChar()
                val deviceAddress = scanResult.device.address

                val deviceName =
                    BuildConfig.DEVICE_NAMES.find { initials == it[0] } ?: return@BleScanManager

                val advData =
                    scanResult.scanRecord?.getManufacturerSpecificData(manufacturerId)
                        ?: return@BleScanManager

                val readings = ReadingsUtils.parseBleData(advData)

                updateWidgetData(
                    deviceName,
                    readings,
                    forceUpdate = true
                )

                updateNotification(
                    getString(
                        R.string.ble_scan_found,
                        "$deviceName | $deviceAddress"
                    )
                )
            },
            onFinishedAction = {
                stopSelf()
            },
            onScanFailedAction = { errorCode ->
                postErrorNotification(getString(R.string.ble_scan_failed, errorCode))
            }
        )
    }

    override fun onBind(intent: Intent?) = null


    private fun createFgNotification() {
        // Build the notification
        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.scanning))
            .setSmallIcon(R.drawable.ic_scanning)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()

        // Move service to foreground state with the notification
        startForeground(FG_NOTIFICATION_ID, notification)
    }

    private fun updateNotification(status: String) {
        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.scanning))
            .setContentText(status)
            .setSmallIcon(R.drawable.ic_scanning)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()

        // Update the notification
        notificationManager.notify(FG_NOTIFICATION_ID, notification)
    }

    private fun postErrorNotification(errorText: String) {
        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.scan_error))
            .setContentText(errorText)
            .setSmallIcon(R.drawable.ic_error)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()

        notificationManager.notify(ERR_NOTIFICATION_ID, notification)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        createFgNotification()

        if (bleScanManager.scanning)
            stopSelf()
        else
            bleScanManager.scanBleDevices()

        return START_NOT_STICKY
    }

    companion object {
        const val CHANNEL_ID = "ble_scan_channel"
        const val FG_NOTIFICATION_ID = 1
        const val ERR_NOTIFICATION_ID = 2
    }
}