'use strict';

const vscode = require('vscode');
const childProcess = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const KEYWORDS = ['const', 'var', 'if', 'else', 'for', 'while', 'switch', 'case', 'default', 'break', 'continue', 'return', 'try', 'catch', 'throw', 'delete', 'class'];
const TYPES = ['bool', 'int', 'float', 'void', 'string', 'array', 'dict'];
const LITERALS = ['true', 'false', 'null'];
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
    insertText: 'for (var ${1:i} = 0; ${1:i} < ${2:10}; ${1:i}++) {\n\t$0\n};',
  },
  {
    label: 'while',
    detail: 'while loop',
    insertText: 'while (${1:condition}) {\n\t$0\n};',
  },
  {
    label: 'if',
    detail: 'if statement',
    insertText: 'if (${1:condition}) {\n\t$0\n};',
  },
  {
    label: 'if else',
    detail: 'if / else statement',
    insertText: 'if (${1:condition}) {\n\t$2\n} else {\n\t$0\n};',
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
  {
    label: 'class',
    detail: 'class declaration (a namespace of methods and fields)',
    insertText: 'class ${1:Name} {\n\tfunc ${2:method}(${3:type} ${4:param}) {\n\t\t$0\n\t};\n};',
  },
  {
    label: 'try',
    detail: 'try / catch statement',
    insertText: 'try {\n\t$1\n} catch (${2:err}) {\n\t$0\n};',
  },
  {
    label: 'throw',
    detail: 'throw a value (caught by the nearest try/catch)',
    insertText: 'throw ${1:"message"};',
  },
  {
    label: 'array',
    detail: 'array literal',
    insertText: 'var ${1:name} = [${2:value}];',
  },
  {
    label: 'dict',
    detail: 'dictionary literal',
    insertText: 'var ${1:name} = {"${2:key}": ${3:value}};',
  },
  {
    label: 'len',
    detail: 'len(x) - length of a string, array or dictionary',
    insertText: 'len(${1:x})',
  },
  {
    label: 'pop',
    detail: 'pop(array) - remove and return the last element',
    insertText: 'pop(${1:array})',
  },
  {
    label: 'has',
    detail: 'has(dict, key) - true when the dictionary contains the key',
    insertText: 'has(${1:dict}, ${2:key})',
  },
  {
    label: 'keys',
    detail: 'keys(dict) - array of the dictionary\u2019s keys',
    insertText: 'keys(${1:dict})',
  },
  {
    label: 'values',
    detail: 'values(dict) - array of the dictionary\u2019s values',
    insertText: 'values(${1:dict})',
  },
];

// ── Symbol index ─────────────────────────────────────────────────────────────
// Scans the open document for `func name(params)` declarations and for
// `var`/`const` variables so that user-defined functions and variables both
// show up in completions. Results are cached per document version and rebuilt
// only when the buffer changes.

const MAX_CACHED_DOCS = 64;
const symbolCache = new Map(); // document uri -> { version, funcs, vars }

// Remove comments before indexing so commented-out declarations are not
// suggested. (Heuristic: a `//` inside a string literal would also be
// stripped, which is acceptable for a completion index.)
function stripComments(text) {
  return text
    .replace(/\/\*[\s\S]*?\*\//g, ' ')
    .replace(/\/\/[^\n]*/g, ' ');
}

// Names we don't want to spend a completion slot on (keywords, built-in types,
// literals and the statement snippets are already offered elsewhere).
function isReservedName(name) {
  return (
    KEYWORDS.includes(name) ||
    TYPES.includes(name) ||
    LITERALS.includes(name) ||
    STATEMENT_SNIPPETS.some((s) => s.label === name)
  );
}

function indexFunctions(text) {
  const funcs = [];
  // `func` must be the first token on its line (top-level style).
  const re = /^\s*func\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/gm;
  let m;
  while ((m = re.exec(text)) !== null) {
    const name = m[1];
    if (isReservedName(name)) continue;
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

// Matches `[const|var] [type]? name [= value];` declarations; the variable name
// is the last identifier before `=` or `;`. Class declarations
// (`const class X {`) have no `=`/`;` after the name, so they are skipped.
// Variables may shadow built-in type names (`array`, `string`, `dict`) and
// snippet labels, so only true keywords/literals are excluded.
function indexVariables(text) {
  const vars = [];
  const seen = new Set();
  const re = /^\s*(const|var)\s+(?:(?:[A-Za-z_][A-Za-z0-9_]*)(?:\[\])?\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*(?:=|;)/gm;
  let m;
  while ((m = re.exec(text)) !== null) {
    const name = m[2];
    if (seen.has(name) || KEYWORDS.includes(name) || LITERALS.includes(name)) continue;
    seen.add(name);
    vars.push({ name, detail: `${m[1]} ${name}` });
  }
  return vars;
}

function getDocumentIndex(document) {
  const key = document.uri.toString();
  const cached = symbolCache.get(key);
  if (cached && cached.version === document.version) return cached;
  const text = stripComments(document.getText());
  const entry = {
    version: document.version,
    funcs: indexFunctions(text),
    vars: indexVariables(text),
  };
  symbolCache.set(key, entry);
  if (symbolCache.size > MAX_CACHED_DOCS) {
    symbolCache.delete(symbolCache.keys().next().value); // drop the oldest document
  }
  return entry;
}

// ── Diagnostics (real validation) ────────────────────────────────────────────
// Runs the `dino` compiler in --check mode on the current document (debounced)
// and publishes a red squiggle + message for every error, using the editor's
// Diagnostics API. The compiler is the source of truth: syntax errors from the
// parser and semantic errors from codegen both print one
//   <anything> (line N, col N): message
// line per problem, which we parse below. --check never writes files and does
// not invoke gcc, so validation is cheap and side-effect free.

const diagnostics = vscode.languages.createDiagnosticCollection('dino');
const VALIDATE_DEBOUNCE_MS = 400;
let validateTimer = null;
let compilerMissingWarned = false;

// Where to find the `dino` binary, in order of preference:
//   1. an explicitly-set "dino.compilerPath" (absolute path, or 'dino' for PATH)
//   2. <workspaceFolder>/dino, or <documentFolder>/dino
//      (e.g. while developing the compiler itself, or when opening a single file)
//   3. 'dino' on PATH
function findCompiler(document) {
  const cfg = vscode.workspace.getConfiguration('dino', document.uri);
  const info = cfg.inspect('compilerPath');
  const explicit =
    info && (info.workspaceFolderValue || info.workspaceValue || info.globalValue);
  if (explicit) return explicit;

  const candidates = [];
  const ws = vscode.workspace.getWorkspaceFolder(document.uri);
  if (ws) candidates.push(path.join(ws.uri.fsPath, 'dino'));
  candidates.push(path.join(path.dirname(document.fileName), 'dino'));
  for (const c of candidates) {
    try {
      fs.accessSync(c, fs.constants.X_OK);
      return c;
    } catch (e) {
      // not present or not executable — try the next candidate
    }
  }
  return 'dino';
}

// Turn a 1-based line/col from the compiler into a squiggle range, extending
// over the offending word (or the rest of the line) so the underline shows
// exactly what the compiler is complaining about.
function rangeForError(document, line, col) {
  const lineText = document.lineAt(line - 1).text;
  let start = Math.min(Math.max(col - 1, 0), lineText.length);
  let end = start;
  while (end < lineText.length && /[A-Za-z0-9_]/.test(lineText[end])) end++;
  if (end === start) {
    // Not on a word: underline one character (or the last one on the line).
    if (start > 0) {
      start -= 1;
      end = start + 1;
    } else if (lineText.length > 0) {
      end = 1;
    }
  }
  return new vscode.Range(line - 1, start, line - 1, end);
}

function parseDiagnostics(document, stderr) {
  const diags = [];
  const re = /line (\d+), col (\d+)[^\n]*:\s*(.*)$/gm;
  let m;
  while ((m = re.exec(stderr)) !== null) {
    let line = parseInt(m[1], 10);
    const col = parseInt(m[2], 10);
    if (line < 1) continue;
    if (line > document.lineCount) line = document.lineCount;
    // Errors "at end" land on the empty line after a trailing newline.
    // Move back to the last line with content so the squiggle is visible.
    while (line > 1 && document.lineAt(line - 1).text.length === 0) line--;
    const diag = new vscode.Diagnostic(
      rangeForError(document, line, col),
      m[3].trim(),
      vscode.DiagnosticSeverity.Error
    );
    diag.source = 'dino';
    diags.push(diag);
  }
  return diags;
}

function validateDocument(document) {
  if (document.languageId !== 'dino') return;
  const compiler = findCompiler(document);

  // Validate the LIVE buffer, not the file on disk: write the current text to
  // a throwaway .dn file (--check never writes anything itself) so unsaved
  // edits produce diagnostics immediately.
  const tmp = path.join(os.tmpdir(), `dino-check-${process.pid}-${Date.now()}.dn`);
  try {
    fs.writeFileSync(tmp, document.getText());
  } catch (e) {
    return; // cannot create the temp file; skip this round
  }

  childProcess.execFile(
    compiler,
    [tmp, '--check'],
    { timeout: 10000 },
    (err, stdout, stderr) => {
      try {
        fs.unlinkSync(tmp);
      } catch (e) {
        // best effort cleanup
      }
      if (err && err.code === 'ENOENT') {
        diagnostics.set(document.uri, []);
        if (!compilerMissingWarned) {
          compilerMissingWarned = true;
          vscode.window.showWarningMessage(
            `Dino compiler not found ('${compiler}'). Set "dino.compilerPath" in settings to enable validation.`
          );
        }
        return;
      }
      diagnostics.set(document.uri, parseDiagnostics(document, String(stderr || '')));
    }
  );
}

function scheduleValidation(document) {
  clearTimeout(validateTimer);
  validateTimer = setTimeout(() => validateDocument(document), VALIDATE_DEBOUNCE_MS);
}

function activate(context) {
  context.subscriptions.push(diagnostics);

  // Validate on edits (debounced) and when documents open/close.
  context.subscriptions.push(
    vscode.workspace.onDidChangeTextDocument((e) => {
      if (e.document.languageId === 'dino') scheduleValidation(e.document);
    }),
    vscode.workspace.onDidOpenTextDocument((d) => {
      if (d.languageId === 'dino') scheduleValidation(d);
    }),
    vscode.workspace.onDidCloseTextDocument((d) => {
      diagnostics.delete(d.uri);
    })
  );

  // Validate anything already open when the extension activates.
  for (const doc of vscode.workspace.textDocuments) {
    if (doc.languageId === 'dino') scheduleValidation(doc);
  }

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

        // User-defined functions and variables from the open document.
        // Functions get a snippet that fills in the parameter names.
        const index = getDocumentIndex(document);
        for (const fn of index.funcs) {
          const item = new vscode.CompletionItem(fn.name, vscode.CompletionItemKind.Function);
          item.range = range;
          item.detail = fn.rawParams
            ? `func ${fn.name}(${fn.rawParams})`
            : `func ${fn.name}()`;
          const args = fn.params.map((p, i) => `\${${i + 1}:${p}}`).join(', ');
          item.insertText = new vscode.SnippetString(fn.name + '(' + args + ')');
          list.push(item);
        }
        for (const v of index.vars) {
          const item = new vscode.CompletionItem(v.name, vscode.CompletionItemKind.Variable);
          item.range = range;
          item.detail = v.detail;
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