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

/* JSON log entry creation */
void log_jni_call_json(const char* func_name, const char** arg_names, 
                       const char** arg_values, int arg_count);

/* Utility functions */
char* escape_json_string(const char* str);
void free_escaped_string(char* str);

#ifdef __cplusplus
}
#endif

#endif /* JSON_LOGGER_H */
