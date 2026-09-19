#include "common.h"

void sb_append(StringBuilder *sb, StringView sv) {
    if (sb->length + sv.length >= sb->capacity) {
        size_t new_capacity = sb->capacity ? sb->capacity * 2 : 256;
        while (sb->length + sv.length >= new_capacity) new_capacity *= 2;
        sb->data = realloc(sb->data, new_capacity);
        sb->capacity = new_capacity;
    }
    memcpy(sb->data + sb->length, sv.data, sv.length);
    sb->length += sv.length;
    sb->data[sb->length] = '\0';
}

void sb_append_cstr(StringBuilder *sb, const char *cstr) {
    sb_append(sb, sv_from_cstr(cstr));
}

void sb_append_char(StringBuilder *sb, char c) {
    if (sb->length + 1 >= sb->capacity) {
        size_t new_capacity = sb->capacity ? sb->capacity * 2 : 256;
        sb->data = realloc(sb->data, new_capacity);
        sb->capacity = new_capacity;
    }
    sb->data[sb->length++] = c;
    sb->data[sb->length] = '\0';
}

void sb_free(StringBuilder *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
}

Arena *arena_new(void) {
    Arena *arena = malloc(sizeof(Arena));
    arena->memory = malloc(ARENA_SIZE);
    arena->offset = 0;
    arena->capacity = ARENA_SIZE;
    return arena;
}

void *arena_alloc(Arena *arena, size_t size) {
    size = (size + 7) & ~7; // align to 8 bytes
    if (arena->offset + size > arena->capacity) {
        size_t new_capacity = arena->capacity * 2;
        while (arena->offset + size > new_capacity) new_capacity *= 2;
        arena->memory = realloc(arena->memory, new_capacity);
        arena->capacity = new_capacity;
    }
    void *ptr = arena->memory + arena->offset;
    arena->offset += size;
    return ptr;
}

void arena_free(Arena *arena) {
    free(arena->memory);
    free(arena);
}