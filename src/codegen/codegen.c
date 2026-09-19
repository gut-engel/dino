#include "codegen.h"
#include "runtime.h"
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

// ── Lexical scopes (identifier validation) ──────────────────────────────────
// A flat symbol list with a mark stack of scope boundaries. Mirrors the
// scoping of the generated C (blocks nest, shadowing is allowed).

static void scope_push(Codegen *cg) {
    if (cg->scope_marks_count >= cg->scope_marks_cap) {
        size_t cap = cg->scope_marks_cap ? cg->scope_marks_cap * 2 : 8;
        cg->scope_marks = realloc(cg->scope_marks, sizeof(size_t) * cap);
        cg->scope_marks_cap = cap;
    }
    cg->scope_marks[cg->scope_marks_count++] = cg->scope_count;
}

static void scope_pop(Codegen *cg) {
    if (cg->scope_marks_count == 0) return;
    cg->scope_count = cg->scope_marks[--cg->scope_marks_count];
}

static void scope_declare(Codegen *cg, StringView name) {
    if (cg->scope_count >= cg->scope_cap) {
        size_t cap = cg->scope_cap ? cg->scope_cap * 2 : 16;
        cg->scope_names = realloc(cg->scope_names, sizeof(StringView) * cap);
        cg->scope_cap = cap;
    }
    cg->scope_names[cg->scope_count++] = name;
}

static bool scope_has(Codegen *cg, StringView name) {
    for (size_t i = 0; i < cg->scope_count; i++) {
        if (sv_eq(cg->scope_names[i], name)) return true;
    }
    return false;
}

static void scope_free(Codegen *cg) {
    free(cg->scope_names);
    free(cg->scope_marks);
    cg->scope_names = NULL;
    cg->scope_count = 0;
    cg->scope_cap = 0;
    cg->scope_marks = NULL;
    cg->scope_marks_count = 0;
    cg->scope_marks_cap = 0;
}

// ── Class registry ───────────────────────────────────────────────────────────

static CodegenClass *class_find(Codegen *cg, StringView name) {
    for (size_t i = 0; i < cg->class_count; i++)
        if (sv_eq(cg->classes[i].name, name)) return &cg->classes[i];
    return NULL;
}

static void class_register(Codegen *cg, ASTNode *cls) {
    if (class_find(cg, cls->as.class_decl.name)) return;
    if (cg->class_count >= cg->class_cap) {
        size_t cap = cg->class_cap ? cg->class_cap * 2 : 8;
        cg->classes = realloc(cg->classes, sizeof(CodegenClass) * cap);
        cg->class_cap = cap;
    }
    CodegenClass *c = &cg->classes[cg->class_count++];
    memset(c, 0, sizeof(*c));
    c->name = cls->as.class_decl.name;
    for (size_t i = 0; i < cls->as.class_decl.members.count; i++) {
        ASTNode *m = cls->as.class_decl.members.nodes[i];
        if (c->member_count >= c->member_cap) {
            size_t cap = c->member_cap ? c->member_cap * 2 : 8;
            c->members = realloc(c->members, sizeof(StringView) * cap);
            c->member_is_method = realloc(c->member_is_method, sizeof(bool) * cap);
            c->member_cap = cap;
        }
        if (m->type == AST_FUNC_DECL) {
            c->members[c->member_count] = m->as.func_decl.name;
            c->member_is_method[c->member_count] = true;
        } else {
            c->members[c->member_count] = m->as.var_decl.name;
            c->member_is_method[c->member_count] = false;
        }
        c->member_count++;
    }
}

static bool class_member_lookup(CodegenClass *c, StringView name, bool *is_method) {
    for (size_t i = 0; i < c->member_count; i++) {
        if (sv_eq(c->members[i], name)) {
            if (is_method) *is_method = c->member_is_method[i];
            return true;
        }
    }
    return false;
}

static void class_free(Codegen *cg) {
    for (size_t i = 0; i < cg->class_count; i++) {
        free(cg->classes[i].members);
        free(cg->classes[i].member_is_method);
    }
    free(cg->classes);
    cg->classes = NULL;
    cg->class_count = 0;
    cg->class_cap = 0;
}

// Built-in names that are always in scope.
static bool is_builtin_name(StringView name) {
    return sv_eq(name, sv_from_cstr("console")) ||
           sv_eq(name, sv_from_cstr("delay")) ||
           sv_eq(name, sv_from_cstr("input")) ||
           sv_eq(name, sv_from_cstr("len")) ||
           sv_eq(name, sv_from_cstr("push")) ||
           sv_eq(name, sv_from_cstr("pop")) ||
           sv_eq(name, sv_from_cstr("has")) ||
           sv_eq(name, sv_from_cstr("keys")) ||
           sv_eq(name, sv_from_cstr("values")) ||
           sv_eq(name, sv_from_cstr("break")) ||
           sv_eq(name, sv_from_cstr("continue"));
}

static void validate_identifier(Codegen *cg, ASTNode *node, StringView name) {
    if (scope_has(cg, name) || is_builtin_name(name)) return;
    char buf[192];
    snprintf(buf, sizeof(buf),
             "Unknown identifier '%.*s' (declare it with var/const first)",
             (int)name.length, name.data);
    error_at_node(cg, node, buf);
}

// Render a plain string literal, converting Dino escape sequences to C.
// sv includes surrounding quotes.
static void emit_string_literal(Codegen *cg, StringView sv) {
    sb_append_char(&cg->out, '"');
    for (size_t i = 1; i < sv.length - 1; i++) {
        char c = sv.data[i];
        if (c == '\\' && i + 1 < sv.length - 1) {
            sb_append_char(&cg->out, '\\');
            sb_append_char(&cg->out, sv.data[++i]);
        } else {
            sb_append_char(&cg->out, c);
        }
    }
    sb_append_char(&cg->out, '"');
}

// ── Forward declarations ─────────────────────────────────────────────────────

static void codegen_statement(Codegen *cg, ASTNode *node);
static void codegen_expression(Codegen *cg, ASTNode *node);

// ── Assignment ───────────────────────────────────────────────────────────────

// Emit a statement that performs `target = value`. Only variables and indexes
// are assignable.
static void emit_assignment_statement(Codegen *cg, ASTNode *node) {
    ASTNode *target = node->as.assign.target;
    ASTNode *value = node->as.assign.value;

    if (target->type == AST_IDENTIFIER) {
        validate_identifier(cg, node, target->as.identifier.name);
        emit_sv(cg, target->as.identifier.name);
        emit(cg, " = ");
        codegen_expression(cg, value);
    } else if (target->type == AST_INDEX_EXPR) {
        emit(cg, "_dino_set(");
        codegen_expression(cg, target->as.index_expr.object);
        emit(cg, ", ");
        codegen_expression(cg, target->as.index_expr.index);
        emit(cg, ", ");
        codegen_expression(cg, value);
        emit(cg, ")");
    } else {
        error_at_node(cg, node, "Invalid assignment target (expected a variable or an index)");
    }
}

// ── Expression codegen ───────────────────────────────────────────────────────

static void codegen_expression(Codegen *cg, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_NUMBER: {
            StringView val = node->as.number.value;
            bool is_float = false;
            for (size_t i = 0; i < val.length; i++) {
                if (val.data[i] == '.') { is_float = true; break; }
            }
            emit(cg, is_float ? "_dino_float(" : "_dino_int(");
            emit_sv(cg, val);
            emit(cg, ")");
            break;
        }

        case AST_BOOL_LITERAL:
            emit(cg, node->as.bool_literal.value ? "_dino_bool(1)" : "_dino_bool(0)");
            break;

        case AST_NULL_LITERAL:
            emit(cg, "_dino_null()");
            break;

        case AST_STRING:
            emit(cg, "_dino_str(");
            emit_string_literal(cg, node->as.string.value);
            emit(cg, ")");
            break;

        case AST_INTERPOLATED_STRING: {
            // Build a printf format (text parts, %s for values) plus a matching
            // argument list of `_dino_cstr(<generated expr>)`.
            StringBuilder fmt = sb_new();
            StringBuilder args = sb_new();
            sb_append_char(&fmt, '"');
            ASTNodeList *parts = &node->as.interpolated_string.parts;
            for (size_t i = 0; i < parts->count; i++) {
                ASTNode *part = parts->nodes[i];
                if (part->type == AST_STRING) {
                    StringView sv = part->as.string.value; // includes quotes
                    for (size_t j = 1; j + 1 < sv.length; j++) {
                        char c = sv.data[j];
                        if (c == '%') sb_append_char(&fmt, '%'); // escape for vsnprintf
                        sb_append_char(&fmt, c);
                    }
                } else {
                    sb_append_cstr(&fmt, "%s");
                    if (args.length > 0) sb_append_cstr(&args, ", ");
                    sb_append_cstr(&args, "_dino_cstr(");
                    // Generate the sub-expression into args by borrowing the
                    // output buffer temporarily.
                    StringBuilder saved = cg->out;
                    cg->out = sb_new();
                    codegen_expression(cg, part);
                    sb_append(&args, sv_from_parts(cg->out.data, cg->out.length));
                    sb_free(&cg->out);
                    cg->out = saved;
                    sb_append_cstr(&args, ")");
                }
            }
            sb_append_char(&fmt, '"');

            emit(cg, "_dino_fmt(");
            sb_append(&cg->out, sv_from_parts(fmt.data, fmt.length));
            if (args.length > 0) {
                emit(cg, ", ");
                sb_append(&cg->out, sv_from_parts(args.data, args.length));
            }
            emit(cg, ")");
            sb_free(&fmt);
            sb_free(&args);
            break;
        }

        case AST_IDENTIFIER: {
            StringView name = node->as.identifier.name;
            if (class_find(cg, name)) {
                char buf[192];
                snprintf(buf, sizeof(buf),
                         "Class '%.*s' can only be used as '%.*s.member'",
                         (int)name.length, name.data, (int)name.length, name.data);
                error_at_node(cg, node, buf);
                break;
            }
            validate_identifier(cg, node, name);
            emit_sv(cg, name);
            break;
        }

        case AST_ARRAY_LITERAL: {
            size_t n = node->as.array_literal.elements.count;
            if (n == 0) {
                emit(cg, "_dino_array_from(0, NULL)");
                break;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "_dino_array_from(%zu, (DinoValue[]){", n);
            emit(cg, buf);
            for (size_t i = 0; i < n; i++) {
                if (i > 0) emit(cg, ", ");
                codegen_expression(cg, node->as.array_literal.elements.nodes[i]);
            }
            emit(cg, "})");
            break;
        }

        case AST_DICT_LITERAL: {
            size_t n = node->as.dict_literal.keys.count;
            if (n == 0) {
                emit(cg, "_dino_dict_from(0, NULL)");
                break;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "_dino_dict_from(%zu, (DinoValue[]){", n);
            emit(cg, buf);
            for (size_t i = 0; i < n; i++) {
                if (i > 0) emit(cg, ", ");
                // A bare identifier key is treated as a literal string key
                // (`{name: 1}` is `{"name": 1}`), like JavaScript objects.
                ASTNode *key = node->as.dict_literal.keys.nodes[i];
                if (key->type == AST_IDENTIFIER) {
                    emit(cg, "_dino_str(");
                    sb_append_char(&cg->out, '"');
                    emit_sv(cg, key->as.identifier.name);
                    sb_append_char(&cg->out, '"');
                    emit(cg, ")");
                } else {
                    codegen_expression(cg, key);
                }
                emit(cg, ", ");
                codegen_expression(cg, node->as.dict_literal.values.nodes[i]);
            }
            emit(cg, "})");
            break;
        }

        case AST_INDEX_EXPR:
            emit(cg, "_dino_get(");
            codegen_expression(cg, node->as.index_expr.object);
            emit(cg, ", ");
            codegen_expression(cg, node->as.index_expr.index);
            emit(cg, ")");
            break;

        case AST_ASSIGN: {
            ASTNode *target = node->as.assign.target;
            if (target->type == AST_IDENTIFIER) {
                validate_identifier(cg, node, target->as.identifier.name);
                emit(cg, "(");
                emit_sv(cg, target->as.identifier.name);
                emit(cg, " = ");
                codegen_expression(cg, node->as.assign.value);
                emit(cg, ")");
            } else if (target->type == AST_INDEX_EXPR) {
                // GCC statement expression: evaluate each part exactly once and
                // yield the assigned value.
                emit(cg, "({ DinoValue _dino_tmp = ");
                codegen_expression(cg, node->as.assign.value);
                emit(cg, "; _dino_set(");
                codegen_expression(cg, target->as.index_expr.object);
                emit(cg, ", ");
                codegen_expression(cg, target->as.index_expr.index);
                emit(cg, ", _dino_tmp); _dino_tmp; })");
            } else {
                error_at_node(cg, node, "Invalid assignment target (expected a variable or an index)");
            }
            break;
        }

        case AST_BINARY_EXPR: {
            StringView op = node->as.binary_expr.op;
            const char *fn = NULL;
            if      (sv_eq(op, sv_from_cstr("+")))  fn = "_dino_add";
            else if (sv_eq(op, sv_from_cstr("-")))  fn = "_dino_sub";
            else if (sv_eq(op, sv_from_cstr("*")))  fn = "_dino_mul";
            else if (sv_eq(op, sv_from_cstr("/")))  fn = "_dino_div";
            else if (sv_eq(op, sv_from_cstr("%")))  fn = "_dino_mod";
            else if (sv_eq(op, sv_from_cstr("=="))) fn = "_dino_eq";
            else if (sv_eq(op, sv_from_cstr("!="))) fn = "_dino_ne";
            else if (sv_eq(op, sv_from_cstr("<")))  fn = "_dino_lt";
            else if (sv_eq(op, sv_from_cstr("<="))) fn = "_dino_le";
            else if (sv_eq(op, sv_from_cstr(">")))  fn = "_dino_gt";
            else if (sv_eq(op, sv_from_cstr(">="))) fn = "_dino_ge";
            else if (sv_eq(op, sv_from_cstr("&&"))) fn = "_dino_and";
            else if (sv_eq(op, sv_from_cstr("||"))) fn = "_dino_or";
            if (!fn) { error_at_node(cg, node, "Unsupported operator"); break; }
            emit(cg, fn);
            emit(cg, "(");
            codegen_expression(cg, node->as.binary_expr.left);
            emit(cg, ", ");
            codegen_expression(cg, node->as.binary_expr.right);
            emit(cg, ")");
            break;
        }

        case AST_UNARY_EXPR: {
            StringView op = node->as.unary_expr.op;
            if (sv_eq(op, sv_from_cstr("++")) || sv_eq(op, sv_from_cstr("--"))) {
                ASTNode *operand = node->as.unary_expr.operand;
                if (operand->type != AST_IDENTIFIER) {
                    error_at_node(cg, node, "'++' / '--' require a variable");
                    break;
                }
                validate_identifier(cg, node, operand->as.identifier.name);
                emit(cg, sv_eq(op, sv_from_cstr("++")) ? "_dino_inc(&" : "_dino_dec(&");
                emit_sv(cg, operand->as.identifier.name);
                emit(cg, ")");
                break;
            }
            emit(cg, sv_eq(op, sv_from_cstr("!")) ? "_dino_not(" : "_dino_neg(");
            codegen_expression(cg, node->as.unary_expr.operand);
            emit(cg, ")");
            break;
        }

        case AST_CALL_EXPR: {
            ASTNode *callee = node->as.call_expr.callee;
            size_t nargs = node->as.call_expr.arguments.count;

            // Built-in functions.
            if (callee->type == AST_IDENTIFIER) {
                StringView name = callee->as.identifier.name;
                if (sv_eq(name, sv_from_cstr("delay"))) {
                    if (nargs != 1) { error_at_node(cg, node, "delay() expects exactly 1 argument (seconds)"); break; }
                    emit(cg, "(_dino_delay(_dino_to_double(");
                    codegen_expression(cg, node->as.call_expr.arguments.nodes[0]);
                    emit(cg, ")), _dino_null())");
                    break;
                }
                if (sv_eq(name, sv_from_cstr("input"))) {
                    if (nargs != 1) { error_at_node(cg, node, "input() expects exactly 1 argument (the prompt)"); break; }
                    emit(cg, "_dino_input(");
                    codegen_expression(cg, node->as.call_expr.arguments.nodes[0]);
                    emit(cg, ")");
                    break;
                }
                if (sv_eq(name, sv_from_cstr("len"))) {
                    if (nargs != 1) { error_at_node(cg, node, "len() expects exactly 1 argument"); break; }
                    emit(cg, "_dino_len(");
                    codegen_expression(cg, node->as.call_expr.arguments.nodes[0]);
                    emit(cg, ")");
                    break;
                }
                if (sv_eq(name, sv_from_cstr("push"))) {
                    if (nargs != 2) { error_at_node(cg, node, "push() expects 2 arguments (array, value)"); break; }
                    emit(cg, "_dino_push(");
                    codegen_expression(cg, node->as.call_expr.arguments.nodes[0]);
                    emit(cg, ", ");
                    codegen_expression(cg, node->as.call_expr.arguments.nodes[1]);
                    emit(cg, ")");
                    break;
                }
                if (sv_eq(name, sv_from_cstr("pop"))) {
                    if (nargs != 1) { error_at_node(cg, node, "pop() expects exactly 1 argument (array)"); break; }
                    emit(cg, "_dino_pop(");
                    codegen_expression(cg, node->as.call_expr.arguments.nodes[0]);
                    emit(cg, ")");
                    break;
                }
                if (sv_eq(name, sv_from_cstr("has"))) {
                    if (nargs != 2) { error_at_node(cg, node, "has() expects 2 arguments (container, key)"); break; }
                    emit(cg, "_dino_has(");
                    codegen_expression(cg, node->as.call_expr.arguments.nodes[0]);
                    emit(cg, ", ");
                    codegen_expression(cg, node->as.call_expr.arguments.nodes[1]);
                    emit(cg, ")");
                    break;
                }
                if (sv_eq(name, sv_from_cstr("keys"))) {
                    if (nargs != 1) { error_at_node(cg, node, "keys() expects exactly 1 argument (dict)"); break; }
                    emit(cg, "_dino_keys(");
                    codegen_expression(cg, node->as.call_expr.arguments.nodes[0]);
                    emit(cg, ")");
                    break;
                }
                if (sv_eq(name, sv_from_cstr("values"))) {
                    if (nargs != 1) { error_at_node(cg, node, "values() expects exactly 1 argument (dict)"); break; }
                    emit(cg, "_dino_values(");
                    codegen_expression(cg, node->as.call_expr.arguments.nodes[0]);
                    emit(cg, ")");
                    break;
                }
            }

            // Class method call: ClassName.method(args).
            if (callee->type == AST_MEMBER_EXPR &&
                callee->as.member_expr.object->type == AST_IDENTIFIER) {
                CodegenClass *cls = class_find(cg, callee->as.member_expr.object->as.identifier.name);
                if (cls) {
                    StringView prop = callee->as.member_expr.property;
                    bool is_method = false;
                    if (!class_member_lookup(cls, prop, &is_method)) {
                        char buf[192];
                        snprintf(buf, sizeof(buf), "Class '%.*s' has no member '%.*s'",
                                 (int)cls->name.length, cls->name.data, (int)prop.length, prop.data);
                        error_at_node(cg, node, buf);
                        break;
                    }
                    if (!is_method) {
                        char buf[192];
                        snprintf(buf, sizeof(buf), "'%.*s.%.*s' is a field, not a method",
                                 (int)cls->name.length, cls->name.data, (int)prop.length, prop.data);
                        error_at_node(cg, node, buf);
                        break;
                    }
                    emit_sv(cg, cls->name);
                    emit(cg, "_");
                    emit_sv(cg, prop);
                    emit(cg, "(");
                    for (size_t i = 0; i < nargs; i++) {
                        if (i > 0) emit(cg, ", ");
                        codegen_expression(cg, node->as.call_expr.arguments.nodes[i]);
                    }
                    emit(cg, ")");
                    break;
                }
            }

            // console.* builtins.
            if (callee->type == AST_MEMBER_EXPR) {
                ASTNode *obj = callee->as.member_expr.object;
                StringView prop = callee->as.member_expr.property;
                if (obj->type == AST_IDENTIFIER && sv_eq(obj->as.identifier.name, sv_from_cstr("console"))) {
                    if (sv_eq(prop, sv_from_cstr("do"))) {
                        emit(cg, "(system(_dino_cstr(");
                        if (nargs == 1) codegen_expression(cg, node->as.call_expr.arguments.nodes[0]);
                        else emit(cg, "_dino_null()");
                        emit(cg, ")), _dino_null())");
                        break;
                    }
                    bool is_log = sv_eq(prop, sv_from_cstr("log"));
                    bool is_warn = sv_eq(prop, sv_from_cstr("warn"));
                    bool is_error = sv_eq(prop, sv_from_cstr("error"));
                    if (!is_log && !is_warn && !is_error) {
                        char buf[160];
                        snprintf(buf, sizeof(buf),
                                 "Unknown console method '%.*s' (expected log, warn, error or do)",
                                 (int)prop.length, prop.data);
                        error_at_node(cg, node, buf);
                        break;
                    }
                    if (is_log) {
                        emit(cg, "_dino_printv(");
                    } else {
                        emit(cg, "_dino_eprintv(");
                        if (is_warn) emit(cg, "\"\\033[33m\"");
                        else emit(cg, "\"\\033[31m\"");
                        emit(cg, ", ");
                    }
                    if (nargs == 0) {
                        emit(cg, "0, NULL");
                    } else {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%zu, (DinoValue[]){", nargs);
                        emit(cg, buf);
                        for (size_t i = 0; i < nargs; i++) {
                            if (i > 0) emit(cg, ", ");
                            codegen_expression(cg, node->as.call_expr.arguments.nodes[i]);
                        }
                        emit(cg, "}");
                    }
                    emit(cg, ")");
                    break;
                }
            }

            if (callee->type == AST_CALL_EXPR) {
                error_at_node(cg, node, "Cannot call the result of a call (stray '()'?)");
                break;
            }
            // `obj.value(i)` / `dict.value(i)` reads the i-th value (or the
            // value for key i) — a convenience alias for indexing.
            if (callee->type == AST_MEMBER_EXPR &&
                sv_eq(callee->as.member_expr.property, sv_from_cstr("value"))) {
                if (nargs != 1) { error_at_node(cg, node, "value() expects exactly 1 argument (index or key)"); break; }
                emit(cg, "_dino_get(");
                codegen_expression(cg, callee->as.member_expr.object);
                emit(cg, ", ");
                codegen_expression(cg, node->as.call_expr.arguments.nodes[0]);
                emit(cg, ")");
                break;
            }
            // Identifier call targets are not validated as Dino identifiers:
            // any C symbol visible to the generated code may be called.
            if (callee->type == AST_IDENTIFIER) {
                emit_sv(cg, callee->as.identifier.name);
            } else if (callee->type == AST_MEMBER_EXPR) {
                char buf[160];
                snprintf(buf, sizeof(buf), "Unknown method '%.*s'", (int)callee->as.member_expr.property.length, callee->as.member_expr.property.data);
                error_at_node(cg, node, buf);
                break;
            } else {
                codegen_expression(cg, callee);
            }
            emit(cg, "(");
            for (size_t i = 0; i < nargs; i++) {
                if (i > 0) emit(cg, ", ");
                codegen_expression(cg, node->as.call_expr.arguments.nodes[i]);
            }
            emit(cg, ")");
            break;
        }

        case AST_MEMBER_EXPR: {
            // ClassName.member — fields are read; methods must be called.
            if (node->as.member_expr.object->type == AST_IDENTIFIER) {
                CodegenClass *cls = class_find(cg, node->as.member_expr.object->as.identifier.name);
                if (cls) {
                    StringView prop = node->as.member_expr.property;
                    bool is_method = false;
                    if (!class_member_lookup(cls, prop, &is_method)) {
                        char buf[192];
                        snprintf(buf, sizeof(buf), "Class '%.*s' has no member '%.*s'",
                                 (int)cls->name.length, cls->name.data, (int)prop.length, prop.data);
                        error_at_node(cg, node, buf);
                        break;
                    }
                    if (is_method) {
                        char buf[256];
                        snprintf(buf, sizeof(buf),
                                 "Method '%.*s.%.*s' must be called, e.g. '%.*s.%.*s(...)'",
                                 (int)cls->name.length, cls->name.data, (int)prop.length, prop.data,
                                 (int)cls->name.length, cls->name.data, (int)prop.length, prop.data);
                        error_at_node(cg, node, buf);
                        break;
                    }
                    emit_sv(cg, cls->name);
                    emit(cg, "_");
                    emit_sv(cg, prop);
                    break;
                }
            }
            if (node->as.member_expr.object->type == AST_IDENTIFIER &&
                sv_eq(node->as.member_expr.object->as.identifier.name, sv_from_cstr("console"))) {
                char buf[160];
                snprintf(buf, sizeof(buf),
                         "console can only be called as console.log/warn/error/do, got 'console.%.*s'",
                         (int)node->as.member_expr.property.length, node->as.member_expr.property.data);
                error_at_node(cg, node, buf);
                break;
            }
            if (sv_eq(node->as.member_expr.property, sv_from_cstr("length")) ||
                sv_eq(node->as.member_expr.property, sv_from_cstr("len"))) {
                emit(cg, "_dino_len(");
                codegen_expression(cg, node->as.member_expr.object);
                emit(cg, ")");
                break;
            }
            if (sv_eq(node->as.member_expr.property, sv_from_cstr("keys")) ||
                sv_eq(node->as.member_expr.property, sv_from_cstr("key"))) {
                emit(cg, "_dino_keys(");
                codegen_expression(cg, node->as.member_expr.object);
                emit(cg, ")");
                break;
            }
            if (sv_eq(node->as.member_expr.property, sv_from_cstr("values")) ||
                sv_eq(node->as.member_expr.property, sv_from_cstr("value"))) {
                emit(cg, "_dino_values(");
                codegen_expression(cg, node->as.member_expr.object);
                emit(cg, ")");
                break;
            }
            if (sv_eq(node->as.member_expr.property, sv_from_cstr("valueOfKey"))) {
                // Alias for the container itself, so `d.valueOfKey[k]` performs
                // a lookup (by key, or positionally for an integer).
                emit(cg, "(");
                codegen_expression(cg, node->as.member_expr.object);
                emit(cg, ")");
                break;
            }
            // Any other member name is a string-key lookup, so `person.name`
            // is sugar for `person["name"]` (and misses yield null).
            emit(cg, "_dino_get(");
            codegen_expression(cg, node->as.member_expr.object);
            emit(cg, ", _dino_str(\"");
            emit_sv(cg, node->as.member_expr.property);
            emit(cg, "\"))");
            break;
        }

        default:
            error_at_node(cg, node, "Cannot generate expression for this node");
            break;
    }
}

// ── Statement codegen ────────────────────────────────────────────────────────

// Emit a top-level function definition. Functions take and (currently) return
// nothing meaningful — parameters are dynamic values. `prefix` is the class
// name for methods (emitted as `Class_method`), or empty for plain functions.
static void codegen_func_decl_prefixed(Codegen *cg, ASTNode *node, StringView prefix) {
    emit(cg, "static __attribute__((unused)) void ");
    if (prefix.length > 0) { emit_sv(cg, prefix); emit(cg, "_"); }
    emit_sv(cg, node->as.func_decl.name);
    emit(cg, "(");
    if (node->as.func_decl.params.count == 0) {
        emit(cg, "void");
    } else {
        for (size_t i = 0; i < node->as.func_decl.params.count; i++) {
            if (i > 0) emit(cg, ", ");
            ASTNode *param = node->as.func_decl.params.nodes[i];
            emit(cg, "DinoValue ");
            emit_sv(cg, param->as.var_decl.name);
        }
    }
    emit(cg, ") {\n");
    cg->indent_level++;
    scope_push(cg);

    for (size_t i = 0; i < node->as.func_decl.params.count; i++) {
        ASTNode *param = node->as.func_decl.params.nodes[i];
        scope_declare(cg, param->as.var_decl.name);
    }

    if (node->as.func_decl.body) {
        ASTNode *body = node->as.func_decl.body;
        for (size_t i = 0; i < body->as.block.statements.count; i++) {
            codegen_statement(cg, body->as.block.statements.nodes[i]);
        }
    }

    scope_pop(cg);
    cg->indent_level--;
    emit_indent(cg);
    emit(cg, "}\n");
}

static void codegen_func_decl(Codegen *cg, ASTNode *node) {
    codegen_func_decl_prefixed(cg, node, sv_from_cstr(""));
}

// Forward declaration, so any function or method may call any other.
static void codegen_func_prototype(Codegen *cg, ASTNode *node, StringView prefix) {
    emit(cg, "static void ");
    if (prefix.length > 0) { emit_sv(cg, prefix); emit(cg, "_"); }
    emit_sv(cg, node->as.func_decl.name);
    emit(cg, "(");
    if (node->as.func_decl.params.count == 0) {
        emit(cg, "void");
    } else {
        for (size_t i = 0; i < node->as.func_decl.params.count; i++) {
            if (i > 0) emit(cg, ", ");
            emit(cg, "DinoValue");
        }
    }
    emit(cg, ");\n");
}

// Emit a class: a global DinoValue per field, then a static function per
// method, all named `Class_member`.
static void codegen_class_decl(Codegen *cg, ASTNode *cls) {
    StringView cname = cls->as.class_decl.name;

    // Field storage (zero-initialized == null). Their initializers run at the
    // top of main(), because Dino value constructors are not constant
    // expressions and so cannot initialize a static C global.
    for (size_t i = 0; i < cls->as.class_decl.members.count; i++) {
        ASTNode *m = cls->as.class_decl.members.nodes[i];
        if (m->type != AST_VAR_DECL) continue;
        emit(cg, "static __attribute__((unused)) DinoValue ");
        emit_sv(cg, cname);
        emit(cg, "_");
        emit_sv(cg, m->as.var_decl.name);
        emit(cg, ";\n");
    }

    // Then the methods.
    for (size_t i = 0; i < cls->as.class_decl.members.count; i++) {
        ASTNode *m = cls->as.class_decl.members.nodes[i];
        if (m->type == AST_FUNC_DECL) codegen_func_decl_prefixed(cg, m, cname);
    }
}

// Emit a control-flow body: blocks start immediately with '{' on the current
// line; anything else falls through to normal statement codegen.
static void codegen_braced_body(Codegen *cg, ASTNode *body) {
    scope_push(cg);
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
    scope_pop(cg);
}

static void codegen_statement(Codegen *cg, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_VAR_DECL: {
            emit_indent(cg);
            if (node->as.var_decl.is_const) emit(cg, "const ");
            emit(cg, "DinoValue ");
            emit_sv(cg, node->as.var_decl.name);
            emit(cg, " = ");
            if (node->as.var_decl.initializer) {
                codegen_expression(cg, node->as.var_decl.initializer);
            } else {
                emit(cg, "_dino_null()");
            }
            emit(cg, ";");
            scope_declare(cg, node->as.var_decl.name);
            emit_line(cg);
            break;
        }

        case AST_IF_STMT:
            emit_indent(cg);
            emit(cg, "if (_dino_truthy(");
            codegen_expression(cg, node->as.if_stmt.condition);
            emit(cg, ")) ");
            codegen_braced_body(cg, node->as.if_stmt.then_branch);
            if (node->as.if_stmt.else_branch) {
                emit_indent(cg);
                emit(cg, "else ");
                codegen_braced_body(cg, node->as.if_stmt.else_branch);
            }
            break;

        case AST_FOR_STMT:
            scope_push(cg);
            emit_indent(cg);
            emit(cg, "for (");
            if (node->as.for_stmt.init) {
                if (node->as.for_stmt.init->type == AST_VAR_DECL) {
                    ASTNode *init = node->as.for_stmt.init;
                    if (init->as.var_decl.is_const) emit(cg, "const ");
                    emit(cg, "DinoValue ");
                    emit_sv(cg, init->as.var_decl.name);
                    emit(cg, " = ");
                    if (init->as.var_decl.initializer) codegen_expression(cg, init->as.var_decl.initializer);
                    else emit(cg, "_dino_null()");
                    scope_declare(cg, init->as.var_decl.name);
                } else {
                    codegen_expression(cg, node->as.for_stmt.init);
                }
            }
            emit(cg, "; ");
            if (node->as.for_stmt.condition) {
                emit(cg, "_dino_truthy(");
                codegen_expression(cg, node->as.for_stmt.condition);
                emit(cg, ")");
            }
            emit(cg, "; ");
            if (node->as.for_stmt.increment) {
                codegen_expression(cg, node->as.for_stmt.increment);
            }
            emit(cg, ") ");
            codegen_braced_body(cg, node->as.for_stmt.body);
            scope_pop(cg);
            break;

        case AST_WHILE_STMT:
            emit_indent(cg);
            emit(cg, "while (_dino_truthy(");
            codegen_expression(cg, node->as.while_stmt.condition);
            emit(cg, ")) ");
            codegen_braced_body(cg, node->as.while_stmt.body);
            break;

        case AST_SWITCH_STMT: {
            // Dino `case` labels are arbitrary runtime expressions, so a switch
            // transpiles to an if / else-if chain over the case conditions.
            bool first = true;
            for (size_t i = 0; i < node->as.switch_stmt.cases.count; i++) {
                ASTNode *c = node->as.switch_stmt.cases.nodes[i];
                emit_indent(cg);
                emit(cg, first ? "if (_dino_truthy(" : "else if (_dino_truthy(");
                codegen_expression(cg, c->as.case_stmt.condition);
                emit(cg, ")) {\n");
                cg->indent_level++;
                scope_push(cg);
                for (size_t j = 0; j < c->as.case_stmt.body.count; j++) {
                    codegen_statement(cg, c->as.case_stmt.body.nodes[j]);
                }
                scope_pop(cg);
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
                scope_push(cg);
                for (size_t j = 0; j < dc->as.case_stmt.body.count; j++) {
                    codegen_statement(cg, dc->as.case_stmt.body.nodes[j]);
                }
                scope_pop(cg);
                cg->indent_level--;
                emit_indent(cg);
                emit(cg, "}\n");
            }
            break;
        }

        case AST_BLOCK: {
            scope_push(cg);
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
            scope_pop(cg);
            break;
        }

        case AST_EXPR_STMT:
            emit_indent(cg);
            if (node->as.expr_stmt.expression) {
                if (node->as.expr_stmt.expression->type == AST_ASSIGN) {
                    emit_assignment_statement(cg, node->as.expr_stmt.expression);
                } else {
                    codegen_expression(cg, node->as.expr_stmt.expression);
                }
                emit(cg, ";");
            }
            emit_line(cg);
            break;

        case AST_THROW_STMT:
            emit_indent(cg);
            emit(cg, "_dino_throw(");
            codegen_expression(cg, node->as.throw_stmt.value);
            emit(cg, ");");
            emit_line(cg);
            break;

        case AST_TRY_STMT: {
            StringView name = node->as.try_stmt.catch_name;
            emit_indent(cg);
            emit(cg, "{ DinoTryFrame _dino_f;\n");
            cg->indent_level++;
            emit_indent(cg);
            emit(cg, "_dino_f.prev = _dino_try_top;\n");
            emit_indent(cg);
            emit(cg, "_dino_try_top = &_dino_f;\n");
            emit_indent(cg);
            emit(cg, "if (setjmp(_dino_f.buf) == 0) {\n");
            cg->indent_level++;
            scope_push(cg);
            for (size_t i = 0; i < node->as.try_stmt.try_body->as.block.statements.count; i++) {
                codegen_statement(cg, node->as.try_stmt.try_body->as.block.statements.nodes[i]);
            }
            scope_pop(cg);
            emit_indent(cg);
            emit(cg, "_dino_try_top = _dino_f.prev;\n");
            cg->indent_level--;
            emit_indent(cg);
            emit(cg, "} else {\n");
            cg->indent_level++;
            emit_indent(cg);
            emit(cg, "_dino_try_top = _dino_f.prev;\n");
            scope_push(cg);
            scope_declare(cg, name);
            emit_indent(cg);
            emit(cg, "DinoValue ");
            emit_sv(cg, name);
            emit(cg, " = _dino_f.err;\n");
            for (size_t i = 0; i < node->as.try_stmt.catch_body->as.block.statements.count; i++) {
                codegen_statement(cg, node->as.try_stmt.catch_body->as.block.statements.nodes[i]);
            }
            scope_pop(cg);
            cg->indent_level--;
            emit_indent(cg);
            emit(cg, "}\n");
            cg->indent_level--;
            emit_indent(cg);
            emit(cg, "}\n");
            break;
        }

        case AST_CLASS_DECL:
            error_at_node(cg, node, "Classes must be declared at the top level, not inside a block");
            break;

        case AST_FUNC_DECL:
            error_at_node(cg, node, "Functions must be declared at the top level, not inside a block");
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

    // Runtime prelude (dynamic values, containers, try/catch, print helpers).
    emit(&cg, DINO_RUNTIME_C);
    emit(&cg, "\n");

    // Register classes before generating anything, so `ClassName.member`
    // references resolve regardless of declaration order.
    for (size_t i = 0; i < program->as.program.statements.count; i++) {
        ASTNode *stmt = program->as.program.statements.nodes[i];
        if (stmt->type == AST_CLASS_DECL) class_register(&cg, stmt);
    }

    // Forward-declare every function and class method so they can call each
    // other regardless of definition order.
    for (size_t i = 0; i < program->as.program.statements.count; i++) {
        ASTNode *stmt = program->as.program.statements.nodes[i];
        if (stmt->type == AST_FUNC_DECL) {
            codegen_func_prototype(&cg, stmt, sv_from_cstr(""));
        } else if (stmt->type == AST_CLASS_DECL) {
            for (size_t j = 0; j < stmt->as.class_decl.members.count; j++) {
                ASTNode *m = stmt->as.class_decl.members.nodes[j];
                if (m->type == AST_FUNC_DECL)
                    codegen_func_prototype(&cg, m, stmt->as.class_decl.name);
            }
        }
    }
    emit(&cg, "\n");

    // Hoist user-defined functions and classes above main() so main can use
    // them (fields become globals; methods become static functions).
    for (size_t i = 0; i < program->as.program.statements.count; i++) {
        ASTNode *stmt = program->as.program.statements.nodes[i];
        if (stmt->type == AST_FUNC_DECL) {
            codegen_func_decl(&cg, stmt);
        } else if (stmt->type == AST_CLASS_DECL) {
            codegen_class_decl(&cg, stmt);
        }
    }
    emit(&cg, "\n");

    // Generate all statements inside main(). The top level of main() is a
    // scope of its own for validation purposes (top-level vars are C locals
    // of main, invisible to hoisted functions — matching the generated C).
    emit(&cg, "int main(void) {\n");
    cg.indent_level = 1;
    scope_push(&cg);
    // Run class field initializers now that we are inside main().
    for (size_t i = 0; i < program->as.program.statements.count; i++) {
        ASTNode *stmt = program->as.program.statements.nodes[i];
        if (stmt->type != AST_CLASS_DECL) continue;
        for (size_t j = 0; j < stmt->as.class_decl.members.count; j++) {
            ASTNode *m = stmt->as.class_decl.members.nodes[j];
            if (m->type != AST_VAR_DECL || !m->as.var_decl.initializer) continue;
            emit_indent(&cg);
            emit_sv(&cg, stmt->as.class_decl.name);
            emit(&cg, "_");
            emit_sv(&cg, m->as.var_decl.name);
            emit(&cg, " = ");
            codegen_expression(&cg, m->as.var_decl.initializer);
            emit(&cg, ";");
            emit_line(&cg);
        }
    }
    for (size_t i = 0; i < program->as.program.statements.count; i++) {
        ASTNode *stmt = program->as.program.statements.nodes[i];
        if (stmt->type == AST_FUNC_DECL || stmt->type == AST_CLASS_DECL) continue; // already hoisted
        codegen_statement(&cg, stmt);
    }
    scope_pop(&cg);
    emit(&cg, "    return 0;\n");
    emit(&cg, "}\n");

    if (cg.had_error) {
        *error_out = cg.error_msg.data;
        sb_free(&cg.out);
        scope_free(&cg);
        class_free(&cg);
        return NULL;
    }

    *error_out = NULL;
    sb_free(&cg.error_msg);
    scope_free(&cg);
    class_free(&cg);
    // Transfer ownership of the buffer to the caller
    char *result = cg.out.data;
    cg.out.data = NULL;
    sb_free(&cg.out);
    return result;
}
