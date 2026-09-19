# Commands Reference

Everything you can run: the `dino` compiler CLI, the Makefile build, and the
VS Code / VS Codium extension commands.

## 1. The `dino` compiler

```
Usage: dino <file.dn> [options]

        Transpiles a Dino source file to C and compiles it with a C compiler.
        The generated C is written to CCode/<file>.c and the executable to
        <file> (give it another name with -o).
```

### Common invocations

```sh
./dino example.dn                       # transpile + compile → ./example
./example                               # run the program

./dino example.dn -o build/app          # name the executable
./dino example.dn build/app             # same as -o (bare positional)

./dino example.dn --doNotCompile        # only write CCode/example.c
./dino example.dn --check               # parse + codegen check, write nothing

CC=clang ./dino example.dn              # use a different C compiler
```

### Options

| Option           | Meaning                                                    |
| ---------------- | ---------------------------------------------------------- |
| `-o <exe>`       | Output executable name (default: `./<input stem>`)         |
| `<exe>`          | A bare (non-flag) argument is treated like `-o`            |
| `--doNotCompile` | Transpile to C only; skip the C compiler (alias: `--no-compile`) |
| `--check`        | Syntax/semantic check only; writes nothing, prints `Syntax OK: '<file>'` |

Any other `-…` flag is rejected with `Error: unknown option '<flag>'` +
usage, exit code 1.

### Environment

| Variable | Default | Meaning                          |
| -------- | ------- | -------------------------------- |
| `CC`     | `gcc`   | C compiler used for the final step |

The C compiler is only invoked after its command has been found on `$PATH`, so
a missing compiler gives a clear error instead of a confusing failure.

### What it prints

```
Transpiled example.dn -> CCode/example.c
Compiled CCode/example.c -> example
```

- `--doNotCompile` adds `Note: skipped compilation (--doNotCompile).`
- A failed C build prints `Error: compilation failed (exit code N).` and
  `The generated C source is at 'CCode/example.c'.` (the C is left on disk for
  inspection).
- Syntax errors: `[line N, col N] Error at 'tok': message`, then
  `N syntax error(s) in '<file>'.`
- Codegen errors: `Codegen error (line N, col N): message`.

### Exit codes

| Code | Meaning                              |
| ---- | ------------------------------------ |
| `0`  | success (including `--check` OK)     |
| `1`  | any error: usage, missing file, parse, codegen, or C compile failure |

### Pipeline

```
<file>.dn → lexer → parser (AST) → codegen → CCode/<stem>.c → C compiler → <exe>
```

## 2. Makefile targets

Run from the repository root. Requires `make`, a C compiler, and Python 3
(only for the `.vsix` packaging).

| Target    | Builds                                                          |
| --------- | --------------------------------------------------------------- |
| `make`    | **everything** — the `dino` compiler **and** the extension `.vsix` |
| `make dino` | only the `dino` compiler                                      |
| `make vsix` | only `dino-language-<version>.vsix` (version from `vscode-dino/package.json`) |
| `make clean` | removes the `dino` binary, the `CCode/` directory, and the `.vsix` |

```sh
make                # -> ./dino  and  dino-language-<version>.vsix
make dino           # -> ./dino
make vsix           # -> dino-language-<version>.vsix
make clean
```

The compiler is built with `CFLAGS = -Wall -Wextra -std=c11 -g`, overridable:

```sh
CC=clang make
CFLAGS="-O2" make
```

## 3. VS Code / VS Codium extension

### Install from a `.vsix`

```sh
# VS Code
code --install-extension dino-language-<version>.vsix

# VS Codium (note: --force must come BEFORE --install-extension)
codium --force --install-extension dino-language-<version>.vsix
```

### Verify / manage

```sh
codium --list-extensions | grep dino     # → dino.dino-language
codium --uninstall-extension dino.dino-language
```

Installed files land in:

- VS Code: `~/.vscode/extensions/dino.dino-language-<version>/`
- VS Codium: `~/.vscode-oss/extensions/dino.dino-language-<version>/`

The extension provides syntax highlighting (including the console
log/warn/error colours and `func` declarations), **validation diagnostics**
and completions: `console.*` members, statement snippets, and
**user-defined functions** — the open document is indexed for `func`
declarations and each one is offered as `name(param, ...)` with tab stops per
parameter. Completions refresh as the file changes; commented-out functions
are not suggested.

### Validation (red squiggles)

The extension runs the `dino` compiler in `--check` mode on the **live buffer**
(debounced ~400 ms; the text is written to a temp file so unsaved edits are
checked) and reports every syntax/semantic error as a red squiggle with the
compiler's message in the hover and Problems panel. This catches undeclared
identifiers (e.g. a bare `stefan;`), scope mistakes, bad built-in calls and
syntax errors — the same messages the CLI prints. Diagnostics clear once the
file is valid; nothing is written into the project.

Where the compiler binary is found:

1. the `dino.compilerPath` setting (absolute path or a name on `PATH`);
2. otherwise `<workspaceFolder>/dino`, or `<documentFolder>/dino`, if that
   exists and is executable (so it works out of the box in the compiler's own
   repo, even when a single file is opened);
3. otherwise `dino` on `PATH`.

### Rebuild after changes in `vscode-dino/`

```sh
make vsix        # re-packages dino-language-<version>.vsix
codium --force --install-extension dino-language-<version>.vsix
```

### Development host (F5)

Open the `vscode-dino/` folder in VS Code/Codium and press `F5` — the
`.vscode/launch.json` starts an Extension Development Host with the extension
loaded.

### Editor features

- Syntax highlighting for `.dn` (keywords, types, literals incl. `null`,
  strings, interpolated strings, `console.*`, built-ins, operators, comments)
- Validation diagnostics from the `dino --check` compiler (red squiggles +
  messages; see above)
- Completion provider: `console` + `console.log/warn/error/do`, keywords
  (`try`/`catch`/`throw`), types (`bool`, `int`, `float`, `void`, `string`,
  `array`, `dict`), literals (`true`, `false`, `null`), user-defined functions,
  built-in helpers (`len`, `push`, `pop`, `has`, `keys`, `values`), and
  statement snippets (`for`, `while`, `if`, `switch`, `var`, `const`, `func`,
  `try`, `throw`, `array`, `dict`, `delay`, `input`)
- Language configuration: bracket auto-pairs, comment toggling (`Ctrl+/`),
  block indentation

## 4. Quick start

```sh
make                          # build compiler + extension package
cat > hello.dn <<'EOF'
var name = input("Name? ");
console.log($"Hello, {name}!");
EOF
./dino hello.dn -o hello
printf 'world\n' | ./hello    # → Hello, world!
```