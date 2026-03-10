/*
 * JNI Logger - Text format logging
 */

#ifndef JNI_LOGGER_H
#define JNI_LOGGER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Logger initialization and cleanup */
void init_logger(const char* log_path);
void close_logger(void);

/* Logging functions */
void log_info(const char* format, ...);
void log_error(const char* format, ...);
void log_warning(const char* format, ...);
void log_jni_call(const char* func_name, const char* format, ...);

/* Statistics */
void increment_call_count(const char* func_name);
void print_log_summary(void);

/* Utility */
int get_total_call_count(void);

#ifdef __cplusplus
}
#endif

#endif /* JNI_LOGGER_H */
