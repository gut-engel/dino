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
    `delay`, `input`) — `warn` is color-coded yellow and `error` red
  - operators, comments (`//`, `/* */`)
- **Completions** (built-in provider)
  - `console` is suggested when you start typing it
  - after typing `console.` you get `log` / `warn` / `error` / `do` — accepted,
    they insert correctly (just the method name, cursor inside the parens)
  - statement completions with tab stops: `for`, `while`, `if`, `if else`,
    `switch`, `var`, `const`, `delay`, `input`
  - keywords, types and literals
- **Language configuration**: bracket matching / auto-closing pairs, comment
  toggling (`Ctrl+/`), indentation rules for blocks

## Requirements

- VS Code `^1.75.0`

## Installation

From a `.vsix` package (build it with `make vsix` in the repo root, which
produces `dino-language-<version>.vsix`):

```sh
code --install-extension dino-language-0.1.1.vsix
# or on VS Codium:
codium --install-extension dino-language-0.1.1.vsix
```

Or run the extension in a development host: open this folder in VS Code and
press `F5`.

## Usage

- Open any `*.dn` file — highlighting and completions are active automatically.
- Type `console.` and accept `log` to get `console.log()` without any
  duplication.
- Transpile it with the `dino` compiler (built from the repo root with `make`):

```sh
make
./dino example.dn          # writes CCode/example.c
gcc CCode/example.c -o example
```

The full language reference and command docs live in the `docs/` folder of
the dino repository (`docs/syntax.md`, `docs/commands.md`).

## Release Notes

See [CHANGELOG.md](CHANGELOG.md).