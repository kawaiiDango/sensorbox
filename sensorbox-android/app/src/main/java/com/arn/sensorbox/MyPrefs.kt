package com.arn.sensorbox

import androidx.datastore.core.Serializer
import com.arn.sensorbox.widget.SensorBoxData
import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.decodeFromStream
import kotlinx.serialization.json.encodeToStream
import java.io.InputStream
import java.io.OutputStream

@Serializable
data class MyPrefs(
    val cachedData: Map<String, SensorBoxData> = emptyMap(),
    val subscribedTopics: Set<String> = emptySet(),
)

object MyPrefsSerializer : Serializer<MyPrefs> {
    override val defaultValue = MyPrefs()

    override suspend fun readFrom(input: InputStream) =
        try {
            App.json.decodeFromStream<MyPrefs>(input)
        } catch (exception: SerializationException) {
            defaultValue
        }

    override suspend fun writeTo(t: MyPrefs, output: OutputStream) =
        App.json.encodeToStream(t, output)
}
