package com.leftymods.atrios.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.leftymods.atrios.AtriAccountState
import com.leftymods.atrios.model.SpeakerStatus
import com.leftymods.atrios.model.YMusicOrb
import com.leftymods.atrios.ui.theme.AppPalette
import com.leftymods.atrios.ui.theme.OledBlack
import com.leftymods.atrios.ui.theme.OledCard
import com.leftymods.atrios.ui.theme.OledCardHigh
import java.util.Locale

private data class WavePreset(val label: String, val mode: String, val query: String?)

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun AtriHomeScreen(
    status: SpeakerStatus,
    speakerName: String,
    transportLabel: String,
    accountState: AtriAccountState,
    palette: AppPalette,
    volumePct: Int,
    progressFrac: Float = 0f,
    orbs: List<YMusicOrb> = emptyList(),
    onBlobTap: () -> Unit,
    onPrev: () -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit,
    onSeek: (Long) -> Unit = {},
    onPlayWavePreset: (mode: String, query: String?) -> Unit = { _, _ -> },
    onOpenVolume: () -> Unit,
    onOpenEq: () -> Unit,
    onOpenIntegrations: () -> Unit,
    onOpenTimers: () -> Unit
) {
    val ym = status.ymusic
    val isPlaying = ym.state == "playing"
    val isLoading = ym.state == "loading"
    val hasTrack = ym.title.isNotBlank()
    val accountDisplay = if (accountState.username.contains("@")) accountState.username else "@${accountState.username}"
    var showWaveSheet by remember { mutableStateOf(false) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(OledBlack)
            .statusBarsPadding()
            .padding(horizontal = 20.dp, vertical = 10.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        // Top status bar
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = speakerName.ifBlank { "AtriStation" },
                    color = Color.White,
                    fontSize = 22.sp,
                    fontWeight = FontWeight.Bold,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .clip(CircleShape)
                            .background(Color(0xFF4ADE80))
                    )
                    Text(
                        text = buildString {
                            append(transportLabel)
                            append(" · Подключена")
                            if (accountState.loggedIn) append(" · $accountDisplay")
                        },
                        color = Color(0xFFA0A0B0),
                        fontSize = 12.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            if (ym.loggedIn) {
                Surface(
                    shape = RoundedCornerShape(99.dp),
                    color = OledCard,
                    modifier = Modifier
                        .clip(RoundedCornerShape(99.dp))
                        .border(1.dp, palette.accent.copy(alpha = 0.35f), RoundedCornerShape(99.dp))
                        .clickable { showWaveSheet = true }
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 7.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(5.dp)
                    ) {
                        Icon(
                            Icons.Rounded.Tune,
                            contentDescription = "Настроить волну",
                            tint = palette.accent,
                            modifier = Modifier.size(15.dp)
                        )
                        Text(
                            "Волна",
                            color = Color.White,
                            fontSize = 12.sp,
                            fontWeight = FontWeight.SemiBold
                        )
                    }
                }
            }
        }

        // Hero GPU Blob — Living Atri Core & Audio Visualizer
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .heightIn(min = 150.dp)
                .clip(RoundedCornerShape(30.dp))
                .clickable { onBlobTap() },
            contentAlignment = Alignment.Center
        ) {
            SingleLiquidBlobShaderAura(
                isPlaying = isPlaying || isLoading,
                palette = palette,
                coverUrl = ym.coverUrl,
                audioBass = ym.audioBass,
                audioMid = ym.audioMid,
                audioHigh = ym.audioHigh,
                audioEnergy = ym.audioEnergy,
                audioBeat = ym.audioBeat,
                modifier = Modifier.fillMaxSize()
            )
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                if (isLoading) {
                    CircularProgressIndicator(
                        color = Color.White,
                        strokeWidth = 2.5.dp,
                        modifier = Modifier.size(34.dp)
                    )
                } else {
                    Icon(
                        imageVector = if (isPlaying) Icons.Rounded.GraphicEq else Icons.Rounded.Mic,
                        contentDescription = null,
                        tint = Color.White,
                        modifier = Modifier.size(36.dp)
                    )
                }
                Text(
                    text = when {
                        isLoading -> "Загрузка..."
                        isPlaying -> "Играет"
                        ym.loggedIn -> "Нажмите для Моей волны"
                        else -> "Атри готова"
                    },
                    color = Color.White,
                    fontSize = 15.sp,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    text = if (isPlaying) "Тап — пауза" else "Управление музыкой на колонке",
                    color = Color.White.copy(alpha = 0.72f),
                    fontSize = 11.sp
                )
            }
        }

        // Now Playing status card — non-clickable container, bounded buttons + interactive seekable progress bar
        if (hasTrack) {
            var draggingFrac by remember(ym.trackId) { mutableStateOf<Float?>(null) }
            val shownFrac = (draggingFrac ?: progressFrac).coerceIn(0f, 1f)
            val shownPosMs = if (ym.durationMs > 0L) (shownFrac * ym.durationMs).toLong() else 0L

            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(22.dp))
                    .background(OledCard)
                    .border(1.dp, Color.White.copy(alpha = 0.06f), RoundedCornerShape(22.dp))
                    .padding(horizontal = 12.dp, vertical = 10.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    AsyncCoverArt(
                        url = ym.coverUrl,
                        shape = RoundedCornerShape(12.dp),
                        modifier = Modifier.size(46.dp)
                    )
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = ym.title,
                            color = Color.White,
                            fontSize = 15.sp,
                            fontWeight = FontWeight.SemiBold,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        Text(
                            text = ym.artist.ifBlank { if (ym.loggedIn) "Яндекс Музыка" else "AtriStation" },
                            color = Color(0xFFA0A0B0),
                            fontSize = 12.sp,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(2.dp)
                    ) {
                        IconButton(
                            onClick = onPrev,
                            enabled = ym.canPrev,
                            modifier = Modifier
                                .size(38.dp)
                                .clip(CircleShape)
                        ) {
                            Icon(
                                Icons.Rounded.SkipPrevious,
                                contentDescription = "Назад",
                                tint = if (ym.canPrev) Color.White else Color.White.copy(alpha = 0.25f)
                            )
                        }
                        IconButton(
                            onClick = onPlayPause,
                            modifier = Modifier
                                .size(40.dp)
                                .clip(CircleShape)
                                .background(palette.accent)
                        ) {
                            Icon(
                                imageVector = if (isPlaying) Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                                contentDescription = "Пуск/Пауза",
                                tint = palette.onAccent
                            )
                        }
                        IconButton(
                            onClick = onNext,
                            enabled = ym.canNext && !isLoading,
                            modifier = Modifier
                                .size(38.dp)
                                .clip(CircleShape)
                        ) {
                            Icon(
                                Icons.Rounded.SkipNext,
                                contentDescription = "Вперёд",
                                tint = if (ym.canNext && !isLoading) Color.White else Color.White.copy(alpha = 0.25f)
                            )
                        }
                    }
                }

                if (ym.durationMs > 0L) {
                    Slider(
                        value = shownFrac,
                        onValueChange = { draggingFrac = it },
                        onValueChangeFinished = {
                            val target = draggingFrac
                            draggingFrac = null
                            if (target != null && ym.durationMs > 0L) {
                                onSeek((target.coerceIn(0f, 1f) * ym.durationMs).toLong())
                            }
                        },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(26.dp),
                        colors = SliderDefaults.colors(
                            thumbColor = palette.accent,
                            activeTrackColor = palette.accent,
                            inactiveTrackColor = Color.White.copy(alpha = 0.14f)
                        )
                    )
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 4.dp),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = formatDurationMs(shownPosMs),
                            color = Color(0xFFA0A0B0),
                            fontSize = 11.sp
                        )
                        Text(
                            text = formatDurationMs(ym.durationMs),
                            color = Color(0xFFA0A0B0),
                            fontSize = 11.sp
                        )
                    }
                }
            }
        }

        // Quick Action Tiles
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            QuickTile(
                icon = Icons.Rounded.VolumeUp,
                title = "Громкость",
                subtitle = "$volumePct%",
                accent = palette.accent,
                modifier = Modifier.weight(1f),
                onClick = onOpenVolume
            )
            QuickTile(
                icon = Icons.Rounded.Equalizer,
                title = "Эквалайзер",
                subtitle = "5 полос",
                accent = palette.accent,
                modifier = Modifier.weight(1f),
                onClick = onOpenEq
            )
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            QuickTile(
                icon = Icons.Rounded.MusicNote,
                title = "Яндекс Музыка",
                subtitle = if (ym.loggedIn) "Подключена" else "Настроить",
                accent = if (ym.loggedIn) Color(0xFF4ADE80) else palette.accent,
                modifier = Modifier.weight(1f),
                onClick = onOpenIntegrations
            )
            QuickTile(
                icon = Icons.Rounded.Timer,
                title = "Таймеры",
                subtitle = "Будильники и таймеры",
                accent = palette.accent,
                modifier = Modifier.weight(1f),
                onClick = onOpenTimers
            )
        }

        Spacer(Modifier.height(4.dp))
    }

    if (showWaveSheet) {
        val corePresets = remember {
            listOf(
                WavePreset("Моя волна", "wave", null),
                WavePreset("Мне нравится", "likes", null),
                WavePreset("Тренды", "chart", null)
            )
        }
        val moodPresets = remember {
            listOf(
                WavePreset("Бодрое", "station", "mood:energetic"),
                WavePreset("Весёлое", "station", "mood:happy"),
                WavePreset("Спокойное", "station", "mood:calm"),
                WavePreset("Грустное", "station", "mood:sad"),
                WavePreset("Романтичное", "station", "mood:romantic")
            )
        }
        val activityPresets = remember {
            listOf(
                WavePreset("За работой", "station", "activity:work"),
                WavePreset("Тренировка", "station", "activity:workout"),
                WavePreset("В дороге", "station", "activity:driving"),
                WavePreset("Вечеринка", "station", "activity:party"),
                WavePreset("Перед сном", "station", "activity:fall-asleep")
            )
        }
        ModalBottomSheet(
            onDismissRequest = { showWaveSheet = false },
            containerColor = OledCard
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 20.dp, vertical = 8.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                Text(
                    "Настройка Моей волны",
                    color = Color.White,
                    fontSize = 19.sp,
                    fontWeight = FontWeight.Bold
                )
                Text("Основное", color = palette.accent, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    corePresets.forEach { preset ->
                        AssistChip(
                            onClick = {
                                showWaveSheet = false
                                onPlayWavePreset(preset.mode, preset.query)
                            },
                            label = { Text(preset.label, color = Color.White) }
                        )
                    }
                }

                Text("Под настроение", color = palette.accent, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    moodPresets.forEach { preset ->
                        AssistChip(
                            onClick = {
                                showWaveSheet = false
                                onPlayWavePreset(preset.mode, preset.query)
                            },
                            label = { Text(preset.label, color = Color.White) }
                        )
                    }
                }

                Text("Под занятие", color = palette.accent, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    activityPresets.forEach { preset ->
                        AssistChip(
                            onClick = {
                                showWaveSheet = false
                                onPlayWavePreset(preset.mode, preset.query)
                            },
                            label = { Text(preset.label, color = Color.White) }
                        )
                    }
                }

                if (orbs.isNotEmpty()) {
                    Text("Персональные сферы", color = palette.accent, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        orbs.take(12).forEach { orb ->
                            AssistChip(
                                onClick = {
                                    showWaveSheet = false
                                    onPlayWavePreset(orb.mode, orb.query.ifBlank { null })
                                },
                                label = { Text(orb.title, color = Color.White) }
                            )
                        }
                    }
                }

                Spacer(Modifier.height(16.dp))
            }
        }
    }
}

private fun formatDurationMs(ms: Long): String {
    val totalSec = (ms / 1000L).coerceAtLeast(0L)
    val min = totalSec / 60L
    val sec = totalSec % 60L
    return String.format(Locale.US, "%d:%02d", min, sec)
}

@Composable
private fun QuickTile(
    icon: ImageVector,
    title: String,
    subtitle: String,
    accent: Color,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    Column(
        modifier = modifier
            .clip(RoundedCornerShape(20.dp))
            .background(OledCard)
            .border(1.dp, Color.White.copy(alpha = 0.06f), RoundedCornerShape(20.dp))
            .clickable { onClick() }
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        Box(
            modifier = Modifier
                .size(32.dp)
                .clip(RoundedCornerShape(10.dp))
                .background(OledCardHigh),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, contentDescription = null, tint = accent, modifier = Modifier.size(18.dp))
        }
        Text(title, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.SemiBold, maxLines = 1)
        Text(subtitle, color = Color(0xFFA0A0B0), fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}
