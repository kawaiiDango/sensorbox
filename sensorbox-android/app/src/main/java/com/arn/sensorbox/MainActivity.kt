package com.arn.sensorbox

import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MediumTopAppBar
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.arn.sensorbox.ui.theme.AppTheme
import com.google.firebase.messaging.FirebaseMessaging
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlin.math.roundToInt

class MainActivity : ComponentActivity() {

    private val snackbarFlow = MutableSharedFlow<String?>()

    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        enableEdgeToEdge()

        setContent {
            AppTheme {
                val snackbarHostState = remember { SnackbarHostState() }
                val snackbarText by snackbarFlow.collectAsState(initial = null)

                // Observe the global snackbar flow
                LaunchedEffect(snackbarText) {
                    snackbarHostState.showSnackbar(snackbarText ?: return@LaunchedEffect)
                }

                Scaffold(
                    topBar = {
                        MediumTopAppBar(
                            title = { Text(stringResource(R.string.app_name)) },
                        )
                    },
//                    floatingActionButtonPosition = FabPosition.End,
//                    floatingActionButton = {
//                        ExtendedFloatingActionButton(onClick = ::startScan) {
//                            Text(
//                                stringResource(R.string.ble_scan)
//                            )
//                        }
//                    },
                    snackbarHost = { SnackbarHost(snackbarHostState) },
                    content = { innerPadding ->
                        AppContent(modifier = Modifier.padding(innerPadding))
                    }
                )
            }
        }

        // Request BLE scanning and notification permissions
        if (!BleScanManager.hasPermissions())
            BleScanManager.requestPermissions(this)
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AppContent(
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .padding(16.dp),
    ) {
        val subscribedTopics by App.prefs.data.map { it.subscribedTopics }
            .collectAsState(emptySet())

        val scanDurationSecs by App.prefs.data.map { it.scanDurationSecs }
            .collectAsState(initial = 60)
        var sliderPosition by remember(scanDurationSecs) { mutableFloatStateOf(scanDurationSecs.toFloat()) }
        val interactionSource = remember { MutableInteractionSource() }

        val scope = rememberCoroutineScope()

        Text(
            text = stringResource(R.string.subscribe_to_topics),
        )

        arrayOf("widget", "digests", "alerts").forEach { topic ->
            LabelledCheckbox(
                name = topic,
                checked = topic in subscribedTopics
            ) {
                subscribeToTopic(scope, topic, it)
            }
        }

        // slider for scan duration

        Text(stringResource(R.string.scan_duration) + ": ${sliderPosition.toInt()}s")

        Slider(
            value = sliderPosition,
            onValueChange = { sliderPosition = it },
            valueRange = 30f..180f,
            steps = (180 - 30) / 10 - 1,
            interactionSource = interactionSource,
            onValueChangeFinished = {
                scope.launch {
                    App.prefs.updateData { it.copy(scanDurationSecs = sliderPosition.roundToInt()) }
                }
            }
        )
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
