#include "common.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "codegen/codegen.h"
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

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
    fprintf(stderr, "Usage: %s <file.dn> [options]\n", prog);
    fprintf(stderr, "  Transpiles a Dino source file to C and compiles it with a C compiler.\n");
    fprintf(stderr, "  The generated C is written to CCode/<file>.c and the executable to\n");
    fprintf(stderr, "  <file> (give it another name with -o).\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <exe>         Name of the output executable (default: input name)\n");
    fprintf(stderr, "  --doNotCompile   Only transpile to C; skip the C compiler step\n");
    fprintf(stderr, "  --check          Check syntax/semantics only; write nothing\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Environment:\n");
    fprintf(stderr, "  CC               C compiler to use (default: gcc)\n");
}

// ── Path helpers ─────────────────────────────────────────────────────────────

// File stem of the input: basename without the trailing .dn extension.
static char *input_stem(const char *input_path) {
    const char *base = strrchr(input_path, '/');
    base = base ? base + 1 : input_path;

    size_t len = strlen(base);
    if (len > 3 && strcmp(base + len - 3, ".dn") == 0) {
        len -= 3;
    }

    char *stem = malloc(len + 1);
    memcpy(stem, base, len);
    stem[len] = '\0';
    return stem;
}

// C output path is always CCode/<stem>.c
static char *build_c_path(const char *stem) {
    size_t out_len = strlen("CCode/") + strlen(stem) + 3;
    char *out = malloc(out_len + 1);
    snprintf(out, out_len + 1, "CCode/%s.c", stem);
    return out;
}

static void ensure_ccode_dir(void) {
    struct stat st;
    if (stat("CCode", &st) != 0) {
        mkdir("CCode", 0755);
    }
}

// ── Command helpers ──────────────────────────────────────────────────────────

// Returns a heap-allocated path to `command` if it is executable and present in
// $PATH (or directly executable when it contains a '/'), else NULL.
static char *find_command_in_path(const char *command) {
    if (strchr(command, '/')) {
        return access(command, X_OK) == 0 ? strdup(command) : NULL;
    }

    const char *path_env = getenv("PATH");
    if (!path_env) path_env = "";

    size_t len = strlen(path_env);
    char *path_copy = malloc(len + 1);
    if (!path_copy) return NULL;
    memcpy(path_copy, path_env, len + 1);

    char *result = NULL;
    char *saveptr = NULL;
    for (char *dir = strtok_r(path_copy, ":", &saveptr); dir; dir = strtok_r(NULL, ":", &saveptr)) {
        size_t dir_len = strlen(dir);
        size_t cmd_len = strlen(command);
        char *candidate = malloc(dir_len + cmd_len + 2);
        if (!candidate) break;
        if (dir_len > 0 && dir[dir_len - 1] == '/') {
            snprintf(candidate, dir_len + cmd_len + 2, "%s%s", dir, command);
        } else {
            snprintf(candidate, dir_len + cmd_len + 2, "%s/%s", dir, command);
        }
        if (access(candidate, X_OK) == 0) {
            result = candidate;
            break;
        }
        free(candidate);
    }
    free(path_copy);
    return result;
}

#define MAX_CMD_WORDS 16

// Splits a command line (e.g. CC="ccache gcc") into whitespace-separated words.
static size_t split_command(char *cmd, char **words, size_t max_words) {
    size_t n = 0;
    char *saveptr = NULL;
    for (char *tok = strtok_r(cmd, " \t", &saveptr); tok && n < max_words - 1;
         tok = strtok_r(NULL, " \t", &saveptr)) {
        words[n++] = tok;
    }
    words[n] = NULL;
    return n;
}

// Compiles the generated C file with the C compiler from $CC (default "gcc").
// Returns 0 on success, 1 on failure.
static int compile_c_code(const char *c_path, const char *exe_path) {
    const char *cc_env = getenv("CC");
    if (!cc_env || !*cc_env) cc_env = "gcc";

    char cc_buf[512];
    snprintf(cc_buf, sizeof(cc_buf), "%s", cc_env);

    char *words[MAX_CMD_WORDS];
    size_t nwords = split_command(cc_buf, words, MAX_CMD_WORDS);
    if (nwords == 0) {
        fprintf(stderr, "Error: CC is set but contains no command.\n");
        return 1;
    }
    const char *program = words[0];

    // Check that the compiler command exists before trying to run it.
    char *found = find_command_in_path(program);
    if (!found) {
        fprintf(stderr, "Error: C compiler '%s' was not found in PATH.\n", program);
        fprintf(stderr, "  Install gcc (e.g. 'sudo pacman -S gcc') or point CC at a\n");
        fprintf(stderr, "  compiler you have, e.g. 'CC=clang ./dino <file.dn>'.\n");
        return 1;
    }

    // Sanity check: the generated C must actually exist and be non-empty.
    struct stat st;
    if (stat(c_path, &st) != 0 || st.st_size == 0) {
        fprintf(stderr, "Error: generated C file '%s' is missing or empty; cannot compile.\n", c_path);
        free(found);
        return 1;
    }

    printf("Compiling with %s: %s -> %s\n", program, c_path, exe_path);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Error: could not fork to run '%s'.\n", program);
        free(found);
        return 1;
    }

    if (pid == 0) {
        char *argv[MAX_CMD_WORDS + 3];
        size_t i;
        for (i = 0; i < nwords; i++) argv[i] = words[i];
        argv[i++] = (char *)c_path;
        argv[i++] = (char *)"-o";
        argv[i++] = (char *)exe_path;
        argv[i] = NULL;
        execv(found, argv);
        fprintf(stderr, "Error: failed to execute '%s': %s\n", program, strerror(errno));
        _exit(127);
    }
    free(found); // child got its own copy

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "Error: failed waiting for '%s': %s\n", program, strerror(errno));
        return 1;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf("Compiled %s -> %s\n", c_path, exe_path);
        return 0;
    }

    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    fprintf(stderr, "Error: compilation failed (exit code %d).\n", code);
    fprintf(stderr, "  The generated C source is at '%s'.\n", c_path);
    return 1;
}

int main(int argc, char *argv[]) {
    const char *input_path = NULL;
    const char *exe_arg = NULL;
    bool do_compile = true;
    bool check_only = false;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    input_path = argv[1];

    // Parse options
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            exe_arg = argv[++i];
        } else if (strcmp(argv[i], "--doNotCompile") == 0 ||
                   strcmp(argv[i], "--no-compile") == 0) {
            do_compile = false;
        } else if (strcmp(argv[i], "--check") == 0) {
            check_only = true;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else if (!exe_arg) {
            exe_arg = argv[i]; // bare positional = executable name (same as -o)
        } else {
            fprintf(stderr, "Error: unexpected argument '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    char *source = read_file(input_path);
    if (!source) return 1;

    char *stem = input_stem(input_path);
    char *c_path = build_c_path(stem);

    Arena *arena = arena_new();
    Parser parser = parser_new(source, strlen(source), arena);
    ASTNode *program = parser_parse(&parser);

    int status = 1;
    char *c_code = NULL;

    if (parser.had_error) {
        fprintf(stderr, "\n%d syntax error(s) in '%s'.\n", (int)parser.error_count, input_path);
        goto done;
    }

    char *codegen_error = NULL;
    c_code = codegen_generate(arena, program, &codegen_error);
    if (!c_code) {
        fprintf(stderr, "%s\n", codegen_error ? codegen_error : "Code generation failed.");
        goto done;
    }

    if (check_only) {
        printf("Syntax OK: '%s'\n", input_path);
        status = 0;
        goto done;
    }

    ensure_ccode_dir();
    FILE *out = fopen(c_path, "w");
    if (!out) {
        fprintf(stderr, "Error: could not write to '%s'\n", c_path);
        goto done;
    }
    fputs(c_code, out);
    fclose(out);
    printf("Transpiled %s -> %s\n", input_path, c_path);

    if (do_compile) {
        char *exe_path = exe_arg ? strdup(exe_arg) : strdup(stem);
        status = compile_c_code(c_path, exe_path);
        free(exe_path);
    } else {
        printf("Note: skipped compilation (--doNotCompile).\n");
        printf("  Compile it yourself with: gcc %s -o %s\n", c_path, stem);
        status = 0;
    }

done:
    free(c_code);
    arena_free(arena);
    free(source);
    free(stem);
    free(c_path);
    return status;
}