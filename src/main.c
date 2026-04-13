/*
 * Main Harness Program
 * Loads target SO file and executes JNI_OnLoad with fake JNIEnv
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(int argc, char* argv[]) {
    const char* so_path   = NULL;
    const char* mock_path = NULL;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--mock") == 0 || strcmp(argv[i], "-m") == 0) && i + 1 < argc) {
            mock_path = argv[++i];
        } else if (argv[i][0] != '-') {
            so_path = argv[i];
        }
    }

    if (!so_path) {
        fprintf(stderr, "Usage: %s [--mock <config.json>] <target_so_path>\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  --mock, -m <file>  Load mock return-value config (JSON)\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Description:\n");
        fprintf(stderr, "  JNI Function Hooking Harness for Android SO files\n");
        fprintf(stderr, "  Logs all JNI function calls to text and JSON formats\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s target/libnative.so\n", argv[0]);
        fprintf(stderr, "  %s --mock mock.json target/libnative.so\n", argv[0]);
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

        /* Step 3: Prepare fake JavaVM */
        FakeJavaVM fake_vm;
        fake_vm.functions = &fake_vm_funcs;

        /* Register fake JavaVM so GetJavaVM() stub can return the correct pointer */
        set_fake_javavm((JavaVM*)&fake_vm);

        log_info("Fake JavaVM created");

        /* Step 4: Prepare fake JNIEnv */
        JNIEnv* fake_env = create_fake_jnienv();
        
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

    /* Step 6: Optional - Try to find and call other exported functions */
    /* This can be extended to call other JNI functions found in the SO */

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
