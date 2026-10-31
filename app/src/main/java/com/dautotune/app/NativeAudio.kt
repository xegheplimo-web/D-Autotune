package com.dautotune.app

object NativeAudio {
    init { System.loadLibrary("dautotune") }
    external fun start(): Boolean
    external fun stop()
    external fun configure(root: Int, scale: Int, amount: Float, retuneMs: Float, reverb: Float)
    // [detectedHz, targetHz, correctionCents, voiced, sampleRate, missingFrames, processedFrames,
    //  running, xruns, retuneMs (control smoothing param, NOT a latency), cbMaxMs, cbAvgMs,
    //  cbP99Ms (histogram upper bound), xrunValid (1=getXRunCount supported, 0=unsupported),
    //  missingFramesPerSec, inputShortReadsPerSec]
    external fun snapshot(): DoubleArray
    // Granted stream properties after start(): [out, in] x [sampleRate, bufferCapacityFrames,
    //  framesPerBurst, sharingMode (0=Shared,1=Exclusive,2=other),
    //  performanceMode (0=LowLatency,1=PowerSaving,2=other)]. Entries are -1 while not running.
    external fun getStreamInfo(): IntArray
    external fun lastError(): String

    // ---- DC-003 REV2: diagnostic acoustic loopback (measurement CLASS 1) ----
    // DEBUG ONLY: emits a low-amplitude chirp (peak <= 0.30 FS) on the speaker,
    // captures mic input inside the SAME output callback (one stream pair, one
    // clock domain) and detects it via normalized cross-correlation. Reports a
    // speaker->mic round-trip diagnostic - NEVER a mic->DSP->headphone latency.
    // pollLoopback() states: 0 idle, 1 armed, 2 playing, 3 capture-ready
    // (internal), 4 complete. armLoopback() returns 0 ok / 1 engine-not-running
    // / 3 bad arguments; amplitudeCap clamps to <=0.30, nccThreshold to
    // [0.10,0.95]. Fail-closed: cycles only count as valid with NCC >=
    // threshold and a non-clipped peak.
    external fun armLoopback(cycles: Int, amplitudeCap: Float, nccThreshold: Float, maxLagMs: Int): Int
    external fun pollLoopback(): Int
    external fun stopLoopback()
    // [medianMs, p95Ms, validCycles, completedCycles, requestedCycles,
    //  sampleRateHz, minMs, maxMs]; NaN stats when no cycle validated.
    external fun loopbackSummary(): DoubleArray
    // Per completed cycle {lagSamples (-1 = none), nccPeak, flags}; flags:
    // bit0 valid, bit1 clipped-at-window-edge, bit2 timeout, bit3 low NCC.
    external fun loopbackCycles(): FloatArray
    // Measurement CLASS 2 (authoritative end-to-end latency) requires a
    // calibrated hardware loopback fixture with a common reference clock.
    // Absent in this environment -> NaN = MEASUREMENT_UNAVAILABLE, fail-closed.
    // This is the ONLY allowed source for a G1 latency number.
    external fun authoritativeLatencyMs(): Double
}
