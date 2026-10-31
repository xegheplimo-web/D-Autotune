#include "AudioEngine.h"
#include <jni.h>

namespace { dautotune::AudioEngine gEngine; }

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dautotune_app_NativeAudio_start(JNIEnv*, jobject) { return gEngine.start() ? JNI_TRUE : JNI_FALSE; }
extern "C" JNIEXPORT void JNICALL
Java_com_dautotune_app_NativeAudio_stop(JNIEnv*, jobject) { gEngine.stop(); }
extern "C" JNIEXPORT void JNICALL
Java_com_dautotune_app_NativeAudio_configure(JNIEnv*, jobject, jint root, jint scale, jfloat amount, jfloat retune, jfloat reverb) {
    gEngine.setParameters(root, scale, amount, retune, reverb);
}
extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_dautotune_app_NativeAudio_snapshot(JNIEnv* env, jobject) {
    const auto values = gEngine.snapshot();
    auto result = env->NewDoubleArray(static_cast<jsize>(values.size()));
    if (result) env->SetDoubleArrayRegion(result, 0, static_cast<jsize>(values.size()), values.data());
    return result;
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_dautotune_app_NativeAudio_lastError(JNIEnv* env, jobject) {
    const std::string err = gEngine.lastError();
    return env->NewStringUTF(err.c_str());
}

// ---- DC-003 REV2: diagnostic acoustic loopback (measurement CLASS 1) ------
// Debug-only. Class-2 authoritative end-to-end latency is fixture-gated and
// reports MEASUREMENT_UNAVAILABLE here (see AudioEngine::authoritativeLatencyMs).
extern "C" JNIEXPORT jint JNICALL
Java_com_dautotune_app_NativeAudio_armLoopback(JNIEnv*, jobject, jint cycles,
                                               jfloat amplitudeCap, jfloat nccThreshold,
                                               jint maxLagMs) {
    return gEngine.armMeasurement(cycles, amplitudeCap, nccThreshold, maxLagMs);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_dautotune_app_NativeAudio_pollLoopback(JNIEnv*, jobject) {
    return gEngine.pollMeasurement();
}
extern "C" JNIEXPORT void JNICALL
Java_com_dautotune_app_NativeAudio_stopLoopback(JNIEnv*, jobject) {
    gEngine.stopMeasurement();
}
extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_dautotune_app_NativeAudio_loopbackSummary(JNIEnv* env, jobject) {
    const auto values = gEngine.measurementSummary();
    auto result = env->NewDoubleArray(static_cast<jsize>(values.size()));
    if (result) env->SetDoubleArrayRegion(result, 0, static_cast<jsize>(values.size()), values.data());
    return result;
}
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_dautotune_app_NativeAudio_loopbackCycles(JNIEnv* env, jobject) {
    const auto values = gEngine.measurementCycles();
    auto result = env->NewFloatArray(static_cast<jsize>(values.size()));
    if (result) env->SetFloatArrayRegion(result, 0, static_cast<jsize>(values.size()), values.data());
    return result;
}
extern "C" JNIEXPORT jdouble JNICALL
Java_com_dautotune_app_NativeAudio_authoritativeLatencyMs(JNIEnv*, jobject) {
    return gEngine.authoritativeLatencyMs();
}
