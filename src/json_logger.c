/*
 * JSON Logger Implementation
 */

#include "json_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* Module info for address-to-offset conversion */
#define MAX_MODULES 1024
typedef struct {
    unsigned long base_addr;
    unsigned long end_addr;
    char name[256];
} ModuleInfo;

static ModuleInfo g_modules[MAX_MODULES];
static int g_module_count = 0;

static FILE* g_json_file = NULL;
static FILE* g_ndjson_file = NULL;
static int g_json_call_index = 0;
static int g_first_entry = 1;
static int g_maps_parsed = 0;  /* Lazy loading flag */

/* Set JNI_TRACER_VERBOSE=1 in environment to enable debug prints */
#define VERBOSE_ENABLED() (getenv("JNI_TRACER_VERBOSE") != NULL)
#define DEBUG_PRINT(...) do { if (VERBOSE_ENABLED()) fprintf(stderr, __VA_ARGS__); } while(0)

/* Parse /proc/self/maps to get loaded module base addresses */
static void parse_proc_maps(void) {
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        fprintf(stderr, "[ERROR] Failed to open /proc/self/maps\n");
        return;
    }
    
    char line[512];
    while (fgets(line, sizeof(line), maps) && g_module_count < MAX_MODULES) {
        unsigned long start, end;
        char perms[8], path[256];
        path[0] = '\0';
        
        int fields = sscanf(line, "%lx-%lx %s %*s %*s %*s %255s", 
                           &start, &end, perms, path);
        
        /* Track all executable memory regions with paths */
        if (fields >= 3 && strchr(perms, 'x') && path[0] != '\0') {
            /* Add all segments (don't skip duplicates - same .so has multiple segments) */
            g_modules[g_module_count].base_addr = start;
            g_modules[g_module_count].end_addr = end;
            strncpy(g_modules[g_module_count].name, path, sizeof(g_modules[g_module_count].name) - 1);
            g_modules[g_module_count].name[sizeof(g_modules[g_module_count].name) - 1] = '\0';

            DEBUG_PRINT("[DEBUG] Module[%d]: %lx-%lx %s %s\n",
                        g_module_count, start, end, perms, path);

            g_module_count++;
        }
    }

    fclose(maps);
    DEBUG_PRINT("[INFO] Loaded %d executable memory segments from /proc/self/maps\n", g_module_count);
    g_maps_parsed = 1;  /* Mark as parsed */
}

/* Find which module contains the given address and calculate offset */
static int find_module_offset(void* addr, unsigned long* offset, char* module_name, size_t name_size) {
    unsigned long addr_val = (unsigned long)addr;
    
    /* First, find which module contains this address */
    for (int i = 0; i < g_module_count; i++) {
        if (addr_val >= g_modules[i].base_addr && addr_val < g_modules[i].end_addr) {
            /* Found the segment containing this address */
            /* Now find the lowest base address for this module (first segment) */
            unsigned long lowest_base = g_modules[i].base_addr;
            const char* found_name = g_modules[i].name;
            
            for (int j = 0; j < g_module_count; j++) {
                if (strcmp(g_modules[j].name, found_name) == 0 && 
                    g_modules[j].base_addr < lowest_base) {
                    lowest_base = g_modules[j].base_addr;
                }
            }
            
            /* Calculate offset from the module's first segment */
            *offset = addr_val - lowest_base;
            strncpy(module_name, found_name, name_size - 1);
            module_name[name_size - 1] = '\0';
            
            DEBUG_PRINT("[DEBUG] Address %p found in %s, offset=0x%lx (base=0x%lx)\n",
                        addr, module_name, *offset, lowest_base);
            
            return 1; /* Found */
        }
    }
    
    DEBUG_PRINT("[DEBUG] Address %p NOT found in any loaded module\n", addr);
    return 0; /* Not found */
}

static void derive_ndjson_path(const char* json_path, char* out, size_t out_size) {
    if (!json_path || !out || out_size == 0) return;
    snprintf(out, out_size, "%s", json_path);
    char* dot = strrchr(out, '.');
    if (dot && strcmp(dot, ".json") == 0) {
        snprintf(dot, out_size - (size_t)(dot - out), ".ndjson");
    } else {
        size_t used = strlen(out);
        if (used + 7 < out_size) {
            snprintf(out + used, out_size - used, ".ndjson");
        }
    }
}

static void write_ndjson_entry(const char* func_name, void* caller_addr,
                               int found, unsigned long offset, const char* basename,
                               long ts_sec, long ts_nsec,
                               const char** arg_names, const char** arg_values, int arg_count,
                               const char* raw_field_name, const char* raw_field_value) {
    if (!g_ndjson_file) return;

    fprintf(g_ndjson_file, "{");
    fprintf(g_ndjson_file, "\"index\":%d,", g_json_call_index);
    fprintf(g_ndjson_file, "\"timestamp_sec\":%ld,", ts_sec);
    fprintf(g_ndjson_file, "\"timestamp_nsec\":%ld,", ts_nsec);

    char* escaped_func = escape_json_string(func_name);
    fprintf(g_ndjson_file, "\"function\":%s,", escaped_func);
    free_escaped_string(escaped_func);

    fprintf(g_ndjson_file, "\"caller_address\":\"%p\",", caller_addr);
    if (found) {
        fprintf(g_ndjson_file, "\"caller_offset\":\"0x%lx\",", offset);
        char* escaped_module = escape_json_string(basename);
        fprintf(g_ndjson_file, "\"caller_module\":%s,", escaped_module);
        free_escaped_string(escaped_module);
    } else {
        fprintf(g_ndjson_file, "\"caller_offset\":null,");
        fprintf(g_ndjson_file, "\"caller_module\":null,");
    }

    fprintf(g_ndjson_file, "\"arguments\":{");
    for (int i = 0; i < arg_count; i++) {
        char* escaped_name = escape_json_string(arg_names[i]);
        char* escaped_value = escape_json_string(arg_values[i]);
        fprintf(g_ndjson_file, "%s%s:%s", i == 0 ? "" : ",", escaped_name, escaped_value);
        free_escaped_string(escaped_name);
        free_escaped_string(escaped_value);
    }

    if (raw_field_name) {
        char* escaped_name = escape_json_string(raw_field_name);
        fprintf(g_ndjson_file, "%s%s:%s",
                arg_count > 0 ? "," : "",
                escaped_name,
                raw_field_value ? raw_field_value : "null");
        free_escaped_string(escaped_name);
    }

    fprintf(g_ndjson_file, "}}\n");
    fflush(g_ndjson_file);
}

void init_json_logger(const char* json_path) {
    /* Note: parse_proc_maps() will be called lazily on first JNI call */
    /* This ensures target .so is already loaded via dlopen() */
    
    g_json_file = fopen(json_path, "w");
    if (!g_json_file) {
        fprintf(stderr, "Failed to open JSON log file: %s\n", json_path);
        return;
    }

    char ndjson_path[1024];
    derive_ndjson_path(json_path, ndjson_path, sizeof(ndjson_path));
    g_ndjson_file = fopen(ndjson_path, "w");
    if (!g_ndjson_file) {
        fprintf(stderr, "Failed to open NDJSON log file: %s\n", ndjson_path);
    }
    
    /* JSON structure start */
    fprintf(g_json_file, "{\n");
    fprintf(g_json_file, "  \"log_version\": \"1.0\",\n");
    fprintf(g_json_file, "  \"timestamp\": %ld,\n", time(NULL));
    fprintf(g_json_file, "  \"calls\": [\n");
    fflush(g_json_file);
}

char* escape_json_string(const char* str) {
    if (!str) {
        return strdup("null");
    }
    
    size_t len = strlen(str);
    /* Allocate enough space for worst case:
     * - Control chars below 0x20 expand to \uXXXX (6 bytes per char)
     * - Other escapable chars (", \, etc.) expand to 2 bytes per char
     * - 2 surrounding quotes + null terminator = +3
     */
    char* escaped = (char*)malloc(len * 6 + 3);
    if (!escaped) {
        return strdup("null");
    }
    
    char* p = escaped;
    
    *p++ = '\"';
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
            case '\"': 
                *p++ = '\\'; 
                *p++ = '\"'; 
                break;
            case '\\': 
                *p++ = '\\'; 
                *p++ = '\\'; 
                break;
            case '\n': 
                *p++ = '\\'; 
                *p++ = 'n'; 
                break;
            case '\r': 
                *p++ = '\\'; 
                *p++ = 'r'; 
                break;
            case '\t': 
                *p++ = '\\'; 
                *p++ = 't'; 
                break;
            case '\b': 
                *p++ = '\\'; 
                *p++ = 'b'; 
                break;
            case '\f': 
                *p++ = '\\'; 
                *p++ = 'f'; 
                break;
            default: 
                if ((unsigned char)str[i] < 0x20) {
                    /* Control characters */
                    p += sprintf(p, "\\u%04x", (unsigned char)str[i]);
                } else {
                    *p++ = str[i];
                }
        }
    }
    *p++ = '\"';
    *p = '\0';
    
    return escaped;
}

void free_escaped_string(char* str) {
    if (str) {
        free(str);
    }
}

__attribute__((noinline))
void log_jni_call_json(const char* func_name, const char** arg_names,
                       const char** arg_values, int arg_count) {
    if (!g_json_file) return;

    /*
     * Multiple JNI stubs can reach the logger back-to-back from different
     * threads. Emit each JSON entry while holding the FILE lock so one entry
     * cannot be interleaved into the middle of another.
     */
    flockfile(g_json_file);
    
    /* Lazy loading: parse /proc/self/maps on first call (after dlopen) */
    if (!g_maps_parsed) {
        parse_proc_maps();
    }
    
    /* Get caller address from the .so file that called the JNI function */
    void* caller_addr = __builtin_return_address(1);
    
    /* Find module and calculate offset */
    unsigned long offset = 0;
    char module_name[256] = "unknown";
    int found = find_module_offset(caller_addr, &offset, module_name, sizeof(module_name));
    
    /* Extract just the filename from full path */
    char* basename = strrchr(module_name, '/');
    if (basename) {
        basename++; /* Skip the '/' */
    } else {
        basename = module_name;
    }
    
    /* Comma handling (skip for first entry) */
    if (!g_first_entry) {
        fprintf(g_json_file, ",\n");
    }
    g_first_entry = 0;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    fprintf(g_json_file, "    {\n");
    fprintf(g_json_file, "      \"index\": %d,\n", g_json_call_index);
    fprintf(g_json_file, "      \"timestamp_sec\": %ld,\n", ts.tv_sec);
    fprintf(g_json_file, "      \"timestamp_nsec\": %ld,\n", ts.tv_nsec);
    fprintf(g_json_file, "      \"function\": \"%s\",\n", func_name);
    fprintf(g_json_file, "      \"caller_address\": \"%p\",\n", caller_addr);
    
    if (found) {
        fprintf(g_json_file, "      \"caller_offset\": \"0x%lx\",\n", offset);
        fprintf(g_json_file, "      \"caller_module\": \"%s\",\n", basename);
    } else {
        fprintf(g_json_file, "      \"caller_offset\": null,\n");
        fprintf(g_json_file, "      \"caller_module\": null,\n");
    }
    
    fprintf(g_json_file, "      \"arguments\": {\n");
    
    for (int i = 0; i < arg_count; i++) {
        char* escaped_value = escape_json_string(arg_values[i]);
        fprintf(g_json_file, "        \"%s\": %s", arg_names[i], escaped_value);
        if (i < arg_count - 1) {
            fprintf(g_json_file, ",\n");
        } else {
            fprintf(g_json_file, "\n");
        }
        free_escaped_string(escaped_value);
    }
    
    fprintf(g_json_file, "      }\n");
    fprintf(g_json_file, "    }");
    write_ndjson_entry(func_name, caller_addr, found, offset, basename,
                       ts.tv_sec, ts.tv_nsec, arg_names, arg_values, arg_count,
                       NULL, NULL);
    g_json_call_index++;
    fflush(g_json_file);
    funlockfile(g_json_file);
}

__attribute__((noinline))
void log_jni_call_json_raw_last(const char* func_name,
                                const char** arg_names,
                                const char** arg_values, int arg_count,
                                const char* raw_field_name,
                                const char* raw_field_value) {
    if (!g_json_file) return;

    /*
     * Keep raw-field entries atomic as well; RegisterNatives can produce a
     * fairly large JSON payload and is especially sensitive to interleaving.
     */
    flockfile(g_json_file);

    if (!g_maps_parsed) {
        parse_proc_maps();
    }

    void* caller_addr = __builtin_return_address(1);

    unsigned long offset = 0;
    char module_name[256] = "unknown";
    int found = find_module_offset(caller_addr, &offset, module_name, sizeof(module_name));

    char* basename = strrchr(module_name, '/');
    if (basename) {
        basename++;
    } else {
        basename = module_name;
    }

    if (!g_first_entry) {
        fprintf(g_json_file, ",\n");
    }
    g_first_entry = 0;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    fprintf(g_json_file, "    {\n");
    fprintf(g_json_file, "      \"index\": %d,\n", g_json_call_index);
    fprintf(g_json_file, "      \"timestamp_sec\": %ld,\n", ts.tv_sec);
    fprintf(g_json_file, "      \"timestamp_nsec\": %ld,\n", ts.tv_nsec);
    fprintf(g_json_file, "      \"function\": \"%s\",\n", func_name);
    fprintf(g_json_file, "      \"caller_address\": \"%p\",\n", caller_addr);

    if (found) {
        fprintf(g_json_file, "      \"caller_offset\": \"0x%lx\",\n", offset);
        fprintf(g_json_file, "      \"caller_module\": \"%s\",\n", basename);
    } else {
        fprintf(g_json_file, "      \"caller_offset\": null,\n");
        fprintf(g_json_file, "      \"caller_module\": null,\n");
    }

    fprintf(g_json_file, "      \"arguments\": {\n");

    /* Standard string-escaped arguments */
    for (int i = 0; i < arg_count; i++) {
        char* escaped_value = escape_json_string(arg_values[i]);
        fprintf(g_json_file, "        \"%s\": %s,\n", arg_names[i], escaped_value);
        free_escaped_string(escaped_value);
    }

    /* Last field as raw JSON (array / sub-object) — no escaping */
    fprintf(g_json_file, "        \"%s\": %s\n",
            raw_field_name,
            raw_field_value ? raw_field_value : "null");

    fprintf(g_json_file, "      }\n");
    fprintf(g_json_file, "    }");
    write_ndjson_entry(func_name, caller_addr, found, offset, basename,
                       ts.tv_sec, ts.tv_nsec, arg_names, arg_values, arg_count,
                       raw_field_name, raw_field_value);
    g_json_call_index++;
    fflush(g_json_file);
    funlockfile(g_json_file);
}

void close_json_logger(void) {
    if (g_json_file) {
        fprintf(g_json_file, "\n  ],\n");
        fprintf(g_json_file, "  \"total_calls\": %d\n", g_json_call_index);
        fprintf(g_json_file, "}\n");
        fclose(g_json_file);
        g_json_file = NULL;
    }
    if (g_ndjson_file) {
        fclose(g_ndjson_file);
        g_ndjson_file = NULL;
    }
}
