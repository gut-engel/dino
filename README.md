# Dino

A tiny language (`*.dn`) that **transpiles to C** and compiles it with a C
compiler (default: `gcc`). The generated C is written to `CCode/` and the
resulting executable is placed next to your input file.

![docs](docs/) — full documentation lives in [`docs/`](docs/README.md):

- **[Language reference](docs/syntax.md)** — dynamic values (incl. arrays and
  dictionaries), declarations, `func` functions, statements
  (`if`/`for`/`while`/`switch`/`try`), expressions, interpolated strings,
  built-ins (`console.*`, `delay`, `input`, `len`, `push`, `pop`, `has`,
  `keys`, `values`), compile-time errors.
- **[Commands](docs/commands.md)** — `dino` CLI usage, Makefile targets, the
  VS Code / VS Codium extension commands, quick start.

## Build

```sh
make               # builds the `dino` compiler and the VS Code extension package
make dino          # only the compiler
make vsix          # only the extension package -> dino-language-<ver>.vsix
```

`make` produces both `./dino` and the `.vsix` in one step.

## Quick start

```sh
./dino example.dn                  # transpile to CCode/example.c and compile to ./example
./example                          # run the compiled program

./dino example.dn -o my_prog       # name the executable differently
./dino example.dn --doNotCompile   # only write CCode/example.c, skip the C compiler
./dino example.dn --check          # check syntax/semantics only; writes nothing
CC=clang ./dino example.dn         # use a different C compiler (default: gcc)
```

Syntax errors are reported with file, line and column before anything is
written, and the C compiler is only invoked after its command has been found
on `$PATH`. See [commands.md](docs/commands.md) for the full CLI.

## Hello, world

```dn
var name = input("What is your name? ");   // read a string from stdin
console.log($"Hello, {name}!");            // interpolated string -> dynamic formatter
delay(1);                                  // sleep one second
console.warn("done");                      // stderr is fine too
```

## Language at a glance

Values are dynamic — a variable can hold a string, number, array or dictionary,
and type annotations are optional hints. Statements still end with `;`.

```dn
const bool boolean = true;          // typed constant (types are advisory)

// if / else — statements are terminated with ';' (after the closing '}')
if (boolean) {
    console.log();                  // prints a blank line
    console.warn("to stderr");
    console.error("also stderr");
};

// for loops; bodies must be { ... }
for (var i = 0; i < 10; i++) {
    // switch cases are runtime conditions -> if/else chains
    switch (i) {
        case (i < 3) {
            console.log($"under 3: {i}");
        };
        default {
            console.log("three or more");
        };
    };
};

var name = "dino";                  // string
var x = 5;                          // int
var flag = false;                   // bool
var answer = input("Say hi: ");     // input() returns a string
var xs = [1, 2.5, "three"];         // array (mixed values)
var person = {"name": "Ada", age: 36};   // dictionary
push(xs, "more");                   // arrays grow; xs[0] = 9 updates
console.log(len(xs), xs[0], person["name"]);

try {
    console.log(1 / 0);
} catch (err) {
    console.error($"caught: {err}");
};
```

Features in brief: dynamic values (`null`, `bool`, `int`, `float`, `string`,
`array`, `dict`) with optional type annotations · assignment (`=`, `x[i] = v`,
`d[k] = v`, `++`/`--`) · `if`/`else`, `while`, `for`, `switch`/`case`/
`default` · `break` / `continue` · `try` / `catch` / `throw` (nested;
uncaught errors abort with a message) · `console.log` / `warn` / `error`
(stdout / stderr, colourised on a terminal) · `console.do` (shells out with
`system()`) · `delay(seconds)` · `input(prompt)` · `len`, `push`, `pop`,
`has`, `keys`, `values` · interpolated strings `$"..."` with `{expr}`
placeholders · full operator set (`* / % + - < <= > >= == != && ||`, prefix
`-`/`!`, postfix `++`/`--`) · `//` and `/* */` comments.

## Project layout

```
src/
  main.c               entry point: reads a .dn file, wires the pipeline
  common.c/.h          arena allocator + string helpers
  lexer/               tokenizer
  parser/              recursive-descent parser → AST
  ast/                 AST node definitions + pretty printer
  codegen/             AST → C source
docs/                  language reference + commands (see docs/README.md)
vscode-dino/           VS Code / VS Codium extension (packaged via `make vsix`)
CCode/                 generated C output (created on demand)
```