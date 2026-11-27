package com.leftymods.atrios

import com.leftymods.atrios.model.YMusicStatus
import org.junit.Assert.assertEquals
import org.junit.Test

class PlaybackTimelineTest {
    private val playing = YMusicStatus(
        state = "playing", trackId = "1", durationMs = 180_000, positionMs = 5_000, seq = 102
    )

    @Test
    fun seekIgnoresStaleSnapshotsAndEarlierRequests() {
        val request = PendingMusicSeek("new", "1", 85_000, 102)
        assertEquals(SeekOutcome.WAITING, request.outcome(playing))
        assertEquals(SeekOutcome.WAITING, request.outcome(playing.copy(
            seq = 103, seekId = "old", seekState = "applied"
        )))
        assertEquals(SeekOutcome.WAITING, request.outcome(playing.copy(
            seq = 104, seekId = "new", seekState = "pending"
        )))
        assertEquals(SeekOutcome.APPLIED, request.outcome(playing.copy(
            seq = 105, seekId = "new", seekState = "applied"
        )))
        assertEquals(SeekOutcome.FAILED, request.outcome(playing.copy(
            seq = 106, seekId = "new", seekState = "failed"
        )))
        assertEquals(SeekOutcome.REPLACED, request.outcome(playing.copy(trackId = "2")))
    }

    @Test
    fun olderFirmwareRequiresAFreshPositionNearTheTarget() {
        val request = PendingMusicSeek("a", "1", 85_000, 102)
        assertEquals(SeekOutcome.WAITING, request.outcome(playing.copy(positionMs = 85_000)))
        assertEquals(SeekOutcome.WAITING, request.outcome(playing.copy(seq = 103)))
        assertEquals(SeekOutcome.APPLIED, request.outcome(playing.copy(seq = 104, positionMs = 85_020)))
    }

    @Test
    fun clockUsesElapsedTimeAndAcceptsAnExplicitBackwardSeek() {
        val clock = PlaybackClock(playing.copy(positionMs = 85_000), 100)
        assertEquals(88_500, clock.positionAt(3600))
        assertEquals(clock, clock.sync(playing, 3700))
        val rewound = clock.sync(playing.copy(seq = 103), 4000)
        assertEquals(5_003, rewound.positionAt(4003))
        assertEquals(5_000, rewound.sync(playing.copy(state = "paused", seq = 104), 4200).positionAt(7000))
    }
}
