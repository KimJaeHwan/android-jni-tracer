/*
 * JSON Logger - Structured JSON format logging
 */

#ifndef JSON_LOGGER_H
#define JSON_LOGGER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* JSON logger initialization and cleanup */
void init_json_logger(const char* json_path);
void close_json_logger(void);

/*
 * __builtin_return_address(1) inside these functions must always see:
 *   depth 0 = inside log_jni_call_json
 *   depth 1 = inside fake_Xxx (the JNI stub)          <- target .so call site
 *
 * If the compiler inlines log_jni_call_json into fake_Xxx the depth shifts
 * by one and we capture the wrong address.  noinline guarantees a real call
 * frame is always present, making depth 1 reliable on all optimisation levels.
 */

/* JSON log entry creation */
__attribute__((noinline))
void log_jni_call_json(const char* func_name, const char** arg_names,
                       const char** arg_values, int arg_count);

/*
 * Same as log_jni_call_json but the last argument is written as a raw JSON
 * value (not escaped/quoted).  Use this when one field contains a pre-built
 * JSON sub-object or array (e.g. the methods list from RegisterNatives).
 */
__attribute__((noinline))
void log_jni_call_json_raw_last(const char* func_name,
                                const char** arg_names,
                                const char** arg_values, int arg_count,
                                const char* raw_field_name,
                                const char* raw_field_value);

/* Utility functions */
char* escape_json_string(const char* str);
void free_escaped_string(char* str);

#ifdef __cplusplus
}
#endif

#endif /* JSON_LOGGER_H */
