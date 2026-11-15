#include "smoke_runtime.h"
#include "vulkan_smoke.h"
#include "native_swap_image.h"

#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <string>

namespace {

constexpr const char* kLogTag = "RecompBootstrap";
recomp::bootstrap::SmokeRuntime gRuntime;
recomp::bootstrap::VulkanSmokeRenderer gRenderer(gRuntime);

std::string toUtf8(JNIEnv* environment, jstring value) {
    if (value == nullptr) {
        return {};
    }

    const char* characters = environment->GetStringUTFChars(value, nullptr);
    if (characters == nullptr) {
        return {};
    }

    std::string result(characters);
    environment->ReleaseStringUTFChars(value, characters);
    return result;
}

constexpr const char* currentAbi() noexcept {
#if defined(__aarch64__)
    return "arm64-v8a";
#elif defined(__arm__)
    return "armeabi-v7a";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_recompbootstrap_smoke_NativeBridge_create(
        JNIEnv* environment,
        jclass,
        jstring dataDirectory,
        jstring cacheDirectory) {
    const bool initialized = gRuntime.initialize(
            toUtf8(environment, dataDirectory),
            toUtf8(environment, cacheDirectory));
    if(!gRenderer.configureRgbaDiagnostic({}))return JNI_FALSE;
    __android_log_print(
            initialized ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
            kLogTag,
            "Native runtime initialization %s",
            initialized ? "succeeded" : "failed");
    return initialized ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_recompbootstrap_smoke_NativeBridge_configureRgbaDiagnostic(JNIEnv* environment,jclass,jbyteArray bytes,jboolean rejectReadback) {
    if(!bytes || environment->GetArrayLength(bytes)!=recomp::gpu::NativeSwapImagePlan::PixelBytes)return JNI_FALSE;
    try {
        std::vector<std::uint8_t> owned(recomp::gpu::NativeSwapImagePlan::PixelBytes);
        environment->GetByteArrayRegion(bytes,0,static_cast<jsize>(owned.size()),reinterpret_cast<jbyte*>(owned.data()));
        if(environment->ExceptionCheck())return JNI_FALSE;
        return gRenderer.configureRgbaDiagnostic(std::move(owned),rejectReadback==JNI_TRUE)?JNI_TRUE:JNI_FALSE;
    } catch(...) {return JNI_FALSE;}
}

extern "C" JNIEXPORT void JNICALL
Java_dev_recompbootstrap_smoke_NativeBridge_resume(JNIEnv*, jclass) {
    gRuntime.resume();
}

extern "C" JNIEXPORT void JNICALL
Java_dev_recompbootstrap_smoke_NativeBridge_pause(JNIEnv*, jclass) {
    gRuntime.suspend();
}

extern "C" JNIEXPORT void JNICALL
Java_dev_recompbootstrap_smoke_NativeBridge_destroy(JNIEnv*, jclass) {
    gRenderer.detachWindow();
    gRuntime.shutdown();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_recompbootstrap_smoke_NativeBridge_surfaceCreated(
        JNIEnv* environment,
        jclass,
        jobject surface,
        jint width,
        jint height) {
    ANativeWindow* window = surface == nullptr
            ? nullptr
            : ANativeWindow_fromSurface(environment, surface);
    const bool presented = gRenderer.attachWindow(window, width, height);
    __android_log_print(
            presented ? ANDROID_LOG_INFO : ANDROID_LOG_WARN,
            kLogTag,
            "%s",
            gRuntime.describe(currentAbi()).c_str());
    return presented ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_recompbootstrap_smoke_NativeBridge_surfaceChanged(
        JNIEnv*,
        jclass,
        jint width,
        jint height) {
    const bool presented = gRenderer.resize(width, height);
    __android_log_print(
            presented ? ANDROID_LOG_INFO : ANDROID_LOG_WARN,
            kLogTag,
            "%s",
            gRuntime.describe(currentAbi()).c_str());
    return presented ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_recompbootstrap_smoke_NativeBridge_surfaceDestroyed(JNIEnv*, jclass) {
    gRenderer.detachWindow();
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_recompbootstrap_smoke_NativeBridge_describe(JNIEnv* environment, jclass) {
    const std::string description = gRuntime.describe(currentAbi());
    return environment->NewStringUTF(description.c_str());
}
