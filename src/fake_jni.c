/*
 * Fake JNI Environment Implementation
 * Provides stub implementations for all JNI functions
 * Logs function calls with detailed arguments
 */

#include "fake_jni.h"
#include "jni_logger.h"
#include "json_logger.h"
#include "mock_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ============================================
 * CLASS NAME MAPPING TABLE
 * Maps jclass pointers to class names
 * ============================================ */

#define MAX_CLASSES 256

typedef struct {
    jclass ptr;
    char name[256];
} ClassEntry;

static ClassEntry class_table[MAX_CLASSES];
static int class_count = 0;
static uintptr_t next_class_id = 0x10000;

/* Fake JavaVM pointer registered from main.c */
static JavaVM* g_fake_javavm = NULL;

void set_fake_javavm(JavaVM* vm) {
    g_fake_javavm = vm;
}

/* Register a new class and return unique jclass pointer */
static jclass register_class(const char* name) {
    if (class_count >= MAX_CLASSES) {
        return (jclass)0xFFFF; // overflow
    }
    
    // Check if already registered
    for (int i = 0; i < class_count; i++) {
        if (strcmp(class_table[i].name, name) == 0) {
            return class_table[i].ptr;
        }
    }
    
    // Register new class
    jclass new_class = (jclass)(next_class_id);
    next_class_id += 0x1000;
    
    class_table[class_count].ptr = new_class;
    strncpy(class_table[class_count].name, name, 255);
    class_table[class_count].name[255] = '\0';
    class_count++;
    
    return new_class;
}

/* Get class name from jclass pointer */
static const char* get_class_name(jclass clazz) {
    for (int i = 0; i < class_count; i++) {
        if (class_table[i].ptr == clazz) {
            return class_table[i].name;
        }
    }
    return "Unknown";
}

jclass fake_jni_find_class(const char* class_name) {
    return register_class(class_name ? class_name : "Unknown");
}

/* ============================================
 * NATIVE REGISTRY
 * Captures RegisterNatives entries so the harness can invoke a registered
 * native function by Java class/name/signature after JNI_OnLoad completes.
 * ============================================ */

#define MAX_NATIVE_REGISTRY 1024

static NativeRegistryEntry native_registry[MAX_NATIVE_REGISTRY];
static int native_registry_count = 0;

static void native_registry_register(const char* class_name,
                                     const char* method_name,
                                     const char* signature,
                                     void* fnPtr) {
    const char* cn = class_name ? class_name : "Unknown";
    const char* mn = method_name ? method_name : "NULL";
    const char* sg = signature ? signature : "NULL";

    for (int i = 0; i < native_registry_count; i++) {
        if (strcmp(native_registry[i].class_name, cn) == 0 &&
            strcmp(native_registry[i].method_name, mn) == 0 &&
            strcmp(native_registry[i].signature, sg) == 0) {
            native_registry[i].fnPtr = fnPtr;
            return;
        }
    }

    if (native_registry_count >= MAX_NATIVE_REGISTRY) {
        log_warning("Native registry full; skipping %s.%s%s", cn, mn, sg);
        return;
    }

    NativeRegistryEntry* e = &native_registry[native_registry_count++];
    strncpy(e->class_name, cn, sizeof(e->class_name) - 1);
    e->class_name[sizeof(e->class_name) - 1] = '\0';
    strncpy(e->method_name, mn, sizeof(e->method_name) - 1);
    e->method_name[sizeof(e->method_name) - 1] = '\0';
    strncpy(e->signature, sg, sizeof(e->signature) - 1);
    e->signature[sizeof(e->signature) - 1] = '\0';
    e->fnPtr = fnPtr;
}

const NativeRegistryEntry* native_registry_find(const char* class_name,
                                                const char* method_name,
                                                const char* signature) {
    if (!class_name || !method_name || !signature) return NULL;
    for (int i = 0; i < native_registry_count; i++) {
        if (strcmp(native_registry[i].class_name, class_name) == 0 &&
            strcmp(native_registry[i].method_name, method_name) == 0 &&
            strcmp(native_registry[i].signature, signature) == 0) {
            return &native_registry[i];
        }
    }
    return NULL;
}

/* ============================================
 * STRING POOL
 * Maps jstring handles to real UTF-8 / UTF-16 data so that subsequent
 * calls like GetStringUTFChars / GetStringLength see the actual content
 * instead of NULL / "FakeString" / 0.
 * ============================================ */

#define MAX_STRINGS 512

typedef struct {
    jstring  handle;
    char*    utf8;        /* owned, null-terminated              */
    jchar*   utf16;       /* owned, null-terminated (extra slot) */
    jsize    utf8_len;    /* byte length, not counting null      */
    jsize    utf16_len;   /* char count,  not counting null      */
} StringEntry;

static StringEntry  string_table[MAX_STRINGS];
static int          string_count   = 0;
static uintptr_t    next_string_id = 0x70000;

/* ---- UTF helpers ---- */

/* UTF-8 → UTF-16 (BMP only; 4-byte sequences become U+FFFD) */
static jchar* sp_utf8_to_utf16(const char* utf8, jsize* out_len) {
    if (!utf8) { *out_len = 0; return NULL; }
    size_t bytes = strlen(utf8);
    jchar* out = (jchar*)malloc((bytes + 1) * sizeof(jchar));
    if (!out) { *out_len = 0; return NULL; }
    jsize n = 0;
    const unsigned char* p = (const unsigned char*)utf8;
    while (*p) {
        unsigned int cp;
        if (*p < 0x80) {
            cp = *p++;
        } else if ((*p & 0xE0) == 0xC0 && p[1]) {
            cp  = (*p++ & 0x1F) << 6;
            cp |= (*p++ & 0x3F);
        } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
            cp  = (*p++ & 0x0F) << 12;
            cp |= (*p++ & 0x3F) << 6;
            cp |= (*p++ & 0x3F);
        } else {
            p++;
            cp = 0xFFFD; /* replacement character */
        }
        out[n++] = (jchar)(cp & 0xFFFF);
    }
    out[n] = 0;
    *out_len = n;
    return out;
}

/* UTF-16 → UTF-8 (BMP only) */
static char* sp_utf16_to_utf8(const jchar* utf16, jsize len) {
    if (!utf16 || len <= 0) {
        char* e = (char*)malloc(1);
        if (e) e[0] = '\0';
        return e;
    }
    /* Worst case: every BMP code point needs 3 UTF-8 bytes */
    char* out = (char*)malloc((size_t)len * 3 + 1);
    if (!out) return NULL;
    char* p = out;
    for (jsize i = 0; i < len; i++) {
        unsigned int cp = (unsigned int)utf16[i];
        if (cp < 0x80) {
            *p++ = (char)cp;
        } else if (cp < 0x800) {
            *p++ = (char)(0xC0 | (cp >> 6));
            *p++ = (char)(0x80 | (cp & 0x3F));
        } else {
            *p++ = (char)(0xE0 | (cp >> 12));
            *p++ = (char)(0x80 | ((cp >> 6) & 0x3F));
            *p++ = (char)(0x80 | (cp & 0x3F));
        }
    }
    *p = '\0';
    return out;
}

/* ---- Pool operations ---- */

/* Register a UTF-8 string and return a unique jstring handle */
static jstring pool_intern_utf8(const char* utf8) {
    if (string_count >= MAX_STRINGS) return (jstring)0x7FFFF;
    const char* src = utf8 ? utf8 : "";

    /* Deduplicate: return existing handle for identical content */
    for (int i = 0; i < string_count; i++) {
        if (strcmp(string_table[i].utf8, src) == 0)
            return string_table[i].handle;
    }

    char* u8 = strdup(src);
    if (!u8) return (jstring)0x7FFFE;

    jsize u16_len;
    jchar* u16 = sp_utf8_to_utf16(u8, &u16_len);

    jstring handle = (jstring)(next_string_id);
    next_string_id += 0x10;

    StringEntry* e    = &string_table[string_count++];
    e->handle         = handle;
    e->utf8           = u8;
    e->utf16          = u16;
    e->utf8_len       = (jsize)strlen(u8);
    e->utf16_len      = u16_len;
    return handle;
}

jstring fake_jni_new_string_utf(const char* utf8) {
    return pool_intern_utf8(utf8);
}

/* Register a UTF-16 string and return a unique jstring handle */
static jstring pool_intern_utf16(const jchar* chars, jsize len) {
    if (string_count >= MAX_STRINGS) return (jstring)0x7FFFF;

    jchar* u16 = (jchar*)malloc(((size_t)len + 1) * sizeof(jchar));
    if (!u16) return (jstring)0x7FFFE;
    if (chars && len > 0) memcpy(u16, chars, (size_t)len * sizeof(jchar));
    u16[len] = 0;

    char* u8 = sp_utf16_to_utf8(u16, len);

    jstring handle = (jstring)(next_string_id);
    next_string_id += 0x10;

    StringEntry* e    = &string_table[string_count++];
    e->handle         = handle;
    e->utf8           = u8 ? u8 : strdup("");
    e->utf16          = u16;
    e->utf8_len       = u8 ? (jsize)strlen(u8) : 0;
    e->utf16_len      = len;
    return handle;
}

/* Look up entry by handle; returns NULL if not found */
static StringEntry* pool_find(jstring handle) {
    for (int i = 0; i < string_count; i++) {
        if (string_table[i].handle == handle)
            return &string_table[i];
    }
    return NULL;
}

const char* fake_jni_string_utf8(jstring string) {
    StringEntry* entry = pool_find(string);
    return entry ? entry->utf8 : NULL;
}

/* Release all pool memory */
static void pool_destroy(void) {
    for (int i = 0; i < string_count; i++) {
        free(string_table[i].utf8);
        free(string_table[i].utf16);
    }
    string_count   = 0;
    next_string_id = 0x70000;
}

/* ============================================
 * METHOD TABLE
 * Maps unique jmethodID handles to the
 * (class_name, method_name, signature) triple
 * captured at GetMethodID / GetStaticMethodID.
 * This lets Call*Method stubs know WHICH Java
 * method is actually being invoked.
 * ============================================ */

#define MAX_METHODS 1024

typedef struct {
    jmethodID id;
    char      class_name[256];
    char      method_name[128];
    char      signature[256];
    jboolean  is_static;
} MethodEntry;

static MethodEntry  method_table[MAX_METHODS];
static int          method_count   = 0;
static uintptr_t    next_method_id = 0x20000;

/* Register a method and return a unique jmethodID.
 * Returns existing ID if the same (class, name, sig, static) was already registered. */
static jmethodID method_table_register(const char* class_name,
                                        const char* method_name,
                                        const char* sig,
                                        jboolean    is_static) {
    const char* cn  = class_name  ? class_name  : "Unknown";
    const char* mn  = method_name ? method_name : "unknown";
    const char* sg  = sig         ? sig         : "";

    for (int i = 0; i < method_count; i++) {
        if (method_table[i].is_static == is_static &&
            strcmp(method_table[i].class_name,  cn) == 0 &&
            strcmp(method_table[i].method_name, mn) == 0 &&
            strcmp(method_table[i].signature,   sg) == 0) {
            return method_table[i].id;
        }
    }

    if (method_count >= MAX_METHODS) return (jmethodID)0xFFFF;

    jmethodID new_id = (jmethodID)(next_method_id);
    next_method_id += 0x10;

    MethodEntry* e   = &method_table[method_count++];
    e->id            = new_id;
    e->is_static     = is_static;
    strncpy(e->class_name,  cn, sizeof(e->class_name)  - 1);
    strncpy(e->method_name, mn, sizeof(e->method_name) - 1);
    strncpy(e->signature,   sg, sizeof(e->signature)   - 1);
    return new_id;
}

/* Look up a registered method by its ID. Returns NULL if not found. */
static MethodEntry* method_table_find(jmethodID id) {
    for (int i = 0; i < method_count; i++) {
        if (method_table[i].id == id) return &method_table[i];
    }
    return NULL;
}

/* Helper macros for argument logging */
#define LOG_CALL_START(funcname) \
    log_jni_call(funcname, "")

#define LOG_CALL_1(funcname, fmt, a1) \
    log_jni_call(funcname, fmt, a1)

#define LOG_CALL_2(funcname, fmt, a1, a2) \
    log_jni_call(funcname, fmt, a1, a2)

#define LOG_CALL_3(funcname, fmt, a1, a2, a3) \
    log_jni_call(funcname, fmt, a1, a2, a3)

#define LOG_CALL_4(funcname, fmt, a1, a2, a3, a4) \
    log_jni_call(funcname, fmt, a1, a2, a3, a4)

/* Helper function to format argument for JSON */
static void format_arg_value(char* buf, size_t bufsize, const void* ptr, const char* strval) {
    if (strval) {
        snprintf(buf, bufsize, "%s", strval);
    } else {
        snprintf(buf, bufsize, "%p", ptr);
    }
}

/*
 * Call*Method JSON logging must happen directly inside each JNI stub.
 * json_logger.c uses __builtin_return_address(1) to recover the target
 * SO call site, so wrapping log_jni_call_json() in a helper function would
 * shift the stack frame and produce the wrong caller_offset.
 */
#define LOG_METHOD_CALL_JSON(FUNC_NAME, ENV_PTR, RECEIVER_NAME, RECEIVER_VALUE, METHOD_ID, METHOD_ENTRY, IS_STATIC_TEXT, RETURN_TYPE_TEXT, RETURN_VALUE_TEXT, MOCK_TEXT) \
    do { \
        const char* _arg_names[] = { \
            "env", RECEIVER_NAME, "methodID", "resolved", "class_name", "method_name", \
            "sig", "resolved_method", "is_static", "return_type", "return_value", "mock_applied" \
        }; \
        char _arg_values[12][512]; \
        snprintf(_arg_values[0], sizeof(_arg_values[0]), "%p", (void*)(ENV_PTR)); \
        snprintf(_arg_values[1], sizeof(_arg_values[1]), "%p", (void*)(RECEIVER_VALUE)); \
        snprintf(_arg_values[2], sizeof(_arg_values[2]), "%p", (void*)(METHOD_ID)); \
        if ((METHOD_ENTRY)) { \
            snprintf(_arg_values[3], sizeof(_arg_values[3]), "true"); \
            snprintf(_arg_values[4], sizeof(_arg_values[4]), "%s", (METHOD_ENTRY)->class_name); \
            snprintf(_arg_values[5], sizeof(_arg_values[5]), "%s", (METHOD_ENTRY)->method_name); \
            snprintf(_arg_values[6], sizeof(_arg_values[6]), "%s", (METHOD_ENTRY)->signature); \
            snprintf(_arg_values[7], sizeof(_arg_values[7]), "%s.%s%s", \
                     (METHOD_ENTRY)->class_name, (METHOD_ENTRY)->method_name, (METHOD_ENTRY)->signature); \
        } else { \
            snprintf(_arg_values[3], sizeof(_arg_values[3]), "false"); \
            snprintf(_arg_values[4], sizeof(_arg_values[4]), "Unknown"); \
            snprintf(_arg_values[5], sizeof(_arg_values[5]), "unknown"); \
            snprintf(_arg_values[6], sizeof(_arg_values[6]), ""); \
            snprintf(_arg_values[7], sizeof(_arg_values[7]), "unresolved"); \
        } \
        snprintf(_arg_values[8], sizeof(_arg_values[8]), "%s", IS_STATIC_TEXT); \
        snprintf(_arg_values[9], sizeof(_arg_values[9]), "%s", RETURN_TYPE_TEXT); \
        snprintf(_arg_values[10], sizeof(_arg_values[10]), "%s", RETURN_VALUE_TEXT); \
        snprintf(_arg_values[11], sizeof(_arg_values[11]), "%s", MOCK_TEXT); \
        const char* _arg_value_ptrs[] = { \
            _arg_values[0], _arg_values[1], _arg_values[2], _arg_values[3], \
            _arg_values[4], _arg_values[5], _arg_values[6], _arg_values[7], \
            _arg_values[8], _arg_values[9], _arg_values[10], _arg_values[11] \
        }; \
        log_jni_call_json(FUNC_NAME, _arg_names, _arg_value_ptrs, 12); \
    } while (0)

#define LOG_SIMPLE_JSON_1(FUNC_NAME, ENV_PTR, NAME1, VALUE1) \
    do { \
        const char* _arg_names[] = {"env", NAME1}; \
        char _arg_values[2][256]; \
        snprintf(_arg_values[0], sizeof(_arg_values[0]), "%p", (void*)(ENV_PTR)); \
        snprintf(_arg_values[1], sizeof(_arg_values[1]), "%s", VALUE1); \
        const char* _arg_value_ptrs[] = {_arg_values[0], _arg_values[1]}; \
        log_jni_call_json(FUNC_NAME, _arg_names, _arg_value_ptrs, 2); \
    } while (0)

#define LOG_SIMPLE_JSON_PTR1(FUNC_NAME, ENV_PTR, NAME1, VALUE1) \
    do { \
        const char* _arg_names[] = {"env", NAME1}; \
        char _arg_values[2][256]; \
        snprintf(_arg_values[0], sizeof(_arg_values[0]), "%p", (void*)(ENV_PTR)); \
        snprintf(_arg_values[1], sizeof(_arg_values[1]), "%p", (void*)(VALUE1)); \
        const char* _arg_value_ptrs[] = {_arg_values[0], _arg_values[1]}; \
        log_jni_call_json(FUNC_NAME, _arg_names, _arg_value_ptrs, 2); \
    } while (0)

/* ============================================
 * VERSION INFORMATION
 * ============================================ */

static jint JNICALL fake_GetVersion(JNIEnv* env) {
    LOG_CALL_START("GetVersion");
    
    const char* arg_names[] = {"env"};
    char arg_values[1][64];
    snprintf(arg_values[0], 64, "%p", (void*)env);
    const char* arg_value_ptrs[] = {arg_values[0]};
    log_jni_call_json("GetVersion", arg_names, arg_value_ptrs, 1);
    
    increment_call_count("GetVersion");
    return JNI_VERSION_1_6;
}

/* ============================================
 * CLASS OPERATIONS
 * ============================================ */

static jclass JNICALL fake_DefineClass(JNIEnv* env, const char* name, jobject loader, 
                                        const jbyte* buf, jsize len) {
    LOG_CALL_2("DefineClass", "name=%s, len=%d", name ? name : "NULL", len);
    
    const char* arg_names[] = {"env", "name", "loader", "buf", "len"};
    char arg_values[5][256];
    snprintf(arg_values[0], 256, "%p", (void*)env);
    snprintf(arg_values[1], 256, "%s", name ? name : "NULL");
    snprintf(arg_values[2], 256, "%p", (void*)loader);
    snprintf(arg_values[3], 256, "%p", (void*)buf);
    snprintf(arg_values[4], 256, "%d", len);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2], arg_values[3], arg_values[4]};
    log_jni_call_json("DefineClass", arg_names, arg_value_ptrs, 5);
    
    increment_call_count("DefineClass");
    return (jclass)0x1001;
}

static jclass JNICALL fake_FindClass(JNIEnv* env, const char* name) {
    // Register class and get unique jclass pointer
    jclass clazz = register_class(name ? name : "NULL");
    
    LOG_CALL_2("FindClass", "name=%s, jclass=%p", name ? name : "NULL", clazz);
    
    const char* arg_names[] = {"env", "name", "jclass"};
    char arg_values[3][256];
    snprintf(arg_values[0], 256, "%p", (void*)env);
    snprintf(arg_values[1], 256, "%s", name ? name : "NULL");
    snprintf(arg_values[2], 256, "%p", (void*)clazz);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2]};
    log_jni_call_json("FindClass", arg_names, arg_value_ptrs, 3);
    
    increment_call_count("FindClass");
    return clazz;
}

static jmethodID JNICALL fake_FromReflectedMethod(JNIEnv* env, jobject method) {
    LOG_CALL_1("FromReflectedMethod", "method=%p", method);
    increment_call_count("FromReflectedMethod");
    return (jmethodID)0x2001;
}

static jfieldID JNICALL fake_FromReflectedField(JNIEnv* env, jobject field) {
    LOG_CALL_1("FromReflectedField", "field=%p", field);
    increment_call_count("FromReflectedField");
    return (jfieldID)0x3001;
}

static jobject JNICALL fake_ToReflectedMethod(JNIEnv* env, jclass clazz, jmethodID methodID, jboolean isStatic) {
    LOG_CALL_3("ToReflectedMethod", "clazz=%p, methodID=%p, isStatic=%d", clazz, methodID, isStatic);
    increment_call_count("ToReflectedMethod");
    return (jobject)0x4001;
}

static jclass JNICALL fake_GetSuperclass(JNIEnv* env, jclass clazz) {
    const char* class_name = get_class_name(clazz);
    LOG_CALL_2("GetSuperclass", "clazz=%p [%s]", clazz, class_name);
    increment_call_count("GetSuperclass");
    return (jclass)0x1002;
}

static jboolean JNICALL fake_IsAssignableFrom(JNIEnv* env, jclass clazz1, jclass clazz2) {
    const char* class_name1 = get_class_name(clazz1);
    const char* class_name2 = get_class_name(clazz2);
    LOG_CALL_4("IsAssignableFrom", "clazz1=%p [%s], clazz2=%p [%s]", clazz1, class_name1, clazz2, class_name2);
    increment_call_count("IsAssignableFrom");
    return JNI_FALSE;
}

static jobject JNICALL fake_ToReflectedField(JNIEnv* env, jclass clazz, jfieldID fieldID, jboolean isStatic) {
    const char* class_name = get_class_name(clazz);
    LOG_CALL_4("ToReflectedField", "clazz=%p [%s], fieldID=%p, isStatic=%d", clazz, class_name, fieldID, isStatic);
    increment_call_count("ToReflectedField");
    return (jobject)0x4002;
}

/* ============================================
 * EXCEPTION HANDLING
 * ============================================ */

static jint JNICALL fake_Throw(JNIEnv* env, jthrowable obj) {
    LOG_CALL_1("Throw", "obj=%p", obj);
    LOG_SIMPLE_JSON_PTR1("Throw", env, "obj", obj);
    increment_call_count("Throw");
    return JNI_OK;
}

static jint JNICALL fake_ThrowNew(JNIEnv* env, jclass clazz, const char* message) {
    const char* class_name = get_class_name(clazz);
    LOG_CALL_3("ThrowNew", "clazz=%p [%s], message=%s", clazz, class_name, message ? message : "NULL");
    const char* arg_names[] = {"env", "clazz", "class_name", "message"};
    char arg_values[4][256];
    snprintf(arg_values[0], sizeof(arg_values[0]), "%p", (void*)env);
    snprintf(arg_values[1], sizeof(arg_values[1]), "%p", (void*)clazz);
    snprintf(arg_values[2], sizeof(arg_values[2]), "%s", class_name);
    snprintf(arg_values[3], sizeof(arg_values[3]), "%s", message ? message : "NULL");
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2], arg_values[3]};
    log_jni_call_json("ThrowNew", arg_names, arg_value_ptrs, 4);
    increment_call_count("ThrowNew");
    return JNI_OK;
}

static jthrowable JNICALL fake_ExceptionOccurred(JNIEnv* env) {
    LOG_CALL_START("ExceptionOccurred");
    LOG_SIMPLE_JSON_1("ExceptionOccurred", env, "return_value", "NULL");
    increment_call_count("ExceptionOccurred");
    return NULL;
}

static void JNICALL fake_ExceptionDescribe(JNIEnv* env) {
    LOG_CALL_START("ExceptionDescribe");
    LOG_SIMPLE_JSON_1("ExceptionDescribe", env, "status", "called");
    increment_call_count("ExceptionDescribe");
}

static void JNICALL fake_ExceptionClear(JNIEnv* env) {
    LOG_CALL_START("ExceptionClear");
    LOG_SIMPLE_JSON_1("ExceptionClear", env, "status", "called");
    increment_call_count("ExceptionClear");
}

static void JNICALL fake_FatalError(JNIEnv* env, const char* msg) {
    LOG_CALL_1("FatalError", "msg=%s", msg ? msg : "NULL");
    increment_call_count("FatalError");
    /* JNI spec: FatalError must never return — flush logs then abort */
    log_info("[FATAL] FatalError called: %s — aborting process", msg ? msg : "NULL");
    abort();
}

/* ============================================
 * LOCAL/GLOBAL REFERENCES
 * ============================================ */

static jint JNICALL fake_PushLocalFrame(JNIEnv* env, jint capacity) {
    LOG_CALL_1("PushLocalFrame", "capacity=%d", capacity);
    char capacity_text[32];
    snprintf(capacity_text, sizeof(capacity_text), "%d", capacity);
    LOG_SIMPLE_JSON_1("PushLocalFrame", env, "capacity", capacity_text);
    increment_call_count("PushLocalFrame");
    return JNI_OK;
}

static jobject JNICALL fake_PopLocalFrame(JNIEnv* env, jobject result) {
    LOG_CALL_1("PopLocalFrame", "result=%p", result);
    LOG_SIMPLE_JSON_PTR1("PopLocalFrame", env, "result", result);
    increment_call_count("PopLocalFrame");
    return result;
}

static jobject JNICALL fake_NewGlobalRef(JNIEnv* env, jobject obj) {
    LOG_CALL_1("NewGlobalRef", "obj=%p", obj);
    
    const char* arg_names[] = {"env", "obj"};
    char arg_values[2][64];
    snprintf(arg_values[0], 64, "%p", (void*)env);
    snprintf(arg_values[1], 64, "%p", (void*)obj);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1]};
    log_jni_call_json("NewGlobalRef", arg_names, arg_value_ptrs, 2);
    
    increment_call_count("NewGlobalRef");
    return obj;
}

static void JNICALL fake_DeleteGlobalRef(JNIEnv* env, jobject globalRef) {
    LOG_CALL_1("DeleteGlobalRef", "globalRef=%p", globalRef);
    
    const char* arg_names[] = {"env", "globalRef"};
    char arg_values[2][64];
    snprintf(arg_values[0], 64, "%p", (void*)env);
    snprintf(arg_values[1], 64, "%p", (void*)globalRef);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1]};
    log_jni_call_json("DeleteGlobalRef", arg_names, arg_value_ptrs, 2);
    
    increment_call_count("DeleteGlobalRef");
}

static void JNICALL fake_DeleteLocalRef(JNIEnv* env, jobject localRef) {
    LOG_CALL_1("DeleteLocalRef", "localRef=%p", localRef);
    LOG_SIMPLE_JSON_PTR1("DeleteLocalRef", env, "localRef", localRef);
    increment_call_count("DeleteLocalRef");
}

static jboolean JNICALL fake_IsSameObject(JNIEnv* env, jobject ref1, jobject ref2) {
    LOG_CALL_2("IsSameObject", "ref1=%p, ref2=%p", ref1, ref2);
    const char* arg_names[] = {"env", "ref1", "ref2", "return_value"};
    char arg_values[4][64];
    snprintf(arg_values[0], sizeof(arg_values[0]), "%p", (void*)env);
    snprintf(arg_values[1], sizeof(arg_values[1]), "%p", (void*)ref1);
    snprintf(arg_values[2], sizeof(arg_values[2]), "%p", (void*)ref2);
    snprintf(arg_values[3], sizeof(arg_values[3]), "%s", (ref1 == ref2) ? "true" : "false");
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2], arg_values[3]};
    log_jni_call_json("IsSameObject", arg_names, arg_value_ptrs, 4);
    increment_call_count("IsSameObject");
    return (ref1 == ref2) ? JNI_TRUE : JNI_FALSE;
}

static jobject JNICALL fake_NewLocalRef(JNIEnv* env, jobject ref) {
    LOG_CALL_1("NewLocalRef", "ref=%p", ref);
    LOG_SIMPLE_JSON_PTR1("NewLocalRef", env, "ref", ref);
    increment_call_count("NewLocalRef");
    return ref;
}

static jint JNICALL fake_EnsureLocalCapacity(JNIEnv* env, jint capacity) {
    LOG_CALL_1("EnsureLocalCapacity", "capacity=%d", capacity);
    char capacity_text[32];
    snprintf(capacity_text, sizeof(capacity_text), "%d", capacity);
    LOG_SIMPLE_JSON_1("EnsureLocalCapacity", env, "capacity", capacity_text);
    increment_call_count("EnsureLocalCapacity");
    return JNI_OK;
}

/* ============================================
 * OBJECT OPERATIONS
 * ============================================ */

static jobject JNICALL fake_AllocObject(JNIEnv* env, jclass clazz) {
    const char* class_name = get_class_name(clazz);
    LOG_CALL_2("AllocObject", "clazz=%p [%s]", clazz, class_name);
    increment_call_count("AllocObject");
    return (jobject)0x5000;
}

static jobject JNICALL fake_NewObject(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    const char* class_name = get_class_name(clazz);
    LOG_CALL_3("NewObject", "clazz=%p [%s], methodID=%p", clazz, class_name, methodID);
    increment_call_count("NewObject");
    return (jobject)0x5001;
}

static jobject JNICALL fake_NewObjectV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    const char* class_name = get_class_name(clazz);
    LOG_CALL_3("NewObjectV", "clazz=%p [%s], methodID=%p", clazz, class_name, methodID);
    increment_call_count("NewObjectV");
    return (jobject)0x5002;
}

static jobject JNICALL fake_NewObjectA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    const char* class_name = get_class_name(clazz);
    LOG_CALL_3("NewObjectA", "clazz=%p [%s], methodID=%p", clazz, class_name, methodID);
    increment_call_count("NewObjectA");
    return (jobject)0x5003;
}

static jclass JNICALL fake_GetObjectClass(JNIEnv* env, jobject obj) {
    LOG_CALL_1("GetObjectClass", "obj=%p", obj);
    
    const char* arg_names[] = {"env", "obj"};
    char arg_values[2][64];
    snprintf(arg_values[0], 64, "%p", (void*)env);
    snprintf(arg_values[1], 64, "%p", (void*)obj);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1]};
    log_jni_call_json("GetObjectClass", arg_names, arg_value_ptrs, 2);
    
    increment_call_count("GetObjectClass");
    return (jclass)0x1003;
}

static jboolean JNICALL fake_IsInstanceOf(JNIEnv* env, jobject obj, jclass clazz) {
    const char* class_name = get_class_name(clazz);
    LOG_CALL_3("IsInstanceOf", "obj=%p, clazz=%p [%s]", obj, clazz, class_name);
    increment_call_count("IsInstanceOf");
    return JNI_TRUE;
}

/* ============================================
 * METHOD ACCESS
 * ============================================ */

static jmethodID JNICALL fake_GetMethodID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    const char* class_name = get_class_name(clazz);
    jmethodID mid = method_table_register(class_name, name, sig, JNI_FALSE);

    LOG_CALL_4("GetMethodID", "clazz=%p [%s], name=%s, sig=%s",
               clazz, class_name, name ? name : "NULL", sig ? sig : "NULL");

    const char* arg_names[] = {"env", "clazz", "class_name", "name", "sig", "methodID"};
    char arg_values[6][256];
    snprintf(arg_values[0], 256, "%p", (void*)env);
    snprintf(arg_values[1], 256, "%p", (void*)clazz);
    snprintf(arg_values[2], 256, "%s", class_name);
    snprintf(arg_values[3], 256, "%s", name ? name : "NULL");
    snprintf(arg_values[4], 256, "%s", sig  ? sig  : "NULL");
    snprintf(arg_values[5], 256, "%p", (void*)mid);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2],
                                    arg_values[3], arg_values[4], arg_values[5]};
    log_jni_call_json("GetMethodID", arg_names, arg_value_ptrs, 6);

    increment_call_count("GetMethodID");
    return mid;
}

/* Continue with Call*Method functions */
#define DEFINE_CALL_METHOD(Type, type) \
static j##type JNICALL fake_Call##Type##Method(JNIEnv* env, jobject obj, jmethodID methodID, ...) { \
    MethodEntry* _me = method_table_find(methodID); \
    jlong _mv = 0; \
    int _mock_applied = 0; \
    if (_me) { \
        log_jni_call("Call" #Type "Method", "obj=%p, %s.%s%s", \
                     obj, _me->class_name, _me->method_name, _me->signature); \
        if (mock_get_primitive(_me->class_name, _me->method_name, _me->signature, &_mv)) { \
            _mock_applied = 1; \
            log_info("  [MOCK] Call" #Type "Method %s.%s%s => %lld", \
                     _me->class_name, _me->method_name, _me->signature, (long long)_mv); \
            char _return_value[64]; \
            snprintf(_return_value, sizeof(_return_value), "%lld", (long long)_mv); \
            LOG_METHOD_CALL_JSON("Call" #Type "Method", env, "obj", obj, methodID, _me, "false", #type, _return_value, "true"); \
            increment_call_count("Call" #Type "Method"); \
            return (j##type)_mv; \
        } \
    } else { \
        log_jni_call("Call" #Type "Method", "obj=%p, methodID=%p (unresolved)", obj, methodID); \
    } \
    LOG_METHOD_CALL_JSON("Call" #Type "Method", env, "obj", obj, methodID, _me, "false", #type, "0", _mock_applied ? "true" : "false"); \
    increment_call_count("Call" #Type "Method"); \
    return (j##type)0; \
} \
static j##type JNICALL fake_Call##Type##MethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) { \
    (void)args; \
    MethodEntry* _me = method_table_find(methodID); \
    if (_me) { log_jni_call("Call" #Type "MethodV", "obj=%p, %s.%s%s", obj, _me->class_name, _me->method_name, _me->signature); } \
    else      { log_jni_call("Call" #Type "MethodV", "obj=%p, methodID=%p (unresolved)", obj, methodID); } \
    LOG_METHOD_CALL_JSON("Call" #Type "MethodV", env, "obj", obj, methodID, _me, "false", #type, "0", "false"); \
    increment_call_count("Call" #Type "MethodV"); \
    return (j##type)0; \
} \
static j##type JNICALL fake_Call##Type##MethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) { \
    (void)args; \
    MethodEntry* _me = method_table_find(methodID); \
    if (_me) { log_jni_call("Call" #Type "MethodA", "obj=%p, %s.%s%s", obj, _me->class_name, _me->method_name, _me->signature); } \
    else      { log_jni_call("Call" #Type "MethodA", "obj=%p, methodID=%p (unresolved)", obj, methodID); } \
    LOG_METHOD_CALL_JSON("Call" #Type "MethodA", env, "obj", obj, methodID, _me, "false", #type, "0", "false"); \
    increment_call_count("Call" #Type "MethodA"); \
    return (j##type)0; \
}

DEFINE_CALL_METHOD(Boolean, boolean)
DEFINE_CALL_METHOD(Byte, byte)
DEFINE_CALL_METHOD(Char, char)
DEFINE_CALL_METHOD(Short, short)
DEFINE_CALL_METHOD(Int, int)
DEFINE_CALL_METHOD(Long, long)
DEFINE_CALL_METHOD(Float, float)
DEFINE_CALL_METHOD(Double, double)

static jobject JNICALL fake_CallObjectMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    MethodEntry* me = method_table_find(methodID);
    if (me) {
        log_jni_call("CallObjectMethod", "obj=%p, %s.%s%s",
                     obj, me->class_name, me->method_name, me->signature);
        char mock_str[512];
        if (mock_get_string(me->class_name, me->method_name, me->signature, mock_str, sizeof(mock_str))) {
            log_info("  [MOCK] CallObjectMethod %s.%s%s => \"%s\"",
                     me->class_name, me->method_name, me->signature, mock_str);
            LOG_METHOD_CALL_JSON("CallObjectMethod", env, "obj", obj, methodID, me, "false", "string", mock_str, "true");
            increment_call_count("CallObjectMethod");
            return (jobject)pool_intern_utf8(mock_str);
        }
    } else {
        log_jni_call("CallObjectMethod", "obj=%p, methodID=%p (unresolved)", obj, methodID);
    }
    LOG_METHOD_CALL_JSON("CallObjectMethod", env, "obj", obj, methodID, me, "false", "object", "0x5004", "false");
    increment_call_count("CallObjectMethod");
    return (jobject)0x5004;
}

static jobject JNICALL fake_CallObjectMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    (void)args;
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallObjectMethodV", "obj=%p, %s.%s%s", obj, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallObjectMethodV", "obj=%p, methodID=%p (unresolved)", obj, methodID); }
    LOG_METHOD_CALL_JSON("CallObjectMethodV", env, "obj", obj, methodID, me, "false", "object", "0x5005", "false");
    increment_call_count("CallObjectMethodV");
    return (jobject)0x5005;
}

static jobject JNICALL fake_CallObjectMethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) {
    (void)args;
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallObjectMethodA", "obj=%p, %s.%s%s", obj, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallObjectMethodA", "obj=%p, methodID=%p (unresolved)", obj, methodID); }
    LOG_METHOD_CALL_JSON("CallObjectMethodA", env, "obj", obj, methodID, me, "false", "object", "0x5006", "false");
    increment_call_count("CallObjectMethodA");
    return (jobject)0x5006;
}

static void JNICALL fake_CallVoidMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallVoidMethod", "obj=%p, %s.%s%s", obj, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallVoidMethod", "obj=%p, methodID=%p (unresolved)", obj, methodID); }
    LOG_METHOD_CALL_JSON("CallVoidMethod", env, "obj", obj, methodID, me, "false", "void", "void", "false");
    increment_call_count("CallVoidMethod");
}

static void JNICALL fake_CallVoidMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    (void)args;
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallVoidMethodV", "obj=%p, %s.%s%s", obj, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallVoidMethodV", "obj=%p, methodID=%p (unresolved)", obj, methodID); }
    LOG_METHOD_CALL_JSON("CallVoidMethodV", env, "obj", obj, methodID, me, "false", "void", "void", "false");
    increment_call_count("CallVoidMethodV");
}

static void JNICALL fake_CallVoidMethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) {
    (void)args;
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallVoidMethodA", "obj=%p, %s.%s%s", obj, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallVoidMethodA", "obj=%p, methodID=%p (unresolved)", obj, methodID); }
    LOG_METHOD_CALL_JSON("CallVoidMethodA", env, "obj", obj, methodID, me, "false", "void", "void", "false");
    increment_call_count("CallVoidMethodA");
}

/* CallNonvirtual*Method functions */
#define DEFINE_CALL_NONVIRTUAL_METHOD(Type, type) \
static j##type JNICALL fake_CallNonvirtual##Type##Method(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) { \
    MethodEntry* _me = method_table_find(methodID); \
    if (_me) { log_jni_call("CallNonvirtual" #Type "Method", "obj=%p, clazz=%p, %s.%s%s", obj, clazz, _me->class_name, _me->method_name, _me->signature); } \
    else     { log_jni_call("CallNonvirtual" #Type "Method", "obj=%p, clazz=%p, methodID=%p (unresolved)", obj, clazz, methodID); } \
    LOG_METHOD_CALL_JSON("CallNonvirtual" #Type "Method", env, "obj", obj, methodID, _me, "false", #type, "0", "false"); \
    increment_call_count("CallNonvirtual" #Type "Method"); \
    return (j##type)0; \
} \
static j##type JNICALL fake_CallNonvirtual##Type##MethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) { \
    (void)args; \
    MethodEntry* _me = method_table_find(methodID); \
    if (_me) { log_jni_call("CallNonvirtual" #Type "MethodV", "obj=%p, clazz=%p, %s.%s%s", obj, clazz, _me->class_name, _me->method_name, _me->signature); } \
    else     { log_jni_call("CallNonvirtual" #Type "MethodV", "obj=%p, clazz=%p, methodID=%p (unresolved)", obj, clazz, methodID); } \
    LOG_METHOD_CALL_JSON("CallNonvirtual" #Type "MethodV", env, "obj", obj, methodID, _me, "false", #type, "0", "false"); \
    increment_call_count("CallNonvirtual" #Type "MethodV"); \
    return (j##type)0; \
} \
static j##type JNICALL fake_CallNonvirtual##Type##MethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) { \
    (void)args; \
    MethodEntry* _me = method_table_find(methodID); \
    if (_me) { log_jni_call("CallNonvirtual" #Type "MethodA", "obj=%p, clazz=%p, %s.%s%s", obj, clazz, _me->class_name, _me->method_name, _me->signature); } \
    else     { log_jni_call("CallNonvirtual" #Type "MethodA", "obj=%p, clazz=%p, methodID=%p (unresolved)", obj, clazz, methodID); } \
    LOG_METHOD_CALL_JSON("CallNonvirtual" #Type "MethodA", env, "obj", obj, methodID, _me, "false", #type, "0", "false"); \
    increment_call_count("CallNonvirtual" #Type "MethodA"); \
    return (j##type)0; \
}

DEFINE_CALL_NONVIRTUAL_METHOD(Boolean, boolean)
DEFINE_CALL_NONVIRTUAL_METHOD(Byte, byte)
DEFINE_CALL_NONVIRTUAL_METHOD(Char, char)
DEFINE_CALL_NONVIRTUAL_METHOD(Short, short)
DEFINE_CALL_NONVIRTUAL_METHOD(Int, int)
DEFINE_CALL_NONVIRTUAL_METHOD(Long, long)
DEFINE_CALL_NONVIRTUAL_METHOD(Float, float)
DEFINE_CALL_NONVIRTUAL_METHOD(Double, double)

static jobject JNICALL fake_CallNonvirtualObjectMethod(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallNonvirtualObjectMethod", "obj=%p, clazz=%p, %s.%s%s", obj, clazz, me->class_name, me->method_name, me->signature); }
    else    { log_jni_call("CallNonvirtualObjectMethod", "obj=%p, clazz=%p, methodID=%p (unresolved)", obj, clazz, methodID); }
    LOG_METHOD_CALL_JSON("CallNonvirtualObjectMethod", env, "obj", obj, methodID, me, "false", "object", "0x5007", "false");
    increment_call_count("CallNonvirtualObjectMethod");
    return (jobject)0x5007;
}

static jobject JNICALL fake_CallNonvirtualObjectMethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    (void)args;
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallNonvirtualObjectMethodV", "obj=%p, clazz=%p, %s.%s%s", obj, clazz, me->class_name, me->method_name, me->signature); }
    else    { log_jni_call("CallNonvirtualObjectMethodV", "obj=%p, clazz=%p, methodID=%p (unresolved)", obj, clazz, methodID); }
    LOG_METHOD_CALL_JSON("CallNonvirtualObjectMethodV", env, "obj", obj, methodID, me, "false", "object", "0x5008", "false");
    increment_call_count("CallNonvirtualObjectMethodV");
    return (jobject)0x5008;
}

static jobject JNICALL fake_CallNonvirtualObjectMethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) {
    (void)args;
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallNonvirtualObjectMethodA", "obj=%p, clazz=%p, %s.%s%s", obj, clazz, me->class_name, me->method_name, me->signature); }
    else    { log_jni_call("CallNonvirtualObjectMethodA", "obj=%p, clazz=%p, methodID=%p (unresolved)", obj, clazz, methodID); }
    LOG_METHOD_CALL_JSON("CallNonvirtualObjectMethodA", env, "obj", obj, methodID, me, "false", "object", "0x5009", "false");
    increment_call_count("CallNonvirtualObjectMethodA");
    return (jobject)0x5009;
}

static void JNICALL fake_CallNonvirtualVoidMethod(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallNonvirtualVoidMethod", "obj=%p, clazz=%p, %s.%s%s", obj, clazz, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallNonvirtualVoidMethod", "obj=%p, clazz=%p, methodID=%p (unresolved)", obj, clazz, methodID); }
    LOG_METHOD_CALL_JSON("CallNonvirtualVoidMethod", env, "obj", obj, methodID, me, "false", "void", "void", "false");
    increment_call_count("CallNonvirtualVoidMethod");
}

static void JNICALL fake_CallNonvirtualVoidMethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    (void)args;
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallNonvirtualVoidMethodV", "obj=%p, clazz=%p, %s.%s%s", obj, clazz, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallNonvirtualVoidMethodV", "obj=%p, clazz=%p, methodID=%p (unresolved)", obj, clazz, methodID); }
    LOG_METHOD_CALL_JSON("CallNonvirtualVoidMethodV", env, "obj", obj, methodID, me, "false", "void", "void", "false");
    increment_call_count("CallNonvirtualVoidMethodV");
}

static void JNICALL fake_CallNonvirtualVoidMethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) {
    (void)args;
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallNonvirtualVoidMethodA", "obj=%p, clazz=%p, %s.%s%s", obj, clazz, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallNonvirtualVoidMethodA", "obj=%p, clazz=%p, methodID=%p (unresolved)", obj, clazz, methodID); }
    LOG_METHOD_CALL_JSON("CallNonvirtualVoidMethodA", env, "obj", obj, methodID, me, "false", "void", "void", "false");
    increment_call_count("CallNonvirtualVoidMethodA");
}

/* ============================================
 * FIELD ACCESS
 * ============================================ */

static jfieldID JNICALL fake_GetFieldID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    const char* class_name = get_class_name(clazz);
    
    LOG_CALL_4("GetFieldID", "clazz=%p [%s], name=%s, sig=%s",
               clazz, class_name, name ? name : "NULL", sig ? sig : "NULL");
    
    const char* arg_names[] = {"env", "clazz", "class_name", "name", "sig"};
    char arg_values[5][256];
    snprintf(arg_values[0], 256, "%p", (void*)env);
    snprintf(arg_values[1], 256, "%p", (void*)clazz);
    snprintf(arg_values[2], 256, "%s", class_name);
    snprintf(arg_values[3], 256, "%s", name ? name : "NULL");
    snprintf(arg_values[4], 256, "%s", sig ? sig : "NULL");
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2], arg_values[3], arg_values[4]};
    log_jni_call_json("GetFieldID", arg_names, arg_value_ptrs, 5);
    
    increment_call_count("GetFieldID");
    return (jfieldID)0x3000;
}

/* Get*Field functions */
#define DEFINE_GET_FIELD(Type, type) \
static j##type JNICALL fake_Get##Type##Field(JNIEnv* env, jobject obj, jfieldID fieldID) { \
    LOG_CALL_2("Get" #Type "Field", "obj=%p, fieldID=%p", obj, fieldID); \
    increment_call_count("Get" #Type "Field"); \
    return (j##type)0; \
}

DEFINE_GET_FIELD(Boolean, boolean)
DEFINE_GET_FIELD(Byte, byte)
DEFINE_GET_FIELD(Char, char)
DEFINE_GET_FIELD(Short, short)
DEFINE_GET_FIELD(Int, int)
DEFINE_GET_FIELD(Long, long)
DEFINE_GET_FIELD(Float, float)
DEFINE_GET_FIELD(Double, double)

static jobject JNICALL fake_GetObjectField(JNIEnv* env, jobject obj, jfieldID fieldID) {
    LOG_CALL_2("GetObjectField", "obj=%p, fieldID=%p", obj, fieldID);
    
    const char* arg_names[] = {"env", "obj", "fieldID"};
    char arg_values[3][64];
    snprintf(arg_values[0], 64, "%p", (void*)env);
    snprintf(arg_values[1], 64, "%p", (void*)obj);
    snprintf(arg_values[2], 64, "%p", (void*)fieldID);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2]};
    log_jni_call_json("GetObjectField", arg_names, arg_value_ptrs, 3);
    
    increment_call_count("GetObjectField");
    return (jobject)0x5010;
}

/* Set*Field functions */
#define DEFINE_SET_FIELD(Type, type) \
static void JNICALL fake_Set##Type##Field(JNIEnv* env, jobject obj, jfieldID fieldID, j##type value) { \
    LOG_CALL_2("Set" #Type "Field", "obj=%p, fieldID=%p", obj, fieldID); \
    increment_call_count("Set" #Type "Field"); \
}

DEFINE_SET_FIELD(Boolean, boolean)
DEFINE_SET_FIELD(Byte, byte)
DEFINE_SET_FIELD(Char, char)
DEFINE_SET_FIELD(Short, short)
DEFINE_SET_FIELD(Int, int)
DEFINE_SET_FIELD(Long, long)
DEFINE_SET_FIELD(Float, float)
DEFINE_SET_FIELD(Double, double)

static void JNICALL fake_SetObjectField(JNIEnv* env, jobject obj, jfieldID fieldID, jobject value) {
    LOG_CALL_3("SetObjectField", "obj=%p, fieldID=%p, value=%p", obj, fieldID, value);
    increment_call_count("SetObjectField");
}

/* ============================================
 * STATIC METHOD ACCESS
 * ============================================ */

static jmethodID JNICALL fake_GetStaticMethodID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    const char* class_name = get_class_name(clazz);
    jmethodID mid = method_table_register(class_name, name, sig, JNI_TRUE);

    LOG_CALL_4("GetStaticMethodID", "clazz=%p [%s], name=%s, sig=%s",
               clazz, class_name, name ? name : "NULL", sig ? sig : "NULL");

    const char* arg_names[] = {"env", "clazz", "class_name", "name", "sig", "methodID"};
    char arg_values[6][256];
    snprintf(arg_values[0], 256, "%p", (void*)env);
    snprintf(arg_values[1], 256, "%p", (void*)clazz);
    snprintf(arg_values[2], 256, "%s", class_name);
    snprintf(arg_values[3], 256, "%s", name ? name : "NULL");
    snprintf(arg_values[4], 256, "%s", sig  ? sig  : "NULL");
    snprintf(arg_values[5], 256, "%p", (void*)mid);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2],
                                    arg_values[3], arg_values[4], arg_values[5]};
    log_jni_call_json("GetStaticMethodID", arg_names, arg_value_ptrs, 6);

    increment_call_count("GetStaticMethodID");
    return mid;
}

/* CallStatic*Method functions */
#define DEFINE_CALL_STATIC_METHOD(Type, type) \
static j##type JNICALL fake_CallStatic##Type##Method(JNIEnv* env, jclass clazz, jmethodID methodID, ...) { \
    MethodEntry* _me = method_table_find(methodID); \
    jlong _mv = 0; \
    int _mock_applied = 0; \
    if (_me) { \
        log_jni_call("CallStatic" #Type "Method", "clazz=%p, %s.%s%s", \
                     clazz, _me->class_name, _me->method_name, _me->signature); \
        if (mock_get_primitive(_me->class_name, _me->method_name, _me->signature, &_mv)) { \
            _mock_applied = 1; \
            log_info("  [MOCK] CallStatic" #Type "Method %s.%s%s => %lld", \
                     _me->class_name, _me->method_name, _me->signature, (long long)_mv); \
            char _return_value[64]; \
            snprintf(_return_value, sizeof(_return_value), "%lld", (long long)_mv); \
            LOG_METHOD_CALL_JSON("CallStatic" #Type "Method", env, "clazz", clazz, methodID, _me, "true", #type, _return_value, "true"); \
            increment_call_count("CallStatic" #Type "Method"); \
            return (j##type)_mv; \
        } \
    } else { \
        log_jni_call("CallStatic" #Type "Method", "clazz=%p, methodID=%p (unresolved)", clazz, methodID); \
    } \
    LOG_METHOD_CALL_JSON("CallStatic" #Type "Method", env, "clazz", clazz, methodID, _me, "true", #type, "0", _mock_applied ? "true" : "false"); \
    increment_call_count("CallStatic" #Type "Method"); \
    return (j##type)0; \
} \
static j##type JNICALL fake_CallStatic##Type##MethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) { \
    (void)args; \
    MethodEntry* _me = method_table_find(methodID); \
    if (_me) { log_jni_call("CallStatic" #Type "MethodV", "clazz=%p, %s.%s%s", clazz, _me->class_name, _me->method_name, _me->signature); } \
    else      { log_jni_call("CallStatic" #Type "MethodV", "clazz=%p, methodID=%p (unresolved)", clazz, methodID); } \
    LOG_METHOD_CALL_JSON("CallStatic" #Type "MethodV", env, "clazz", clazz, methodID, _me, "true", #type, "0", "false"); \
    increment_call_count("CallStatic" #Type "MethodV"); \
    return (j##type)0; \
} \
static j##type JNICALL fake_CallStatic##Type##MethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) { \
    (void)args; \
    MethodEntry* _me = method_table_find(methodID); \
    if (_me) { log_jni_call("CallStatic" #Type "MethodA", "clazz=%p, %s.%s%s", clazz, _me->class_name, _me->method_name, _me->signature); } \
    else      { log_jni_call("CallStatic" #Type "MethodA", "clazz=%p, methodID=%p (unresolved)", clazz, methodID); } \
    LOG_METHOD_CALL_JSON("CallStatic" #Type "MethodA", env, "clazz", clazz, methodID, _me, "true", #type, "0", "false"); \
    increment_call_count("CallStatic" #Type "MethodA"); \
    return (j##type)0; \
}

DEFINE_CALL_STATIC_METHOD(Boolean, boolean)
DEFINE_CALL_STATIC_METHOD(Byte, byte)
DEFINE_CALL_STATIC_METHOD(Char, char)
DEFINE_CALL_STATIC_METHOD(Short, short)
DEFINE_CALL_STATIC_METHOD(Int, int)
DEFINE_CALL_STATIC_METHOD(Long, long)
DEFINE_CALL_STATIC_METHOD(Float, float)
DEFINE_CALL_STATIC_METHOD(Double, double)

static jobject JNICALL fake_CallStaticObjectMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    MethodEntry* me = method_table_find(methodID);
    if (me) {
        log_jni_call("CallStaticObjectMethod", "clazz=%p, %s.%s%s",
                     clazz, me->class_name, me->method_name, me->signature);
        char mock_str[512];
        if (mock_get_string(me->class_name, me->method_name, me->signature, mock_str, sizeof(mock_str))) {
            log_info("  [MOCK] CallStaticObjectMethod %s.%s%s => \"%s\"",
                     me->class_name, me->method_name, me->signature, mock_str);
            LOG_METHOD_CALL_JSON("CallStaticObjectMethod", env, "clazz", clazz, methodID, me, "true", "string", mock_str, "true");
            increment_call_count("CallStaticObjectMethod");
            return (jobject)pool_intern_utf8(mock_str);
        }
    } else {
        log_jni_call("CallStaticObjectMethod", "clazz=%p, methodID=%p (unresolved)", clazz, methodID);
    }
    LOG_METHOD_CALL_JSON("CallStaticObjectMethod", env, "clazz", clazz, methodID, me, "true", "object", "0x5011", "false");
    increment_call_count("CallStaticObjectMethod");
    return (jobject)0x5011;
}

static jobject JNICALL fake_CallStaticObjectMethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    (void)args;
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallStaticObjectMethodV", "clazz=%p, %s.%s%s", clazz, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallStaticObjectMethodV", "clazz=%p, methodID=%p (unresolved)", clazz, methodID); }
    LOG_METHOD_CALL_JSON("CallStaticObjectMethodV", env, "clazz", clazz, methodID, me, "true", "object", "0x5012", "false");
    increment_call_count("CallStaticObjectMethodV");
    return (jobject)0x5012;
}

static jobject JNICALL fake_CallStaticObjectMethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    (void)args;
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallStaticObjectMethodA", "clazz=%p, %s.%s%s", clazz, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallStaticObjectMethodA", "clazz=%p, methodID=%p (unresolved)", clazz, methodID); }
    LOG_METHOD_CALL_JSON("CallStaticObjectMethodA", env, "clazz", clazz, methodID, me, "true", "object", "0x5013", "false");
    increment_call_count("CallStaticObjectMethodA");
    return (jobject)0x5013;
}

static void JNICALL fake_CallStaticVoidMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallStaticVoidMethod", "clazz=%p, %s.%s%s", clazz, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallStaticVoidMethod", "clazz=%p, methodID=%p (unresolved)", clazz, methodID); }
    LOG_METHOD_CALL_JSON("CallStaticVoidMethod", env, "clazz", clazz, methodID, me, "true", "void", "void", "false");
    increment_call_count("CallStaticVoidMethod");
}

static void JNICALL fake_CallStaticVoidMethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    (void)args;
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallStaticVoidMethodV", "clazz=%p, %s.%s%s", clazz, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallStaticVoidMethodV", "clazz=%p, methodID=%p (unresolved)", clazz, methodID); }
    LOG_METHOD_CALL_JSON("CallStaticVoidMethodV", env, "clazz", clazz, methodID, me, "true", "void", "void", "false");
    increment_call_count("CallStaticVoidMethodV");
}

static void JNICALL fake_CallStaticVoidMethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    (void)args;
    MethodEntry* me = method_table_find(methodID);
    if (me) { log_jni_call("CallStaticVoidMethodA", "clazz=%p, %s.%s%s", clazz, me->class_name, me->method_name, me->signature); }
    else     { log_jni_call("CallStaticVoidMethodA", "clazz=%p, methodID=%p (unresolved)", clazz, methodID); }
    LOG_METHOD_CALL_JSON("CallStaticVoidMethodA", env, "clazz", clazz, methodID, me, "true", "void", "void", "false");
    increment_call_count("CallStaticVoidMethodA");
}

/* ============================================
 * STATIC FIELD ACCESS
 * ============================================ */

static jfieldID JNICALL fake_GetStaticFieldID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    const char* class_name = get_class_name(clazz);
    
    LOG_CALL_4("GetStaticFieldID", "clazz=%p [%s], name=%s, sig=%s",
               clazz, class_name, name ? name : "NULL", sig ? sig : "NULL");
    
    const char* arg_names[] = {"env", "clazz", "class_name", "name", "sig"};
    char arg_values[5][256];
    snprintf(arg_values[0], 256, "%p", (void*)env);
    snprintf(arg_values[1], 256, "%p", (void*)clazz);
    snprintf(arg_values[2], 256, "%s", class_name);
    snprintf(arg_values[3], 256, "%s", name ? name : "NULL");
    snprintf(arg_values[4], 256, "%s", sig ? sig : "NULL");
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2], arg_values[3], arg_values[4]};
    log_jni_call_json("GetStaticFieldID", arg_names, arg_value_ptrs, 5);
    
    increment_call_count("GetStaticFieldID");
    return (jfieldID)0x3100;
}

/* GetStatic*Field functions */
#define DEFINE_GET_STATIC_FIELD(Type, type) \
static j##type JNICALL fake_GetStatic##Type##Field(JNIEnv* env, jclass clazz, jfieldID fieldID) { \
    LOG_CALL_2("GetStatic" #Type "Field", "clazz=%p, fieldID=%p", clazz, fieldID); \
    increment_call_count("GetStatic" #Type "Field"); \
    return (j##type)0; \
}

DEFINE_GET_STATIC_FIELD(Boolean, boolean)
DEFINE_GET_STATIC_FIELD(Byte, byte)
DEFINE_GET_STATIC_FIELD(Char, char)
DEFINE_GET_STATIC_FIELD(Short, short)
DEFINE_GET_STATIC_FIELD(Int, int)
DEFINE_GET_STATIC_FIELD(Long, long)
DEFINE_GET_STATIC_FIELD(Float, float)
DEFINE_GET_STATIC_FIELD(Double, double)

static jobject JNICALL fake_GetStaticObjectField(JNIEnv* env, jclass clazz, jfieldID fieldID) {
    LOG_CALL_2("GetStaticObjectField", "clazz=%p, fieldID=%p", clazz, fieldID);
    increment_call_count("GetStaticObjectField");
    return (jobject)0x5014;
}

/* SetStatic*Field functions */
#define DEFINE_SET_STATIC_FIELD(Type, type) \
static void JNICALL fake_SetStatic##Type##Field(JNIEnv* env, jclass clazz, jfieldID fieldID, j##type value) { \
    LOG_CALL_2("SetStatic" #Type "Field", "clazz=%p, fieldID=%p", clazz, fieldID); \
    increment_call_count("SetStatic" #Type "Field"); \
}

DEFINE_SET_STATIC_FIELD(Boolean, boolean)
DEFINE_SET_STATIC_FIELD(Byte, byte)
DEFINE_SET_STATIC_FIELD(Char, char)
DEFINE_SET_STATIC_FIELD(Short, short)
DEFINE_SET_STATIC_FIELD(Int, int)
DEFINE_SET_STATIC_FIELD(Long, long)
DEFINE_SET_STATIC_FIELD(Float, float)
DEFINE_SET_STATIC_FIELD(Double, double)

static void JNICALL fake_SetStaticObjectField(JNIEnv* env, jclass clazz, jfieldID fieldID, jobject value) {
    LOG_CALL_3("SetStaticObjectField", "clazz=%p, fieldID=%p, value=%p", clazz, fieldID, value);
    increment_call_count("SetStaticObjectField");
}

/* ============================================
 * STRING OPERATIONS
 * ============================================ */

static jstring JNICALL fake_NewString(JNIEnv* env, const jchar* unicodeChars, jsize len) {
    jstring s = pool_intern_utf16(unicodeChars, len);

    /* Convert to UTF-8 for the log message */
    char* preview = sp_utf16_to_utf8(unicodeChars, len);
    LOG_CALL_3("NewString", "len=%d, preview=\"%s\", jstring=%p",
               len, preview ? preview : "", s);
    free(preview);

    const char* arg_names[] = {"env", "len", "jstring"};
    char arg_values[3][64];
    snprintf(arg_values[0], 64, "%p", (void*)env);
    snprintf(arg_values[1], 64, "%d", len);
    snprintf(arg_values[2], 64, "%p", (void*)s);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2]};
    log_jni_call_json("NewString", arg_names, arg_value_ptrs, 3);

    increment_call_count("NewString");
    return s;
}

static jsize JNICALL fake_GetStringLength(JNIEnv* env, jstring string) {
    StringEntry* e = pool_find(string);
    jsize len = e ? e->utf16_len : 0;
    LOG_CALL_2("GetStringLength", "string=%p, len=%d", string, len);
    const char* arg_names[] = {"env", "string", "len"};
    char arg_values[3][64];
    snprintf(arg_values[0], sizeof(arg_values[0]), "%p", (void*)env);
    snprintf(arg_values[1], sizeof(arg_values[1]), "%p", (void*)string);
    snprintf(arg_values[2], sizeof(arg_values[2]), "%d", len);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2]};
    log_jni_call_json("GetStringLength", arg_names, arg_value_ptrs, 3);
    increment_call_count("GetStringLength");
    return len;
}

static const jchar* JNICALL fake_GetStringChars(JNIEnv* env, jstring string, jboolean* isCopy) {
    StringEntry* e = pool_find(string);
    LOG_CALL_2("GetStringChars", "string=%p, utf8=\"%s\"",
               string, e ? e->utf8 : "(not in pool)");
    const char* arg_names[] = {"env", "string", "value"};
    char arg_values[3][256];
    snprintf(arg_values[0], sizeof(arg_values[0]), "%p", (void*)env);
    snprintf(arg_values[1], sizeof(arg_values[1]), "%p", (void*)string);
    snprintf(arg_values[2], sizeof(arg_values[2]), "%s", e ? e->utf8 : "");
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2]};
    log_jni_call_json("GetStringChars", arg_names, arg_value_ptrs, 3);
    increment_call_count("GetStringChars");
    if (isCopy) *isCopy = JNI_FALSE;
    return e ? e->utf16 : NULL;
}

static void JNICALL fake_ReleaseStringChars(JNIEnv* env, jstring string, const jchar* chars) {
    LOG_CALL_2("ReleaseStringChars", "string=%p, chars=%p", string, chars);
    const char* arg_names[] = {"env", "string", "chars"};
    char arg_values[3][64];
    snprintf(arg_values[0], sizeof(arg_values[0]), "%p", (void*)env);
    snprintf(arg_values[1], sizeof(arg_values[1]), "%p", (void*)string);
    snprintf(arg_values[2], sizeof(arg_values[2]), "%p", (void*)chars);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2]};
    log_jni_call_json("ReleaseStringChars", arg_names, arg_value_ptrs, 3);
    increment_call_count("ReleaseStringChars");
    /* Pool owns the UTF-16 buffer — nothing to free */
}

static jstring JNICALL fake_NewStringUTF(JNIEnv* env, const char* bytes) {
    jstring s = pool_intern_utf8(bytes);

    LOG_CALL_2("NewStringUTF", "bytes=\"%s\", jstring=%p",
               bytes ? bytes : "NULL", s);

    const char* arg_names[] = {"env", "bytes", "jstring"};
    char arg_values[3][256];
    snprintf(arg_values[0], 256, "%p", (void*)env);
    snprintf(arg_values[1], 256, "%s", bytes ? bytes : "NULL");
    snprintf(arg_values[2], 256, "%p", (void*)s);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2]};
    log_jni_call_json("NewStringUTF", arg_names, arg_value_ptrs, 3);

    increment_call_count("NewStringUTF");
    return s;
}

static jsize JNICALL fake_GetStringUTFLength(JNIEnv* env, jstring string) {
    StringEntry* e = pool_find(string);
    jsize len = e ? e->utf8_len : 0;
    LOG_CALL_2("GetStringUTFLength", "string=%p, len=%d", string, len);
    const char* arg_names[] = {"env", "string", "len"};
    char arg_values[3][64];
    snprintf(arg_values[0], sizeof(arg_values[0]), "%p", (void*)env);
    snprintf(arg_values[1], sizeof(arg_values[1]), "%p", (void*)string);
    snprintf(arg_values[2], sizeof(arg_values[2]), "%d", len);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2]};
    log_jni_call_json("GetStringUTFLength", arg_names, arg_value_ptrs, 3);
    increment_call_count("GetStringUTFLength");
    return len;
}

static const char* JNICALL fake_GetStringUTFChars(JNIEnv* env, jstring string, jboolean* isCopy) {
    StringEntry* e = pool_find(string);
    const char*  result = e ? e->utf8 : "";

    LOG_CALL_2("GetStringUTFChars", "string=%p, value=\"%s\"", string, result);

    const char* arg_names[] = {"env", "string", "value"};
    char arg_values[3][256];
    snprintf(arg_values[0], 256, "%p", (void*)env);
    snprintf(arg_values[1], 256, "%p", (void*)string);
    snprintf(arg_values[2], 256, "%s", result);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2]};
    log_jni_call_json("GetStringUTFChars", arg_names, arg_value_ptrs, 3);

    increment_call_count("GetStringUTFChars");
    if (isCopy) *isCopy = JNI_FALSE;
    return result;
}

static void JNICALL fake_ReleaseStringUTFChars(JNIEnv* env, jstring string, const char* utf) {
    LOG_CALL_2("ReleaseStringUTFChars", "string=%p, utf=\"%s\"",
               string, utf ? utf : "NULL");
    const char* arg_names[] = {"env", "string", "utf"};
    char arg_values[3][256];
    snprintf(arg_values[0], sizeof(arg_values[0]), "%p", (void*)env);
    snprintf(arg_values[1], sizeof(arg_values[1]), "%p", (void*)string);
    snprintf(arg_values[2], sizeof(arg_values[2]), "%s", utf ? utf : "NULL");
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2]};
    log_jni_call_json("ReleaseStringUTFChars", arg_names, arg_value_ptrs, 3);
    increment_call_count("ReleaseStringUTFChars");
    /* Pool owns the UTF-8 buffer — nothing to free */
}

/* ============================================
 * ARRAY OPERATIONS
 * ============================================ */

static jsize JNICALL fake_GetArrayLength(JNIEnv* env, jarray array) {
    LOG_CALL_1("GetArrayLength", "array=%p", array);
    
    const char* arg_names[] = {"env", "array"};
    char arg_values[2][64];
    snprintf(arg_values[0], 64, "%p", (void*)env);
    snprintf(arg_values[1], 64, "%p", (void*)array);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1]};
    log_jni_call_json("GetArrayLength", arg_names, arg_value_ptrs, 2);
    
    increment_call_count("GetArrayLength");
    return 0;
}

static jobjectArray JNICALL fake_NewObjectArray(JNIEnv* env, jsize length, jclass elementClass, jobject initialElement) {
    const char* class_name = get_class_name(elementClass);
    LOG_CALL_4("NewObjectArray", "length=%d, elementClass=%p [%s], initialElement=%p", length, elementClass, class_name, initialElement);
    
    const char* arg_names[] = {"env", "length", "elementClass", "class_name", "initialElement"};
    char arg_values[5][256];
    snprintf(arg_values[0], 256, "%p", (void*)env);
    snprintf(arg_values[1], 256, "%d", length);
    snprintf(arg_values[2], 256, "%p", (void*)elementClass);
    snprintf(arg_values[3], 256, "%s", class_name);
    snprintf(arg_values[4], 256, "%p", (void*)initialElement);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2], arg_values[3], arg_values[4]};
    log_jni_call_json("NewObjectArray", arg_names, arg_value_ptrs, 5);
    
    increment_call_count("NewObjectArray");
    return (jobjectArray)0x7000;
}

static jobject JNICALL fake_GetObjectArrayElement(JNIEnv* env, jobjectArray array, jsize index) {
    LOG_CALL_2("GetObjectArrayElement", "array=%p, index=%d", array, index);
    increment_call_count("GetObjectArrayElement");
    return (jobject)0x5015;
}

static void JNICALL fake_SetObjectArrayElement(JNIEnv* env, jobjectArray array, jsize index, jobject value) {
    LOG_CALL_3("SetObjectArrayElement", "array=%p, index=%d, value=%p", array, index, value);
    increment_call_count("SetObjectArrayElement");
}

/* New*Array functions */
#define DEFINE_NEW_ARRAY(Type, type) \
static j##type##Array JNICALL fake_New##Type##Array(JNIEnv* env, jsize length) { \
    LOG_CALL_1("New" #Type "Array", "length=%d", length); \
    increment_call_count("New" #Type "Array"); \
    return (j##type##Array)0x7001; \
}

DEFINE_NEW_ARRAY(Boolean, boolean)
DEFINE_NEW_ARRAY(Byte, byte)
DEFINE_NEW_ARRAY(Char, char)
DEFINE_NEW_ARRAY(Short, short)
DEFINE_NEW_ARRAY(Int, int)
DEFINE_NEW_ARRAY(Long, long)
DEFINE_NEW_ARRAY(Float, float)
DEFINE_NEW_ARRAY(Double, double)

/* Get*ArrayElements functions */
#define DEFINE_GET_ARRAY_ELEMENTS(Type, type) \
static j##type* JNICALL fake_Get##Type##ArrayElements(JNIEnv* env, j##type##Array array, jboolean* isCopy) { \
    LOG_CALL_1("Get" #Type "ArrayElements", "array=%p", array); \
    increment_call_count("Get" #Type "ArrayElements"); \
    if (isCopy) *isCopy = JNI_FALSE; \
    return NULL; \
}

DEFINE_GET_ARRAY_ELEMENTS(Boolean, boolean)
DEFINE_GET_ARRAY_ELEMENTS(Byte, byte)
DEFINE_GET_ARRAY_ELEMENTS(Char, char)
DEFINE_GET_ARRAY_ELEMENTS(Short, short)
DEFINE_GET_ARRAY_ELEMENTS(Int, int)
DEFINE_GET_ARRAY_ELEMENTS(Long, long)
DEFINE_GET_ARRAY_ELEMENTS(Float, float)
DEFINE_GET_ARRAY_ELEMENTS(Double, double)

/* Release*ArrayElements functions */
#define DEFINE_RELEASE_ARRAY_ELEMENTS(Type, type) \
static void JNICALL fake_Release##Type##ArrayElements(JNIEnv* env, j##type##Array array, j##type* elems, jint mode) { \
    LOG_CALL_2("Release" #Type "ArrayElements", "array=%p, mode=%d", array, mode); \
    increment_call_count("Release" #Type "ArrayElements"); \
}

DEFINE_RELEASE_ARRAY_ELEMENTS(Boolean, boolean)
DEFINE_RELEASE_ARRAY_ELEMENTS(Byte, byte)
DEFINE_RELEASE_ARRAY_ELEMENTS(Char, char)
DEFINE_RELEASE_ARRAY_ELEMENTS(Short, short)
DEFINE_RELEASE_ARRAY_ELEMENTS(Int, int)
DEFINE_RELEASE_ARRAY_ELEMENTS(Long, long)
DEFINE_RELEASE_ARRAY_ELEMENTS(Float, float)
DEFINE_RELEASE_ARRAY_ELEMENTS(Double, double)

/* Get*ArrayRegion functions */
#define DEFINE_GET_ARRAY_REGION(Type, type) \
static void JNICALL fake_Get##Type##ArrayRegion(JNIEnv* env, j##type##Array array, jsize start, jsize len, j##type* buf) { \
    LOG_CALL_3("Get" #Type "ArrayRegion", "array=%p, start=%d, len=%d", array, start, len); \
    increment_call_count("Get" #Type "ArrayRegion"); \
}

DEFINE_GET_ARRAY_REGION(Boolean, boolean)
DEFINE_GET_ARRAY_REGION(Byte, byte)
DEFINE_GET_ARRAY_REGION(Char, char)
DEFINE_GET_ARRAY_REGION(Short, short)
DEFINE_GET_ARRAY_REGION(Int, int)
DEFINE_GET_ARRAY_REGION(Long, long)
DEFINE_GET_ARRAY_REGION(Float, float)
DEFINE_GET_ARRAY_REGION(Double, double)

/* Set*ArrayRegion functions */
#define DEFINE_SET_ARRAY_REGION(Type, type) \
static void JNICALL fake_Set##Type##ArrayRegion(JNIEnv* env, j##type##Array array, jsize start, jsize len, const j##type* buf) { \
    LOG_CALL_3("Set" #Type "ArrayRegion", "array=%p, start=%d, len=%d", array, start, len); \
    increment_call_count("Set" #Type "ArrayRegion"); \
}

DEFINE_SET_ARRAY_REGION(Boolean, boolean)
DEFINE_SET_ARRAY_REGION(Byte, byte)
DEFINE_SET_ARRAY_REGION(Char, char)
DEFINE_SET_ARRAY_REGION(Short, short)
DEFINE_SET_ARRAY_REGION(Int, int)
DEFINE_SET_ARRAY_REGION(Long, long)
DEFINE_SET_ARRAY_REGION(Float, float)
DEFINE_SET_ARRAY_REGION(Double, double)

/* ============================================
 * REGISTER NATIVES
 * ============================================ */

static jint JNICALL fake_RegisterNatives(JNIEnv* env, jclass clazz, const JNINativeMethod* methods, jint nMethods) {
    const char* class_name = get_class_name(clazz);

    LOG_CALL_3("RegisterNatives", "clazz=%p [%s], nMethods=%d", clazz, class_name, nMethods);

    /* Log each method to text log */
    if (methods && nMethods > 0) {
        for (jint i = 0; i < nMethods; i++) {
            native_registry_register(class_name,
                                     methods[i].name,
                                     methods[i].signature,
                                     methods[i].fnPtr);
            log_info("  [%d] name=%s, signature=%s, fnPtr=%p",
                     i,
                     methods[i].name ? methods[i].name : "NULL",
                     methods[i].signature ? methods[i].signature : "NULL",
                     methods[i].fnPtr);
        }
    }

    /* Build a JSON methods array string inline */
    /* Each entry: {"name":"...","signature":"...","fnPtr":"0x..."} */
    /* Budget: nMethods * ~512 bytes should be more than enough */
    int methods_buf_size = (nMethods > 0 ? nMethods : 1) * 512 + 16;
    char* methods_json = (char*)malloc(methods_buf_size);
    if (methods_json) {
        int pos = 0;
        pos += snprintf(methods_json + pos, methods_buf_size - pos, "[");
        if (methods && nMethods > 0) {
            for (jint i = 0; i < nMethods; i++) {
                char* esc_name = escape_json_string(methods[i].name ? methods[i].name : "NULL");
                char* esc_sig  = escape_json_string(methods[i].signature ? methods[i].signature : "NULL");
                pos += snprintf(methods_json + pos, methods_buf_size - pos,
                                "%s{\"name\":%s,\"signature\":%s,\"fnPtr\":\"%p\"}",
                                (i > 0 ? "," : ""),
                                esc_name, esc_sig, methods[i].fnPtr);
                free_escaped_string(esc_name);
                free_escaped_string(esc_sig);
            }
        }
        snprintf(methods_json + pos, methods_buf_size - pos, "]");
    }

    /* Log to JSON: standard fields first, then methods array as raw JSON */
    const char* arg_names[] = {"env", "clazz", "class_name", "nMethods"};
    char arg_values[4][256];
    snprintf(arg_values[0], 256, "%p", (void*)env);
    snprintf(arg_values[1], 256, "%p", (void*)clazz);
    snprintf(arg_values[2], 256, "%s", class_name);
    snprintf(arg_values[3], 256, "%d", nMethods);
    const char* arg_value_ptrs[] = {arg_values[0], arg_values[1], arg_values[2], arg_values[3]};
    log_jni_call_json_raw_last("RegisterNatives",
                               arg_names, arg_value_ptrs, 4,
                               "methods", methods_json ? methods_json : "[]");

    if (methods_json) free(methods_json);

    increment_call_count("RegisterNatives");
    return JNI_OK;
}

static jint JNICALL fake_UnregisterNatives(JNIEnv* env, jclass clazz) {
    LOG_CALL_1("UnregisterNatives", "clazz=%p", clazz);
    increment_call_count("UnregisterNatives");
    return JNI_OK;
}

/* ============================================
 * MONITOR OPERATIONS
 * ============================================ */

static jint JNICALL fake_MonitorEnter(JNIEnv* env, jobject obj) {
    LOG_CALL_1("MonitorEnter", "obj=%p", obj);
    increment_call_count("MonitorEnter");
    return JNI_OK;
}

static jint JNICALL fake_MonitorExit(JNIEnv* env, jobject obj) {
    LOG_CALL_1("MonitorExit", "obj=%p", obj);
    increment_call_count("MonitorExit");
    return JNI_OK;
}

static jint JNICALL fake_GetJavaVM(JNIEnv* env, JavaVM** vm) {
    LOG_CALL_1("GetJavaVM", "vm=%p", vm);
    increment_call_count("GetJavaVM");
    if (vm) {
        *vm = g_fake_javavm;  /* Return registered fake JavaVM, not NULL */
    }
    return JNI_OK;
}

/* ============================================
 * STRING REGION OPERATIONS
 * ============================================ */

static void JNICALL fake_GetStringRegion(JNIEnv* env, jstring str, jsize start, jsize len, jchar* buf) {
    LOG_CALL_3("GetStringRegion", "str=%p, start=%d, len=%d", str, start, len);
    increment_call_count("GetStringRegion");
    if (!buf || len <= 0) return;
    StringEntry* e = pool_find(str);
    if (e && e->utf16 && start >= 0 && (start + len) <= e->utf16_len) {
        memcpy(buf, e->utf16 + start, (size_t)len * sizeof(jchar));
    } else {
        memset(buf, 0, (size_t)len * sizeof(jchar));
    }
}

static void JNICALL fake_GetStringUTFRegion(JNIEnv* env, jstring str, jsize start, jsize len, char* buf) {
    LOG_CALL_3("GetStringUTFRegion", "str=%p, start=%d, len=%d", str, start, len);
    increment_call_count("GetStringUTFRegion");
    if (!buf || len <= 0) return;
    StringEntry* e = pool_find(str);
    /* JNI spec: start/len are in UTF-16 units; output is UTF-8 */
    if (e && e->utf16 && start >= 0 && (start + len) <= e->utf16_len) {
        char* sub = sp_utf16_to_utf8(e->utf16 + start, len);
        if (sub) {
            size_t sub_len = strlen(sub);
            memcpy(buf, sub, sub_len);
            buf[sub_len] = '\0';
            free(sub);
        } else {
            memset(buf, 0, (size_t)len);
        }
    } else {
        memset(buf, 0, (size_t)len);
    }
}

/* ============================================
 * CRITICAL OPERATIONS
 * ============================================ */

static void* JNICALL fake_GetPrimitiveArrayCritical(JNIEnv* env, jarray array, jboolean* isCopy) {
    LOG_CALL_1("GetPrimitiveArrayCritical", "array=%p", array);
    increment_call_count("GetPrimitiveArrayCritical");
    if (isCopy) *isCopy = JNI_FALSE;
    return NULL;
}

static void JNICALL fake_ReleasePrimitiveArrayCritical(JNIEnv* env, jarray array, void* carray, jint mode) {
    LOG_CALL_2("ReleasePrimitiveArrayCritical", "array=%p, mode=%d", array, mode);
    increment_call_count("ReleasePrimitiveArrayCritical");
}

static const jchar* JNICALL fake_GetStringCritical(JNIEnv* env, jstring string, jboolean* isCopy) {
    StringEntry* e = pool_find(string);
    LOG_CALL_2("GetStringCritical", "string=%p, utf8=\"%s\"",
               string, e ? e->utf8 : "(not in pool)");
    increment_call_count("GetStringCritical");
    if (isCopy) *isCopy = JNI_FALSE;
    return e ? e->utf16 : NULL;
}

static void JNICALL fake_ReleaseStringCritical(JNIEnv* env, jstring string, const jchar* carray) {
    LOG_CALL_2("ReleaseStringCritical", "string=%p, carray=%p", string, carray);
    increment_call_count("ReleaseStringCritical");
    /* Pool owns the buffer — nothing to free */
}

/* ============================================
 * WEAK GLOBAL REFERENCES
 * ============================================ */

static jweak JNICALL fake_NewWeakGlobalRef(JNIEnv* env, jobject obj) {
    LOG_CALL_1("NewWeakGlobalRef", "obj=%p", obj);
    increment_call_count("NewWeakGlobalRef");
    return (jweak)obj;
}

static void JNICALL fake_DeleteWeakGlobalRef(JNIEnv* env, jweak obj) {
    LOG_CALL_1("DeleteWeakGlobalRef", "obj=%p", obj);
    increment_call_count("DeleteWeakGlobalRef");
}

/* ============================================
 * EXCEPTION CHECK
 * ============================================ */

static jboolean JNICALL fake_ExceptionCheck(JNIEnv* env) {
    LOG_CALL_START("ExceptionCheck");
    LOG_SIMPLE_JSON_1("ExceptionCheck", env, "return_value", "false");
    increment_call_count("ExceptionCheck");
    return JNI_FALSE;
}

/* ============================================
 * NIO SUPPORT
 * ============================================ */

static jobject JNICALL fake_NewDirectByteBuffer(JNIEnv* env, void* address, jlong capacity) {
    LOG_CALL_2("NewDirectByteBuffer", "address=%p, capacity=%lld", address, (long long)capacity);
    increment_call_count("NewDirectByteBuffer");
    return (jobject)0x8000;
}

static void* JNICALL fake_GetDirectBufferAddress(JNIEnv* env, jobject buf) {
    LOG_CALL_1("GetDirectBufferAddress", "buf=%p", buf);
    increment_call_count("GetDirectBufferAddress");
    return NULL;
}

static jlong JNICALL fake_GetDirectBufferCapacity(JNIEnv* env, jobject buf) {
    LOG_CALL_1("GetDirectBufferCapacity", "buf=%p", buf);
    increment_call_count("GetDirectBufferCapacity");
    return 0;
}

/* ============================================
 * OBJECT REF TYPE
 * ============================================ */

static jobjectRefType JNICALL fake_GetObjectRefType(JNIEnv* env, jobject obj) {
    LOG_CALL_1("GetObjectRefType", "obj=%p", obj);
    increment_call_count("GetObjectRefType");
    return JNILocalRefType;
}

/* ============================================
 * FUNCTION TABLE DEFINITION
 * ============================================ */

static struct JNINativeInterface g_fake_jni_funcs = {
    NULL, NULL, NULL, NULL, /* reserved0-3 */
    
    fake_GetVersion,
    
    fake_DefineClass,
    fake_FindClass,
    
    fake_FromReflectedMethod,
    fake_FromReflectedField,
    fake_ToReflectedMethod,
    
    fake_GetSuperclass,
    fake_IsAssignableFrom,
    
    fake_ToReflectedField,
    
    fake_Throw,
    fake_ThrowNew,
    fake_ExceptionOccurred,
    fake_ExceptionDescribe,
    fake_ExceptionClear,
    fake_FatalError,
    
    fake_PushLocalFrame,
    fake_PopLocalFrame,
    
    fake_NewGlobalRef,
    fake_DeleteGlobalRef,
    fake_DeleteLocalRef,
    fake_IsSameObject,
    fake_NewLocalRef,
    fake_EnsureLocalCapacity,
    
    fake_AllocObject,
    fake_NewObject,
    fake_NewObjectV,
    fake_NewObjectA,
    
    fake_GetObjectClass,
    fake_IsInstanceOf,
    
    fake_GetMethodID,
    
    fake_CallObjectMethod,
    fake_CallObjectMethodV,
    fake_CallObjectMethodA,
    fake_CallBooleanMethod,
    fake_CallBooleanMethodV,
    fake_CallBooleanMethodA,
    fake_CallByteMethod,
    fake_CallByteMethodV,
    fake_CallByteMethodA,
    fake_CallCharMethod,
    fake_CallCharMethodV,
    fake_CallCharMethodA,
    fake_CallShortMethod,
    fake_CallShortMethodV,
    fake_CallShortMethodA,
    fake_CallIntMethod,
    fake_CallIntMethodV,
    fake_CallIntMethodA,
    fake_CallLongMethod,
    fake_CallLongMethodV,
    fake_CallLongMethodA,
    fake_CallFloatMethod,
    fake_CallFloatMethodV,
    fake_CallFloatMethodA,
    fake_CallDoubleMethod,
    fake_CallDoubleMethodV,
    fake_CallDoubleMethodA,
    fake_CallVoidMethod,
    fake_CallVoidMethodV,
    fake_CallVoidMethodA,
    
    fake_CallNonvirtualObjectMethod,
    fake_CallNonvirtualObjectMethodV,
    fake_CallNonvirtualObjectMethodA,
    fake_CallNonvirtualBooleanMethod,
    fake_CallNonvirtualBooleanMethodV,
    fake_CallNonvirtualBooleanMethodA,
    fake_CallNonvirtualByteMethod,
    fake_CallNonvirtualByteMethodV,
    fake_CallNonvirtualByteMethodA,
    fake_CallNonvirtualCharMethod,
    fake_CallNonvirtualCharMethodV,
    fake_CallNonvirtualCharMethodA,
    fake_CallNonvirtualShortMethod,
    fake_CallNonvirtualShortMethodV,
    fake_CallNonvirtualShortMethodA,
    fake_CallNonvirtualIntMethod,
    fake_CallNonvirtualIntMethodV,
    fake_CallNonvirtualIntMethodA,
    fake_CallNonvirtualLongMethod,
    fake_CallNonvirtualLongMethodV,
    fake_CallNonvirtualLongMethodA,
    fake_CallNonvirtualFloatMethod,
    fake_CallNonvirtualFloatMethodV,
    fake_CallNonvirtualFloatMethodA,
    fake_CallNonvirtualDoubleMethod,
    fake_CallNonvirtualDoubleMethodV,
    fake_CallNonvirtualDoubleMethodA,
    fake_CallNonvirtualVoidMethod,
    fake_CallNonvirtualVoidMethodV,
    fake_CallNonvirtualVoidMethodA,
    
    fake_GetFieldID,
    
    fake_GetObjectField,
    fake_GetBooleanField,
    fake_GetByteField,
    fake_GetCharField,
    fake_GetShortField,
    fake_GetIntField,
    fake_GetLongField,
    fake_GetFloatField,
    fake_GetDoubleField,
    
    fake_SetObjectField,
    fake_SetBooleanField,
    fake_SetByteField,
    fake_SetCharField,
    fake_SetShortField,
    fake_SetIntField,
    fake_SetLongField,
    fake_SetFloatField,
    fake_SetDoubleField,
    
    fake_GetStaticMethodID,
    
    fake_CallStaticObjectMethod,
    fake_CallStaticObjectMethodV,
    fake_CallStaticObjectMethodA,
    fake_CallStaticBooleanMethod,
    fake_CallStaticBooleanMethodV,
    fake_CallStaticBooleanMethodA,
    fake_CallStaticByteMethod,
    fake_CallStaticByteMethodV,
    fake_CallStaticByteMethodA,
    fake_CallStaticCharMethod,
    fake_CallStaticCharMethodV,
    fake_CallStaticCharMethodA,
    fake_CallStaticShortMethod,
    fake_CallStaticShortMethodV,
    fake_CallStaticShortMethodA,
    fake_CallStaticIntMethod,
    fake_CallStaticIntMethodV,
    fake_CallStaticIntMethodA,
    fake_CallStaticLongMethod,
    fake_CallStaticLongMethodV,
    fake_CallStaticLongMethodA,
    fake_CallStaticFloatMethod,
    fake_CallStaticFloatMethodV,
    fake_CallStaticFloatMethodA,
    fake_CallStaticDoubleMethod,
    fake_CallStaticDoubleMethodV,
    fake_CallStaticDoubleMethodA,
    fake_CallStaticVoidMethod,
    fake_CallStaticVoidMethodV,
    fake_CallStaticVoidMethodA,
    
    fake_GetStaticFieldID,
    
    fake_GetStaticObjectField,
    fake_GetStaticBooleanField,
    fake_GetStaticByteField,
    fake_GetStaticCharField,
    fake_GetStaticShortField,
    fake_GetStaticIntField,
    fake_GetStaticLongField,
    fake_GetStaticFloatField,
    fake_GetStaticDoubleField,
    
    fake_SetStaticObjectField,
    fake_SetStaticBooleanField,
    fake_SetStaticByteField,
    fake_SetStaticCharField,
    fake_SetStaticShortField,
    fake_SetStaticIntField,
    fake_SetStaticLongField,
    fake_SetStaticFloatField,
    fake_SetStaticDoubleField,
    
    fake_NewString,
    fake_GetStringLength,
    fake_GetStringChars,
    fake_ReleaseStringChars,
    
    fake_NewStringUTF,
    fake_GetStringUTFLength,
    fake_GetStringUTFChars,
    fake_ReleaseStringUTFChars,
    
    fake_GetArrayLength,
    
    fake_NewObjectArray,
    fake_GetObjectArrayElement,
    fake_SetObjectArrayElement,
    
    fake_NewBooleanArray,
    fake_NewByteArray,
    fake_NewCharArray,
    fake_NewShortArray,
    fake_NewIntArray,
    fake_NewLongArray,
    fake_NewFloatArray,
    fake_NewDoubleArray,
    
    fake_GetBooleanArrayElements,
    fake_GetByteArrayElements,
    fake_GetCharArrayElements,
    fake_GetShortArrayElements,
    fake_GetIntArrayElements,
    fake_GetLongArrayElements,
    fake_GetFloatArrayElements,
    fake_GetDoubleArrayElements,
    
    fake_ReleaseBooleanArrayElements,
    fake_ReleaseByteArrayElements,
    fake_ReleaseCharArrayElements,
    fake_ReleaseShortArrayElements,
    fake_ReleaseIntArrayElements,
    fake_ReleaseLongArrayElements,
    fake_ReleaseFloatArrayElements,
    fake_ReleaseDoubleArrayElements,
    
    fake_GetBooleanArrayRegion,
    fake_GetByteArrayRegion,
    fake_GetCharArrayRegion,
    fake_GetShortArrayRegion,
    fake_GetIntArrayRegion,
    fake_GetLongArrayRegion,
    fake_GetFloatArrayRegion,
    fake_GetDoubleArrayRegion,
    
    fake_SetBooleanArrayRegion,
    fake_SetByteArrayRegion,
    fake_SetCharArrayRegion,
    fake_SetShortArrayRegion,
    fake_SetIntArrayRegion,
    fake_SetLongArrayRegion,
    fake_SetFloatArrayRegion,
    fake_SetDoubleArrayRegion,
    
    fake_RegisterNatives,
    fake_UnregisterNatives,
    
    fake_MonitorEnter,
    fake_MonitorExit,
    
    fake_GetJavaVM,
    
    fake_GetStringRegion,
    fake_GetStringUTFRegion,
    
    fake_GetPrimitiveArrayCritical,
    fake_ReleasePrimitiveArrayCritical,
    
    fake_GetStringCritical,
    fake_ReleaseStringCritical,
    
    fake_NewWeakGlobalRef,
    fake_DeleteWeakGlobalRef,
    
    fake_ExceptionCheck,
    
    fake_NewDirectByteBuffer,
    fake_GetDirectBufferAddress,
    fake_GetDirectBufferCapacity,
    
    fake_GetObjectRefType
};

/* ============================================
 * PUBLIC API
 * ============================================ */

static struct _JNIEnv g_fake_jnienv;

JNIEnv* create_fake_jnienv(void) {
    g_fake_jnienv.functions = &g_fake_jni_funcs;
    
    log_info("Fake JNIEnv created with full function table (%zu functions)", 
             sizeof(g_fake_jni_funcs) / sizeof(void*));
    
    return &g_fake_jnienv;
}

void destroy_fake_jnienv(JNIEnv* env) {
    (void)env;
    pool_destroy();
}
