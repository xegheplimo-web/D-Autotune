package com.dautotune.app

object NativeAudio {
    init { System.loadLibrary("dautotune") }
    external fun start(): Boolean
    external fun stop()
    external fun configure(root: Int, scale: Int, amount: Float, retuneMs: Float, reverb: Float)
    // [detectedHz, targetHz, correctionCents, voiced, sampleRate, missingFrames, processedFrames, running]
    external fun snapshot(): DoubleArray
    external fun lastError(): String
}
