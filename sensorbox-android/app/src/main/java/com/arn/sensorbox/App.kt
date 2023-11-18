package com.arn.sensorbox

import android.annotation.SuppressLint
import android.app.Application
import android.content.Context
import com.google.firebase.FirebaseApp
import kotlinx.serialization.json.Json

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

        val prefs by lazy { MyPrefs() }
    }
}