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
| `--check`        | Syntax/type check only; writes nothing, prints `Syntax OK: '<file>'` |

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
make                # -> ./dino  and  dino-language-0.1.2.vsix
make dino           # -> ./dino
make vsix           # -> dino-language-0.1.2.vsix
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
code --install-extension dino-language-0.1.2.vsix

# VS Codium (note: --force must come BEFORE --install-extension)
codium --force --install-extension dino-language-0.1.2.vsix
```

### Verify / manage

```sh
codium --list-extensions | grep dino     # → dino.dino-language
codium --uninstall-extension dino.dino-language
```

Installed files land in:

- VS Code: `~/.vscode/extensions/dino.dino-language-<version>/`
- VS Codium: `~/.vscode-oss/extensions/dino.dino-language-<version>/`

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

- Syntax highlighting for `.dn` (keywords, types, literals, strings,
  interpolated strings, `console.*`, built-ins, operators, comments)
- Completion provider: `console` + `console.log/warn/error/do`, keywords,
  types, literals, and statement snippets (`for`, `while`, `if`, `switch`,
  `var`, `const`, `delay`, `input`)
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