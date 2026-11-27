package com.leftymods.atrios

import com.leftymods.atrios.model.YMusicStatus
import kotlin.math.abs

internal data class PendingMusicSeek(val id: String, val trackId: String, val targetMs: Long, val beforeSeq: Long) {
    fun outcome(status: YMusicStatus): SeekOutcome {
        if (status.trackId != trackId || !status.isActive) return SeekOutcome.REPLACED
        if (status.seekState == null) {
            // Old firmware reports only position and sequence. A stale poll is not an acknowledgement.
            return if (status.seq > beforeSeq && abs(status.positionMs - targetMs) < 1200L) {
                SeekOutcome.APPLIED
            } else SeekOutcome.WAITING
        }
        if (status.seekId != id) return SeekOutcome.WAITING
        return when (status.seekState) {
            "applied" -> SeekOutcome.APPLIED
            "failed" -> SeekOutcome.FAILED
            else -> SeekOutcome.WAITING
        }
    }
}

internal enum class SeekOutcome { WAITING, APPLIED, FAILED, REPLACED }

internal data class PlaybackClock(val music: YMusicStatus, val sampledAtMs: Long) {
    fun positionAt(nowMs: Long): Long {
        val elapsed = if (music.state == "playing") (nowMs - sampledAtMs).coerceAtLeast(0) else 0L
        return (music.positionMs.coerceAtLeast(0) + elapsed).let { position ->
            if (music.durationMs > 0) position.coerceAtMost(music.durationMs) else position
        }
    }

    fun sync(snapshot: YMusicStatus, nowMs: Long): PlaybackClock {
        if (snapshot.trackId != music.trackId || snapshot.state != music.state ||
            snapshot.seq != music.seq || snapshot.durationMs != music.durationMs ||
            snapshot.positionMs > positionAt(nowMs) + 350
        ) {
            return PlaybackClock(snapshot, nowMs)
        }
        return this
    }
}
