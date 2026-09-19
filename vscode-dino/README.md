# Dino Language Support

VS Code extension for the **Dino** programming language (`*.dn`), a tiny
language that transpiles to C. See the [main repository](../README.md) for the
transpiler itself.

## Features

- **Syntax highlighting** for `.dn` files
  - keywords (`const`, `var`, `if`, `else`, `for`, `while`, `switch`, `case`, ...)
  - types (`bool`, `int`, `float`, `void`)
  - literals (`true`, `false`, numbers, strings, `$"..."` interpolated strings
    with `{expr}` placeholders)
  - built-ins (`console.log`, `console.warn`, `console.error`, `console.do`,
    `delay`)
  - operators, comments (`//`, `/* */`)
- **Snippets** for common constructs: `for`, `while`, `if`, `ife`, `switch`,
  `var`, `const`, `log`, `warn`, `error`, `do`, `delay`
- **Language configuration**: bracket matching / auto-closing pairs, comment
  toggling (`Ctrl+/`), indentation rules for blocks

## Requirements

- VS Code `^1.75.0`

## Installation

From a `.vsix` package (build it with `make vsix` in the repo root, which
produces `dino-language-<version>.vsix`):

```sh
code --install-extension dino-language-0.1.0.vsix
# or on VS Codium:
codium --install-extension dino-language-0.1.0.vsix
```

Or run the extension in a development host: open this folder in VS Code and
press `F5`.

## Usage

- Open any `*.dn` file — highlighting and snippets are active automatically.
- Transpile it with the `dino` compiler (built from the repo root with `make`):

```sh
make
./dino example.dn          # writes CCode/example.c
gcc CCode/example.c -o example
```

## Snippets

| Prefix    | Expands to                                  |
| --------- | ------------------------------------------- |
| `for`     | `for (var i = 0; i < N; i++) { ... }`       |
| `while`   | `while (cond) { ... }`                      |
| `if`      | `if (cond) { ... }`                         |
| `ife`     | `if (cond) { ... } else { ... }`            |
| `switch`  | `switch (v) { case (...) { ... }; ... }`    |
| `var`     | `var name = value;`                         |
| `const`   | `const type name = value;`                  |
| `log`     | `console.log(...);`                         |
| `warn`    | `console.warn(...);`                        |
| `error`   | `console.error(...);`                       |
| `do`      | `console.do(command);`                      |
| `delay`   | `delay(seconds);`                           |

## Release Notes

See [CHANGELOG.md](CHANGELOG.md).