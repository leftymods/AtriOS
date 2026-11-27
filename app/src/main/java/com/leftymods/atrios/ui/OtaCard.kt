package com.leftymods.atrios.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.leftymods.atrios.AppReleaseInfo
import com.leftymods.atrios.model.OtaStatus
import com.leftymods.atrios.ui.theme.AppPalette
import com.leftymods.atrios.ui.theme.OledCard
import java.util.Locale

@Composable
fun UnifiedUpdatesCard(
    fwVersion: String,
    ota: OtaStatus,
    palette: AppPalette,
    curAppVer: String,
    appUpdateStatusMsg: String?,
    availableAppUpdate: AppReleaseInfo?,
    appUpdateProgress: Float?,
    checkingAppUpdate: Boolean,
    onCheckOta: () -> Unit,
    onRefreshOtaStatus: () -> Unit,
    onApplyOta: (Boolean) -> Unit,
    onRollbackOta: (Boolean) -> Unit,
    onRebootNow: () -> Unit,
    onCheckAppUpdate: () -> Unit,
    onDownloadAndInstallApp: (AppReleaseInfo) -> Unit
) {
    var showApplyDialog by remember { mutableStateOf(false) }
    var showRollbackDialog by remember { mutableStateOf(false) }
    var autoReboot by remember { mutableStateOf(true) }
    var showChangelog by remember { mutableStateOf(false) }
    var localCheckingOta by remember { mutableStateOf(false) }

    // Clear localCheckingOta as soon as speaker settles into a terminal state
    LaunchedEffect(ota.state, ota.updateAvailable, ota.lastCheckTs) {
        if (ota.state != "checking" && (ota.updateAvailable || ota.state in setOf("up_to_date", "available", "error", "installed", "rolled_back"))) {
            localCheckingOta = false
        }
    }

    // Bounded polling: max 6 polls (15s) for "checking", max 60 polls (150s) for download/install.
    val isCheckingOtaNow = localCheckingOta || ota.state == "checking"
    LaunchedEffect(ota.state, localCheckingOta) {
        if (isCheckingOtaNow && !ota.updateAvailable) {
            repeat(6) {
                kotlinx.coroutines.delay(2500L)
                if (ota.updateAvailable || (ota.state != "checking" && !localCheckingOta)) return@LaunchedEffect
                onRefreshOtaStatus()
            }
            localCheckingOta = false
        } else if (ota.isBusy && ota.state != "checking") {
            repeat(60) {
                kotlinx.coroutines.delay(2500L)
                if (!ota.isBusy) return@LaunchedEffect
                onRefreshOtaStatus()
            }
        }
    }

    val displayFw = ota.currentVersion.ifBlank { fwVersion.ifBlank { "—" } }
    val otaStatusText = when {
        !ota.error.isNullOrBlank() -> "Ошибка: ${ota.error}"
        ota.state == "checking" || localCheckingOta -> "Проверка обновлений..."
        ota.state == "downloading" -> "Загрузка прошивки: ${ota.progress}%"
        ota.state == "verifying" -> "Проверка целостности..."
        ota.state == "installing" -> "Установка прошивки: ${ota.progress}%"
        ota.state == "rolling_back" -> "Откат прошивки..."
        ota.rebootRequired -> "Обновление установлено. Требуется перезагрузка."
        ota.updateAvailable && !ota.latestVersion.isNullOrBlank() -> {
            val sz = if (ota.assetSize > 0L) " (${formatMb(ota.assetSize)})" else ""
            "Доступна новая версия v${ota.latestVersion}$sz"
        }
        ota.state == "up_to_date" || ota.state == "installed" -> "Установлена актуальная версия v$displayFw"
        ota.state == "rolled_back" -> "Выполнен откат до версии v$displayFw"
        ota.message.isNotBlank() -> ota.message
        else -> null
    }

    Surface(shape = RoundedCornerShape(22.dp), color = OledCard) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // === Section 1: Speaker Firmware (OTA) ===
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("Прошивка колонки (OTA)", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 16.sp)
                Text("v$displayFw", color = palette.accent, fontWeight = FontWeight.SemiBold, fontSize = 13.sp)
            }

            if (otaStatusText != null) {
                Text(
                    otaStatusText,
                    color = if (!ota.error.isNullOrBlank()) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                    fontSize = 13.sp
                )
            }

            if (ota.updateAvailable && !ota.changelog.isNullOrBlank()) {
                TextButton(
                    onClick = { showChangelog = !showChangelog },
                    contentPadding = PaddingValues(horizontal = 4.dp, vertical = 0.dp)
                ) {
                    Text(
                        if (showChangelog) "Скрыть список изменений" else "Что нового",
                        color = palette.accent,
                        fontSize = 12.sp
                    )
                }
                if (showChangelog) {
                    Text(
                        ota.changelog.trim(),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontSize = 12.sp,
                        lineHeight = 16.sp
                    )
                }
            }

            when {
                ota.isBusy && ota.state != "checking" -> {
                    LinearProgressIndicator(
                        progress = { (ota.progress / 100f).coerceIn(0f, 1f) },
                        modifier = Modifier.fillMaxWidth(),
                        color = palette.accent
                    )
                }
                ota.updateAvailable -> {
                    Button(
                        onClick = { showApplyDialog = true },
                        shape = RoundedCornerShape(99.dp)
                    ) {
                        Icon(Icons.Rounded.Download, contentDescription = null)
                        Spacer(Modifier.width(6.dp))
                        Text("Обновить до v${ota.latestVersion ?: "новой"}", fontWeight = FontWeight.Bold)
                    }
                }
                ota.rebootRequired -> {
                    Button(
                        onClick = onRebootNow,
                        shape = RoundedCornerShape(99.dp)
                    ) {
                        Icon(Icons.Rounded.RestartAlt, contentDescription = null)
                        Spacer(Modifier.width(6.dp))
                        Text("Перезагрузить колонку", fontWeight = FontWeight.Bold)
                    }
                }
                else -> {
                    FilledTonalButton(
                        enabled = !isCheckingOtaNow && !ota.isBusy,
                        onClick = {
                            if (!localCheckingOta && !ota.isBusy) {
                                localCheckingOta = true
                                onCheckOta()
                            }
                        },
                        shape = RoundedCornerShape(99.dp)
                    ) {
                        if (isCheckingOtaNow) {
                            CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                        } else {
                            Icon(Icons.Rounded.Refresh, contentDescription = null)
                        }
                        Spacer(Modifier.width(6.dp))
                        Text("Проверить обновления колонки")
                    }
                }
            }

            if (ota.rollbackAvailable && !ota.isBusy) {
                OutlinedButton(
                    onClick = { showRollbackDialog = true },
                    shape = RoundedCornerShape(99.dp)
                ) {
                    Icon(Icons.Rounded.History, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(6.dp))
                    Text("Откат на прошлую версию")
                }
            }

            HorizontalDivider(
                modifier = Modifier.padding(vertical = 4.dp),
                color = Color.White.copy(alpha = 0.08f)
            )

            // === Section 2: Android App ===
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("Приложение AtriOS Control", color = Color.White, fontWeight = FontWeight.Bold, fontSize = 16.sp)
                Text("v$curAppVer", color = palette.accent, fontWeight = FontWeight.SemiBold, fontSize = 13.sp)
            }

            if (appUpdateStatusMsg != null) {
                Text(appUpdateStatusMsg, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 13.sp)
            }

            if (availableAppUpdate != null) {
                if (appUpdateProgress != null) {
                    LinearProgressIndicator(
                        progress = { appUpdateProgress.coerceIn(0f, 1f) },
                        modifier = Modifier.fillMaxWidth(),
                        color = palette.accent
                    )
                    Text("Загрузка APK: ${(appUpdateProgress * 100).toInt()}%", style = MaterialTheme.typography.bodySmall)
                } else {
                    Button(
                        onClick = { onDownloadAndInstallApp(availableAppUpdate) },
                        shape = RoundedCornerShape(99.dp)
                    ) {
                        Icon(Icons.Rounded.Download, contentDescription = null)
                        Spacer(Modifier.width(6.dp))
                        Text("Обновить до v${availableAppUpdate.version}", fontWeight = FontWeight.Bold)
                    }
                }
            } else {
                FilledTonalButton(
                    enabled = !checkingAppUpdate,
                    onClick = onCheckAppUpdate,
                    shape = RoundedCornerShape(99.dp)
                ) {
                    if (checkingAppUpdate) {
                        CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                    } else {
                        Icon(Icons.Rounded.Refresh, contentDescription = null)
                    }
                    Spacer(Modifier.width(6.dp))
                    Text("Проверить обновления приложения")
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
                    onApplyOta(autoReboot)
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
                Text("Будут восстановлены резервные копии предыдущей версии прошивки. После отката колонка перезагрузится.")
            },
            confirmButton = {
                Button(onClick = {
                    showRollbackDialog = false
                    onRollbackOta(true)
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
