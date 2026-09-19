# Dino Documentation

The Dino language (`*.dn`) transpiles to C and compiles it with `gcc`/`$CC`.
The code lives in `src/`; the editor extension in `vscode-dino/`.

| Document                        | Contents                                          |
| ------------------------------- | ------------------------------------------------- |
| [syntax.md](syntax.md)          | Complete language reference: dynamic values, statements, expressions, built-ins, errors |
| [commands.md](commands.md)      | CLI usage, Makefile targets, extension install/build commands, quick start |

- **Project layout** — see the compact map in the root [README](../README.md#project-layout).
- **Editor extension** — the shipped extension README is at
  [`vscode-dino/README.md`](../vscode-dino/README.md); usage commands are in
  [commands.md §3](commands.md#3-vs-code--vs-codium-extension).

## Quick start

```sh
make                # build ./dino and dino-language-<ver>.vsix
./dino example.dn   # transpile + compile -> ./example
./example           # run it
```

Read [syntax.md](syntax.md) to learn the language and
[commands.md](commands.md) for every command.