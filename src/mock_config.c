/*
 * Mock Config — runtime return-value injection for JNI Call*Method stubs.
 *
 * Parses a JSON file of the form:
 * {
 *   "bool_returns":   [{"class":"...", "method":"...", "sig":"...", "return": true}],
 *   "int_returns":    [{"class":"...", "method":"...", "sig":"...", "return": 42}],
 *   "string_returns": [{"class":"...", "method":"...", "sig":"...", "return": "..."}]
 * }
 *
 * No external JSON library is required.
 */

#include "mock_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ============================================================
 * Storage
 * ============================================================ */

#define MAX_MOCK_ENTRIES 256

static MockEntry mock_entries[MAX_MOCK_ENTRIES];
static int       mock_entry_count = 0;

/* ============================================================
 * Minimal hand-rolled JSON helpers
 * ============================================================ */

/* Skip whitespace.  Returns pointer past whitespace. */
static const char* skip_ws(const char* p) {
    while (p && *p && isspace((unsigned char)*p))
        p++;
    return p;
}

/* Expect a literal character.  Advances *pp past it on success.
 * Returns 1 on success, 0 on failure. */
static int expect_char(const char** pp, char c) {
    *pp = skip_ws(*pp);
    if (!*pp || **pp != c)
        return 0;
    (*pp)++;
    return 1;
}

/* Parse a JSON string starting at *pp (which must point at the opening '"').
 * Writes at most buf_size-1 bytes into buf (null-terminated).
 * Handles \" \\ \/ \n \r \t \b \f \uXXXX (BMP only → UTF-8).
 * Returns 1 on success, 0 on error.  Advances *pp past the closing '"'. */
static int parse_json_string(const char** pp, char* buf, size_t buf_size) {
    const char* p = skip_ws(*pp);
    if (!p || *p != '"')
        return 0;
    p++; /* skip opening quote */

    size_t out = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"': case '\\': case '/':
                    if (out + 1 < buf_size) buf[out++] = *p;
                    p++;
                    break;
                case 'n':
                    if (out + 1 < buf_size) buf[out++] = '\n';
                    p++;
                    break;
                case 'r':
                    if (out + 1 < buf_size) buf[out++] = '\r';
                    p++;
                    break;
                case 't':
                    if (out + 1 < buf_size) buf[out++] = '\t';
                    p++;
                    break;
                case 'b':
                    if (out + 1 < buf_size) buf[out++] = '\b';
                    p++;
                    break;
                case 'f':
                    if (out + 1 < buf_size) buf[out++] = '\f';
                    p++;
                    break;
                case 'u': {
                    /* \uXXXX */
                    p++;
                    unsigned int cp = 0;
                    int i;
                    for (i = 0; i < 4 && *p; i++, p++) {
                        cp <<= 4;
                        char c = *p;
                        if      (c >= '0' && c <= '9') cp |= (unsigned)(c - '0');
                        else if (c >= 'a' && c <= 'f') cp |= (unsigned)(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') cp |= (unsigned)(c - 'A' + 10);
                        else { cp = 0xFFFD; break; }
                    }
                    /* Encode as UTF-8 */
                    if (cp < 0x80) {
                        if (out + 1 < buf_size) buf[out++] = (char)cp;
                    } else if (cp < 0x800) {
                        if (out + 2 < buf_size) {
                            buf[out++] = (char)(0xC0 | (cp >> 6));
                            buf[out++] = (char)(0x80 | (cp & 0x3F));
                        }
                    } else {
                        if (out + 3 < buf_size) {
                            buf[out++] = (char)(0xE0 | (cp >> 12));
                            buf[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            buf[out++] = (char)(0x80 | (cp & 0x3F));
                        }
                    }
                    break;
                }
                default:
                    /* Unknown escape: copy literally */
                    if (out + 1 < buf_size) buf[out++] = *p;
                    p++;
                    break;
            }
        } else {
            if (out + 1 < buf_size) buf[out++] = *p;
            p++;
        }
    }
    if (*p != '"')
        return 0; /* unterminated string */
    p++; /* skip closing quote */
    if (buf_size > 0) buf[out] = '\0';
    *pp = p;
    return 1;
}

/* Parse a JSON integer (optionally negative).
 * Advances *pp.  Returns 1 on success. */
static int parse_json_integer(const char** pp, long long* out) {
    const char* p = skip_ws(*pp);
    if (!p) return 0;

    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (!isdigit((unsigned char)*p)) return 0;

    long long val = 0;
    while (isdigit((unsigned char)*p)) {
        val = val * 10 + (*p - '0');
        p++;
    }
    *out = neg ? -val : val;
    *pp = p;
    return 1;
}

/* Skip over any JSON value (string, number, bool, null, array, object).
 * Used to skip unknown keys.  Returns pointer past the value. */
static const char* skip_json_value(const char* p) {
    p = skip_ws(p);
    if (!p || !*p) return p;
    if (*p == '"') {
        /* string */
        p++;
        while (*p && *p != '"') {
            if (*p == '\\') p++;
            if (*p) p++;
        }
        if (*p == '"') p++;
        return p;
    }
    if (*p == '{') {
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                p++;
                while (*p && *p != '"') { if (*p == '\\') p++; if (*p) p++; }
                if (*p == '"') p++;
            } else if (*p == '{') { depth++; p++; }
            else if (*p == '}') { depth--; p++; }
            else p++;
        }
        return p;
    }
    if (*p == '[') {
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                p++;
                while (*p && *p != '"') { if (*p == '\\') p++; if (*p) p++; }
                if (*p == '"') p++;
            } else if (*p == '[') { depth++; p++; }
            else if (*p == ']') { depth--; p++; }
            else p++;
        }
        return p;
    }
    /* number, bool, null */
    while (*p && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p))
        p++;
    return p;
}

/* ============================================================
 * Section parsers
 * ============================================================ */

typedef enum { SECTION_BOOL, SECTION_INT, SECTION_STRING } SectionKind;

/*
 * Parse an array of objects like:
 *   [{"class":"...", "method":"...", "sig":"...", "return": <value>}, ...]
 *
 * *pp must point at the '['.
 * kind controls how "return" is interpreted.
 */
static void parse_section(const char** pp, SectionKind kind) {
    if (!expect_char(pp, '[')) {
        fprintf(stderr, "[mock_config] Expected '[' at start of section\n");
        return;
    }

    while (1) {
        *pp = skip_ws(*pp);
        if (!*pp || !**pp) break;
        if (**pp == ']') { (*pp)++; break; }
        if (**pp == ',') { (*pp)++; continue; }

        /* Parse one object { ... } */
        if (!expect_char(pp, '{')) {
            fprintf(stderr, "[mock_config] Expected '{' for array element\n");
            return;
        }

        char class_name[256]  = {0};
        char method_name[128] = {0};
        char sig[256]         = {0};
        int  has_class  = 0;
        int  has_method = 0;
        int  has_sig    = 0;
        int  has_return = 0;
        long long int_ret = 0;
        char str_ret[512] = {0};

        while (1) {
            *pp = skip_ws(*pp);
            if (!*pp || !**pp) break;
            if (**pp == '}') { (*pp)++; break; }
            if (**pp == ',') { (*pp)++; continue; }

            /* Key */
            char key[64] = {0};
            if (!parse_json_string(pp, key, sizeof(key))) {
                fprintf(stderr, "[mock_config] Failed to parse object key\n");
                return;
            }
            if (!expect_char(pp, ':')) {
                fprintf(stderr, "[mock_config] Expected ':' after key \"%s\"\n", key);
                return;
            }

            *pp = skip_ws(*pp);

            if (strcmp(key, "class") == 0) {
                if (parse_json_string(pp, class_name, sizeof(class_name)))
                    has_class = 1;
                else
                    fprintf(stderr, "[mock_config] Failed to parse 'class' value\n");
            } else if (strcmp(key, "method") == 0) {
                if (parse_json_string(pp, method_name, sizeof(method_name)))
                    has_method = 1;
                else
                    fprintf(stderr, "[mock_config] Failed to parse 'method' value\n");
            } else if (strcmp(key, "sig") == 0) {
                if (parse_json_string(pp, sig, sizeof(sig)))
                    has_sig = 1;
                else
                    fprintf(stderr, "[mock_config] Failed to parse 'sig' value\n");
            } else if (strcmp(key, "return") == 0) {
                if (kind == SECTION_STRING) {
                    if (parse_json_string(pp, str_ret, sizeof(str_ret)))
                        has_return = 1;
                    else
                        fprintf(stderr, "[mock_config] Failed to parse string 'return' value\n");
                } else if (kind == SECTION_BOOL) {
                    *pp = skip_ws(*pp);
                    if (strncmp(*pp, "true", 4) == 0) {
                        int_ret = 1; has_return = 1; *pp += 4;
                    } else if (strncmp(*pp, "false", 5) == 0) {
                        int_ret = 0; has_return = 1; *pp += 5;
                    } else {
                        fprintf(stderr, "[mock_config] Expected 'true' or 'false' for bool return\n");
                        *pp = skip_json_value(*pp);
                    }
                } else { /* SECTION_INT */
                    if (parse_json_integer(pp, &int_ret))
                        has_return = 1;
                    else
                        fprintf(stderr, "[mock_config] Failed to parse integer 'return' value\n");
                }
            } else {
                /* Unknown key: skip value */
                *pp = skip_json_value(*pp);
            }
        }

        /* Validate and store entry */
        if (!has_class || !has_method || !has_return) {
            fprintf(stderr, "[mock_config] Skipping incomplete entry (missing class=%d method=%d return=%d)\n",
                    has_class, has_method, has_return);
            continue;
        }
        if (mock_entry_count >= MAX_MOCK_ENTRIES) {
            fprintf(stderr, "[mock_config] Too many mock entries (max %d), skipping rest\n", MAX_MOCK_ENTRIES);
            return;
        }

        MockEntry* e = &mock_entries[mock_entry_count++];
        strncpy(e->class_name,  class_name,  sizeof(e->class_name)  - 1);
        strncpy(e->method_name, method_name, sizeof(e->method_name) - 1);
        strncpy(e->signature,   sig,         sizeof(e->signature)   - 1);
        e->class_name [sizeof(e->class_name)  - 1] = '\0';
        e->method_name[sizeof(e->method_name) - 1] = '\0';
        e->signature  [sizeof(e->signature)   - 1] = '\0';

        if (kind == SECTION_STRING) {
            e->type = MOCK_TYPE_STRING;
            strncpy(e->str_value, str_ret, sizeof(e->str_value) - 1);
            e->str_value[sizeof(e->str_value) - 1] = '\0';
            e->prim_value = 0;
        } else {
            e->type = MOCK_TYPE_PRIMITIVE;
            e->prim_value = (jlong)int_ret;
            e->str_value[0] = '\0';
        }
    }
}

/* ============================================================
 * Public API
 * ============================================================ */

int mock_load(const char* path) {
    if (!path) {
        fprintf(stderr, "[mock_config] mock_load: NULL path\n");
        return -1;
    }

    /* Read entire file into memory */
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[mock_config] Cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "[mock_config] fseek failed for '%s'\n", path);
        fclose(f);
        return -1;
    }
    long file_size = ftell(f);
    if (file_size < 0) {
        fprintf(stderr, "[mock_config] ftell failed for '%s'\n", path);
        fclose(f);
        return -1;
    }
    rewind(f);

    char* buf = (char*)malloc((size_t)file_size + 1);
    if (!buf) {
        fprintf(stderr, "[mock_config] Out of memory reading '%s'\n", path);
        fclose(f);
        return -1;
    }

    size_t nread = fread(buf, 1, (size_t)file_size, f);
    fclose(f);
    buf[nread] = '\0';

    if (nread == 0) {
        fprintf(stderr, "[mock_config] Empty file '%s'\n", path);
        free(buf);
        return -1;
    }

    /* Reset table */
    mock_entry_count = 0;

    /* Parse top-level object */
    const char* p = buf;
    if (!expect_char(&p, '{')) {
        fprintf(stderr, "[mock_config] Expected '{' at start of JSON in '%s'\n", path);
        free(buf);
        return -1;
    }

    while (1) {
        p = skip_ws(p);
        if (!p || !*p) break;
        if (*p == '}') { p++; break; }
        if (*p == ',') { p++; continue; }

        char key[64] = {0};
        if (!parse_json_string(&p, key, sizeof(key))) {
            fprintf(stderr, "[mock_config] Failed to parse top-level key\n");
            break;
        }
        if (!expect_char(&p, ':')) {
            fprintf(stderr, "[mock_config] Expected ':' after top-level key '%s'\n", key);
            break;
        }
        p = skip_ws(p);

        if (strcmp(key, "bool_returns") == 0) {
            parse_section(&p, SECTION_BOOL);
        } else if (strcmp(key, "int_returns") == 0) {
            parse_section(&p, SECTION_INT);
        } else if (strcmp(key, "string_returns") == 0) {
            parse_section(&p, SECTION_STRING);
        } else {
            /* Unknown top-level key: skip value */
            p = skip_json_value(p);
        }
    }

    free(buf);

    /* Print summary (only when verbose) */
    const char* verbose = getenv("JNI_TRACER_VERBOSE");
    if (verbose && atoi(verbose) > 0) {
        fprintf(stderr, "[mock_config] Loaded %d mock entries from '%s'\n",
                mock_entry_count, path);
    }

    return 0;
}

int mock_get_primitive(const char* class_name, const char* method_name,
                       const char* sig, jlong* out_val) {
    if (!class_name || !method_name || !out_val) return 0;
    const char* sg = sig ? sig : "";

    for (int i = 0; i < mock_entry_count; i++) {
        MockEntry* e = &mock_entries[i];
        if (e->type != MOCK_TYPE_PRIMITIVE) continue;
        if (strcmp(e->class_name,  class_name)  != 0) continue;
        if (strcmp(e->method_name, method_name) != 0) continue;
        if (*e->signature && strcmp(e->signature, sg) != 0) continue;
        *out_val = e->prim_value;
        return 1;
    }
    return 0;
}

int mock_get_string(const char* class_name, const char* method_name,
                    const char* sig, char* buf, size_t size) {
    if (!class_name || !method_name || !buf || size == 0) return 0;
    const char* sg = sig ? sig : "";

    for (int i = 0; i < mock_entry_count; i++) {
        MockEntry* e = &mock_entries[i];
        if (e->type != MOCK_TYPE_STRING) continue;
        if (strcmp(e->class_name,  class_name)  != 0) continue;
        if (strcmp(e->method_name, method_name) != 0) continue;
        if (*e->signature && strcmp(e->signature, sg) != 0) continue;
        strncpy(buf, e->str_value, size - 1);
        buf[size - 1] = '\0';
        return 1;
    }
    return 0;
}

int mock_count(void) {
    return mock_entry_count;
}

void mock_destroy(void) {
    mock_entry_count = 0;
}
