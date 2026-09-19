# Change Log

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