'use strict';

const vscode = require('vscode');

const KEYWORDS = ['const', 'var', 'if', 'else', 'for', 'while', 'switch', 'case', 'default', 'break', 'continue', 'return'];
const TYPES = ['bool', 'int', 'float', 'void', 'string'];
const LITERALS = ['true', 'false'];
const CONSOLE_METHODS = ['log', 'warn', 'error', 'do'];
const METHOD_DOCS = {
  log: 'Print to stdout.',
  warn: 'Print to stderr.',
  error: 'Print to stderr.',
  do: 'Run a shell command via system().',
};

/**
 * Statement-style completions: inserted as snippets with tab stops.
 * label is also used for fuzzy matching, so it mirrors what the user types.
 */
const STATEMENT_SNIPPETS = [
  {
    label: 'for',
    detail: 'for loop',
    insertText: 'for (var ${1:i} = 0; ${1:i} < ${2:10}; ${1:i}++) {\n\t$0\n}',
  },
  {
    label: 'while',
    detail: 'while loop',
    insertText: 'while (${1:condition}) {\n\t$0\n}',
  },
  {
    label: 'if',
    detail: 'if statement',
    insertText: 'if (${1:condition}) {\n\t$0\n}',
  },
  {
    label: 'if else',
    detail: 'if / else statement',
    insertText: 'if (${1:condition}) {\n\t$2\n} else {\n\t$0\n}',
  },
  {
    label: 'switch',
    detail: 'switch statement (cases are runtime conditions)',
    insertText: 'switch (${1:value}) {\n\tcase (${2:condition}) {\n\t\t$3\n\t};\n\tdefault {\n\t\t$0\n\t};\n};',
  },
  {
    label: 'var',
    detail: 'inferred variable declaration',
    insertText: 'var ${1:name} = ${2:value};',
  },
  {
    label: 'const',
    detail: 'typed constant declaration',
    insertText: 'const ${1:type} ${2:name} = ${3:value};',
  },
  {
    label: 'delay',
    detail: 'delay(seconds) - sleep for a whole or fractional number of seconds',
    insertText: 'delay(${1:seconds});',
  },
  {
    label: 'input',
    detail: 'input(prompt) - print a prompt and return the user\u2019s answer as a string',
    insertText: 'input(${1:prompt})',
  },
  {
    label: 'func',
    detail: 'function declaration (top-level only, returns nothing)',
    insertText: 'func ${1:name}(${2:type} ${3:param}) {\n\t$0\n}',
  },
];

// ── Function index ───────────────────────────────────────────────────────────
// Scans the open document for `func name(params)` declarations so that
// user-defined functions show up in completions, with a snippet for their
// parameters. Results are cached per document version and rebuilt only when
// the buffer changes.

const MAX_CACHED_DOCS = 64;
const funcCache = new Map(); // document uri -> { version, funcs }

// Remove comments before indexing so commented-out functions are not
// suggested. (Heuristic: a `//` inside a string literal would also be
// stripped, which is acceptable for a completion index.)
function stripComments(text) {
  return text
    .replace(/\/\*[\s\S]*?\*\//g, ' ')
    .replace(/\/\/[^\n]*/g, ' ');
}

function indexFunctions(document) {
  const text = stripComments(document.getText());
  const funcs = [];
  // `func` must be the first token on its line (top-level style).
  const re = /^\s*func\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/gm;
  let m;
  while ((m = re.exec(text)) !== null) {
    const name = m[1];
    // Don't shadow keywords, types or the built-in statement snippets.
    if (
      KEYWORDS.includes(name) ||
      TYPES.includes(name) ||
      LITERALS.includes(name) ||
      STATEMENT_SNIPPETS.some((s) => s.label === name)
    ) {
      continue;
    }
    const rawParams = m[2].trim();
    const params = [];
    if (rawParams) {
      for (const part of rawParams.split(',')) {
        const pm = /^\s*(?:[A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*$/.exec(part);
        if (pm) params.push(pm[1]);
      }
    }
    funcs.push({ name, rawParams, params });
  }
  return funcs;
}

function getFunctionIndex(document) {
  const key = document.uri.toString();
  const cached = funcCache.get(key);
  if (cached && cached.version === document.version) return cached.funcs;
  const funcs = indexFunctions(document);
  funcCache.set(key, { version: document.version, funcs });
  if (funcCache.size > MAX_CACHED_DOCS) {
    funcCache.delete(funcCache.keys().next().value); // drop the oldest document
  }
  return funcs;
}

function activate(context) {
  const provider = vscode.languages.registerCompletionItemProvider(
    'dino',
    {
      provideCompletionItems(document, position) {
        const linePrefix = document.lineAt(position).text.slice(0, position.character);
        const list = [];

        // Inside "console." -> only member methods, replacing the member word.
        const memberMatch = /console\.([A-Za-z_]*)$/.exec(linePrefix);
        if (memberMatch) {
          const typed = memberMatch[1];
          const range = new vscode.Range(
            position.line,
            position.character - typed.length,
            position.line,
            position.character
          );
          for (let i = 0; i < CONSOLE_METHODS.length; i++) {
            const m = CONSOLE_METHODS[i];
            const item = new vscode.CompletionItem(m, vscode.CompletionItemKind.Method);
            item.range = range;
            item.documentation = METHOD_DOCS[m];
            item.insertText = new vscode.SnippetString(m + '($0)');
            item.sortText = String(i); // keep log/warn/error/do in a stable order
            if (i === 0) item.preselect = true; // Tab accepts 'log' right after 'console.'
            list.push(item);
          }
          return list;
        }

        // General context: replace the word under the cursor.
        const wordStart = linePrefix.search(/[A-Za-z_][A-Za-z0-9_]*$/);
        const range = new vscode.Range(
          position.line,
          wordStart >= 0 ? wordStart : position.character,
          position.line,
          position.character
        );

        for (const kw of KEYWORDS) {
          const item = new vscode.CompletionItem(kw, vscode.CompletionItemKind.Keyword);
          item.range = range;
          list.push(item);
        }
        for (const t of TYPES) {
          const item = new vscode.CompletionItem(t, vscode.CompletionItemKind.TypeParameter);
          item.range = range;
          list.push(item);
        }
        for (const lit of LITERALS) {
          const item = new vscode.CompletionItem(lit, vscode.CompletionItemKind.Value);
          item.range = range;
          list.push(item);
        }

        const consoleItem = new vscode.CompletionItem('console', vscode.CompletionItemKind.Class);
        consoleItem.range = range;
        consoleItem.documentation = 'Root object of the console.* built-ins (log, warn, error, do).';
        list.push(consoleItem);

        // User-defined functions from the open document, with a snippet that
        // fills in the parameter names.
        for (const fn of getFunctionIndex(document)) {
          const item = new vscode.CompletionItem(fn.name, vscode.CompletionItemKind.Function);
          item.range = range;
          item.detail = fn.rawParams
            ? `func ${fn.name}(${fn.rawParams})`
            : `func ${fn.name}()`;
          const args = fn.params.map((p, i) => `\${${i + 1}:${p}}`).join(', ');
          item.insertText = new vscode.SnippetString(fn.name + '(' + args + ')');
          list.push(item);
        }

        for (const st of STATEMENT_SNIPPETS) {
          const item = new vscode.CompletionItem(st.label, vscode.CompletionItemKind.Snippet);
          item.range = range;
          item.detail = st.detail;
          item.insertText = new vscode.SnippetString(st.insertText);
          list.push(item);
        }

        return list;
      },
    },
    '.' // trigger completion immediately when a dot is typed (console.)
  );

  context.subscriptions.push(provider);
}

function deactivate() {}

module.exports = { activate, deactivate };