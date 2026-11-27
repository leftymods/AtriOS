package com.leftymods.atrios.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.leftymods.atrios.model.OtaStatus
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun OtaUpdateCard(
    fwVersion: String,
    codename: String,
    ota: OtaStatus,
    onCheckUpdates: () -> Unit,
    onRefreshStatus: () -> Unit,
    onApplyUpdate: (Boolean) -> Unit,
    onRollback: (Boolean) -> Unit,
    onRebootNow: () -> Unit
) {
    var showApplyDialog by remember { mutableStateOf(false) }
    var showRollbackDialog by remember { mutableStateOf(false) }
    var autoReboot by remember { mutableStateOf(true) }
    var showChangelog by remember { mutableStateOf(false) }

    LaunchedEffect(ota.isBusy) {
        while (ota.isBusy) {
            kotlinx.coroutines.delay(2500L)
            onRefreshStatus()
        }
    }

    val displayCurrent = ota.currentVersion.ifBlank { fwVersion.ifBlank { "—" } }
    val stateLabel = when (ota.state) {
        "checking" -> "Проверка…"
        "up_to_date" -> "Актуально"
        "available" -> "Доступно обновление"
        "downloading" -> "Загрузка (${ota.progress}%)"
        "verifying" -> "Проверка целостности"
        "installing" -> "Установка (${ota.progress}%)"
        "installed" -> "Установлено"
        "rolling_back" -> "Откат прошивки…"
        "rolled_back" -> "Выполнен откат"
        "error" -> "Ошибка OTA"
        else -> "Ожидание"
    }

    val badgeContainer = when (ota.state) {
        "available" -> MaterialTheme.colorScheme.primaryContainer
        "error" -> MaterialTheme.colorScheme.errorContainer
        "up_to_date", "installed" -> MaterialTheme.colorScheme.secondaryContainer
        else -> MaterialTheme.colorScheme.surfaceVariant
    }
    val badgeContent = when (ota.state) {
        "available" -> MaterialTheme.colorScheme.onPrimaryContainer
        "error" -> MaterialTheme.colorScheme.onErrorContainer
        "up_to_date", "installed" -> MaterialTheme.colorScheme.onSecondaryContainer
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }

    ElevatedCard(shape = RoundedCornerShape(24.dp)) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Surface(
                    shape = RoundedCornerShape(14.dp),
                    color = MaterialTheme.colorScheme.primaryContainer
                ) {
                    Icon(
                        if (ota.updateAvailable) Icons.Rounded.SystemUpdateAlt else Icons.Rounded.SystemUpdate,
                        contentDescription = null,
                        modifier = Modifier.padding(10.dp),
                        tint = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                }
                Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(
                        "Обновление колонки",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1
                    )
                    Text(
                        if (codename.isNotBlank()) "v$displayCurrent · $codename" else "v$displayCurrent",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Surface(
                    shape = RoundedCornerShape(99.dp),
                    color = badgeContainer
                ) {
                    Text(
                        stateLabel,
                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
                        style = MaterialTheme.typography.labelMedium,
                        fontWeight = FontWeight.SemiBold,
                        color = badgeContent,
                        maxLines = 1,
                        softWrap = false
                    )
                }
            }

            if (ota.lastCheckTs > 0L && !ota.isBusy) {
                val formatted = remember(ota.lastCheckTs) {
                    SimpleDateFormat("dd.MM.yyyy HH:mm", Locale.getDefault())
                        .format(Date(ota.lastCheckTs * 1000L))
                }
                Text(
                    "Последняя проверка: $formatted",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            if (ota.isBusy) {
                Surface(
                    shape = RoundedCornerShape(16.dp),
                    color = MaterialTheme.colorScheme.surfaceVariant
                ) {
                    Column(
                        modifier = Modifier.fillMaxWidth().padding(14.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        if (ota.progress > 0) {
                            LinearProgressIndicator(
                                progress = { ota.progress / 100f },
                                modifier = Modifier.fillMaxWidth().height(6.dp)
                            )
                        } else {
                            LinearProgressIndicator(modifier = Modifier.fillMaxWidth().height(6.dp))
                        }
                        val sizeText = if (ota.assetSize > 0L) {
                            " · ${formatMb(ota.downloadedBytes)} / ${formatMb(ota.assetSize)}"
                        } else ""
                        Text(
                            "${ota.message.ifBlank { ota.stage }}$sizeText",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            } else if (ota.message.isNotBlank() && !ota.updateAvailable) {
                val localizedMsg = when {
                    ota.message.equals("AtriOS is up to date", ignoreCase = true) ||
                        ota.message.equals("Up to date", ignoreCase = true) -> "Установлена актуальная прошивка"
                    ota.message.equals("Update check started", ignoreCase = true) -> "Проверка обновлений…"
                    ota.message.equals("OTA update started", ignoreCase = true) -> "Запуск обновления прошивки…"
                    else -> ota.message
                }
                Text(
                    localizedMsg,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            if (!ota.error.isNullOrBlank()) {
                Surface(
                    shape = RoundedCornerShape(14.dp),
                    color = MaterialTheme.colorScheme.errorContainer
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Icon(
                            Icons.Rounded.ErrorOutline,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onErrorContainer
                        )
                        Text(
                            ota.error,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onErrorContainer
                        )
                    }
                }
            }

            if (ota.state == "rolled_back") {
                Surface(
                    shape = RoundedCornerShape(14.dp),
                    color = MaterialTheme.colorScheme.tertiaryContainer
                ) {
                    Text(
                        "Обновление было автоматически отменено и восстановлена предыдущая рабочая прошивка.",
                        modifier = Modifier.fillMaxWidth().padding(12.dp),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onTertiaryContainer
                    )
                }
            }

            if (ota.updateAvailable && !ota.latestVersion.isNullOrBlank()) {
                Surface(
                    shape = RoundedCornerShape(16.dp),
                    color = MaterialTheme.colorScheme.secondaryContainer
                ) {
                    Column(
                        modifier = Modifier.fillMaxWidth().padding(14.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(6.dp)
                            ) {
                                Text(
                                    "v$displayCurrent",
                                    style = MaterialTheme.typography.labelLarge,
                                    color = MaterialTheme.colorScheme.onSecondaryContainer.copy(alpha = 0.7f)
                                )
                                Icon(
                                    Icons.Rounded.ArrowForward,
                                    contentDescription = null,
                                    modifier = Modifier.size(16.dp),
                                    tint = MaterialTheme.colorScheme.onSecondaryContainer
                                )
                                Text(
                                    "v${ota.latestVersion}",
                                    style = MaterialTheme.typography.titleSmall,
                                    fontWeight = FontWeight.Bold,
                                    color = MaterialTheme.colorScheme.onSecondaryContainer
                                )
                            }
                            if (ota.assetSize > 0L) {
                                Surface(
                                    shape = RoundedCornerShape(99.dp),
                                    color = MaterialTheme.colorScheme.surface.copy(alpha = 0.45f)
                                ) {
                                    Text(
                                        formatMb(ota.assetSize),
                                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp),
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSecondaryContainer
                                    )
                                }
                            }
                        }
                        if (ota.changelog.isNotBlank()) {
                            TextButton(
                                onClick = { showChangelog = !showChangelog },
                                contentPadding = PaddingValues(0.dp)
                            ) {
                                Text(if (showChangelog) "Скрыть изменения" else "Что нового")
                            }
                            if (showChangelog) {
                                Text(
                                    ota.changelog,
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSecondaryContainer
                                )
                            }
                        }
                    }
                }
            }

            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                when {
                    ota.isBusy -> {
                        FilledTonalButton(
                            onClick = {},
                            enabled = false,
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(14.dp)
                        ) {
                            CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                            Spacer(Modifier.width(8.dp))
                            Text(stateLabel)
                        }
                    }
                    ota.updateAvailable -> {
                        Button(
                            onClick = { showApplyDialog = true },
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(14.dp)
                        ) {
                            Icon(Icons.Rounded.Download, contentDescription = null)
                            Spacer(Modifier.width(8.dp))
                            Text("Установить v${ota.latestVersion ?: ""}".trim(), fontWeight = FontWeight.Bold)
                        }
                    }
                    ota.rebootRequired -> {
                        Button(
                            onClick = onRebootNow,
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(14.dp)
                        ) {
                            Icon(Icons.Rounded.RestartAlt, contentDescription = null)
                            Spacer(Modifier.width(8.dp))
                            Text("Перезагрузить колонку", fontWeight = FontWeight.Bold)
                        }
                    }
                    else -> {
                        Button(
                            onClick = onCheckUpdates,
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(14.dp)
                        ) {
                            Icon(Icons.Rounded.Refresh, contentDescription = null)
                            Spacer(Modifier.width(8.dp))
                            Text("Проверить обновления", fontWeight = FontWeight.SemiBold)
                        }
                    }
                }

                if (ota.rollbackAvailable && !ota.isBusy) {
                    OutlinedButton(
                        onClick = { showRollbackDialog = true },
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(14.dp)
                    ) {
                        Icon(Icons.Rounded.History, contentDescription = null)
                        Spacer(Modifier.width(6.dp))
                        Text("Откат на прошлую версию")
                    }
                }
            }
        }
    }

    if (showApplyDialog) {
        AlertDialog(
            onDismissRequest = { showApplyDialog = false },
            title = { Text("Установить обновление ${ota.latestVersion ?: ""}?") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(
                        "Колонка загрузит образ, проверит контрольную сумму и создаст резервную копию текущей прошивки для автоматического отката при сбое."
                    )
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(checked = autoReboot, onCheckedChange = { autoReboot = it })
                        Text("Перезагрузить автоматически после установки")
                    }
                }
            },
            confirmButton = {
                Button(onClick = {
                    showApplyDialog = false
                    onApplyUpdate(autoReboot)
                }) { Text("Установить") }
            },
            dismissButton = {
                TextButton(onClick = { showApplyDialog = false }) { Text("Отмена") }
            }
        )
    }

    if (showRollbackDialog) {
        AlertDialog(
            onDismissRequest = { showRollbackDialog = false },
            title = { Text("Откатить прошивку?") },
            text = {
                Text("Будут восстановлены резервные копии предыдущей версии прошивки. После отката рекомендуется перезагрузить колонку.")
            },
            confirmButton = {
                Button(onClick = {
                    showRollbackDialog = false
                    onRollback(true)
                }) { Text("Откатить и перезагрузить") }
            },
            dismissButton = {
                TextButton(onClick = { showRollbackDialog = false }) { Text("Отмена") }
            }
        )
    }
}

private fun formatMb(bytes: Long): String {
    if (bytes <= 0L) return "0 МБ"
    val mb = bytes / (1024.0 * 1024.0)
    return String.format(Locale.US, "%.1f МБ", mb)
}
