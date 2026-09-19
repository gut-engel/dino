# Change Log

## [0.1.6] - 2026-09-19

- **Real validation diagnostics**: the editor now runs the `dino` compiler in
  `--check` mode on the live buffer (debounced) and shows red squiggles with
  the compiler's message for syntax and semantic errors, e.g. an undeclared
  identifier like `stefan;` is flagged instead of silently passing.
- New `dino.compilerPath` setting to point at the compiler binary; defaults
  to `dino` on `PATH`, or `<workspaceFolder>/dino` when present and
  executable (so it works out of the box in the compiler's own repo).
- Validation uses the unsaved buffer (written to a temp file), so errors
  appear while typing, and never writes into the project.

## [0.1.5] - 2026-09-19

- Completions now include **user-defined functions**: the open document is
  indexed for top-level `func` declarations (comments stripped, cached per
  document version) and each function is suggested as
  `name(${1:param}, ...)` with tab stops per parameter.
- `string` is now offered as a type in completions.

## [0.1.4] - 2026-09-19

- New `func` keyword support in the grammar (`keyword.declaration.dino`)
  and a `func` declaration snippet in completions; `string` is now
  highlighted as a type.
- Fix console colors not applying:
  - `configurationDefaults` now lives under `contributes` (it was a
    top-level key and therefore ignored by VS Code/Codium).
  - `console.error` uses scopes that match *native theme rules* on both
    Dark+ (`invalid` -> red) and Vibe Black (`invalid.illegal` -> red),
    so error text is red without any settings.
  - `console.warn` uses a scope (`constant.character.escape`) that is
    yellow `#d7ba7d` in both Dark+ and Vibe Black by default; the shipped
    defaults brighten it to `#E5C07B` where settings apply.
- Colors still override via `editor.tokenColorCustomizations` (user
  settings always win).

## [0.1.3] - 2026-09-19

- `console.warn` now highlights **yellow** and `console.error` **red**
  (default theme colors; override via `editor.tokenColorCustomizations`).

## [0.1.2] - 2026-09-19

- Completion provider now suggests the `input` built-in
  (`input(prompt)` returns the user's answer as a string).
- Syntax highlighting recognizes the `input` built-in alongside `delay`.

## [0.1.1] - 2026-09-19

- Replace the standalone JSON snippets with a built-in completion provider:
  - typing `console` now suggests the `console` identifier itself
  - typing `console.` offers `log`, `warn`, `error`, `do` that insert
    correctly (no more `console.console.log` duplication)
  - statement completions with tab stops: `for`, `while`, `if`, `if else`,
    `switch`, `var`, `const`, `delay`
  - keywords, types and literals are suggested as well

## [0.1.0] - 2026-09-19

- Initial release: syntax highlighting, snippets, and language configuration
  for `*.dn` files.
- Recognizes Dino keywords, types, literals, interpolated strings, the
  `console.*` built-ins, and the `delay` built-in.