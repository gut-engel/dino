#ifndef COMMON_H
#define COMMON_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *data;
    size_t length;
} StringView;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} StringBuilder;

static inline StringView sv_from_cstr(const char *cstr) {
    return (StringView){.data = cstr, .length = strlen(cstr)};
}

static inline StringView sv_from_parts(const char *data, size_t length) {
    return (StringView){.data = data, .length = length};
}

static inline bool sv_eq(StringView a, StringView b) {
    return a.length == b.length && memcmp(a.data, b.data, a.length) == 0;
}

static inline bool sv_starts_with(StringView haystack, StringView needle) {
    return haystack.length >= needle.length && memcmp(haystack.data, needle.data, needle.length) == 0;
}

static inline StringBuilder sb_new(void) {
    return (StringBuilder){.data = NULL, .length = 0, .capacity = 0};
}

void sb_append(StringBuilder *sb, StringView sv);
void sb_append_cstr(StringBuilder *sb, const char *cstr);
void sb_append_char(StringBuilder *sb, char c);
void sb_free(StringBuilder *sb);

#define ARENA_SIZE (1024 * 1024)

typedef struct Arena {
    char *memory;
    size_t offset;
    size_t capacity;
} Arena;

Arena *arena_new(void);
void *arena_alloc(Arena *arena, size_t size);
void arena_free(Arena *arena);

#define ARENA_ALLOC(arena, type) (type *)arena_alloc(arena, sizeof(type))
#define ARENA_ALLOC_ARRAY(arena, type, count) (type *)arena_alloc(arena, sizeof(type) * (count))

#endif