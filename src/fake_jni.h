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

#ifdef __cplusplus
}
#endif

#endif /* FAKE_JNI_H */
