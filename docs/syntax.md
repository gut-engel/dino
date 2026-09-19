# Dino Language Reference

Dino (`*.dn`) is a tiny, C-flavoured language that **transpiles to C** and is
compiled with a C compiler. What you write is essentially a structured way of
generating C: top-level `func` definitions become C functions, the rest of the
program maps to a single C `main()`, and the standard library is a handful of
built-ins (`console.*`, `delay`, `input`).

This page is the complete reference. For CLI usage see
[commands.md](commands.md).

---

## 1. Lexical structure

### Comments

```dn
// line comment

/* block
   comment */
```

- Line comments run to the end of the line.
- Block comments run to the next `*/`. They do **not** nest, and an
  unterminated block comment swallows the rest of the file.

### Identifiers

Identifiers start with a letter or `_` and continue with letters, digits or
`_`. The following words are reserved:

```
const  var    if     else   for    while
switch case   default break  continue return
bool   int    float  void   true   false
func   console
```

> `return` is reserved by the lexer but is **not** usable as a statement —
> writing `return ...;` is a syntax error. `break` / `continue` work inside
> `for` / `while` loops.

### Literals

| Literal          | Example              | Notes                                    |
| ---------------- | -------------------- | ---------------------------------------- |
| Integer          | `42`                 | decimal only                             |
| Decimal          | `3.14`               | `3.` / `.5` are not literals             |
| Boolean          | `true` / `false`     |                                          |
| String           | `"hello\n"`          | C-style `\` escapes pass through         |
| Interpolated str | `$"hi {name}!"`      | `{expr}` placeholders, see §4.4          |

Numbers have no exponent or hex forms. A `float` value is stored as a C
`double`.

## 2. Types

| Dino type | C type      | Notes                        |
| --------- | ----------- | ---------------------------- |
| `bool`    | `_Bool`     |                              |
| `int`     | `int`       |                              |
| `float`   | `double`    | decimal literals → `double`  |
| `void`    | `void`      |                              |
| `string`  | `const char *` | parameter/return type for strings (a `const char *`) |
| (none)    | `const char *` | inferred for string values |

### Type inference

`var` (and `const`) declarations without an explicit type infer it from the
initializer:

```dn
var a = 5;                          // int
var b = 5.5;                        // double (float)
var flag = false;                   // _Bool (bool)
var name = "dino";                  // const char *
var greeting = $"hi {name}";        // const char *  (via _dino_fmt)
var answer = input("Name? ");       // const char *  (input returns a string)
```

Anything the compiler can't classify falls back to `int`.

Because the compiler tracks which variables hold strings, printing them
(`console.log(name)`, `$"...{name}..."`) automatically uses `%s`; other
variables print as `%d` (integers and booleans fine — see the decimal-printing
limitation in §5). Variables holding strings are a pointer to a static buffer
— they must not be mutated (there is no assignment anyway).

## 3. Declarations

```
[ const | var ] [ type ]? name [ = initializer ] ;
```

```dn
const bool enabled = true;   // typed constant
var count = 0;               // inferred int
var int total = 10;          // explicit type on var
const name = "dino";         // inferred const char * constant
var ready;                   // uninitialized (falls back to int)
```

- The trailing `;` is required for declarations.
- `=` appears **only** in declarations: there is no assignment operator, so a
  variable can only be set at declaration time (and later via `x++` / `x--`).

### Functions

```dn
func name(type param, ...) {
    // statements
}
```

```dn
func greet(string name) {
    console.log($"Greetings to {name}!");
}

greet("Simon");          // call it like any C function
```

- `func` declarations are **top-level only** — nesting one inside another
  block is a compile-time error.
- Parameters are typed: `bool`, `int`, `float`, `void` or `string`. A missing
  type is a syntax error.
- Functions are `void` for now: they run statements but cannot return a value
  (`return` is not parsed by this language yet).
- Declaration order does not matter — functions are hoisted above `main()` in
  the generated C, so a function can call another defined later.
- String parameters are tracked like string variables, so
  `console.log(name)` / `$"...{name}..."` print them with `%s`.

## 4. Expressions

### 4.1 Operator precedence (lowest → highest)

| Precedence | Operators            | Associativity |
| ---------- | -------------------- | ------------- |
| 1 (lowest) | `\|\|`               | left          |
| 2          | `&&`                 | left          |
| 3          | `==`  `!=`           | left          |
| 4          | `<`  `<=`  `>`  `>=` | left          |
| 5          | `+`  `-`             | left          |
| 6          | `*`  `/`  `%`        | left          |
| 7          | prefix `-x`  `!x`    | right         |
| 8 (highest)| postfix `x++` `x--`  | left          |

```dn
var n = (1 + 2) * 3;   // 9 — grouping works
var m = -a + b;        // prefix minus
flag = !flag;          // ⚠ not valid — no assignment statement
```

### 4.2 Primary expressions

- literals (`42`, `3.14`, `true`, `false`, `"..."`, `$"..."`)
- identifiers (also `console`)
- `( expr )` grouping

### 4.3 Calls and members

```dn
console.log("hi");                 // member call
delay(0.5);                        // builtin call
input("Name? ");                   // builtin call, returns a string
var c = some_func(1, 2, 3);        // general call → C function call
```

- Postfix `++` / `--` operate on any expression (`i++`).
- General calls emit a plain C call `name(arg, arg, ...)` so any symbol
  available to the generated C (includes, helpers) can be invoked. Because
  of this escape hatch, **call targets are not validated** by the compiler:
  `some_func(...)` is accepted even if `some_func` is not declared (the C
  compiler will complain later if it truly does not exist). *Value*
  identifiers, by contrast, are validated — see §7.

### 4.4 Interpolated strings

`$"..."` mixes literal text with `{expr}` placeholders, formatted with
`printf` rules:

```dn
var i = 7;
console.log($"under 3: {i}\n");        // "under 3: 7"
var name = input("Name? ");
console.log($"Hello, {name}");         // strings print with %s
console.log($"sum: {1 + 2}");          // any expression
```

- Strings / string variables inside `{}` use `%s`; all other expressions use
  `%d` (see §5 for where the format is applied).
- Literal braces can appear inside a plain (non-interpolated) string.

## 5. Statements

Every statement ends with `;`. After a closing `}` of a block statement an
extra `;` is allowed and conventional (`if (...) { } ; `).

### Expression statement

```dn
some_call();
i++;
```

### Blocks

```dn
{
    console.log("inside");
}
```

A block is a statement; the bodies of `if` / `for` / `while` / `case` **must**
be a `{ ... }` block — a single statement without braces is a syntax error.

### `if` / `else`

```dn
if (cond) {
    // ...
} else if (other) {
    // ...
} else {
    // ...
};
```

### `while`

```dn
while (cond) {
    // ...
};
```

### `for`

All three clauses are optional (`for (;;)` is an infinite loop). The init may
be a `var`/`const` declaration (consumes its own `;`) or an expression:

```dn
for (var i = 0; i < 10; i++) {
    // ...
};

for (var int i = 0; i < 10; i++) { /* explicit type */ };

for (i++; i < 10; i++) { /* expression init */ };
```

### `switch` / `case` / `default`

`switch` cases are **runtime conditions** rather than constant labels — the
whole statement transpiles to an `if`/`else` chain:

```dn
switch (i) {
    case (i < 3) {
        console.log($"under 3: {i}");
    };
    case (i > 3) {
        console.log($"over 3: {i}");
    };
    default {
        console.log("exactly 3");
    };
};
```

- `case` always takes `( condition )`.
- Cases are tested in order; `break` inside a case exits the enclosing loop.

### `break` / `continue`

```dn
for (var i = 0; i < 10; i++) {
    if (i == 3) {
        break;      // exit the loop
    };
    if (i % 2 == 0) {
        continue;   // skip the rest of the iteration
    };
    console.log(i);
};
```

They pass through to the generated C, so they behave exactly like C's
`break` / `continue` (only meaningful inside loops).

## 6. Built-ins

### `console.log`, `console.warn`, `console.error`

Print to stdout (`log`) or stderr (`warn`, `error`). A newline is always
appended; with no arguments they print a blank line. When the program runs in
a terminal, `console.warn` output is printed **yellow** and `console.error`
**red** (ANSI escape codes); `console.log` keeps the terminal's default
colour. The codes are suppressed when output is redirected/piped, or when the
`NO_COLOR` environment variable is set.

```dn
console.log();                 // blank line
console.log("hello");          // "hello\n"
console.warn("careful");
console.error("boom");
console.log("a", 1, 2.5);      // ⚠ decimals print unreliably — see note below
```

Formatting rules per argument: string / interpolated-string literals → `%s`,
string variables → `%s`, everything else → `%d` (see §2). A single
interpolated-string argument also prints with a trailing newline:

```dn
console.log($"value: {x}");
```

> **Known limitation — decimal printing.** Values are formatted with `%s`
> (strings) or `%d` (everything else). Integer and boolean values print
> correctly, but **`float`/decimal values are passed to `%d` and print
> garbage**; the same applies inside interpolated strings, and one mis-sized
> argument can corrupt the rest of the line. There is currently no reliable
> way to print a non-integer value — keep printed values integral (or format
> them via `console.do("printf ...")`).

### `console.do`

Runs a command with `system()`; expects exactly one argument (usually an
interpolated string):

```dn
console.do($"command {boolean}");   // runs  command true  via the shell
console.do("ls -la");
```

With no arguments it compiles to a no-op.

### `delay(seconds)`

Sleeps for a whole or fractional number of seconds (via `nanosleep`), and
takes exactly one argument:

```dn
delay(1);     // one second
delay(0.25);  // quarter second
```

Any other argument count is a compile-time error:
`delay() expects exactly 1 argument (seconds)`.

### `input(prompt)`

Prints the prompt, reads one line from stdin, and returns it as a string
(leading/trailing newline stripped):

```dn
var name = input("What is your name? ");
console.log($"Hello, {name}");
```

- Exactly one argument is required:
  `input() expects exactly 1 argument (the prompt)`.
- The returned string lives in a static buffer — a single `input()` per
  statement is safe; overlapping uses of several returned strings share
  one 1024-byte buffer.

## 7. Compile-time errors

- **Lexer / parser**: reported as
  `[line N, col N] Error at 'tok': message` with the error count, e.g.
  `3 syntax error(s) in 'file.dn'.`
- **Semantic / codegen**: reported as
  `Codegen error (line N, col N): message`. This covers:
  - unknown `console.*` method, or a wrong argument count for `delay` / `input`;
  - an **unknown identifier** used as a value — e.g. a bare `stefan;` where no
    `var`/`const`/parameter named `stefan` is in scope
    (`Unknown identifier 'stefan' (declare it with var/const first)`);
  - calling the result of a call (a stray `()`, e.g. `console.error()("x")`).
- **Scope rules** mirror the generated C: a variable is visible from its
  declaration to the end of the enclosing `{ ... }` block, and `for`-init
  variables are visible for the whole `for` statement only, so using `i` after
  the loop is reported. Functions cannot see top-level `main` variables.
- A C compile failure is reported with the failing compiler's output; the
  generated C is still left at `CCode/<file>.c` for inspection.

## 8. Generated C

The output C starts with a small runtime preamble:

- `_dino_fmt(fmt, ...)` — printf-style formatting used by interpolated
  strings (8 rotating 1024-byte buffers).
- `_dino_delay(seconds)` — `nanosleep` helper.
- `_dino_input(prompt)` — prompt + `fgets` helper returning a string.
- `_dino_console_output(stream, color, fmt, ...)` — used by
  `console.warn`/`console.error`; `vfprintf`s to stderr and wraps the output
  in an ANSI colour when the stream is a terminal (`NO_COLOR` / redirection
  disables it).

`func` declarations are emitted ahead of `main()` (hoisted), so calls from
`main` (or other functions) resolve even when the definition comes later in
the file.

Programs compile with `gcc -Wall -Wextra -std=c11` without warnings.