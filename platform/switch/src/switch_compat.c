// Compatibility shims for Nintendo Switch (devkitA64).
//
// 1. Thread scheduling: devkitA64's newlib provides POSIX pthreads and sched.h,
//    but lacks POSIX thread scheduling policy APIs which Horizon OS handles
//    via native svc thread priorities.
// 2. Audio stubs: raylib does not implement a native Horizon (libnx audren)
//    audio driver, so raylib is built with SUPPORT_MODULE_RAUDIO=OFF. The stubs
//    below allow ps2_runtime to link without audio errors and gracefully fall
//    back to muted sound (IsAudioDeviceReady() returns false).
#if defined(__SWITCH__)
#include "raylib.h"
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>

int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy) {
    (void)attr;
    (void)policy;
    return 0;
}

int sched_get_priority_min(int policy) {
    (void)policy;
    return 0;
}

int sched_get_priority_max(int policy) {
    (void)policy;
    return 0;
}

void InitAudioDevice(void) {}
void CloseAudioDevice(void) {}
bool IsAudioDeviceReady(void) { return false; }
void SetMasterVolume(float vol) { (void)vol; }

Wave LoadWaveFromMemory(const char *fileType, const unsigned char *fileData, int dataSize) {
    (void)fileType;
    (void)fileData;
    (void)dataSize;
    return (Wave){0};
}

Sound LoadSoundFromWave(Wave wave) {
    (void)wave;
    return (Sound){0};
}

void UnloadWave(Wave wave) {
    (void)wave;
}

void UnloadSound(Sound sound) {
    (void)sound;
}

void PlaySound(Sound sound) {
    (void)sound;
}

void StopSound(Sound sound) {
    (void)sound;
}

bool IsSoundPlaying(Sound sound) {
    (void)sound;
    return false;
}

void SetSoundVolume(Sound sound, float volume) {
    (void)sound;
    (void)volume;
}

void SetSoundPitch(Sound sound, float pitch) {
    (void)sound;
    (void)pitch;
}

#endif
