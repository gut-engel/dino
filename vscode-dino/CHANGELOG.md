# Change Log

## [0.1.11] - 2026-09-19

- **`class` declarations**: a `class` is a static namespace of methods and
  optional fields, declared top-level as `[const|var] class Name { ... };`.
  Call a method with `ClassName.method(args)` and read a field with
  `ClassName.field`. Any method can call any other method or read any field,
  regardless of declaration order; a bare class name, an unknown member, or
  using a method as a value is a compile-time error.
- Syntax highlighting, completions and snippets updated for `class`; the
  language reference documents the feature.

## [0.1.10] - 2026-09-19

- **`*.dn` file icon**: every `*.dn` file now shows the dino
  `icons/thumbnail.svg` as its file icon in the Explorer, editor tabs and
  breadcrumbs.
  - Contributed as the language's default icon, so it appears whenever the
    active file icon theme supports language icons (e.g. the built-in Seti
    theme).
  - A bundled **"Dino Icons" file icon theme** (`iconThemes` →
    `fileicons/dino-icon-theme.json`) maps `.dn` to the dino icon and provides
    dino-styled generic file/folder icons for everything else. Select it via
    **File ▸ Preferences ▸ File Icon Theme ▸ Dino Icons** (or set
    `"workbench.iconTheme": "dino-icons"`) to get the dino icon under any
    color theme, including Minimal.

## [0.1.9] - 2026-09-19

- **Dynamic values**: every variable and parameter is now a tagged value that
  can hold `null`, a `bool`, an `int`, a `float`, a `string`, an `array` or a
  `dict`. Type annotations are optional and advisory — `var`, `const`,
  `func` parameters, and `type[]` are all still accepted, and `string`,
  `array` and `dict` also work as ordinary variable names (e.g.
  `const string array = [...]`).
- **Arrays and dictionaries**: literals (`[1, 2, 3]`,
  `{"name": "Ada", age: 36}`), indexing (`xs[0]`, `person["name"]`), element
  assignment (`xs[0] = 9`, `person["age"] = 37`), negative indexes, and the
  helpers `len`, `push`, `pop`, `has`, `keys`, `values`. Members `.length` /
  `.len`, `.key(s)`, `.value(s)` and `.valueOfKey` are also recognised.
  Reading an array or string index that is out of range **throws**
  (`array index out of range` / `string index out of range`), so it can be
  caught; a missing dictionary key or a failed value search yields `null`.
- **Assignment**: `=` is now a statement (right-associative, so `a = b = 0;`
  works), alongside `++` / `--`.
- **`try` / `catch` / `throw`**: throw any value; nested handlers re-throw to
  the next enclosing `catch`; runtime errors (division by zero, modulo by
  zero, out-of-range array reads **and** writes, out-of-range string reads)
  are catchable; uncaught values print `Uncaught error: …` and exit `1`. A
  `;` between the `try` block and `catch` is tolerated.
- The empty value is spelled **`null`** (there is no `nil`).
- `console.log` / `warn` / `error` now take any number of values, separated by
  a space, and format every value type correctly (including decimals, arrays
  and dictionaries). Uncaught errors are printed in red on a terminal.
- Syntax highlighting, completions and snippets updated for the new keywords
  (`try`, `catch`, `throw`, `null`), types, built-ins and container literals.
- The language reference (`docs/syntax.md`) has been rewritten for the dynamic
  value model.

## [0.1.8] - 2026-09-19

- **Semicolons are now required** after statements, matching the compiler's
  stricter grammar: expression statements, `var`/`const`, `break`/`continue`,
  and every block statement (`if`/`else`, `for`, `while`, `switch`, and each
  `case`/`default` body) must end with `;`. `func` declarations are exempt.
  The built-in `if` / `if else` / `for` / `while` snippets now insert the
  trailing `;`.
- Missing-semicolon diagnostics that point at the end of a file are now placed
  at the end of the last line with content, so the red squiggle is visible
  instead of landing on the trailing blank line.

## [0.1.7] - 2026-09-19

- The extension now has an **icon**: the green dino + `dino.dn` wordmark from
  `icons/extension.svg`, rendered to `icons/icon.png` (256×256, white
  background). Shown in the Extensions view.

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