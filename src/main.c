#include "common.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "codegen/codegen.h"
#include <sys/stat.h>

static char *read_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: could not open file '%s'\n", filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fprintf(stderr, "Error: could not determine size of '%s'\n", filename);
        fclose(f);
        return NULL;
    }

    char *buffer = malloc(size + 1);
    if (!buffer) {
        fprintf(stderr, "Error: out of memory\n");
        fclose(f);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, (size_t)size, f);
    buffer[bytes_read] = '\0';
    fclose(f);
    return buffer;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <file.dn> [output.c]\n", prog);
    fprintf(stderr, "  Transpiles a Dino source file to C.\n");
    fprintf(stderr, "  If output.c is omitted, the result is written to CCode/<file>.c\n");
}

// Derive the C output path. If -o is given use it, otherwise CCode/<basename>.c
static char *build_output_path(const char *input_path, const char *explicit_output) {
    if (explicit_output) return strdup(explicit_output);

    // Strip directory components
    const char *base = strrchr(input_path, '/');
    base = base ? base + 1 : input_path;

    // Strip .dn extension
    size_t len = strlen(base);
    char *stem = malloc(len + 1);
    if (len > 3 && strcmp(base + len - 3, ".dn") == 0) {
        len -= 3;
    }
    memcpy(stem, base, len);
    stem[len] = '\0';

    // CCode/<stem>.c
    size_t out_len = strlen("CCode/") + len + 3;
    char *out = malloc(out_len + 1);
    snprintf(out, out_len + 1, "CCode/%s.c", stem);

    free(stem);
    return out;
}

static void ensure_ccode_dir(void) {
    struct stat st;
    if (stat("CCode", &st) != 0) {
        mkdir("CCode", 0755);
    }
}

int main(int argc, char *argv[]) {
    const char *output_arg = NULL;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    // Parse flags: -o <file> or a plain second positional arg
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_arg = argv[++i];
        } else if (!output_arg) {
            output_arg = argv[i];
        }
    }

    const char *input_path = argv[1];
    char *source = read_file(input_path);
    if (!source) return 1;

    char *output_path = build_output_path(input_path, output_arg);
    ensure_ccode_dir();

    Arena *arena = arena_new();
    Parser parser = parser_new(source, strlen(source), arena);
    ASTNode *program = parser_parse(&parser);

    if (parser.had_error) {
        fprintf(stderr, "Parse failed.\n");
        arena_free(arena);
        free(source);
        free(output_path);
        return 1;
    }

    char *codegen_error = NULL;
    char *c_code = codegen_generate(arena, program, &codegen_error);
    if (!c_code) {
        fprintf(stderr, "%s\n", codegen_error ? codegen_error : "Code generation failed.");
        arena_free(arena);
        free(source);
        free(output_path);
        return 1;
    }

    FILE *out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Error: could not write to '%s'\n", output_path);
        free(c_code);
        arena_free(arena);
        free(source);
        free(output_path);
        return 1;
    }
    fputs(c_code, out);
    fclose(out);

    printf("Transpiled %s -> %s\n", input_path, output_path);

    free(c_code);
    arena_free(arena);
    free(source);
    free(output_path);
    return 0;
}