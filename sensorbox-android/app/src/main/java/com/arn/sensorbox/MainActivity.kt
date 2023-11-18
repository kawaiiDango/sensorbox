package com.arn.sensorbox

import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.Checkbox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.arn.sensorbox.ui.theme.SensorboxTheme
import com.google.firebase.ktx.Firebase
import com.google.firebase.messaging.ktx.messaging
import kotlinx.coroutines.launch

const val TAG = "MainActivity"

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            SensorboxTheme {
                // A surface container using the 'background' color from the theme
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    Column(
                        modifier = Modifier.wrapContentSize(),
                    ) {
                        Text(text = stringResource(R.string.subscribe_to_topics), modifier = Modifier.padding(8.dp))
                        LabelledCheckbox(name = "widget")
                        LabelledCheckbox(name = "digests")
                        LabelledCheckbox(name = "alerts")
                    }
                }
            }
        }

        lifecycleScope.launch {
            if (!App.prefs.fcmRegistered)
                registerDevice()
        }

        // Request notification permissions
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(
                    this,
                    android.Manifest.permission.POST_NOTIFICATIONS
                ) == PackageManager.PERMISSION_GRANTED
            ) {
                // Do nothing
            } else {
                requestPermissions(arrayOf(android.Manifest.permission.POST_NOTIFICATIONS), 1)
            }
        }

    }

    private fun registerDevice() {
        // register device with firebase cloud messaging
        Firebase.messaging.token
            .addOnCompleteListener { task ->
                if (!task.isSuccessful) {
                    Log.e(TAG, task.exception!!.message.toString())
                }
                Log.i("FCM TOKEN", task.result)

                // set clipboard with token
//                val clipboard = getSystemService(ClipboardManager::class.java)
//                val clip = ClipData.newPlainText("FCM Token", task.result)
//                clipboard.setPrimaryClip(clip)
                App.prefs.fcmRegistered = true
                Toast.makeText(this, "Registered!", Toast.LENGTH_SHORT).show()
            }
    }
}

@Composable
fun LabelledCheckbox(name: String) {
    val wasChecked = App.prefs.sharedPreferences.getBoolean("subscribed_to_$name", false)

    val (checkedState, onStateChange) = remember { mutableStateOf(wasChecked) }
    Row(
        Modifier
            .fillMaxWidth()
            .height(56.dp)
            .toggleable(
                value = checkedState,
                onValueChange = {
                    onStateChange(!checkedState)

                    App.prefs.sharedPreferences
                        .edit()
                        .putBoolean("subscribed_to_$name", it)
                        .apply()
                    if (it) {
                        Firebase.messaging.subscribeToTopic(name)
                    } else {
                        Firebase.messaging.unsubscribeFromTopic(name)
                    }
                },
                role = Role.Checkbox
            )
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Checkbox(
            checked = checkedState,
            onCheckedChange = null // null recommended for accessibility with screenreaders
        )
        Text(
            text = name,
            style = MaterialTheme.typography.bodyLarge,
            modifier = Modifier.padding(start = 16.dp)
        )
    }
}

@Composable
fun Greeting(name: String, modifier: Modifier = Modifier) {
    Text(
        text = "Hello $name!",
        modifier = modifier
    )
}

@Preview(showBackground = true)
@Composable
fun GreetingPreview() {
    SensorboxTheme {
        Greeting("Android")
    }

}