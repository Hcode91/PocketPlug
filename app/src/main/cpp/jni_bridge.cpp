// PocketPlug - JNI bridge between the Kotlin UI and the emulation cores
// SPDX-License-Identifier: BSD-3-Clause

#include <jni.h>
#include <android/log.h>
#include <cstdio>
#include <string>
#include <vector>
#include "core_api.h"

#define LOG_TAG "PocketPlug"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::string jstr(JNIEnv* env, jstring s)
{
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r(c);
    env->ReleaseStringUTFChars(s, c);
    return r;
}

static bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    size_t got = n > 0 ? fread(out.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    return got == out.size();
}

static bool writeFile(const std::string& path, const std::vector<uint8_t>& data)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t w = fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return w == data.size();
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_hcode91_pocketplug_Native_create(JNIEnv* env, jclass, jstring machineId)
{
    return (jlong)(intptr_t)createCore(jstr(env, machineId));
}

// names[i] is the file name inside the set, paths[i] where it is stored on disk
JNIEXPORT jstring JNICALL
Java_com_hcode91_pocketplug_Native_load(JNIEnv* env, jclass, jlong h, jobjectArray names, jobjectArray paths)
{
    Core* core = (Core*)(intptr_t)h;
    if (!core) return env->NewStringUTF("Unknown machine");
    std::vector<RomFile> roms;
    jsize n = env->GetArrayLength(names);
    for (jsize i = 0; i < n; i++) {
        RomFile rf;
        rf.name = jstr(env, (jstring)env->GetObjectArrayElement(names, i));
        std::string path = jstr(env, (jstring)env->GetObjectArrayElement(paths, i));
        rf.path = path;
        if (!core->wantsRomPaths() && !readFile(path, rf.data)) {
            std::string err = "Could not read " + rf.name;
            return env->NewStringUTF(err.c_str());
        }
        roms.push_back(std::move(rf));
    }
    std::string err;
    if (!core->load(roms, err)) return env->NewStringUTF(err.c_str());
    return nullptr;
}

JNIEXPORT void JNICALL
Java_com_hcode91_pocketplug_Native_destroy(JNIEnv*, jclass, jlong h)
{
    delete (Core*)(intptr_t)h;
}

JNIEXPORT void JNICALL
Java_com_hcode91_pocketplug_Native_reset(JNIEnv*, jclass, jlong h)
{
    if (h) ((Core*)(intptr_t)h)->reset();
}

JNIEXPORT jintArray JNICALL
Java_com_hcode91_pocketplug_Native_videoInfo(JNIEnv* env, jclass, jlong h)
{
    Core* c = (Core*)(intptr_t)h;
    jint v[4] = { c->frameWidth(), c->frameHeight(), (jint)(c->refreshRate() * 1000.0), c->sampleRate() };
    jintArray arr = env->NewIntArray(4);
    env->SetIntArrayRegion(arr, 0, 4, v);
    return arr;
}

// Runs one frame. Fills pixels (ARGB) and audio; returns the number of audio samples written.
JNIEXPORT jint JNICALL
Java_com_hcode91_pocketplug_Native_runFrame(JNIEnv* env, jclass, jlong h, jint inputs, jintArray pixels, jshortArray audio)
{
    Core* c = (Core*)(intptr_t)h;
    c->setInputs((uint32_t)inputs);
    c->runFrame();
    env->SetIntArrayRegion(pixels, 0, c->frameWidth() * c->frameHeight(), (const jint*)c->frameBuffer());
    jsize cap = env->GetArrayLength(audio);
    jshort* buf = env->GetShortArrayElements(audio, nullptr);
    size_t n = c->readAudio((int16_t*)buf, (size_t)cap);
    env->ReleaseShortArrayElements(audio, buf, 0);
    return (jint)n;
}

JNIEXPORT jboolean JNICALL
Java_com_hcode91_pocketplug_Native_saveState(JNIEnv* env, jclass, jlong h, jstring path)
{
    std::vector<uint8_t> st;
    if (!((Core*)(intptr_t)h)->saveState(st)) return JNI_FALSE;
    return writeFile(jstr(env, path), st) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_hcode91_pocketplug_Native_loadState(JNIEnv* env, jclass, jlong h, jstring path)
{
    std::vector<uint8_t> st;
    if (!readFile(jstr(env, path), st)) return JNI_FALSE;
    return ((Core*)(intptr_t)h)->loadState(st) ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"

extern "C" JNIEXPORT void JNICALL
Java_com_hcode91_pocketplug_Native_setAnalog(JNIEnv*, jclass, jlong h, jint stick, jfloat x, jfloat y)
{
    if (h) ((Core*)(intptr_t)h)->setAnalog(stick, x, y);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_hcode91_pocketplug_Native_tapInputs(JNIEnv*, jclass, jlong h, jint mask)
{
    return (h && ((Core*)(intptr_t)h)->tapInputs((uint32_t)mask)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_hcode91_pocketplug_Native_advanceTime(JNIEnv*, jclass, jlong h, jdouble seconds)
{
    return (h && ((Core*)(intptr_t)h)->advanceTime(seconds)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_hcode91_pocketplug_Native_setWallClock(JNIEnv*, jclass, jlong h, jlong localSeconds)
{
    if (h) ((Core*)(intptr_t)h)->setWallClock((int64_t)localSeconds);
}

extern "C" JNIEXPORT void JNICALL
Java_com_hcode91_pocketplug_Native_setOption(JNIEnv*, jclass, jlong h, jint id, jint value)
{
    if (h) ((Core*)(intptr_t)h)->setOption(id, value);
}
