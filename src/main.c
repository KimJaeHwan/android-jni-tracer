/*
 * Main Harness Program
 * Loads target SO file and executes JNI_OnLoad with fake JNIEnv
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#ifndef _WIN32
#include <dlfcn.h>
#else
/* Windows does not support dlopen - must use WSL */
#error "This harness requires Linux/Unix dlopen(). Please use WSL2 on Windows."
#endif

#include "../include/jni.h"
#include "fake_jni.h"
#include "jni_logger.h"
#include "json_logger.h"
#include "mock_config.h"

/* JNI_OnLoad function pointer type */
typedef jint (*JNI_OnLoad_t)(JavaVM* vm, void* reserved);

/* Fake JavaVM structure (minimal implementation) */
typedef struct {
    const struct JNIInvokeInterface* functions;
} FakeJavaVM;

#define MAX_INVOKE_ARGS 16
#define MAX_INVOKE_STEPS 64

typedef enum {
    INVOKE_ARG_BOOL,
    INVOKE_ARG_INT,
    INVOKE_ARG_LONG,
    INVOKE_ARG_FLOAT,
    INVOKE_ARG_DOUBLE,
    INVOKE_ARG_STRING,
    INVOKE_ARG_OBJECT
} InvokeArgType;

typedef struct {
    InvokeArgType type;
    char text[512];
    union {
        jboolean z;
        jint i;
        jlong j;
        jfloat f;
        jdouble d;
        jobject l;
    } value;
} InvokeArg;

typedef struct {
    char class_name[256];
    char method_name[128];
    char signature[256];
} InvokeTarget;

typedef struct {
    char target_text[768];
    InvokeTarget target;
    InvokeArg args[MAX_INVOKE_ARGS];
    int argc;
} InvokeStep;

static int g_current_invoke_step = -1;

/* Fake JavaVM function implementations */
static jint JNICALL fake_DestroyJavaVM(JavaVM* vm) {
    (void)vm;
    return JNI_OK;
}

static jint JNICALL fake_AttachCurrentThread(JavaVM* vm, JNIEnv** p_env, void* thr_args) {
    (void)vm;
    (void)thr_args;
    if (p_env) {
        *p_env = create_fake_jnienv();
    }
    return JNI_OK;
}

static jint JNICALL fake_DetachCurrentThread(JavaVM* vm) {
    (void)vm;
    return JNI_OK;
}

static jint JNICALL fake_GetEnv(JavaVM* vm, void** env, jint version) {
    (void)vm;
    (void)version;
    if (env) {
        *env = create_fake_jnienv();
    }
    return JNI_OK;
}

static jint JNICALL fake_AttachCurrentThreadAsDaemon(JavaVM* vm, JNIEnv** p_env, void* thr_args) {
    (void)vm;
    (void)thr_args;
    if (p_env) {
        *p_env = create_fake_jnienv();
    }
    return JNI_OK;
}

/* JavaVM function table */
static struct JNIInvokeInterface fake_vm_funcs = {
    NULL, NULL, NULL, /* reserved0-2 */
    fake_DestroyJavaVM,
    fake_AttachCurrentThread,
    fake_DetachCurrentThread,
    fake_GetEnv,
    fake_AttachCurrentThreadAsDaemon
};

static int parse_long_long(const char* text, long long* out) {
    char* end = NULL;
    errno = 0;
    long long value = strtoll(text, &end, 0);
    if (errno != 0 || !end || *end != '\0') return -1;
    *out = value;
    return 0;
}

static int parse_double_value(const char* text, double* out) {
    char* end = NULL;
    errno = 0;
    double value = strtod(text, &end);
    if (errno != 0 || !end || *end != '\0') return -1;
    *out = value;
    return 0;
}

static const char* json_skip_ws(const char* p) {
    while (p && *p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    return p;
}

static int json_expect_char(const char** pp, char c) {
    const char* p = json_skip_ws(*pp);
    if (!p || *p != c) return -1;
    *pp = p + 1;
    return 0;
}

static int json_parse_string(const char** pp, char* out, size_t out_size) {
    const char* p = json_skip_ws(*pp);
    if (!p || *p != '"') return -1;
    p++;

    size_t n = 0;
    while (*p && *p != '"') {
        char ch = *p++;
        if (ch == '\\' && *p) {
            char esc = *p++;
            switch (esc) {
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/': ch = '/'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                default: ch = esc; break;
            }
        }
        if (n + 1 < out_size) out[n++] = ch;
    }
    if (*p != '"') return -1;
    if (out_size > 0) out[n] = '\0';
    *pp = p + 1;
    return 0;
}

static const char* json_skip_value(const char* p) {
    p = json_skip_ws(p);
    if (!p || !*p) return p;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) p++;
            p++;
        }
        return *p == '"' ? p + 1 : p;
    }
    if (*p == '{' || *p == '[') {
        char open = *p;
        char close = open == '{' ? '}' : ']';
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                p = json_skip_value(p);
            } else if (*p == open) {
                depth++;
                p++;
            } else if (*p == close) {
                depth--;
                p++;
            } else {
                p++;
            }
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}

static char* read_text_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static int parse_invoke_arg(const char* spec, InvokeArg* out) {
    const char* colon = strchr(spec, ':');
    if (!colon || colon == spec) return -1;

    size_t type_len = (size_t)(colon - spec);
    const char* value = colon + 1;
    char type[32];
    if (type_len >= sizeof(type)) return -1;
    memcpy(type, spec, type_len);
    type[type_len] = '\0';

    snprintf(out->text, sizeof(out->text), "%s", value);

    if (strcmp(type, "bool") == 0 || strcmp(type, "boolean") == 0) {
        out->type = INVOKE_ARG_BOOL;
        out->value.z = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) ? JNI_TRUE : JNI_FALSE;
        return 0;
    }
    if (strcmp(type, "int") == 0) {
        long long parsed = 0;
        if (parse_long_long(value, &parsed) != 0) return -1;
        out->type = INVOKE_ARG_INT;
        out->value.i = (jint)parsed;
        return 0;
    }
    if (strcmp(type, "long") == 0) {
        long long parsed = 0;
        if (parse_long_long(value, &parsed) != 0) return -1;
        out->type = INVOKE_ARG_LONG;
        out->value.j = (jlong)parsed;
        return 0;
    }
    if (strcmp(type, "float") == 0) {
        double parsed = 0.0;
        if (parse_double_value(value, &parsed) != 0) return -1;
        out->type = INVOKE_ARG_FLOAT;
        out->value.f = (jfloat)parsed;
        return 0;
    }
    if (strcmp(type, "double") == 0) {
        double parsed = 0.0;
        if (parse_double_value(value, &parsed) != 0) return -1;
        out->type = INVOKE_ARG_DOUBLE;
        out->value.d = (jdouble)parsed;
        return 0;
    }
    if (strcmp(type, "string") == 0) {
        out->type = INVOKE_ARG_STRING;
        out->value.l = NULL;
        return 0;
    }
    if (strcmp(type, "object") == 0) {
        long long parsed = 0;
        if (strcmp(value, "null") == 0 || strcmp(value, "NULL") == 0) {
            parsed = 0;
        } else if (parse_long_long(value, &parsed) != 0) {
            return -1;
        }
        out->type = INVOKE_ARG_OBJECT;
        out->value.l = (jobject)(uintptr_t)parsed;
        return 0;
    }

    return -1;
}

static int parse_invoke_target(const char* text, InvokeTarget* out) {
    const char* sig = strchr(text, '(');
    if (!sig) return -1;

    const char* dot = NULL;
    for (const char* p = sig; p > text; p--) {
        if (*(p - 1) == '.') {
            dot = p - 1;
            break;
        }
    }
    if (!dot || dot == text || dot + 1 >= sig) return -1;

    size_t class_len = (size_t)(dot - text);
    size_t method_len = (size_t)(sig - dot - 1);
    if (class_len >= sizeof(out->class_name) ||
        method_len >= sizeof(out->method_name) ||
        strlen(sig) >= sizeof(out->signature)) {
        return -1;
    }

    memcpy(out->class_name, text, class_len);
    out->class_name[class_len] = '\0';
    memcpy(out->method_name, dot + 1, method_len);
    out->method_name[method_len] = '\0';
    snprintf(out->signature, sizeof(out->signature), "%s", sig);
    return 0;
}

static void log_invoke_json(const char* class_name, const char* method_name,
                            const char* signature, const void* fnPtr,
                            const char* status, const char* return_type,
                            const char* return_value, const char* reason) {
    const char* arg_names[] = {
        "class_name", "method_name", "sig", "fnPtr",
        "status", "return_type", "return_value", "reason", "sequence_step"
    };
    char arg_values[9][512];
    snprintf(arg_values[0], sizeof(arg_values[0]), "%s", class_name ? class_name : "");
    snprintf(arg_values[1], sizeof(arg_values[1]), "%s", method_name ? method_name : "");
    snprintf(arg_values[2], sizeof(arg_values[2]), "%s", signature ? signature : "");
    snprintf(arg_values[3], sizeof(arg_values[3]), "%p", fnPtr);
    snprintf(arg_values[4], sizeof(arg_values[4]), "%s", status ? status : "");
    snprintf(arg_values[5], sizeof(arg_values[5]), "%s", return_type ? return_type : "");
    snprintf(arg_values[6], sizeof(arg_values[6]), "%s", return_value ? return_value : "");
    snprintf(arg_values[7], sizeof(arg_values[7]), "%s", reason ? reason : "");
    snprintf(arg_values[8], sizeof(arg_values[8]), "%d", g_current_invoke_step);
    const char* arg_value_ptrs[] = {
        arg_values[0], arg_values[1], arg_values[2], arg_values[3],
        arg_values[4], arg_values[5], arg_values[6], arg_values[7], arg_values[8]
    };
    log_jni_call_json("InvokeNative", arg_names, arg_value_ptrs, 9);
}

static void log_invoke_start_json(const NativeRegistryEntry* entry) {
    const char* arg_names[] = {
        "class_name", "method_name", "sig", "fnPtr", "sequence_step"
    };
    char arg_values[5][512];
    snprintf(arg_values[0], sizeof(arg_values[0]), "%s", entry->class_name);
    snprintf(arg_values[1], sizeof(arg_values[1]), "%s", entry->method_name);
    snprintf(arg_values[2], sizeof(arg_values[2]), "%s", entry->signature);
    snprintf(arg_values[3], sizeof(arg_values[3]), "%p", entry->fnPtr);
    snprintf(arg_values[4], sizeof(arg_values[4]), "%d", g_current_invoke_step);
    const char* arg_value_ptrs[] = {
        arg_values[0], arg_values[1], arg_values[2], arg_values[3], arg_values[4]
    };
    log_jni_call_json("InvokeNativeStart", arg_names, arg_value_ptrs, 5);
}

static int require_arg_count(const InvokeTarget* target, int actual, int expected) {
    if (actual == expected) return 0;
    log_warning("Invoke argument count mismatch for %s.%s%s: expected %d, got %d",
                target->class_name, target->method_name, target->signature, expected, actual);
    return -1;
}

static int invoke_registered_native(JNIEnv* env, const InvokeTarget* target,
                                    InvokeArg* args, int argc);

static int parse_invoke_plan(const char* path, InvokeStep* steps, int* step_count) {
    char* text = read_text_file(path);
    if (!text) {
        fprintf(stderr, "Failed to read invoke plan: %s\n", path);
        return -1;
    }

    const char* p = text;
    int count = 0;

    if (json_expect_char(&p, '[') != 0) {
        fprintf(stderr, "Invoke plan must be a JSON array: %s\n", path);
        free(text);
        return -1;
    }

    while (1) {
        p = json_skip_ws(p);
        if (!*p) break;
        if (*p == ']') {
            p++;
            break;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (count >= MAX_INVOKE_STEPS) {
            fprintf(stderr, "Too many invoke plan steps; max is %d\n", MAX_INVOKE_STEPS);
            free(text);
            return -1;
        }
        if (json_expect_char(&p, '{') != 0) {
            fprintf(stderr, "Invoke plan step must be an object\n");
            free(text);
            return -1;
        }

        InvokeStep* step = &steps[count];
        memset(step, 0, sizeof(*step));

        while (1) {
            p = json_skip_ws(p);
            if (!*p) break;
            if (*p == '}') {
                p++;
                break;
            }
            if (*p == ',') {
                p++;
                continue;
            }

            char key[64] = {0};
            if (json_parse_string(&p, key, sizeof(key)) != 0 ||
                json_expect_char(&p, ':') != 0) {
                fprintf(stderr, "Invalid key/value in invoke plan step %d\n", count);
                free(text);
                return -1;
            }

            if (strcmp(key, "target") == 0) {
                if (json_parse_string(&p, step->target_text, sizeof(step->target_text)) != 0) {
                    fprintf(stderr, "Invalid target string in invoke plan step %d\n", count);
                    free(text);
                    return -1;
                }
            } else if (strcmp(key, "args") == 0) {
                if (json_expect_char(&p, '[') != 0) {
                    fprintf(stderr, "args must be an array in invoke plan step %d\n", count);
                    free(text);
                    return -1;
                }
                while (1) {
                    p = json_skip_ws(p);
                    if (!*p) break;
                    if (*p == ']') {
                        p++;
                        break;
                    }
                    if (*p == ',') {
                        p++;
                        continue;
                    }
                    if (step->argc >= MAX_INVOKE_ARGS) {
                        fprintf(stderr, "Too many args in invoke plan step %d; max is %d\n", count, MAX_INVOKE_ARGS);
                        free(text);
                        return -1;
                    }
                    char arg_spec[768] = {0};
                    if (json_parse_string(&p, arg_spec, sizeof(arg_spec)) != 0 ||
                        parse_invoke_arg(arg_spec, &step->args[step->argc]) != 0) {
                        fprintf(stderr, "Invalid arg in invoke plan step %d\n", count);
                        free(text);
                        return -1;
                    }
                    step->argc++;
                }
            } else {
                p = json_skip_value(p);
            }
        }

        if (step->target_text[0] == '\0' ||
            parse_invoke_target(step->target_text, &step->target) != 0) {
            fprintf(stderr, "Invalid or missing target in invoke plan step %d\n", count);
            free(text);
            return -1;
        }

        count++;
    }

    free(text);
    *step_count = count;
    return 0;
}

static int run_invoke_plan(JNIEnv* env, const char* plan_path) {
    InvokeStep steps[MAX_INVOKE_STEPS];
    int step_count = 0;
    if (parse_invoke_plan(plan_path, steps, &step_count) != 0) return -1;

    log_info("Executing invoke plan: %s (%d steps)", plan_path, step_count);
    int failures = 0;
    for (int i = 0; i < step_count; i++) {
        g_current_invoke_step = i;
        log_info("Invoke plan step %d/%d: %s", i + 1, step_count, steps[i].target_text);
        if (invoke_registered_native(env, &steps[i].target, steps[i].args, steps[i].argc) != 0) {
            failures++;
        }
    }
    g_current_invoke_step = -1;

    if (failures > 0) {
        log_warning("Invoke plan completed with %d failure(s)", failures);
        return -1;
    }
    log_info("Invoke plan completed successfully");
    return 0;
}

static int invoke_registered_native(JNIEnv* env, const InvokeTarget* target,
                                    InvokeArg* args, int argc) {
    const NativeRegistryEntry* entry = native_registry_find(target->class_name,
                                                            target->method_name,
                                                            target->signature);
    if (!entry) {
        log_warning("Invoke target not found: %s.%s%s",
                    target->class_name, target->method_name, target->signature);
        log_invoke_json(target->class_name, target->method_name, target->signature,
                        NULL, "not_found", "", "", "RegisterNatives entry not found");
        return -1;
    }

    jclass clazz = fake_jni_find_class(entry->class_name);
    log_info("Invoking native: %s.%s%s fnPtr=%p",
             entry->class_name, entry->method_name, entry->signature, entry->fnPtr);
    log_invoke_start_json(entry);

    if (strcmp(entry->signature, "()V") == 0) {
        if (require_arg_count(target, argc, 0) != 0) return -1;
        typedef void (*Fn)(JNIEnv*, jclass);
        ((Fn)entry->fnPtr)(env, clazz);
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "void", "void", "");
        return 0;
    }
    if (strcmp(entry->signature, "()I") == 0) {
        if (require_arg_count(target, argc, 0) != 0) return -1;
        typedef jint (*Fn)(JNIEnv*, jclass);
        jint ret = ((Fn)entry->fnPtr)(env, clazz);
        char ret_text[64];
        snprintf(ret_text, sizeof(ret_text), "%d", ret);
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "int", ret_text, "");
        return 0;
    }
    if (strcmp(entry->signature, "(IIIII)V") == 0) {
        if (require_arg_count(target, argc, 5) != 0) return -1;
        typedef void (*Fn)(JNIEnv*, jclass, jint, jint, jint, jint, jint);
        ((Fn)entry->fnPtr)(env, clazz,
                           args[0].value.i, args[1].value.i, args[2].value.i,
                           args[3].value.i, args[4].value.i);
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "void", "void", "");
        return 0;
    }
    if (strcmp(entry->signature, "(JIFFFF)V") == 0) {
        if (require_arg_count(target, argc, 6) != 0) return -1;
        typedef void (*Fn)(JNIEnv*, jclass, jlong, jint, jfloat, jfloat, jfloat, jfloat);
        ((Fn)entry->fnPtr)(env, clazz,
                           args[0].value.j, args[1].value.i, args[2].value.f,
                           args[3].value.f, args[4].value.f, args[5].value.f);
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "void", "void", "");
        return 0;
    }
    if (strcmp(entry->signature, "(JIII)V") == 0) {
        if (require_arg_count(target, argc, 4) != 0) return -1;
        typedef void (*Fn)(JNIEnv*, jclass, jlong, jint, jint, jint);
        ((Fn)entry->fnPtr)(env, clazz, args[0].value.j,
                           args[1].value.i, args[2].value.i, args[3].value.i);
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "void", "void", "");
        return 0;
    }
    if (strcmp(entry->signature, "(IILjava/lang/String;)Ljava/lang/String;") == 0) {
        if (require_arg_count(target, argc, 3) != 0) return -1;
        typedef jstring (*Fn)(JNIEnv*, jclass, jint, jint, jstring);
        jstring s0 = fake_jni_new_string_utf(args[2].text);
        jstring ret = ((Fn)entry->fnPtr)(env, clazz, args[0].value.i, args[1].value.i, s0);
        char ret_text[64];
        snprintf(ret_text, sizeof(ret_text), "%p", (void*)ret);
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "jstring", ret_text, "");
        return 0;
    }
    if (strcmp(entry->signature, "(Ljava/lang/String;)V") == 0) {
        if (require_arg_count(target, argc, 1) != 0) return -1;
        typedef void (*Fn)(JNIEnv*, jclass, jstring);
        ((Fn)entry->fnPtr)(env, clazz, fake_jni_new_string_utf(args[0].text));
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "void", "void", "");
        return 0;
    }
    if (strcmp(entry->signature, "(Ljava/lang/String;Ljava/lang/String;)V") == 0) {
        if (require_arg_count(target, argc, 2) != 0) return -1;
        typedef void (*Fn)(JNIEnv*, jclass, jstring, jstring);
        ((Fn)entry->fnPtr)(env, clazz,
                           fake_jni_new_string_utf(args[0].text),
                           fake_jni_new_string_utf(args[1].text));
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "void", "void", "");
        return 0;
    }
    if (strcmp(entry->signature, "(JIII)I") == 0) {
        if (require_arg_count(target, argc, 4) != 0) return -1;
        typedef jint (*Fn)(JNIEnv*, jclass, jlong, jint, jint, jint);
        jint ret = ((Fn)entry->fnPtr)(env, clazz, args[0].value.j,
                                      args[1].value.i, args[2].value.i, args[3].value.i);
        char ret_text[64];
        snprintf(ret_text, sizeof(ret_text), "%d", ret);
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "int", ret_text, "");
        return 0;
    }
    if (strcmp(entry->signature, "(JLjava/lang/String;)I") == 0) {
        if (require_arg_count(target, argc, 2) != 0) return -1;
        typedef jint (*Fn)(JNIEnv*, jclass, jlong, jstring);
        jint ret = ((Fn)entry->fnPtr)(env, clazz, args[0].value.j,
                                      fake_jni_new_string_utf(args[1].text));
        char ret_text[64];
        snprintf(ret_text, sizeof(ret_text), "%d", ret);
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "int", ret_text, "");
        return 0;
    }
    if (strcmp(entry->signature, "(JI)Z") == 0) {
        if (require_arg_count(target, argc, 2) != 0) return -1;
        typedef jboolean (*Fn)(JNIEnv*, jclass, jlong, jint);
        jboolean ret = ((Fn)entry->fnPtr)(env, clazz, args[0].value.j, args[1].value.i);
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "boolean", ret ? "true" : "false", "");
        return 0;
    }
    if (strcmp(entry->signature, "(JI)Ljava/lang/String;") == 0) {
        if (require_arg_count(target, argc, 2) != 0) return -1;
        typedef jstring (*Fn)(JNIEnv*, jclass, jlong, jint);
        jstring ret = ((Fn)entry->fnPtr)(env, clazz, args[0].value.j, args[1].value.i);
        char ret_text[64];
        snprintf(ret_text, sizeof(ret_text), "%p", (void*)ret);
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "jstring", ret_text, "");
        return 0;
    }
    if (strcmp(entry->signature, "(JI)J") == 0) {
        if (require_arg_count(target, argc, 2) != 0) return -1;
        typedef jlong (*Fn)(JNIEnv*, jclass, jlong, jint);
        jlong ret = ((Fn)entry->fnPtr)(env, clazz, args[0].value.j, args[1].value.i);
        char ret_text[64];
        snprintf(ret_text, sizeof(ret_text), "%lld", (long long)ret);
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "long", ret_text, "");
        return 0;
    }
    if (strcmp(entry->signature, "(JI)D") == 0) {
        if (require_arg_count(target, argc, 2) != 0) return -1;
        typedef jdouble (*Fn)(JNIEnv*, jclass, jlong, jint);
        jdouble ret = ((Fn)entry->fnPtr)(env, clazz, args[0].value.j, args[1].value.i);
        char ret_text[64];
        snprintf(ret_text, sizeof(ret_text), "%f", ret);
        log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                        entry->fnPtr, "called", "double", ret_text, "");
        return 0;
    }

    log_warning("Unsupported invoke signature: %s.%s%s",
                entry->class_name, entry->method_name, entry->signature);
    log_invoke_json(entry->class_name, entry->method_name, entry->signature,
                    entry->fnPtr, "unsupported_signature", "", "",
                    "typed dispatcher not implemented");
    return -1;
}

int main(int argc, char* argv[]) {
    const char* so_path   = NULL;
    const char* mock_path = NULL;
    const char* invoke_target_text = NULL;
    const char* invoke_plan_path = NULL;
    InvokeArg invoke_args[MAX_INVOKE_ARGS];
    int invoke_arg_count = 0;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--mock") == 0 || strcmp(argv[i], "-m") == 0) && i + 1 < argc) {
            mock_path = argv[++i];
        } else if (strcmp(argv[i], "--invoke") == 0 && i + 1 < argc) {
            invoke_target_text = argv[++i];
        } else if (strcmp(argv[i], "--invoke-plan") == 0 && i + 1 < argc) {
            invoke_plan_path = argv[++i];
        } else if (strcmp(argv[i], "--arg") == 0 && i + 1 < argc) {
            if (invoke_arg_count >= MAX_INVOKE_ARGS) {
                fprintf(stderr, "Too many --arg values; max is %d\n", MAX_INVOKE_ARGS);
                return 1;
            }
            if (parse_invoke_arg(argv[++i], &invoke_args[invoke_arg_count]) != 0) {
                fprintf(stderr, "Invalid --arg value: %s\n", argv[i]);
                fprintf(stderr, "Expected format: --arg int:123, long:1, float:1.0, string:text, bool:true\n");
                return 1;
            }
            invoke_arg_count++;
        } else if (argv[i][0] != '-') {
            so_path = argv[i];
        }
    }

    if (!so_path) {
        fprintf(stderr, "Usage: %s [--mock <config.json>] [--invoke <class.method(sig)> --arg <type:value>...] [--invoke-plan <plan.json>] <target_so_path>\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  --mock, -m <file>  Load mock return-value config (JSON)\n");
        fprintf(stderr, "  --invoke <target>  Invoke a RegisterNatives entry after JNI_OnLoad\n");
        fprintf(stderr, "  --arg <type:value> Add one invoke argument. Types: bool,int,long,float,double,string,object\n");
        fprintf(stderr, "  --invoke-plan <file> Execute a JSON array of invoke steps after JNI_OnLoad\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Description:\n");
        fprintf(stderr, "  JNI Function Hooking Harness for Android SO files\n");
        fprintf(stderr, "  Logs all JNI function calls to text and JSON formats\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s target/libnative.so\n", argv[0]);
        fprintf(stderr, "  %s --mock mock.json target/libnative.so\n", argv[0]);
        fprintf(stderr, "  %s target/libnative.so --invoke 'com/example/Foo.bar(II)V' --arg int:1 --arg int:2\n", argv[0]);
        fprintf(stderr, "  %s target/libnative.so --invoke-plan invoke_plan.json\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Output Files:\n");
        fprintf(stderr, "  logs/jni_hook.log  - Text format log\n");
        fprintf(stderr, "  logs/jni_hook.json - JSON format log\n");
        fprintf(stderr, "\n");
        return 1;
    }
    
    /* Initialize loggers */
    init_logger("./logs/jni_hook.log");
    init_json_logger("./logs/jni_hook.json");

    /* Load mock config if provided */
    if (mock_path) {
        if (mock_load(mock_path) == 0) {
            log_info("Mock config loaded: %s (%d entries)", mock_path, mock_count());
        } else {
            log_warning("Failed to load mock config: %s", mock_path);
        }
    } else {
        log_info("No mock config (use --mock <file> to inject return values)");
    }

    log_info("=== JNI Harness Started ===");
    log_info("Build Date: %s %s", __DATE__, __TIME__);
    
    /* Display architecture info */
#if defined(__aarch64__) || defined(__arm64__)
    log_info("Harness Architecture: ARM64 (aarch64)");
#elif defined(__x86_64__) || defined(__amd64__)
    log_info("Harness Architecture: x86_64 (amd64)");
#elif defined(__i386__) || defined(__i686__)
    log_info("Harness Architecture: x86 (i386)");
#elif defined(__arm__)
    log_info("Harness Architecture: ARM (32-bit)");
#else
    log_info("Harness Architecture: Unknown");
#endif
    
    log_info("Target SO: %s", so_path);

    FakeJavaVM fake_vm;
    fake_vm.functions = &fake_vm_funcs;
    set_fake_javavm((JavaVM*)&fake_vm);
    JNIEnv* fake_env = create_fake_jnienv();

    /* Step 1: Load SO file */
    log_info("Loading SO file with dlopen()...");
    void* handle = dlopen(so_path, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        log_error("dlopen failed: %s", dlerror());
        log_error("\n");
        log_error("Common causes:");
        log_error("  1. File not found - Check the path");
        log_error("  2. Architecture mismatch - Use 'file %s' to check", so_path);
        log_error("  3. Missing dependencies - Check with 'ldd %s'", so_path);
        log_error("\n");
        log_error("Architecture must match:");
        log_error("  - ARM64 harness requires ARM64 SO");
        log_error("  - x86_64 harness requires x86_64 SO");
        log_error("\n");
        
        close_json_logger();
        close_logger();
        return 1;
    }
    log_info("SO loaded successfully");
    log_info(".init_array functions executed automatically");

    /* Step 2: Find JNI_OnLoad symbol */
    log_info("Searching for JNI_OnLoad symbol...");
    JNI_OnLoad_t jni_onload = (JNI_OnLoad_t)dlsym(handle, "JNI_OnLoad");
    if (!jni_onload) {
        log_warning("JNI_OnLoad not found in SO");
        log_info("This SO may not have JNI initialization");
        log_info("Only .init_array constructors were executed");
        log_info("Skipping JNI_OnLoad call");
    } else {
        log_info("JNI_OnLoad found at %p", (void*)jni_onload);

        log_info("Fake JavaVM created");
        
        log_info("Calling JNI_OnLoad with fake JavaVM and JNIEnv...");
        log_info(""); /* Empty line for readability */
        
        /* Step 5: Call JNI_OnLoad */
        jint version = jni_onload((JavaVM*)&fake_vm, NULL);
        
        log_info(""); /* Empty line after JNI calls */
        log_info("JNI_OnLoad returned version: 0x%x", version);
        
        /* Decode version */
        int major = (version >> 16) & 0xFFFF;
        int minor = version & 0xFFFF;
        log_info("Version decoded: %d.%d", major, minor);
        
        switch (version) {
            case JNI_VERSION_1_1:
                log_info("JNI Version: 1.1");
                break;
            case JNI_VERSION_1_2:
                log_info("JNI Version: 1.2");
                break;
            case JNI_VERSION_1_4:
                log_info("JNI Version: 1.4");
                break;
            case JNI_VERSION_1_6:
                log_info("JNI Version: 1.6 (Android standard)");
                break;
            case JNI_VERSION_1_8:
                log_info("JNI Version: 1.8");
                break;
            default:
                log_warning("Unknown JNI version: 0x%x", version);
                break;
        }
    }

    if (invoke_target_text) {
        InvokeTarget invoke_target;
        if (parse_invoke_target(invoke_target_text, &invoke_target) != 0) {
            log_error("Invalid --invoke target: %s", invoke_target_text);
            log_error("Expected format: com/example/Class.method(II)V");
        } else {
            log_info("");
            log_info("Executing requested invoke target...");
            invoke_registered_native(fake_env, &invoke_target, invoke_args, invoke_arg_count);
        }
    }

    if (invoke_plan_path) {
        log_info("");
        run_invoke_plan(fake_env, invoke_plan_path);
    }

    /* Step 7: Print log summary */
    log_info(""); /* Empty line */
    print_log_summary();

    /* Step 8: Cleanup */
    log_info(""); /* Empty line */
    log_info("=== JNI Harness Finished ===");
    log_info("Check output files:");
    log_info("  - logs/jni_hook.log  (detailed text log)");
    log_info("  - logs/jni_hook.json (structured JSON log)");

    mock_destroy();
    destroy_fake_jnienv(NULL); /* Release string pool and other resources */
    close_json_logger();
    close_logger();

    /* Close SO handle */
    dlclose(handle);

    return 0;
}
