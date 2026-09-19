'use strict';

const vscode = require('vscode');

const KEYWORDS = ['const', 'var', 'if', 'else', 'for', 'while', 'switch', 'case', 'default', 'break', 'continue', 'return'];
const TYPES = ['bool', 'int', 'float', 'void'];
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
];

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