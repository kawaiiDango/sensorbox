package com.arn.sensorbox

import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
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
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.arn.sensorbox.ui.theme.AppTheme
import com.google.firebase.messaging.FirebaseMessaging
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        enableEdgeToEdge()

        setContent {
            AppTheme {
                // A surface container using the 'background' color from the theme
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    AppContent()
                }
            }
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
}

@Composable
private fun AppContent() {
    Column(
        modifier = Modifier.wrapContentSize(),
    ) {
        val subscribedTopics = App.prefs.data.map { it.subscribedTopics }
            .collectAsState(emptySet())

        val scope = rememberCoroutineScope()

        Text(
            text = stringResource(R.string.subscribe_to_topics),
            modifier = Modifier.padding(8.dp)
        )

        arrayOf("widget", "digests", "alerts").forEach { topic ->
            LabelledCheckbox(
                name = topic,
                checked = subscribedTopics.value.contains(topic)
            ) {
                subscribeToTopic(scope, topic, it)
            }
        }
    }
}

private fun subscribeToTopic(scope: CoroutineScope, topic: String, subscribe: Boolean) {
    FirebaseMessaging.getInstance()
        .let {
            if (subscribe)
                it.subscribeToTopic(topic)
            else
                it.unsubscribeFromTopic(topic)
        }
        .addOnCompleteListener { task ->
            if (!task.isSuccessful) {
                Toast.makeText(App.context, task.exception!!.message.toString(), Toast.LENGTH_SHORT)
                    .show()
            } else {
                scope.launch {
                    if (subscribe)
                        App.prefs.updateData { it.copy(subscribedTopics = it.subscribedTopics + topic) }
                    else
                        App.prefs.updateData { it.copy(subscribedTopics = it.subscribedTopics - topic) }
                }
            }
        }
}


@Composable
private fun LabelledCheckbox(name: String, checked: Boolean, onCheckedChanged: (Boolean) -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .height(56.dp)
            .toggleable(
                value = checked,
                onValueChange = {
                    onCheckedChanged(it)
                },
                role = Role.Checkbox
            )
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Checkbox(
            checked = checked,
            onCheckedChange = null // null recommended for accessibility with screenreaders
        )
        Text(
            text = name,
            style = MaterialTheme.typography.bodyLarge,
            modifier = Modifier.padding(start = 16.dp)
        )
    }
}
