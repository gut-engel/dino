# Dino

A tiny language (`*.dn`) that transpiles to C and compiles it with a C
compiler (default: `gcc`). The generated C is written to a `CCode/` folder
and the resulting executable is placed next to your input file.

## Build

```sh
make               # builds the `dino` compiler and the VS Code extension package
make dino          # only the compiler
make vsix          # only the extension package -> dino-language-<ver>.vsix
```

## Usage

```sh
./dino example.dn                  # transpiles to CCode/example.c and compiles it to ./example
./example                          # run the compiled program
./dino example.dn -o my_prog       # name the executable differently
./dino example.dn --doNotCompile   # only write CCode/example.c, skip the C compiler
./dino example.dn --check          # check syntax/semantics only; writes nothing
CC=clang ./dino example.dn         # use a different C compiler (default: gcc)
```

Syntax errors in your `.dn` file are reported with file, line and column
before anything is written, and the compiler is only invoked after its
command has been found on `$PATH`. If the C compiler is missing you get a
clear error instead of a confusing failure.

## Language

```dn
// typed const declaration
const bool boolean = true;

// if / else (note the optional trailing ';' after '}')
if (boolean) {
    console.log();       // prints a blank line
    console.warn();      // same, to stderr
    console.error();     // same, to stderr
};

// for loops with inferred or explicit types
for (var i = 0; i < 10; i++) {
    // switch cases are runtime conditions → transpiled to if/else chains
    switch (i) {
        case (i < 3) {
            // ...
        };
    };
};

// interpolated strings: $"..." with {expr} placeholders
console.do($"command {boolean}");   // runs the command via system()

var name = "dino";                  // inferred const char *
var x = 5;                          // inferred int
var flag = false;                   // inferred _Bool
```

### Features

- Lexer → parser (AST) → C code generation → C compile, split across
  `src/lexer`, `src/parser`, `src/ast`, and `src/codegen`
- Syntax checking with line/column error reports (`--check` mode included)
- Automatic compilation with `gcc` (or `$CC`), with a PATH check for the
  compiler command
- `const` / `var` declarations with explicit (`int`, `float`, `bool`, `void`)
  or inferred types
- `if`/`else`, `while`, `for`, `switch`/`case`/`default`
- `console.log`, `console.warn`, `console.error` (stdout/stderr)
- `console.do` shells out with `system()`
- `delay(seconds)` sleeps for whole or fractional seconds (`delay(1)`,
  `delay(0.25)`)
- Interpolated strings `$"..."` with `{expr}` placeholders
- Binary/unary/postfix operators (`++`, `--`), grouping, etc.
- Line (`//`) and block (`/* */`) comments

## Project layout

```
src/
  main.c               entry point: reads a .dn file, wires the pipeline
  common.c/.h          arena allocator + string helpers
  lexer/               tokenizer
  parser/              recursive-descent parser → AST
  ast/                 AST node definitions + pretty printer
  codegen/             AST → C source
CCode/                 generated C output (created on demand)
```