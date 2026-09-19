# Dino Language Support

VS Code extension for the **Dino** programming language (`*.dn`), a tiny
language that transpiles to C. See the [main repository](../README.md) for the
transpiler itself.

## Features

- **File icon**: `*.dn` files show the dino icon (`icons/thumbnail.svg`) as
  their file icon in the Explorer, tabs and breadcrumbs.
  - Out of the box this uses the language default icon (works with icon themes
    that support them, built-in Seti theme included).
  - The extension also ships a **"Dino Icons" file icon theme** that maps `.dn`
    to the dino and adds generic file/folder icons for every other file. Pick it
    with **File ▸ Preferences ▸ File Icon Theme ▸ Dino Icons**, or set
    `"workbench.iconTheme": "dino-icons"`, to get the dino icon under *any*
    color theme — including Minimal, which ignores language icons.
- **Syntax highlighting** for `.dn` files
  - keywords (`const`, `var`, `if`, `else`, `for`, `while`, `switch`, `case`,
    `try`, `catch`, `throw`, `class`, `func`, ...)
  - types (`bool`, `int`, `float`, `void`, `string`, `array`, `dict`)
  - literals (`true`, `false`, `null`, numbers, strings, `$"..."` interpolated
    strings with `{expr}` placeholders, array/dict literals)
  - built-ins (`console.log`, `console.warn`, `console.error`, `console.do`,
    `delay`, `input`, `len`, `push`, `pop`, `has`, `keys`, `values`) — `warn` is
    color-coded yellow and `error` red
  - operators, comments (`//`, `/* */`)
- **Completions** (built-in provider)
  - `console` is suggested when you start typing it
  - after typing `console.` you get `log` / `warn` / `error` / `do` — accepted,
    they insert correctly (just the method name, cursor inside the parens)
  - statement completions with tab stops: `for`, `while`, `if`, `if else`,
    `switch`, `var`, `const`, `func`, `class`, `try`, `throw`, `array`, `dict`,
    `len`, `push`, `pop`, `has`, `keys`, `values`, `delay`, `input`
  - keywords, types and literals
- **Language configuration**: bracket matching / auto-closing pairs, comment
  toggling (`Ctrl+/`), indentation rules for blocks

## Requirements

- VS Code `^1.75.0`

## Installation

From a `.vsix` package (build it with `make vsix` in the repo root, which
produces `dino-language-<version>.vsix`):

```sh
code --install-extension dino-language-<version>.vsix
# or on VS Codium (--force must come before --install-extension):
codium --force --install-extension dino-language-<version>.vsix
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