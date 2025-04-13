package com.arn.sensorbox

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper

class BleScanManager(
    btManager: BluetoothManager,
    private val onMissingPermissions: () -> Unit,
    private val scanPeriod: Long,
    private val onScanResultAction: (ScanResult?) -> Unit,
    private val onFinishedAction: () -> Unit,
    private val onScanFailedAction: (Int) -> Unit,
) {
    private val btAdapter = btManager.adapter
    private val bleScanner = btAdapter.bluetoothLeScanner

    private val settings = ScanSettings.Builder()
        .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
        .setReportDelay(0L)
        .setMatchMode(ScanSettings.MATCH_MODE_AGGRESSIVE)
        .setNumOfMatches(ScanSettings.MATCH_NUM_ONE_ADVERTISEMENT)
        .setLegacy(true)
        .build()

    private val callback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult?) {
            super.onScanResult(callbackType, result)
            onScanResultAction(result)
        }

//        override fun onBatchScanResults(results: List<ScanResult?>) {
//            super.onBatchScanResults(results)
//            results.forEach { onScanResultAction(it) }
//        }

        override fun onScanFailed(errorCode: Int) {
            super.onScanFailed(errorCode)
            scanning = false
            onScanFailedAction(errorCode)
            onFinishedAction()
        }

        fun onScanFinished() {
            scanning = false
            onFinishedAction()
        }
    }

    private val handler = Handler(Looper.getMainLooper())

    /** True when the manager is performing the scan */
    var scanning = false
        private set

    private fun createScanFilters(): List<ScanFilter> {
        return BuildConfig.DEVICE_NAMES.map { deviceName ->
            val companyId = (DIS_COMPANY_ID_PREFIX shl 8) or deviceName[0].code

            ScanFilter.Builder()
                .setManufacturerData(
                    companyId,
                    byteArrayOf(0x00),
                    byteArrayOf(0x00),
                )
                .build()
        }
    }

    @SuppressLint("MissingPermission")
    fun scanBleDevices() {
        // checks if the required permissions are granted
        if (!hasPermissions() || !btAdapter.isEnabled || bleScanner == null) {
            onMissingPermissions()
            onFinishedAction()
            return
        }

        fun stopScan() {
            bleScanner.stopScan(callback)
            callback.onScanFinished()
        }

        // scans for bluetooth LE devices
        if (!scanning) {
            // stops scanning after scanPeriod millis
            handler.postDelayed({ stopScan() }, scanPeriod)

            // starts scanning
            scanning = true

            bleScanner.startScan(createScanFilters(), settings, callback)
        }
    }

    companion object {
        const val DIS_COMPANY_ID_PREFIX = 0xF0

        private val blePermissions =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    arrayOf(
                        Manifest.permission.BLUETOOTH_SCAN,
                        Manifest.permission.POST_NOTIFICATIONS,
                    )
                } else
                    arrayOf(Manifest.permission.BLUETOOTH_SCAN)
            } else {
                arrayOf(
                    Manifest.permission.ACCESS_COARSE_LOCATION,
                    Manifest.permission.ACCESS_FINE_LOCATION,
                    Manifest.permission.BLUETOOTH_ADMIN,
                )
            }

        fun hasPermissions(): Boolean {
            return blePermissions.all { permission ->
                App.context.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED
            }
        }

        fun requestPermissions(activity: Activity) {
            activity.requestPermissions(
                blePermissions,
                2
            )
        }
    }
}
