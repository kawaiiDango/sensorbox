package com.arn.sensorbox

import com.arn.sensorbox.widget.SensorBoxData
import hu.autsoft.krate.SimpleKrate
import hu.autsoft.krate.booleanPref
import hu.autsoft.krate.default.withDefault
import hu.autsoft.krate.kotlinx.kotlinxPref

class MyPrefs : SimpleKrate(App.context) {
    var cachedData by kotlinxPref<SensorBoxData>("cached_data").withDefault(SensorBoxData(timestamp = 0L))
//    var subscribedToDigests by booleanPref("subscribed_to_digests").withDefault(false)
    var fcmRegistered by booleanPref("fcm_registered").withDefault(false)
}