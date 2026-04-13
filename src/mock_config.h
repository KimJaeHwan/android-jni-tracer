/*
 * Mock Config — runtime return-value injection for JNI Call*Method stubs.
 *
 * Load a JSON file with --mock <file> to override what specific Java
 * method calls return, without recompiling the harness.
 */

#ifndef MOCK_CONFIG_H
#define MOCK_CONFIG_H

#include <stddef.h>
#include "../include/jni.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MOCK_TYPE_PRIMITIVE, /* covers bool/int/long/byte/char/short */
    MOCK_TYPE_STRING
} MockType;

typedef struct {
    char     class_name[256];
    char     method_name[128];
    char     signature[256];
    MockType type;
    jlong    prim_value;      /* used when type == MOCK_TYPE_PRIMITIVE */
    char     str_value[512];  /* used when type == MOCK_TYPE_STRING    */
} MockEntry;

/* Load a JSON mock config file.  Returns 0 on success, -1 on failure. */
int  mock_load(const char* path);

/* Look up a mock for (class_name, method_name, signature).
 * For primitives: fills *out_val and returns 1.
 * Returns 0 if no mock is registered. */
int  mock_get_primitive(const char* class_name, const char* method_name,
                        const char* sig, jlong* out_val);

/* For string-returning methods.  Copies result into buf[size].
 * Returns 1 if found, 0 otherwise. */
int  mock_get_string(const char* class_name, const char* method_name,
                     const char* sig, char* buf, size_t size);

/* Total number of loaded mock entries */
int  mock_count(void);

/* Free all resources */
void mock_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_CONFIG_H */
