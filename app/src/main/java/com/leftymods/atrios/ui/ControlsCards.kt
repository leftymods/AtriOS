package com.leftymods.atrios.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.VolumeOff
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.leftymods.atrios.model.AiStatus
import com.leftymods.atrios.model.AlarmItem
import com.leftymods.atrios.model.DeviceCapabilities
import com.leftymods.atrios.model.MusicQuality
import com.leftymods.atrios.model.SpeakerStatus
import com.leftymods.atrios.model.TimerItem
import com.leftymods.atrios.model.VoicePreference
import com.leftymods.atrios.model.WeatherOption
import com.leftymods.atrios.model.WeatherSettings
import kotlinx.coroutines.delay
import java.util.TimeZone

@Composable
fun TelemetryCard(status: SpeakerStatus) {
    Card(
        shape = RoundedCornerShape(24.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer)
    ) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    buildString {
                        val cleanOs = status.os.replace(Regex("\\s*\\(v[^)]+\\)"), "").trim()
                        append(cleanOs.ifBlank { status.os })
                        if (status.fwVersion.isNotBlank()) append(" v${status.fwVersion}")
                    },
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
                if (status.codename.isNotBlank()) {
                    Text(
                        status.codename,
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                }
            }
            if (status.kernel.isNotBlank()) {
                Text(
                    "Ядро: ${status.kernel}${if (status.arch.isNotBlank()) " (${status.arch})" else ""}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("CPU: ${status.cpuTemp} °C", fontWeight = FontWeight.Medium)
                Text("ОЗУ: ${status.memUsedMb}/${status.memTotalMb} МБ")
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                val wifiText = if (status.wifiConnected) {
                    "Wi-Fi: ${status.wifiSsid.ifBlank { "OK" }} (${status.wifiIp})"
                } else {
                    "Wi-Fi: не подключён"
                }
                Text(wifiText, style = MaterialTheme.typography.bodySmall)
                if (status.capabilities.hasLuxSensor && status.lux != null) {
                    Text("Свет: ${status.lux} лк", style = MaterialTheme.typography.bodySmall)
                }
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(
                    "Аптайм: ${formatDuration(status.uptimeSec)}",
                    style = MaterialTheme.typography.bodySmall
                )
                if (status.micMuted) {
                    Text(
                        "Микрофон выкл.",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.error
                    )
                }
            }
        }
    }
}

@Composable
fun AudioCard(
    volume: Float,
    mute: Boolean,
    speakersDesc: String,
    onVolumeChange: (Float) -> Unit,
    onVolumeCommit: (Int) -> Unit,
    onToggleMute: () -> Unit,
    onAudioTest: () -> Unit,
    showAudioTest: Boolean = false
) {
    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Icon(
                        if (mute || volume.toInt() == 0) Icons.AutoMirrored.Rounded.VolumeOff else Icons.AutoMirrored.Rounded.VolumeUp,
                        contentDescription = null
                    )
                    Text("Аудио и громкость", style = MaterialTheme.typography.titleMedium)
                }
                Text("${volume.toInt()}%", fontWeight = FontWeight.SemiBold)
            }
            if (speakersDesc.isNotBlank()) {
                Text(
                    speakersDesc,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Slider(
                value = volume,
                onValueChange = onVolumeChange,
                onValueChangeFinished = { onVolumeCommit(volume.toInt()) },
                valueRange = 0f..100f
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilledTonalButton(onClick = onToggleMute) {
                    Text(if (mute) "Вкл. звук" else "Без звука")
                }
                if (showAudioTest) {
                    FilledTonalButton(onClick = onAudioTest) {
                        Text("Тест звука")
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun LedAndScreenCard(
    capabilities: DeviceCapabilities,
    currentScreenMode: String,
    currentRingMode: String,
    onSetScreen: (String, String?, String?) -> Unit,
    onSetRing: (String) -> Unit
) {
    var tickerText by remember { mutableStateOf("") }
    var eyesState by remember { mutableStateOf("happy") }

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Icon(Icons.Rounded.LightMode, contentDescription = null)
                Text("LED-экран и кольцо", style = MaterialTheme.typography.titleMedium)
            }

            if (capabilities.hasLedMatrix) {
                Text("Режим матрицы", style = MaterialTheme.typography.labelLarge)
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    val modes = listOf(
                        "clock" to "Часы",
                        "kisser" to "Kisser",
                        "eyes" to "Глаза",
                        "off" to "Выкл"
                    )
                    modes.forEach { (mode, label) ->
                        FilterChip(
                            selected = currentScreenMode == mode,
                            onClick = {
                                if (mode == "eyes") {
                                    onSetScreen("eyes", eyesState, null)
                                } else {
                                    onSetScreen(mode, null, null)
                                }
                            },
                            label = { Text(label) }
                        )
                    }
                }

                if (currentScreenMode == "eyes") {
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        listOf(
                            "happy" to "Весёлые",
                            "neutral" to "Спокойные",
                            "sleepy" to "Сонные",
                            "love" to "Любовь"
                        ).forEach { (st, lbl) ->
                            AssistChip(
                                onClick = {
                                    eyesState = st
                                    onSetScreen("eyes", st, null)
                                },
                                label = { Text(lbl) }
                            )
                        }
                    }
                }

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedTextField(
                        value = tickerText,
                        onValueChange = { tickerText = it },
                        label = { Text("Бегущая строка") },
                        modifier = Modifier.weight(1f),
                        singleLine = true
                    )
                    Button(
                        onClick = {
                            if (tickerText.isNotBlank()) {
                                onSetScreen("ticker", null, tickerText.trim())
                            }
                        }
                    ) { Text("OK") }
                }
            }

            if (capabilities.hasRgbRing) {
                Text("Верхнее RGB-кольцо", style = MaterialTheme.typography.labelLarge)
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    listOf(
                        "rainbow" to "Радуга",
                        "volume" to "Громкость",
                        "amber" to "Янтарный",
                        "blue" to "Синий",
                        "green" to "Зелёный",
                        "red" to "Красный",
                        "off" to "Выкл"
                    ).forEach { (mode, label) ->
                        FilterChip(
                            selected = currentRingMode == mode,
                            onClick = { onSetRing(mode) },
                            label = { Text(label) }
                        )
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun TimersAndAlarmsCard(
    timers: List<TimerItem>,
    alarms: List<AlarmItem>,
    onRefresh: () -> Unit,
    onAddTimer: (Int) -> Unit,
    onCancelTimer: (Int?) -> Unit,
    onAddAlarm: (Int, Int) -> Unit,
    onCancelAlarm: (Int?) -> Unit,
    showHeader: Boolean = true
) {
    var customMinutes by remember { mutableStateOf("5") }
    var alarmHour by remember { mutableStateOf("07") }
    var alarmMinute by remember { mutableStateOf("30") }
    var tickElapsed by remember(timers) { mutableIntStateOf(0) }
    LaunchedEffect(timers) {
        if (timers.isNotEmpty()) {
            while (true) {
                delay(1000)
                tickElapsed += 1
            }
        }
    }

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            if (showHeader) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Icon(Icons.Rounded.Timer, contentDescription = null)
                        Text("Таймеры и будильники", style = MaterialTheme.typography.titleMedium)
                    }
                    IconButton(onClick = onRefresh) {
                        Icon(Icons.Rounded.Refresh, contentDescription = "Обновить")
                    }
                }
            }

            // Timers section
            Text("Таймеры", style = MaterialTheme.typography.labelLarge)
            if (timers.isEmpty()) {
                Text(
                    "Нет активных таймеров",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else {
                timers.forEach { t ->
                    Surface(
                        shape = RoundedCornerShape(14.dp),
                        color = MaterialTheme.colorScheme.surfaceContainerHigh
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column {
                                Text("Таймер #${t.id}", fontWeight = FontWeight.Medium)
                                val liveRem = (t.remainingSecs - tickElapsed).coerceAtLeast(0)
                                Text(
                                    "Осталось: ${formatDuration(liveRem)} из ${formatDuration(t.totalSecs)}",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            TextButton(onClick = { onCancelTimer(t.id) }) { Text("Отменить") }
                        }
                    }
                }
            }

            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                listOf(60 to "1 мин", 300 to "5 мин", 600 to "10 мин", 900 to "15 мин", 1800 to "30 мин")
                    .forEach { (secs, label) ->
                        AssistChip(onClick = { onAddTimer(secs) }, label = { Text("+$label") })
                    }
            }

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedTextField(
                    value = customMinutes,
                    onValueChange = { customMinutes = it.filter(Char::isDigit).take(4) },
                    label = { Text("Минуты") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.weight(1f),
                    singleLine = true
                )
                Button(onClick = {
                    val mins = customMinutes.toIntOrNull() ?: 0
                    if (mins in 1..1440) onAddTimer(mins * 60)
                }) { Text("Запустить") }
                if (timers.isNotEmpty()) {
                    TextButton(onClick = { onCancelTimer(null) }) { Text("Сброс всех") }
                }
            }

            HorizontalDivider()

            // Alarms section
            Text("Будильники", style = MaterialTheme.typography.labelLarge)
            if (alarms.isEmpty()) {
                Text(
                    "Нет установленных будильников",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else {
                alarms.forEach { a ->
                    Surface(
                        shape = RoundedCornerShape(14.dp),
                        color = MaterialTheme.colorScheme.surfaceContainerHigh
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(10.dp)
                            ) {
                                Icon(Icons.Rounded.Alarm, contentDescription = null)
                                Text(a.formattedTime, style = MaterialTheme.typography.titleMedium)
                                Text(
                                    if (a.enabled) "Вкл" else "Выкл",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            TextButton(onClick = { onCancelAlarm(a.id) }) { Text("Удалить") }
                        }
                    }
                }
            }

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedTextField(
                    value = alarmHour,
                    onValueChange = { alarmHour = it.filter(Char::isDigit).take(2) },
                    label = { Text("Час") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.weight(1f),
                    singleLine = true
                )
                OutlinedTextField(
                    value = alarmMinute,
                    onValueChange = { alarmMinute = it.filter(Char::isDigit).take(2) },
                    label = { Text("Мин") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.weight(1f),
                    singleLine = true
                )
                Button(onClick = {
                    val h = (alarmHour.toIntOrNull() ?: 7).coerceIn(0, 23)
                    val m = (alarmMinute.toIntOrNull() ?: 0).coerceIn(0, 59)
                    onAddAlarm(h, m)
                }) { Text("Добавить") }
            }
            if (alarms.isNotEmpty()) {
                TextButton(onClick = { onCancelAlarm(null) }) { Text("Удалить все будильники") }
            }
        }
    }
}

@Composable
fun RoomCard(room: String, onSave: (String) -> Unit) {
    var name by remember(room) { mutableStateOf(room) }
    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it.take(50) },
                label = { Text("Комната") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true
            )
            TextButton(onClick = { onSave(name.trim()) }, enabled = name.trim() != room) {
                Text("Сохранить комнату")
            }
        }
    }
}

@Composable
fun WeatherCard(
    settings: WeatherSettings?,
    forecast: String,
    onSave: (WeatherSettings) -> Unit,
    onForecast: () -> Unit
) {
    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            if (settings == null) {
                Text("Обновите прошивку колонки, чтобы настроить погоду.")
                return@Column
            }
            var city by remember(settings.city) { mutableStateOf(settings.city) }
            var options by remember(settings.options) { mutableStateOf(settings.options) }
            val draft = WeatherSettings(city.trim(), options)

            OutlinedTextField(
                value = city,
                onValueChange = { city = it.take(80) },
                label = { Text("Город") },
                supportingText = { Text("Например, Москва, Россия") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true
            )
            Text("Дополнительно озвучивать", style = MaterialTheme.typography.titleSmall)
            WeatherOption.entries.forEach { option ->
                VoicePreferenceRow(option.label, option.subtitle, option in options) { checked ->
                    options = if (checked) options + option else options - option
                }
            }
            Button(onClick = { onSave(draft) }, enabled = draft != settings) {
                Text("Сохранить")
            }
            OutlinedButton(onClick = onForecast, enabled = settings.city.isNotBlank() && draft == settings) {
                Text("Показать погоду сейчас")
            }
            if (draft == settings && forecast.isNotBlank()) {
                Text(forecast, style = MaterialTheme.typography.bodyMedium)
            }
            Text("Данные Open-Meteo.", style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun MusicQualityCard(quality: MusicQuality?, onChange: (MusicQuality) -> Unit) {
    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            if (quality == null) {
                Text("Для этой настройки обновите прошивку колонки.", style = MaterialTheme.typography.bodySmall)
            } else {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    MusicQuality.entries.forEach { option ->
                        FilterChip(
                            selected = quality == option,
                            onClick = { if (quality != option) onChange(option) },
                            label = { Text(option.label) }
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun VoicePreferencesCard(
    preferences: Map<VoicePreference, Boolean>?,
    quickCommands: Boolean,
    onChange: (Map<VoicePreference, Boolean>) -> Unit
) {
    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            if (preferences == null) {
                Text(
                    "Обновите прошивку колонки, чтобы менять эти настройки.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                return@Column
            }

            if (!quickCommands) {
                val dialogOptions = listOf(
                    Triple(VoicePreference.ACTIVATION_CHIME, "Звук активации", "Сигнал после слова активации"),
                    Triple(VoicePreference.CONTINUOUS_DIALOG, "Продолжать диалог", "Слушать следующую фразу после ответа"),
                    Triple(VoicePreference.QUIET_REPLY, "Тихий ответ", "Приглушает голос Атри, когда вы говорите тихо"),
                    Triple(VoicePreference.ADAPTIVE_REPLY_VOLUME, "Адаптивная громкость", "Учитывает шум в комнате при озвучивании ответа")
                )
                dialogOptions.forEach { (key, title, description) ->
                    preferences[key]?.let { checked ->
                        VoicePreferenceRow(title, description, checked) { onChange(mapOf(key to it)) }
                    }
                }
                return@Column
            }

            VoicePreferenceRow("Воспроизведение и пауза", "Стоп, хватит, пауза, продолжи, играй",
                preferences[VoicePreference.QUICK_MEDIA] == true) {
                onChange(mapOf(VoicePreference.QUICK_MEDIA to it))
            }
            VoicePreferenceRow("Переключение треков", "Дальше, вперёд, назад, следующий, предыдущий",
                preferences[VoicePreference.QUICK_TRACKS] == true) {
                onChange(mapOf(VoicePreference.QUICK_TRACKS to it))
            }
            VoicePreferenceRow("Оценки треков", "Лайк, лайкни, нравится, дизлайк",
                preferences[VoicePreference.QUICK_FAVORITES] == true) {
                onChange(mapOf(VoicePreference.QUICK_FAVORITES to it))
            }
            VoicePreferenceRow("Громкость", "Тише, потише, громче, погромче", preferences[VoicePreference.QUICK_VOLUME] == true) {
                onChange(mapOf(VoicePreference.QUICK_VOLUME to it))
            }
            VoicePreferenceRow("Bluetooth", "Включи или выключи Bluetooth",
                preferences[VoicePreference.QUICK_BLUETOOTH] == true) {
                onChange(mapOf(VoicePreference.QUICK_BLUETOOTH to it))
            }
            val availableCommands = listOf(
                VoicePreference.QUICK_MEDIA, VoicePreference.QUICK_TRACKS,
                VoicePreference.QUICK_FAVORITES, VoicePreference.QUICK_VOLUME,
                VoicePreference.QUICK_BLUETOOTH
            ).filter(preferences::containsKey)
            val allOn = availableCommands.all { preferences[it] == true }
            TextButton(enabled = availableCommands.isNotEmpty(), onClick = { onChange(availableCommands.associateWith { !allOn }) }) {
                Text(if (allOn) "Выключить все быстрые команды" else "Включить все быстрые команды")
            }
            Text(
                "Команда «Стоп» работает всегда.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun VoicePreferenceRow(
    title: String,
    subtitle: String,
    checked: Boolean,
    onChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth().toggleable(
            value = checked,
            role = Role.Switch,
            onValueChange = onChange
        ),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            if (subtitle.isNotBlank()) {
                Text(
                    subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
        Switch(checked = checked, onCheckedChange = null)
    }
}

@Composable
fun MiniBrainAndVadCard(
    sileroVad: com.leftymods.atrios.model.SileroVadStatus?,
    aiMemory: com.leftymods.atrios.model.AiMemoryStatus?,
    onToggleSileroVad: (Boolean) -> Unit,
    onClearMemory: () -> Unit
) {
    var confirmClear by remember { mutableStateOf(false) }

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text("Мини-Мозг и Silero VAD v5", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)

            if (sileroVad != null) {
                val vadSubtitle = when {
                    !sileroVad.modelPresent -> "Модель silero_vad.onnx не найдена (RMS фоллбэк)"
                    sileroVad.active -> {
                        val ms = if (sileroVad.avgHopUs > 0L) String.format(java.util.Locale.US, " · %.1f мс/хоп", sileroVad.avgHopUs / 1000.0) else ""
                        "Активен (нейро-детектор речи$ms)"
                    }
                    else -> "Выключен (используется RMS VAD)"
                }
                VoicePreferenceRow(
                    title = "Silero VAD v5",
                    subtitle = vadSubtitle,
                    checked = sileroVad.enabled,
                    onChange = onToggleSileroVad
                )
            }

            if (aiMemory != null) {
                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f))
                val copies = when {
                    aiMemory.primaryOk && aiMemory.backupOk -> "2 зашифрованные копии OK"
                    aiMemory.primaryOk || aiMemory.backupOk -> "1 копия (автовосстановление)"
                    else -> "База создастся при первой записи"
                }
                val syncTag = if (aiMemory.serverSync) " · leftymods.tech" else ""
                Text(
                    "Векторная память ИИ: ${aiMemory.count} / ${aiMemory.maxEntries}",
                    style = MaterialTheme.typography.bodyLarge
                )
                Text(
                    "$copies$syncTag (читает только ИИ)",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                OutlinedButton(
                    onClick = { confirmClear = true },
                    enabled = aiMemory.count > 0
                ) {
                    Text("Очистить память ИИ")
                }
            }
        }
    }

    if (confirmClear) {
        AlertDialog(
            onDismissRequest = { confirmClear = false },
            title = { Text("Очистить память ИИ?") },
            text = { Text("Все сохранённые факты и заметки в обеих зашифрованных копиях векторной базы будут удалены.") },
            confirmButton = {
                Button(onClick = {
                    confirmClear = false
                    onClearMemory()
                }) { Text("Очистить") }
            },
            dismissButton = {
                TextButton(onClick = { confirmClear = false }) { Text("Отмена") }
            }
        )
    }
}

@Composable
fun AiAssistantCard(
    ai: AiStatus,
    onApplyAi: (enabled: Boolean?, server: String?, wakeWord: String?) -> Unit
) {
    var serverText by remember(ai.server) { mutableStateOf(ai.server) }
    var wakeText by remember(ai.wakeWord) { mutableStateOf(ai.wakeWord) }
    val serverChanged = serverText.trim() != ai.server
    val wakeChanged = wakeText.trim() != ai.wakeWord

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            VoicePreferenceRow("Ассистент", "", ai.enabled) { enabled ->
                onApplyAi(enabled, null, null)
            }

            if (ai.error.isNotBlank()) {
                Text(ai.error, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
            }

            OutlinedTextField(
                value = wakeText,
                onValueChange = { wakeText = it },
                label = { Text("Слово активации") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true
            )
            OutlinedTextField(
                value = serverText,
                onValueChange = { serverText = it },
                label = { Text("HTTPS-адрес AtriAI") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true
            )

            Button(
                onClick = {
                    onApplyAi(null, serverText.trim().takeIf { serverChanged }, wakeText.trim().takeIf { wakeChanged })
                },
                enabled = (serverChanged || wakeChanged) && (!serverChanged || serverText.trim().startsWith("https://"))
            ) {
                Text("Сохранить")
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun BluetoothCard(status: SpeakerStatus, onPair: () -> Unit, onHide: () -> Unit) {
    var requestedVisibility by remember { mutableStateOf<Boolean?>(null) }
    LaunchedEffect(requestedVisibility, status.btDiscoverable, status.btPairRemainingSec) {
        val requested = requestedVisibility ?: return@LaunchedEffect
        if (requested == status.btDiscoverable && (!requested || status.btPairRemainingSec > 0)) {
            requestedVisibility = null
        }
    }
    LaunchedEffect(requestedVisibility) {
        if (requestedVisibility != null) {
            delay(25_000)
            requestedVisibility = null
        }
    }
    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            val title = when (requestedVisibility) {
                true -> "Открываем поиск…"
                false -> "Скрываем колонку…"
                null -> status.bluetoothVisibility
            }
            Text(title, style = MaterialTheme.typography.titleMedium)
            if (status.btAvailable) {
                Text(
                    "Текущие подключения сохраняются при скрытии колонки из поиска.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        enabled = requestedVisibility == null && status.btPairRemainingSec == 0,
                        onClick = {
                            requestedVisibility = true
                            onPair()
                        }
                    ) {
                        Text("Сопряжение на 2 минуты")
                    }
                    if (status.btDiscoverable) {
                        OutlinedButton(
                            enabled = requestedVisibility == null,
                            onClick = {
                                requestedVisibility = false
                                onHide()
                            }
                        ) { Text("Скрыть") }
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun TimeAndTimezoneCard(
    localTime: String,
    timezone: String,
    onSyncNtp: () -> Unit,
    onSetTimezone: (String) -> Unit
) {
    val phoneTz = remember { TimeZone.getDefault().id }
    var customTz by remember(timezone) { mutableStateOf(timezone) }
    val presets = listOf(
        phoneTz,
        "Europe/Moscow",
        "Europe/Kaliningrad",
        "Asia/Yekaterinburg",
        "Asia/Novosibirsk",
        "Asia/Vladivostok",
        "UTC"
    ).distinct()

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Icon(Icons.Rounded.Schedule, contentDescription = null)
                Text("Время (NTP) и часовой пояс", style = MaterialTheme.typography.titleMedium)
            }
            if (localTime.isNotBlank()) {
                Text("Время на колонке: $localTime", fontWeight = FontWeight.Medium)
            }
            Text(
                "Часовой пояс: ${timezone.ifBlank { "UTC" }}",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Button(onClick = onSyncNtp) {
                    Icon(Icons.Rounded.Sync, contentDescription = null)
                    Spacer(Modifier.width(6.dp))
                    Text("Синхронизировать NTP")
                }
                if (phoneTz != timezone) {
                    FilledTonalButton(onClick = { onSetTimezone(phoneTz) }) {
                        Text("Как на телефоне ($phoneTz)")
                    }
                }
            }
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                presets.forEach { tz ->
                    FilterChip(
                        selected = timezone == tz,
                        onClick = { onSetTimezone(tz) },
                        label = { Text(tz) }
                    )
                }
            }
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                OutlinedTextField(
                    value = customTz,
                    onValueChange = { customTz = it },
                    label = { Text("IANA зона (напр. Europe/Moscow)") },
                    modifier = Modifier.weight(1f),
                    singleLine = true
                )
                Button(onClick = {
                    if (customTz.isNotBlank()) onSetTimezone(customTz.trim())
                }) { Text("Задать") }
            }
        }
    }
}

@Composable
fun WifiCard(
    wifiConnected: Boolean,
    wifiSsid: String,
    wifiIp: String,
    canChangeNetwork: Boolean,
    onConnectWifi: (String, String) -> Unit
) {
    var ssid by remember(wifiSsid) { mutableStateOf(wifiSsid) }
    var pass by remember { mutableStateOf("") }

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Text(
                if (wifiConnected) "Подключено: $wifiSsid ($wifiIp)" else "Не подключено",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            if (!canChangeNetwork) {
                Text("Подключитесь по Bluetooth для смены сети.", style = MaterialTheme.typography.bodySmall)
                return@Column
            }
            OutlinedTextField(
                value = ssid,
                onValueChange = { ssid = it },
                label = { Text("SSID") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )
            OutlinedTextField(
                value = pass,
                onValueChange = { pass = it },
                label = { Text("Пароль") },
                visualTransformation = PasswordVisualTransformation(),
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )
            Button(
                onClick = { onConnectWifi(ssid.trim(), pass) },
                enabled = ssid.trim().toByteArray(Charsets.UTF_8).size in 1..32 &&
                    (pass.isEmpty() || pass.toByteArray(Charsets.UTF_8).size in 8..63)
            ) { Text("Подключить Wi-Fi") }
        }
    }
}

private fun formatDuration(totalSecs: Int): String {
    val s = totalSecs.coerceAtLeast(0)
    val h = s / 3600
    val m = (s % 3600) / 60
    val sec = s % 60
    return when {
        h > 0 -> "${h}ч ${m}м"
        m > 0 -> "${m}м ${sec}с"
        else -> "${sec}с"
    }
}
