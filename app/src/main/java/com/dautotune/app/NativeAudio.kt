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
}
