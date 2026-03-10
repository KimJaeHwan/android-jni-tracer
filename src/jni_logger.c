/*
 * JNI Logger Implementation
 */

#include "jni_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#define CLOCK_MONOTONIC 0
typedef struct {
    long tv_sec;
    long tv_nsec;
} timespec;

static int clock_gettime(int clk_id, struct timespec *spec) {
    (void)clk_id;
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    spec->tv_sec = counter.QuadPart / freq.QuadPart;
    spec->tv_nsec = (long)((counter.QuadPart % freq.QuadPart) * 1000000000LL / freq.QuadPart);
    return 0;
}
#else
#include <sys/time.h>
#endif

static FILE* g_log_file = NULL;
static int g_call_count = 0;

/* Function call statistics */
typedef struct {
    char func_name[128];
    int count;
} CallStat;

static CallStat g_call_stats[500];
static int g_stat_count = 0;

void init_logger(const char* log_path) {
    g_log_file = fopen(log_path, "w");
    if (!g_log_file) {
        fprintf(stderr, "Failed to open log file: %s\n", log_path);
        g_log_file = stdout;
    }
    
    time_t now = time(NULL);
    fprintf(g_log_file, "=== JNI Harness Log ===\n");
    fprintf(g_log_file, "Started at: %s\n", ctime(&now));
    fflush(g_log_file);
}

void log_info(const char* format, ...) {
    if (!g_log_file) return;
    
    va_list args;
    va_start(args, format);
    
    fprintf(g_log_file, "[INFO] ");
    vfprintf(g_log_file, format, args);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
    
    va_end(args);
}

void log_error(const char* format, ...) {
    if (!g_log_file) g_log_file = stderr;
    
    va_list args;
    va_start(args, format);
    
    fprintf(g_log_file, "[ERROR] ");
    vfprintf(g_log_file, format, args);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
    
    /* Also output to stderr */
    fprintf(stderr, "[ERROR] ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    
    va_end(args);
}

void log_warning(const char* format, ...) {
    if (!g_log_file) return;
    
    va_list args;
    va_start(args, format);
    
    fprintf(g_log_file, "[WARN] ");
    vfprintf(g_log_file, format, args);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
    
    va_end(args);
}

void log_jni_call(const char* func_name, const char* format, ...) {
    if (!g_log_file) return;
    
    g_call_count++;
    
    /* Timestamp */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    fprintf(g_log_file, "[JNI #%04d] [%ld.%06ld] %s(", 
            g_call_count,
            ts.tv_sec,
            ts.tv_nsec / 1000,
            func_name);
    
    if (format && strlen(format) > 0) {
        va_list args;
        va_start(args, format);
        vfprintf(g_log_file, format, args);
        va_end(args);
    }
    
    fprintf(g_log_file, ")\n");
    fflush(g_log_file);
}

void increment_call_count(const char* func_name) {
    /* Update statistics */
    for (int i = 0; i < g_stat_count; i++) {
        if (strcmp(g_call_stats[i].func_name, func_name) == 0) {
            g_call_stats[i].count++;
            return;
        }
    }
    
    /* Add new entry */
    if (g_stat_count < 500) {
        strncpy(g_call_stats[g_stat_count].func_name, func_name, 127);
        g_call_stats[g_stat_count].func_name[127] = '\0';
        g_call_stats[g_stat_count].count = 1;
        g_stat_count++;
    }
}

void print_log_summary(void) {
    if (!g_log_file) return;
    
    fprintf(g_log_file, "\n=== Call Statistics ===\n");
    fprintf(g_log_file, "Total JNI calls: %d\n", g_call_count);
    fprintf(g_log_file, "Unique functions called: %d\n\n", g_stat_count);
    
    fprintf(g_log_file, "Function Call Counts:\n");
    for (int i = 0; i < g_stat_count; i++) {
        fprintf(g_log_file, "  %-30s : %d\n", 
                g_call_stats[i].func_name,
                g_call_stats[i].count);
    }
    
    fflush(g_log_file);
}

int get_total_call_count(void) {
    return g_call_count;
}

void close_logger(void) {
    if (g_log_file && g_log_file != stdout && g_log_file != stderr) {
        fclose(g_log_file);
    }
    g_log_file = NULL;
}
