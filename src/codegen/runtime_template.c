#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <setjmp.h>

/* ── Dino dynamic value runtime ────────────────────────────────────────────
   Every Dino value is a DinoValue: a tagged union that can hold null, a bool,
   an integer, a float, a string, an array or a dictionary. Arrays and
   dictionaries are reference types and can hold mixed values and grow. */

typedef enum { DINO_NULL, DINO_BOOL, DINO_INT, DINO_FLOAT, DINO_STRING, DINO_ARRAY, DINO_DICT } DinoType;

typedef struct DinoValue DinoValue;
typedef struct DinoArray DinoArray;
typedef struct DinoDict DinoDict;

struct DinoArray { size_t len; size_t cap; DinoValue *items; };
struct DinoDict { size_t len; size_t cap; DinoValue *keys; DinoValue *vals; };
struct DinoValue {
    DinoType type;
    union {
        long long i;
        double f;
        int b;
        const char *s;
        DinoArray *arr;
        DinoDict *dict;
    } as;
};

static DinoValue _dino_null(void);
static DinoValue _dino_bool(int b);
static DinoValue _dino_int(long long i);
static DinoValue _dino_float(double f);
static DinoValue _dino_str(const char *s);
static const char *_dino_cstr(DinoValue v);
static int _dino_equals(DinoValue a, DinoValue b);
static void _dino_throw(DinoValue v) __attribute__((noreturn));

/* Rotating buffers so that several values can be rendered in one expression. */
#define _DINO_POOL 16
static char _dino_pool[_DINO_POOL][1024];
static int _dino_pool_idx = 0;
static char *_dino_pool_next(void) {
    char *b = _dino_pool[_dino_pool_idx];
    _dino_pool_idx = (_dino_pool_idx + 1) % _DINO_POOL;
    b[0] = '\0';
    return b;
}

static void _dino_render(char *dst, size_t cap, DinoValue v) {
    size_t len = strlen(dst);
    size_t rem = cap > len ? cap - len : 0;
    char *p = dst + len;
    switch (v.type) {
        case DINO_NULL: snprintf(p, rem, "null"); break;
        case DINO_BOOL: snprintf(p, rem, "%s", v.as.b ? "true" : "false"); break;
        case DINO_INT: snprintf(p, rem, "%lld", v.as.i); break;
        case DINO_FLOAT: snprintf(p, rem, "%g", v.as.f); break;
        case DINO_STRING: snprintf(p, rem, "%s", v.as.s ? v.as.s : ""); break;
        case DINO_ARRAY:
            snprintf(p, rem, "[");
            for (size_t i = 0; i < v.as.arr->len; i++) {
                if (i) { len = strlen(dst); snprintf(dst + len, cap > len ? cap - len : 0, ", "); }
                _dino_render(dst, cap, v.as.arr->items[i]);
            }
            len = strlen(dst); snprintf(dst + len, cap > len ? cap - len : 0, "]");
            break;
        case DINO_DICT:
            snprintf(p, rem, "{");
            for (size_t i = 0; i < v.as.dict->len; i++) {
                if (i) { len = strlen(dst); snprintf(dst + len, cap > len ? cap - len : 0, ", "); }
                _dino_render(dst, cap, v.as.dict->keys[i]);
                len = strlen(dst); snprintf(dst + len, cap > len ? cap - len : 0, ": ");
                _dino_render(dst, cap, v.as.dict->vals[i]);
            }
            len = strlen(dst); snprintf(dst + len, cap > len ? cap - len : 0, "}");
            break;
    }
}

static const char *_dino_cstr(DinoValue v) {
    if (v.type == DINO_STRING) return v.as.s ? v.as.s : "";
    char *b = _dino_pool_next();
    _dino_render(b, 1024, v);
    return b;
}

static DinoValue _dino_null(void) { DinoValue v; memset(&v, 0, sizeof v); v.type = DINO_NULL; return v; }
static DinoValue _dino_bool(int b) { DinoValue v; memset(&v, 0, sizeof v); v.type = DINO_BOOL; v.as.b = b ? 1 : 0; return v; }
static DinoValue _dino_int(long long i) { DinoValue v; memset(&v, 0, sizeof v); v.type = DINO_INT; v.as.i = i; return v; }
static DinoValue _dino_float(double f) { DinoValue v; memset(&v, 0, sizeof v); v.type = DINO_FLOAT; v.as.f = f; return v; }
static DinoValue _dino_str(const char *s) { DinoValue v; memset(&v, 0, sizeof v); v.type = DINO_STRING; v.as.s = s ? s : ""; return v; }

static int _dino_is_num(DinoValue v) { return v.type == DINO_INT || v.type == DINO_FLOAT || v.type == DINO_BOOL; }

static double _dino_to_double(DinoValue v) {
    switch (v.type) {
        case DINO_INT: return (double)v.as.i;
        case DINO_FLOAT: return v.as.f;
        case DINO_BOOL: return (double)v.as.b;
        case DINO_STRING: return atof(v.as.s ? v.as.s : "");
        default: return 0.0;
    }
}

static long long _dino_to_int(DinoValue v) {
    switch (v.type) {
        case DINO_INT: return v.as.i;
        case DINO_FLOAT: return (long long)v.as.f;
        case DINO_BOOL: return v.as.b;
        case DINO_STRING: return atoll(v.as.s ? v.as.s : "");
        default: return 0;
    }
}

static int _dino_truthy(DinoValue v) {
    switch (v.type) {
        case DINO_NULL: return 0;
        case DINO_BOOL: return v.as.b;
        case DINO_INT: return v.as.i != 0;
        case DINO_FLOAT: return v.as.f != 0.0;
        case DINO_STRING: return v.as.s && v.as.s[0];
        default: return 1;
    }
}

/* ── try / catch / throw ──────────────────────────────────────────────────── */

typedef struct DinoTryFrame { jmp_buf buf; DinoValue err; struct DinoTryFrame *prev; } DinoTryFrame;
static DinoTryFrame *_dino_try_top = NULL;

static void _dino_throw(DinoValue v) {
    if (_dino_try_top) { _dino_try_top->err = v; longjmp(_dino_try_top->buf, 1); }
    fprintf(stderr, "Uncaught error: %s\n", _dino_cstr(v));
    exit(1);
}

/* ── Comparison / logic ───────────────────────────────────────────────────── */

static int _dino_equals(DinoValue a, DinoValue b) {
    if (a.type == DINO_STRING && b.type == DINO_STRING)
        return strcmp(a.as.s ? a.as.s : "", b.as.s ? b.as.s : "") == 0;
    if (_dino_is_num(a) && _dino_is_num(b)) return _dino_to_double(a) == _dino_to_double(b);
    if (a.type != b.type) return 0;
    switch (a.type) {
        case DINO_NULL: return 1;
        case DINO_ARRAY: return a.as.arr == b.as.arr;
        case DINO_DICT: return a.as.dict == b.as.dict;
        default: return 1;
    }
}

static int _dino_cmp(DinoValue a, DinoValue b) {
    if (a.type == DINO_STRING && b.type == DINO_STRING) {
        int c = strcmp(a.as.s ? a.as.s : "", b.as.s ? b.as.s : "");
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    double x = _dino_to_double(a), y = _dino_to_double(b);
    return x < y ? -1 : (x > y ? 1 : 0);
}

static DinoValue _dino_eq(DinoValue a, DinoValue b) { return _dino_bool(_dino_equals(a, b)); }
static DinoValue _dino_ne(DinoValue a, DinoValue b) { return _dino_bool(!_dino_equals(a, b)); }
static DinoValue _dino_lt(DinoValue a, DinoValue b) { return _dino_bool(_dino_cmp(a, b) < 0); }
static DinoValue _dino_le(DinoValue a, DinoValue b) { return _dino_bool(_dino_cmp(a, b) <= 0); }
static DinoValue _dino_gt(DinoValue a, DinoValue b) { return _dino_bool(_dino_cmp(a, b) > 0); }
static DinoValue _dino_ge(DinoValue a, DinoValue b) { return _dino_bool(_dino_cmp(a, b) >= 0); }
static DinoValue _dino_not(DinoValue a) { return _dino_bool(!_dino_truthy(a)); }
static DinoValue _dino_and(DinoValue a, DinoValue b) { return _dino_bool(_dino_truthy(a) && _dino_truthy(b)); }
static DinoValue _dino_or(DinoValue a, DinoValue b) { return _dino_bool(_dino_truthy(a) || _dino_truthy(b)); }

static DinoValue _dino_neg(DinoValue a) {
    if (a.type == DINO_FLOAT) return _dino_float(-a.as.f);
    return _dino_int(-_dino_to_int(a));
}

static DinoValue _dino_inc(DinoValue *v) {
    if (v->type == DINO_FLOAT) { v->as.f += 1.0; return *v; }
    long long n = _dino_to_int(*v) + 1;
    v->type = DINO_INT; v->as.i = n;
    return *v;
}

static DinoValue _dino_dec(DinoValue *v) {
    if (v->type == DINO_FLOAT) { v->as.f -= 1.0; return *v; }
    long long n = _dino_to_int(*v) - 1;
    v->type = DINO_INT; v->as.i = n;
    return *v;
}

/* ── Arithmetic ───────────────────────────────────────────────────────────── */

static DinoValue _dino_add(DinoValue a, DinoValue b) {
    if (a.type == DINO_STRING || b.type == DINO_STRING) {
        const char *sa = _dino_cstr(a);
        const char *sb = _dino_cstr(b);
        size_t n = strlen(sa) + strlen(sb) + 1;
        char *r = malloc(n);
        strcpy(r, sa);
        strcat(r, sb);
        return _dino_str(r);
    }
    if (a.type == DINO_FLOAT || b.type == DINO_FLOAT)
        return _dino_float(_dino_to_double(a) + _dino_to_double(b));
    return _dino_int(_dino_to_int(a) + _dino_to_int(b));
}

static DinoValue _dino_sub(DinoValue a, DinoValue b) {
    if (a.type == DINO_FLOAT || b.type == DINO_FLOAT)
        return _dino_float(_dino_to_double(a) - _dino_to_double(b));
    return _dino_int(_dino_to_int(a) - _dino_to_int(b));
}

static DinoValue _dino_mul(DinoValue a, DinoValue b) {
    if (a.type == DINO_FLOAT || b.type == DINO_FLOAT)
        return _dino_float(_dino_to_double(a) * _dino_to_double(b));
    return _dino_int(_dino_to_int(a) * _dino_to_int(b));
}

static DinoValue _dino_div(DinoValue a, DinoValue b) {
    double d = _dino_to_double(b);
    if (d == 0.0) _dino_throw(_dino_str("division by zero"));
    if (a.type == DINO_FLOAT || b.type == DINO_FLOAT) return _dino_float(_dino_to_double(a) / d);
    return _dino_int(_dino_to_int(a) / (long long)d);
}

static DinoValue _dino_mod(DinoValue a, DinoValue b) {
    long long d = _dino_to_int(b);
    if (d == 0) _dino_throw(_dino_str("modulo by zero"));
    return _dino_int(_dino_to_int(a) % d);
}

/* ── Arrays ───────────────────────────────────────────────────────────────── */

static DinoArray *_dino_array_new(void) {
    DinoArray *a = malloc(sizeof(DinoArray));
    a->len = 0;
    a->cap = 4;
    a->items = malloc(sizeof(DinoValue) * a->cap);
    return a;
}

static void _dino_array_push(DinoArray *a, DinoValue v) {
    if (a->len == a->cap) {
        a->cap *= 2;
        a->items = realloc(a->items, sizeof(DinoValue) * a->cap);
    }
    a->items[a->len++] = v;
}

static DinoValue _dino_array_from(size_t n, DinoValue *items) {
    DinoArray *a = _dino_array_new();
    for (size_t i = 0; i < n; i++) _dino_array_push(a, items[i]);
    DinoValue v = _dino_null();
    v.type = DINO_ARRAY;
    v.as.arr = a;
    return v;
}

/* ── Dictionaries ─────────────────────────────────────────────────────────── */

static DinoDict *_dino_dict_new(void) {
    DinoDict *d = malloc(sizeof(DinoDict));
    d->len = 0;
    d->cap = 4;
    d->keys = malloc(sizeof(DinoValue) * d->cap);
    d->vals = malloc(sizeof(DinoValue) * d->cap);
    return d;
}

static long long _dino_dict_find(DinoDict *d, DinoValue key) {
    for (size_t i = 0; i < d->len; i++) if (_dino_equals(d->keys[i], key)) return (long long)i;
    return -1;
}

static void _dino_dict_put(DinoDict *d, DinoValue key, DinoValue val) {
    long long i = _dino_dict_find(d, key);
    if (i >= 0) { d->vals[i] = val; return; }
    if (d->len == d->cap) {
        d->cap *= 2;
        d->keys = realloc(d->keys, sizeof(DinoValue) * d->cap);
        d->vals = realloc(d->vals, sizeof(DinoValue) * d->cap);
    }
    d->keys[d->len] = key;
    d->vals[d->len] = val;
    d->len++;
}

static DinoValue _dino_dict_get(DinoDict *d, DinoValue key) {
    long long i = _dino_dict_find(d, key);
    return i >= 0 ? d->vals[i] : _dino_null();
}

static DinoValue _dino_dict_from(size_t n, DinoValue *kv) {
    DinoDict *d = _dino_dict_new();
    for (size_t i = 0; i < n; i++) _dino_dict_put(d, kv[2 * i], kv[2 * i + 1]);
    DinoValue v = _dino_null();
    v.type = DINO_DICT;
    v.as.dict = d;
    return v;
}

/* ── Indexing / length / list helpers ─────────────────────────────────────── */

static DinoValue _dino_get(DinoValue obj, DinoValue key) {
    if (obj.type == DINO_ARRAY) {
        // A non-numeric key searches for a matching value and returns its
        // index (or null). Numeric keys index the array.
        if (key.type != DINO_INT && key.type != DINO_FLOAT && key.type != DINO_BOOL) {
            for (size_t i = 0; i < obj.as.arr->len; i++)
                if (_dino_equals(obj.as.arr->items[i], key)) return _dino_int((long long)i);
            return _dino_null();
        }
        long long i = _dino_to_int(key);
        if (i < 0) i += (long long)obj.as.arr->len;
        if (i < 0 || (size_t)i >= obj.as.arr->len) _dino_throw(_dino_str("array index out of range"));
        return obj.as.arr->items[i];
    }
    if (obj.type == DINO_DICT) {
        long long idx = _dino_dict_find(obj.as.dict, key);
        if (idx >= 0) return obj.as.dict->vals[idx];
        // Positional fallback: an integer key that is not a stored key refers
        // to the nth entry (0-based, negatives from the end).
        if (key.type == DINO_INT || key.type == DINO_FLOAT || key.type == DINO_BOOL) {
            long long i = _dino_to_int(key);
            if (i < 0) i += (long long)obj.as.dict->len;
            if (i >= 0 && (size_t)i < obj.as.dict->len) return obj.as.dict->vals[i];
        }
        return _dino_null();
    }
    if (obj.type == DINO_STRING) {
        if (key.type != DINO_INT && key.type != DINO_FLOAT && key.type != DINO_BOOL) return _dino_null();
        long long i = _dino_to_int(key);
        const char *s = obj.as.s ? obj.as.s : "";
        size_t len = strlen(s);
        if (i < 0) i += (long long)len;
        if (i < 0 || (size_t)i >= len) _dino_throw(_dino_str("string index out of range"));
        char *buf = _dino_pool_next();
        buf[0] = s[i];
        buf[1] = '\0';
        return _dino_str(buf);
    }
    return _dino_null();
}

static void _dino_set(DinoValue obj, DinoValue key, DinoValue val) {
    if (obj.type == DINO_ARRAY) {
        long long i = _dino_to_int(key);
        if (i < 0) i += (long long)obj.as.arr->len;
        if (i < 0 || (size_t)i >= obj.as.arr->len) _dino_throw(_dino_str("array index out of range"));
        obj.as.arr->items[i] = val;
    } else if (obj.type == DINO_DICT) {
        _dino_dict_put(obj.as.dict, key, val);
    } else {
        _dino_throw(_dino_str("cannot assign to an index of this value"));
    }
}

static DinoValue _dino_len(DinoValue v) {
    if (v.type == DINO_STRING) return _dino_int((long long)strlen(v.as.s ? v.as.s : ""));
    if (v.type == DINO_ARRAY) return _dino_int((long long)v.as.arr->len);
    if (v.type == DINO_DICT) return _dino_int((long long)v.as.dict->len);
    return _dino_int(0);
}

static DinoValue _dino_push(DinoValue arr, DinoValue v) {
    if (arr.type == DINO_ARRAY) _dino_array_push(arr.as.arr, v);
    return arr;
}

static DinoValue _dino_pop(DinoValue arr) {
    if (arr.type == DINO_ARRAY && arr.as.arr->len > 0) return arr.as.arr->items[--arr.as.arr->len];
    return _dino_null();
}

static DinoValue _dino_has(DinoValue obj, DinoValue key) {
    if (obj.type == DINO_DICT) return _dino_bool(_dino_dict_find(obj.as.dict, key) >= 0);
    if (obj.type == DINO_ARRAY) {
        for (size_t i = 0; i < obj.as.arr->len; i++)
            if (_dino_equals(obj.as.arr->items[i], key)) return _dino_bool(1);
    }
    return _dino_bool(0);
}

static DinoValue _dino_keys(DinoValue obj) {
    if (obj.type != DINO_DICT) return _dino_array_from(0, NULL);
    DinoArray *a = _dino_array_new();
    for (size_t i = 0; i < obj.as.dict->len; i++) _dino_array_push(a, obj.as.dict->keys[i]);
    DinoValue v = _dino_null();
    v.type = DINO_ARRAY;
    v.as.arr = a;
    return v;
}

static DinoValue _dino_values(DinoValue obj) {
    if (obj.type != DINO_DICT) return _dino_array_from(0, NULL);
    DinoArray *a = _dino_array_new();
    for (size_t i = 0; i < obj.as.dict->len; i++) _dino_array_push(a, obj.as.dict->vals[i]);
    DinoValue v = _dino_null();
    v.type = DINO_ARRAY;
    v.as.arr = a;
    return v;
}

/* ── Output / input / timing ──────────────────────────────────────────────── */

static void _dino_printv(size_t n, DinoValue *vals) {
    for (size_t i = 0; i < n; i++) {
        if (i) fputc(' ', stdout);
        fputs(_dino_cstr(vals[i]), stdout);
    }
    fputc('\n', stdout);
}

static void _dino_eprintv(const char *color, size_t n, DinoValue *vals) {
    int tty = color && !getenv("NO_COLOR") && isatty(fileno(stderr));
    if (tty) fputs(color, stderr);
    for (size_t i = 0; i < n; i++) {
        if (i) fputc(' ', stderr);
        fputs(_dino_cstr(vals[i]), stderr);
    }
    fputc('\n', stderr);
    if (tty) fputs("\033[0m", stderr);
}

static DinoValue _dino_fmt(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    char *buf = malloc((size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return _dino_str(buf);
}

static void _dino_delay(double seconds) {
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1000000000.0);
    nanosleep(&ts, NULL);
}

static DinoValue _dino_input(DinoValue prompt) {
    const char *p = _dino_cstr(prompt);
    if (p && p[0]) { fputs(p, stdout); fflush(stdout); }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, stdin);
    if (n < 0) { free(line); return _dino_str(""); }
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
    return _dino_str(line);
}
