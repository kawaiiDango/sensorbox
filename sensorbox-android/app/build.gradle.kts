import java.io.IOException
import java.util.Properties

plugins {
    alias(libs.plugins.com.android.application)
    alias(libs.plugins.org.jetbrains.kotlin.android)
    alias(libs.plugins.kotlin.serialization)
    alias(libs.plugins.google.services)
}

android {
    namespace = "com.arn.sensorbox"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.arn.sensorbox"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
        setProperty("archivesBaseName", rootProject.name)

        val properties = Properties()
        try {
            properties.load(rootProject.file("local.properties").inputStream())
            val altitudeM = properties.getProperty("altitude_m")!!
            val deviceNames = properties.getProperty("device_names")!!
            buildConfigField("Float", "ALTITUDE_M", "${altitudeM}f")
            buildConfigField(
                "String[]",
                "DEVICE_NAMES",
                "{" + deviceNames.split(',').joinToString(separator = ",") { "\"$it\"" } + "}"
            )
        } catch (e: IOException) {
            e.printStackTrace()
        }

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
//        vectorDrawables {
//            useSupportLibrary = true
//        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    kotlinOptions {
        jvmTarget = "1.8"
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
    composeOptions {
        kotlinCompilerExtensionVersion = "1.4.5"
    }
    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }
}

dependencies {

    implementation(libs.core.ktx)
    implementation(libs.lifecycle.runtime.ktx)
    implementation(libs.activity.compose)
    implementation(platform(libs.compose.bom))
    implementation(libs.ui)
    implementation(libs.ui.graphics)
    implementation(libs.ui.tooling.preview)
    implementation(libs.material3)
    implementation(platform(libs.firebase.bom))
    implementation(libs.firebase.messaging.ktx)
    implementation(libs.krate)
    implementation(libs.krate.kotlinx)
    implementation(libs.harmony)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.androidx.core.remoteviews)
    implementation(libs.material)
    implementation(libs.androidx.datastore.preferences)

    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.test.ext.junit)
    androidTestImplementation(libs.espresso.core)
    androidTestImplementation(platform(libs.compose.bom))
    androidTestImplementation(libs.ui.test.junit4)
    debugImplementation(libs.ui.tooling)
    debugImplementation(libs.ui.test.manifest)
}