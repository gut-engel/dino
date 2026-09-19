# Dino

A tiny language (`*.dn`) that transpiles to C. The generated C is written to a
`CCode/` folder and can be compiled with any C11 compiler.

## Build

```sh
make               # builds the `dino` transpiler
```

## Usage

```sh
./dino example.dn              # writes CCode/example.c
./dino example.dn -o out.c     # writes to out.c instead
gcc CCode/example.c -o example # compile the generated C
```

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

- Lexer → parser (AST) → C code generation, split across `src/lexer`,
  `src/parser`, `src/ast`, and `src/codegen`
- `const` / `var` declarations with explicit (`int`, `float`, `bool`, `void`)
  or inferred types
- `if`/`else`, `while`, `for`, `switch`/`case`/`default`
- `console.log`, `console.warn`, `console.error` (stdout/stderr)
- `console.do` shells out with `system()`
- Interpolated strings `$"..."` with `{expr}` placeholders
- Binary/unary/postfix operators (`++`, `--`), grouping, etc.

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