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
