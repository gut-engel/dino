#include "codegen.h"
#include <stdio.h>
#include <ctype.h>

// ── Helpers ──────────────────────────────────────────────────────────────────

static void emit_indent(Codegen *cg) {
    for (int i = 0; i < cg->indent_level; i++)
        sb_append_cstr(&cg->out, "    ");
}

static void emit_line(Codegen *cg) {
    sb_append_char(&cg->out, '\n');
}

static void emit(Codegen *cg, const char *text) {
    sb_append_cstr(&cg->out, text);
}

static void emit_sv(Codegen *cg, StringView sv) {
    sb_append(&cg->out, sv);
}

static void error_at_node(Codegen *cg, ASTNode *node, const char *msg) {
    if (!cg->had_error) {
        cg->had_error = true;
        char buf[256];
        snprintf(buf, sizeof(buf), "Codegen error (line %zu, col %zu): %s", node->line, node->column, msg);
        sb_append_cstr(&cg->error_msg, buf);
    }
}

// Map a Dino type name to C.  Identifiers are used as-is (user-defined types);
// built-in short names are expanded.
static void emit_type(Codegen *cg, StringView type_sv) {
    if (sv_eq(type_sv, sv_from_cstr("bool")))     { emit(cg, "_Bool"); return; }
    if (sv_eq(type_sv, sv_from_cstr("int")))      { emit(cg, "int");    return; }
    if (sv_eq(type_sv, sv_from_cstr("float")))    { emit(cg, "double"); return; }
    if (sv_eq(type_sv, sv_from_cstr("void")))     { emit(cg, "void");   return; }
    // user-defined or opaque type
    emit_sv(cg, type_sv);
}

// Render a string literal, converting Dino escape sequences to C where needed.
// Strips surrounding quotes and re-emits with " prefix/suffix.
static void emit_string_literal(Codegen *cg, StringView sv) {
    // sv includes surrounding quotes
    sb_append_char(&cg->out, '"');
    for (size_t i = 1; i < sv.length - 1; i++) {
        char c = sv.data[i];
        if (c == '{' || c == '}') {
            // literal braces inside non-interpolated string – pass through
            sb_append_char(&cg->out, c);
        } else if (c == '\\' && i + 1 < sv.length - 1) {
            sb_append_char(&cg->out, '\\');
            sb_append_char(&cg->out, sv.data[++i]);
        } else {
            sb_append_char(&cg->out, c);
        }
    }
    sb_append_char(&cg->out, '"');
}

typedef enum {
    INTERP_PRINT,    // console.log → printf(fmt "\n", args)
    INTERP_PRINT_STDERR, // console.warn/error → fprintf(stderr, fmt "\n", args)
    INTERP_SYSTEM    // console.do → snprintf into buffer then system()
} InterpMode;

// Split an interpolated string into a printf format string (with quotes)
// and a comma-prefixed argument list. sv includes surrounding quotes.
static void build_interp_parts(StringView sv, StringBuilder *fmt, StringBuilder *args) {
    sb_append_char(fmt, '"');
    size_t i = 2; // skip '$' and opening quote
    while (i < sv.length - 1) { // stop before closing quote
        if (sv.data[i] == '{') {
            // interpolation placeholder: {expr}
            i++;
            size_t start = i;
            while (i < sv.length - 1 && sv.data[i] != '}') i++;
            StringView expr = sv_from_parts(sv.data + start, i - start);
            bool is_string = expr.length > 0 && expr.data[0] == '"';
            sb_append_cstr(fmt, is_string ? "%s" : "%d");
            sb_append(args, expr);
            i++; // skip '}'
        } else {
            sb_append_char(fmt, sv.data[i]);
            i++;
        }
    }
    sb_append_char(fmt, '"');
}

// Emits `_dino_fmt("fmt", args)` for use in expression context.
static void emit_interpolated_expression(Codegen *cg, StringView sv) {
    StringBuilder fmt = sb_new();
    StringBuilder args = sb_new();
    build_interp_parts(sv, &fmt, &args);
    emit(cg, "_dino_fmt(");
    sb_append(&cg->out, sv_from_parts(fmt.data, fmt.length));
    if (args.length > 0) {
        emit(cg, ", ");
        sb_append(&cg->out, sv_from_parts(args.data, args.length));
    }
    emit(cg, ")");
    sb_free(&fmt);
    sb_free(&args);
}

// Emit the actual print/system call for the given mode.
static void emit_interpolated_string(Codegen *cg, StringView sv, InterpMode mode) {
    StringBuilder fmt = sb_new();
    StringBuilder args = sb_new();
    build_interp_parts(sv, &fmt, &args);
    StringView fmt_sv = sv_from_parts(fmt.data, fmt.length);
    StringView args_sv = sv_from_parts(args.data, args.length);

    switch (mode) {
        case INTERP_PRINT:
            emit(cg, "printf(");
            sb_append(&cg->out, fmt_sv);
            if (args.length > 0) {
                emit(cg, ", ");
                sb_append(&cg->out, args_sv);
            }
            emit(cg, ")");
            break;
        case INTERP_PRINT_STDERR:
            emit(cg, "fprintf(stderr, ");
            sb_append(&cg->out, fmt_sv);
            if (args.length > 0) {
                emit(cg, ", ");
                sb_append(&cg->out, args_sv);
            }
            emit(cg, ")");
            break;
        case INTERP_SYSTEM: {
            static int interp_counter = 0;
            char bufname[32];
            snprintf(bufname, sizeof(bufname), "_interp_buf_%d", interp_counter++);
            emit(cg, "char ");
            emit(cg, bufname);
            emit(cg, "[512];\n    snprintf(");
            emit(cg, bufname);
            emit(cg, ", sizeof(");
            emit(cg, bufname);
            emit(cg, "), ");
            sb_append(&cg->out, fmt_sv);
            if (args.length > 0) {
                emit(cg, ", ");
                sb_append(&cg->out, args_sv);
            }
            emit(cg, ");\n    system(");
            emit(cg, bufname);
            emit(cg, ")");
            break;
        }
    }

    sb_free(&fmt);
    sb_free(&args);
}

// Best-effort type inference from an initializer expression. Emits the type
// (with trailing space) and returns true, or false if nothing can be inferred.
static bool emit_inferred_type(Codegen *cg, ASTNode *initializer) {
    if (!initializer) return false;
    switch (initializer->type) {
        case AST_BOOL_LITERAL:
            emit(cg, "_Bool ");
            return true;
        case AST_NUMBER: {
            bool has_dot = false;
            for (size_t i = 0; i < initializer->as.number.value.length; i++) {
                if (initializer->as.number.value.data[i] == '.') { has_dot = true; break; }
            }
            emit(cg, has_dot ? "double " : "int ");
            return true;
        }
        case AST_STRING:
        case AST_INTERPOLATED_STRING:
            emit(cg, "const char *");
            return true;
        default:
            return false;
    }
}

// ── Forward declarations ─────────────────────────────────────────────────────

static void codegen_statement(Codegen *cg, ASTNode *node);
static void codegen_expression(Codegen *cg, ASTNode *node);

// ── Expression codegen ───────────────────────────────────────────────────────

static void codegen_expression(Codegen *cg, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_NUMBER:
            emit_sv(cg, node->as.number.value);
            break;

        case AST_BOOL_LITERAL:
            emit(cg, node->as.bool_literal.value ? "1" : "0");
            break;

        case AST_STRING:
            emit_string_literal(cg, node->as.string.value);
            break;

        case AST_INTERPOLATED_STRING:
            emit_interpolated_expression(cg, node->as.interpolated_string.value);
            break;

        case AST_IDENTIFIER:
            emit_sv(cg, node->as.identifier.name);
            break;

        case AST_BINARY_EXPR:
            emit(cg, "(");
            codegen_expression(cg, node->as.binary_expr.left);
            emit(cg, " ");
            emit_sv(cg, node->as.binary_expr.op);
            emit(cg, " ");
            codegen_expression(cg, node->as.binary_expr.right);
            emit(cg, ")");
            break;

        case AST_UNARY_EXPR:
            if (node->as.unary_expr.is_prefix) {
                emit(cg, "(");
                emit_sv(cg, node->as.unary_expr.op);
                codegen_expression(cg, node->as.unary_expr.operand);
                emit(cg, ")");
            } else {
                emit(cg, "(");
                codegen_expression(cg, node->as.unary_expr.operand);
                emit_sv(cg, node->as.unary_expr.op);
                emit(cg, ")");
            }
            break;

        case AST_CALL_EXPR: {
            ASTNode *callee = node->as.call_expr.callee;
            size_t nargs = node->as.call_expr.arguments.count;

            // delay(seconds) builtin → _dino_delay() runtime helper.
            // Accepts whole or fractional seconds (int or float).
            if (callee->type == AST_IDENTIFIER &&
                sv_eq(callee->as.identifier.name, sv_from_cstr("delay"))) {
                if (nargs != 1) {
                    error_at_node(cg, node, "delay() expects exactly 1 argument (seconds)");
                    break;
                }
                emit(cg, "_dino_delay(");
                codegen_expression(cg, node->as.call_expr.arguments.nodes[0]);
                emit(cg, ")");
                break;
            }

            // console.* builtins
            if (callee->type == AST_MEMBER_EXPR) {
                ASTNode *obj = callee->as.member_expr.object;
                StringView prop = callee->as.member_expr.property;
                if (obj->type == AST_IDENTIFIER && sv_eq(obj->as.identifier.name, sv_from_cstr("console"))) {
                    bool is_interp = nargs == 1 &&
                                     node->as.call_expr.arguments.nodes[0]->type == AST_INTERPOLATED_STRING;

                    // console.do(...) → shell out
                    if (sv_eq(prop, sv_from_cstr("do"))) {
                        if (is_interp) {
                            emit_interpolated_string(cg, node->as.call_expr.arguments.nodes[0]->as.interpolated_string.value, INTERP_SYSTEM);
                        } else if (nargs == 1) {
                            emit(cg, "system(");
                            codegen_expression(cg, node->as.call_expr.arguments.nodes[0]);
                            emit(cg, ")");
                        } else {
                            emit(cg, "/* console.do() with no command */");
                        }
                        break;
                    }

                    // Interpolated string: print to stdout (log) or stderr (warn/error)
                    if (is_interp) {
                        InterpMode mode = sv_eq(prop, sv_from_cstr("log")) ? INTERP_PRINT : INTERP_PRINT_STDERR;
                        emit_interpolated_string(cg, node->as.call_expr.arguments.nodes[0]->as.interpolated_string.value, mode);
                        break;
                    }

                    // Plain log/warn/error: print newline or formatted value
                    bool is_log = sv_eq(prop, sv_from_cstr("log"));
                    bool is_stderr = sv_eq(prop, sv_from_cstr("warn")) || sv_eq(prop, sv_from_cstr("error"));
                    if (is_log || is_stderr) {
                        emit(cg, is_log ? "printf(" : "fprintf(stderr, ");
                        if (nargs == 0) {
                            emit(cg, "\"\\n\"");
                        } else {
                            emit(cg, "\"");
                            for (size_t i = 0; i < nargs; i++) {
                                if (i > 0) emit(cg, " ");
                                ASTNode *arg = node->as.call_expr.arguments.nodes[i];
                                switch (arg->type) {
                                    case AST_STRING:
                                    case AST_INTERPOLATED_STRING:
                                        emit(cg, "%s");
                                        break;
                                    default:
                                        emit(cg, "%d");
                                        break;
                                }
                            }
                            emit(cg, "\\n\"");
                            for (size_t i = 0; i < nargs; i++) {
                                emit(cg, ", ");
                                codegen_expression(cg, node->as.call_expr.arguments.nodes[i]);
                            }
                        }
                        emit(cg, ")");
                        break;
                    }

                    // Anything else on console.* is not a known builtin.
                    char buf[160];
                    snprintf(buf, sizeof(buf),
                             "Unknown console method '%.*s' (expected log, warn, error or do)",
                             (int)prop.length, prop.data);
                    error_at_node(cg, node, buf);
                    break;
                }
            }

            // General function call
            codegen_expression(cg, callee);
            emit(cg, "(");
            for (size_t i = 0; i < nargs; i++) {
                if (i > 0) emit(cg, ", ");
                codegen_expression(cg, node->as.call_expr.arguments.nodes[i]);
            }
            emit(cg, ")");
            break;
        }

        case AST_MEMBER_EXPR:
            if (node->as.member_expr.object->type == AST_IDENTIFIER &&
                sv_eq(node->as.member_expr.object->as.identifier.name, sv_from_cstr("console"))) {
                char buf[160];
                snprintf(buf, sizeof(buf),
                         "console can only be called as console.log/warn/error/do, got 'console.%.*s'",
                         (int)node->as.member_expr.property.length, node->as.member_expr.property.data);
                error_at_node(cg, node, buf);
                break;
            }
            codegen_expression(cg, node->as.member_expr.object);
            emit(cg, ".");
            emit_sv(cg, node->as.member_expr.property);
            break;

        default:
            error_at_node(cg, node, "Cannot generate expression for this node");
            break;
    }
}

// ── Statement codegen ────────────────────────────────────────────────────────

// Emit a control-flow body: blocks start immediately with '{' on the current
// line; anything else falls through to normal statement codegen.
static void codegen_braced_body(Codegen *cg, ASTNode *body) {
    if (body->type == AST_BLOCK) {
        emit(cg, "{\n");
        cg->indent_level++;
        for (size_t i = 0; i < body->as.block.statements.count; i++) {
            codegen_statement(cg, body->as.block.statements.nodes[i]);
        }
        cg->indent_level--;
        emit_indent(cg);
        emit(cg, "}\n");
    } else {
        codegen_statement(cg, body);
    }
}

static void codegen_statement(Codegen *cg, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_VAR_DECL: {
            emit_indent(cg);
            if (node->as.var_decl.is_const) {
                emit(cg, "const ");
            }
            if (node->as.var_decl.type) {
                emit_type(cg, node->as.var_decl.type->as.identifier.name);
                emit(cg, " ");
            } else {
                // Infer type from initializer (best effort)
                if (!emit_inferred_type(cg, node->as.var_decl.initializer)) {
                    emit(cg, "int "); // fallback
                }
            }
            emit_sv(cg, node->as.var_decl.name);
            if (node->as.var_decl.initializer) {
                emit(cg, " = ");
                codegen_expression(cg, node->as.var_decl.initializer);
            }
            emit(cg, ";");
            emit_line(cg);
            break;
        }

        case AST_IF_STMT:
            emit_indent(cg);
            emit(cg, "if (");
            codegen_expression(cg, node->as.if_stmt.condition);
            emit(cg, ") ");
            codegen_braced_body(cg, node->as.if_stmt.then_branch);
            if (node->as.if_stmt.else_branch) {
                emit_indent(cg);
                emit(cg, "else ");
                codegen_braced_body(cg, node->as.if_stmt.else_branch);
            }
            break;

        case AST_FOR_STMT:
            emit_indent(cg);
            emit(cg, "for (");
            if (node->as.for_stmt.init) {
                // Emit the init part without indentation / newline
                if (node->as.for_stmt.init->type == AST_VAR_DECL) {
                    ASTNode *init = node->as.for_stmt.init;
                    if (init->as.var_decl.is_const) {
                        emit(cg, "const ");
                    }
                    if (init->as.var_decl.type) {
                        emit_type(cg, init->as.var_decl.type->as.identifier.name);
                        emit(cg, " ");
                    } else {
                        if (!emit_inferred_type(cg, init->as.var_decl.initializer)) {
                            emit(cg, "int "); // fallback
                        }
                    }
                    emit_sv(cg, init->as.var_decl.name);
                    if (init->as.var_decl.initializer) {
                        emit(cg, " = ");
                        codegen_expression(cg, init->as.var_decl.initializer);
                    }
                } else {
                    codegen_expression(cg, node->as.for_stmt.init);
                }
            }
            emit(cg, "; ");
            if (node->as.for_stmt.condition) {
                codegen_expression(cg, node->as.for_stmt.condition);
            }
            emit(cg, "; ");
            if (node->as.for_stmt.increment) {
                codegen_expression(cg, node->as.for_stmt.increment);
            }
            emit(cg, ") ");
            codegen_braced_body(cg, node->as.for_stmt.body);
            break;

        case AST_WHILE_STMT:
            emit_indent(cg);
            emit(cg, "while (");
            codegen_expression(cg, node->as.while_stmt.condition);
            emit(cg, ") ");
            codegen_braced_body(cg, node->as.while_stmt.body);
            break;

        case AST_SWITCH_STMT: {
            // Dino `case` labels are arbitrary runtime expressions, so a switch
            // transpiles to an if / else-if chain over the case conditions.
            bool first = true;
            for (size_t i = 0; i < node->as.switch_stmt.cases.count; i++) {
                ASTNode *c = node->as.switch_stmt.cases.nodes[i];
                emit_indent(cg);
                emit(cg, first ? "if (" : "else if (");
                codegen_expression(cg, c->as.case_stmt.condition);
                emit(cg, ") {\n");
                cg->indent_level++;
                for (size_t j = 0; j < c->as.case_stmt.body.count; j++) {
                    codegen_statement(cg, c->as.case_stmt.body.nodes[j]);
                }
                cg->indent_level--;
                emit_indent(cg);
                emit(cg, "}\n");
                first = false;
            }
            if (node->as.switch_stmt.default_case) {
                ASTNode *dc = node->as.switch_stmt.default_case;
                emit_indent(cg);
                emit(cg, "else {\n");
                cg->indent_level++;
                for (size_t j = 0; j < dc->as.case_stmt.body.count; j++) {
                    codegen_statement(cg, dc->as.case_stmt.body.nodes[j]);
                }
                cg->indent_level--;
                emit_indent(cg);
                emit(cg, "}\n");
            }
            break;
        }

        case AST_BLOCK: {
            emit_indent(cg);
            emit(cg, "{\n");
            cg->indent_level++;
            for (size_t i = 0; i < node->as.block.statements.count; i++) {
                codegen_statement(cg, node->as.block.statements.nodes[i]);
            }
            cg->indent_level--;
            emit_indent(cg);
            emit(cg, "}");
            emit_line(cg);
            break;
        }

        case AST_EXPR_STMT:
            emit_indent(cg);
            if (node->as.expr_stmt.expression) {
                codegen_expression(cg, node->as.expr_stmt.expression);
                emit(cg, ";");
            }
            emit_line(cg);
            break;

        default:
            error_at_node(cg, node, "Cannot generate statement for this node");
            break;
    }
}

// ── Program codegen ──────────────────────────────────────────────────────────

char *codegen_generate(Arena *arena, ASTNode *program, char **error_out) {
    Codegen cg = {
        .arena = arena,
        .out = sb_new(),
        .indent_level = 0,
        .had_error = false,
        .error_msg = sb_new(),
    };

    // Preamble
    emit(&cg, "#define _POSIX_C_SOURCE 200809L\n");
    emit(&cg, "#include <stdio.h>\n");
    emit(&cg, "#include <stdlib.h>\n");
    emit(&cg, "#include <string.h>\n");
    emit(&cg, "#include <stdbool.h>\n");
    emit(&cg, "#include <stdarg.h>\n");
    emit(&cg, "#include <time.h>\n");
    emit(&cg, "\n");
    // Runtime helper for interpolated strings used in expression context
    emit(&cg, "static char _dino_interp_bufs[8][1024];\n");
    emit(&cg, "static int _dino_interp_idx = 0;\n");
    emit(&cg, "__attribute__((unused)) static char *_dino_fmt(const char *fmt, ...) {\n");
    emit(&cg, "    char *buf = _dino_interp_bufs[_dino_interp_idx];\n");
    emit(&cg, "    _dino_interp_idx = (_dino_interp_idx + 1) & 7;\n");
    emit(&cg, "    va_list ap;\n");
    emit(&cg, "    va_start(ap, fmt);\n");
    emit(&cg, "    vsnprintf(buf, 1024, fmt, ap);\n");
    emit(&cg, "    va_end(ap);\n");
    emit(&cg, "    return buf;\n");
    emit(&cg, "}\n");
    emit(&cg, "\n");
    // Runtime helper for delay(seconds): sleeps for a whole or fractional number
    // of seconds using a monotonic-ish high-resolution sleep (nanosleep).
    emit(&cg, "__attribute__((unused)) static void _dino_delay(double seconds) {\n");
    emit(&cg, "    struct timespec ts;\n");
    emit(&cg, "    ts.tv_sec = (time_t)seconds;\n");
    emit(&cg, "    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1000000000.0);\n");
    emit(&cg, "    nanosleep(&ts, NULL);\n");
    emit(&cg, "}\n");
    emit(&cg, "\n");

    // Generate all statements inside main()
    emit(&cg, "int main(void) {\n");
    cg.indent_level = 1;
    for (size_t i = 0; i < program->as.program.statements.count; i++) {
        codegen_statement(&cg, program->as.program.statements.nodes[i]);
    }
    emit(&cg, "    return 0;\n");
    emit(&cg, "}\n");

    if (cg.had_error) {
        *error_out = cg.error_msg.data;
        sb_free(&cg.out);
        return NULL;
    }

    *error_out = NULL;
    sb_free(&cg.error_msg);
    // Transfer ownership of the buffer to the caller
    char *result = cg.out.data;
    cg.out.data = NULL;
    sb_free(&cg.out);
    return result;
}