package com.arn.sensorbox

import android.annotation.SuppressLint
import android.app.Application
import android.content.Context
import androidx.datastore.core.MultiProcessDataStoreFactory
import com.google.firebase.FirebaseApp
import kotlinx.serialization.json.Json
import java.io.File

class App : Application() {
    override fun onCreate() {
        super.onCreate()
        context = applicationContext
        FirebaseApp.initializeApp(applicationContext)
    }

    companion object {
        @SuppressLint("StaticFieldLeak")
        lateinit var context: Context
        val json by lazy {
            Json {
                ignoreUnknownKeys = true
                allowSpecialFloatingPointValues = true
            }
        }

        val prefs by lazy {
            // widget runs in a separate process
            MultiProcessDataStoreFactory.create(
                serializer = MyPrefsSerializer,
                produceFile = {
                    File(context.dataDir, "${MyPrefs::class.simpleName}.json")
                }
            )
        }
    }
}