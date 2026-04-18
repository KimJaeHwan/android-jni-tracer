/*
 * Fake JNI Environment - Stub implementation for hooking
 */

#ifndef FAKE_JNI_H
#define FAKE_JNI_H

#include "../include/jni.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create a fake JNIEnv with full function table */
JNIEnv* create_fake_jnienv(void);

/* Cleanup (if needed) */
void destroy_fake_jnienv(JNIEnv* env);

/* Register the fake JavaVM so GetJavaVM() can return it correctly */
void set_fake_javavm(JavaVM* vm);

typedef struct {
    char class_name[256];
    char method_name[128];
    char signature[256];
    void* fnPtr;
} NativeRegistryEntry;

/* Find a native method captured via RegisterNatives */
const NativeRegistryEntry* native_registry_find(const char* class_name,
                                                const char* method_name,
                                                const char* signature);

/* Resolve or create fake handles used by invoke dispatch */
jclass fake_jni_find_class(const char* class_name);
jstring fake_jni_new_string_utf(const char* utf8);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_JNI_H */
