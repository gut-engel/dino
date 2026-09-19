# Dino Language Reference

Dino (`*.dn`) is a tiny, C-flavoured language that **transpiles to C** and is
compiled with a C compiler. What you write is essentially a structured way of
generating C: top-level `func` definitions become C functions, the rest of the
program maps to a single C `main()`, and the standard library is a handful of
built-ins (`console.*`, `delay`, `input`, `len`, `push`, `pop`, `has`, `keys`,
`values`).

Values are **dynamic**: every variable, parameter and element holds a tagged
`DinoValue` that can be `null`, a `bool`, an `int`, a `float`, a `string`, an
`array` or a `dict`. Type annotations are optional and descriptive — they are
not enforced at runtime.

This page is the complete reference. For CLI usage see
[commands.md](commands.md).

---

## 1. Lexical structure

### Comments

```dn
// line comment

/* block
   comment */
```

- Line comments run to the end of the line.
- Block comments run to the next `*/`. They do **not** nest, and an
  unterminated block comment swallows the rest of the file.

### Identifiers

Identifiers start with a letter or `_` and continue with letters, digits or
`_`. The following words are reserved and cannot be used as names:

```
const  var    if     else   for    while
switch case   default break  continue return
try    catch  throw  func   class  console
null   bool   int    float  void   true
false
```

`string`, `array` and `dict` are **not** reserved: they are the conventional
type names, but they also work as ordinary variable names (e.g.
`const string array = [1, 2, 3];`).

> `return` is reserved by the lexer but is **not** usable as a statement —
> writing `return ...;` is a syntax error. `break` / `continue` work inside
> `for` / `while` loops.

### Literals

| Literal            | Example                | Notes                              |
| ------------------ | ---------------------- | ---------------------------------- |
| Integer            | `42`                   | decimal only                       |
| Decimal            | `3.14`                 | `3.` / `.5` are not literals       |
| Boolean            | `true` / `false`       |                                    |
| Null               | `null`                  | the empty value                    |
| String             | `"hello\n"`            | C-style `\` escapes pass through   |
| Interpolated str   | `$"hi {name}!"`        | `{expr}` placeholders, see §4.5    |
| Array              | `[1, 2, 3]`            | mixed values allowed, see §4.3     |
| Dictionary         | `{"a": 1, "b": 2}`     | string keys, see §4.4              |

Numbers have no exponent or hex forms. An integer stays an `int` until it is
combined with a decimal, which produces a `float` (a C `double`).

## 2. Types

Dino values are dynamic, so a type is a hint rather than a promise. Any of
these names can be written where a type is expected:

| Dino type | Holds                                  |
| --------- | -------------------------------------- |
| `bool`    | `true` / `false`                        |
| `int`     | whole numbers                           |
| `float`   | decimal numbers                         |
| `string`  | text                                    |
| `array`   | an ordered, growable list of values     |
| `dict`    | string-keyed map of values              |
| `void`    | only used as a function return marker   |

`type[]` (for example `int[]`) is accepted as another spelling of an array.
An unknown identifier may also be used as a type name; it is accepted and
treated like any other value.

Because types are not enforced, a variable can change kind as the program
runs:

```dn
var x = 5;        // int
x = "five";       // now a string — legal in this dynamic language
```

### Type inference

`var` / `const` / function parameters do not need a type. When one is given,
it is recorded but does not change the generated code.

```dn
var a = 5;                          // int
var b = 5.5;                        // float
var flag = false;                   // bool
var name = "dino";                  // string
var greeting = $"hi {name}";        // string  (via _dino_fmt)
var answer = input("Name? ");       // string
var xs = [1, 2, 3];                 // array
var person = {"name": "Ada"};       // dict
```

## 3. Declarations

```
[ const | var ] [ type ]? name [ = initializer ] ;
```

```dn
const bool enabled = true;   // typed constant
var count = 0;               // inferred int
var int total = 10;          // explicit type on var
const string name = "dino";  // explicit string
var ready;                   // no initializer → null
var xs = [1, 2, 3];          // array
var int[] ys = [4, 5];       // array via the `[]` suffix
```

- The trailing `;` is required for declarations.
- A declaration without an initializer starts as `null`.
- `const` prevents re-assignment *of the variable* (`x = ...` is rejected by
  the C compiler); the contents of an array/dict it points to can still be
  mutated.

### Assignment

`=` is an assignment **statement** (and the lowest-precedence, right-associative
operator), so variables and container elements can be updated:

```dn
count = count + 1;
count++;                 // equivalent shorthand
xs[0] = 99;              // array element
person["age"] = 37;      // dictionary entry
a = b = 0;               // chained, right-associative
```

Only a variable or an index may be assigned to (`5 = x;` and `f() = x;` are
compile-time errors). There is no `+=` / `-=` yet.

### Functions

```dn
func name(type param, ...) {
    // statements
}
```

```dn
func greet(string name) {
    console.log($"Greetings to {name}!");
}

func add(a, b) {          // parameter types are optional
    console.log($"{a} + {b} = {a + b}");
}

greet("Simon");
add(3, 4);
```

- `func` declarations are **top-level only** — nesting one inside another
  block is a compile-time error.
- Parameters may be typed (`bool`, `int`, `float`, `string`, `array`, `dict`,
  or `type[]`) or left untyped. All parameters are dynamic values.
- Functions are `void` for now: they run statements but cannot return a value
  (`return` is not parsed by this language yet).
- Declaration order does not matter — functions are hoisted above `main()` in
  the generated C, so a function can call another defined later.

### Classes

A `class` is a named namespace of functions (*methods*) plus optional *fields*.
It is declared with `class`, optionally preceded by `const` / `var`:

```dn
const class counters {
    const label = "counter";      // a field

    func count(max) {             // a method
        for (var i = 0; i < max; i++) {
            console.log(i);
        };
    };

    func countBackwards(start) {
        for (var i = start; i > 0; i--) {
            console.log(i);
        };
    };
};

counters.count(3);                 // ClassName.method(args)
counters.countBackwards(2);
console.log(counters.label);       // ClassName.field
```

- Class declarations are **top-level only** (like `func`).
- Call methods as `ClassName.method(args)`; read fields as
  `ClassName.field`.
- A bare class name (without `.member`) is a compile-time error, as is an
  unknown member or reading a method as a value.
- Methods and fields are global: any method can call any other method or read
  any field, from any class, regardless of declaration order. Field
  initializers run once at the start of the program.
- Classes are *static namespaces* — there is no `new`, no instances and no
  `this` yet. Internally each method becomes a static C function named
  `Class_method` and each field a global value named `Class_field`.

## 4. Expressions

### 4.1 Operator precedence (lowest → highest)

| Precedence | Operators                     | Associativity |
| ---------- | ----------------------------- | ------------- |
| 1 (lowest) | `=` assignment                | right         |
| 2          | `\|\|`                        | left          |
| 3          | `&&`                          | left          |
| 4          | `==`  `!=`                    | left          |
| 5          | `<`  `<=`  `>`  `>=`          | left          |
| 6          | `+`  `-`                      | left          |
| 7          | `*`  `/`  `%`                 | left          |
| 8          | prefix `-x`  `!x`  `++x` `--x`| right         |
| 9 (highest)| postfix `x++` `x--`  `x[i]`  `.m` | left      |

```dn
var n = (1 + 2) * 3;   // 9 — grouping works
var m = -a + b;        // prefix minus
flag = !flag;          // assignment + logical not
```

> `&&` and `||` do **not** short-circuit: both sides are evaluated. This is
> safe for ordinary values (comparisons return `false` rather than failing on
> mixed types) but means side effects on the right still run.

### 4.2 Primary expressions

- literals (`42`, `3.14`, `true`, `false`, `null`, `"..."`, `$"..."`)
- array literals (`[1, 2, 3]`) and dictionary literals (`{"a": 1}`)
- identifiers (also `console`)
- `( expr )` grouping
- `x[i]` indexing and `obj.member` member access

### 4.3 Arrays

An array is written `[a, b, c]` (or `[]` for empty) and can hold mixed values:

```dn
var xs = [1, 2.5, "three", true, null];
console.log(xs[0]);        // 1
console.log(xs[-1]);       // null  (the last element)
console.log(xs.length);    // 5  (.len is an alias)
console.log(len(xs));      // 5

push(xs, "new");           // append (mutates the array)
console.log(pop(xs));      // "new" — remove and return the last element
xs[1] = 99;                // element assignment
```

- Negative indexes count from the end.
- Reading **or writing** past the end throws (`array index out of range`),
  which `try`/`catch` can handle. Indexing a string out of range throws
  `string index out of range` in the same way.
- Indexing an array with a non-numeric key searches for a matching value and
  returns its index (or `null`): `console.log(xs["three"]); // 2`.
- `push`, `pop` and element assignment work on the array in place.

### 4.4 Dictionaries

A dictionary maps keys to values. Keys are usually strings; a bare identifier
key is treated as a literal string, like a JavaScript object:

```dn
var person = {"name": "Ada", age: 36, "langs": ["C", "Dino"]};
console.log(person["name"]);   // Ada
console.log(person.age);       // 36 — `person.age` is `person["age"]`
person["age"] = 37;            // update an entry
person["city"] = "London";     // add an entry
```

Access entries with `d[key]`. A key that is not present yields `null` (use
`has(d, key)` to test). Indexing with an integer that is not itself a stored
key falls back to the *n*-th entry (0-based, negatives from the end), which
makes ordered traversal easy:

```dn
console.log(person[0]);        // value of the first entry
console.log(len(person));      // number of entries
console.log(has(person, "age"));   // true
console.log(keys(person));         // ["name", "age", "langs", "city"]
console.log(values(person));       // [ ... matching values ... ]
```

The following member forms are also available as conveniences:

| Member          | Meaning                                          |
| --------------- | ------------------------------------------------ |
| `.length` / `.len` | number of entries (or array elements)          |
| `.key` / `.keys`   | array of keys                                   |
| `.value` / `.values` | array of values                               |
| `.valueOfKey`      | the dictionary itself (so `d.valueOfKey[k]` looks up `k`) |
| `d.value(i)`       | the *i*-th value / value for key `i`            |

> The member names above are special; **any other** `x.name` is sugar for
> `x["name"]` (a string-key lookup, yielding `null` when absent), so
> `person.name` and `person["name"]` are the same.

### 4.5 Interpolated strings

`$"..."` mixes literal text with `{expr}` placeholders. Each placeholder is
converted to text with the runtime formatter, so every value type works (no
guesswork, no `%d`):

```dn
var i = 7;
console.log($"under 3: {i}\n");        // "under 3: 7"
var name = input("Name? ");
console.log($"Hello, {name}");         // strings
console.log($"sum: {1 + 2}");          // any expression
console.log($"pi ~ {3.14}");           // decimals print correctly
```

A literal `%` in the surrounding text is fine (it is never passed to
`printf`). Literal braces can appear inside a plain (non-interpolated) string.

### 4.6 Calls and members

```dn
console.log("hi");                 // member call
delay(0.5);                        // builtin call
input("Name? ");                   // builtin call, returns a string
greet("Simon");                    // user function
var c = some_c_function(1, 2, 3);  // general call → C function call
```

- Postfix `++` / `--` operate on a variable (`i++`); they mutate it in place
  and yield the new value. Prefix and postfix forms are equivalent.
- General calls emit a plain C call `name(arg, arg, ...)`. Any symbol
  available to the generated C can be invoked, but the arguments are
  `DinoValue`s, so this escape hatch is most useful for C functions that take
  no arguments or that you provide yourself in an include. Because of this
  escape hatch, **call targets are not validated** by the compiler. *Value*
  identifiers, by contrast, are validated — see §8.

## 5. Statements

Every statement **must** end with `;`. This includes expression statements,
`var`/`const` declarations, `break`/`continue`, and every block statement:
`if` / `else`, `for`, `while`, `switch`, each `case`/`default` body, `try` /
`catch`, and standalone blocks are all terminated by `;` after their closing
`}`:

```dn
if (cond) {
    // ...
};                              // the ';' is required

while (cond) {
    // ...
};

for (var i = 0; i < 3; i++) {
    // ...
};
```

`func` declarations are the exception — they do not take a trailing `;` (a
stray one is tolerated). Forgetting a `;` is a syntax error, e.g.
`[line 3, col 1] Error at end: Expect ';' after expression.`

### Expression statement

```dn
some_call();
i++;
x = 3;
xs[0] = 9;
```

### Blocks

```dn
{
    console.log("inside");
};
```

A block is a statement (and therefore ends with `;`); the bodies of `if` /
`for` / `while` / `case` **must** be a `{ ... }` block — a single statement
without braces is a syntax error.

### `if` / `else`

```dn
if (cond) {
    // ...
} else if (other) {
    // ...
} else {
    // ...
};
```

### `while`

```dn
while (cond) {
    // ...
};
```

### `for`

All three clauses are optional (`for (;;)` is an infinite loop). The init may
be a `var`/`const` declaration (consumes its own `;`) or an expression:

```dn
for (var i = 0; i < 10; i++) {
    // ...
};

for (var int i = 0; i < 10; i++) { /* explicit type */ };

for (i++; i < 10; i++) { /* expression init */ };
```

### `switch` / `case` / `default`

`switch` cases are **runtime conditions** rather than constant labels — the
whole statement transpiles to an `if`/`else` chain:

```dn
switch (i) {
    case (i < 3) {
        console.log($"under 3: {i}");
    };
    case (i > 3) {
        console.log($"over 3: {i}");
    };
    default {
        console.log("exactly 3");
    };
};
```

- `case` always takes `( condition )`.
- Cases are tested in order; `break` inside a case exits the enclosing loop.

### `break` / `continue`

```dn
for (var i = 0; i < 10; i++) {
    if (i == 3) {
        break;      // exit the loop
    };
    if (i % 2 == 0) {
        continue;   // skip the rest of the iteration
    };
    console.log(i);
};
```

They pass through to the generated C, so they behave exactly like C's
`break` / `continue` (only meaningful inside loops).

### `try` / `catch` / `throw`

`throw` raises a value; `try`/`catch` handles anything thrown while its block
runs, including runtime errors raised by the built-ins:

```dn
try {
    console.log("before");
    throw "something went wrong";
    console.log("never reached");
} catch (err) {
    console.log($"caught: {err}");
};
```

- `catch (name)` binds the caught value. You can throw any value (string,
  number, array, …).
- A nested `try` re-throws to the next enclosing handler, so failures can
  bubble up:
  ```dn
  try {
      try {
          throw "inner";
      } catch (e) {
          throw "wrapped: " + e;   // escapes to the outer catch
      };
  } catch (e2) {
      console.log(e2);             // "wrapped: inner"
  };
  ```
- Runtime errors that can be caught include division by zero
  (`division by zero`), modulo by zero, out-of-range array reads **and** writes,
  and out-of-range string reads.
- Uncaught values abort the program with `Uncaught error: <value>` on stderr
  and exit code `1`.
- A `;` between the `try` block and `catch` is allowed and ignored:
  `try { ... }; catch (e) { ... };` is accepted.

## 6. Built-ins

### `console.log`, `console.warn`, `console.error`

Print their arguments separated by a single space, followed by a newline, to
stdout (`log`) or stderr (`warn`, `error`). With no arguments they print a
blank line. Every value is formatted with the runtime, so integers, decimals,
strings, booleans, `null`, arrays and dictionaries all print correctly:

```dn
console.log();                 // blank line
console.log("hello");          // "hello\n"
console.warn("careful");
console.error("boom");
console.log("a", 1, 2.5, true, [1, 2], {"k": 3}, null);
// a 1 2.5 true [1, 2] {k: 3} null
```

When the program runs in a terminal, `console.warn` output is printed
**yellow** and `console.error` **red** (ANSI escape codes); `console.log`
keeps the terminal's default colour. The codes are suppressed when output is
redirected/piped, or when the `NO_COLOR` environment variable is set.

### `console.do`

Runs a command with `system()`; expects exactly one argument (usually an
interpolated string):

```dn
console.do($"echo {boolean}");   // runs  echo true  via the shell
console.do("ls -la");
```

With no arguments it compiles to a no-op.

### `delay(seconds)`

Sleeps for a whole or fractional number of seconds (via `nanosleep`), and
takes exactly one argument:

```dn
delay(1);     // one second
delay(0.25);  // quarter second
```

Any other argument count is a compile-time error:
`delay() expects exactly 1 argument (seconds)`.

### `input(prompt)`

Prints the prompt, reads one line from stdin, and returns it as a string
(leading/trailing newline stripped):

```dn
var name = input("What is your name? ");
console.log($"Hello, {name}");
```

- Exactly one argument is required:
  `input() expects exactly 1 argument (the prompt)`.
- The returned string is owned by the returned value, so several `input()`
  calls can be stored and used independently.

### Container helpers

| Call             | Result                                                        |
| ---------------- | ------------------------------------------------------------- |
| `len(x)`         | length of a string, array or dict (0 for anything else)       |
| `push(arr, v)`   | append `v` to `arr`, returns `arr`                            |
| `pop(arr)`       | remove and return the last element (or `null`)                 |
| `has(c, key)`    | for a dict: whether `key` is present; for an array: whether a matching value exists |
| `keys(d)`        | array of a dict's keys                                        |
| `values(d)`      | array of a dict's values                                      |

## 7. Runtime behaviour and operators

### Truthiness

`if` / `while` / `for` conditions and `!` use these rules: `null` is false,
`false` is false, `0` and `0.0` are false, `""` is false, and every other
value (including empty arrays/dicts) is true.

### Arithmetic

- `+` adds numbers. If **either** operand is a string, it concatenates the
  text forms instead: `"a" + 1` → `"a1"`.
- `-`, `*`, `/` subtract/multiply/divide; the result is an integer when both
  operands are integer-like, otherwise a float.
- `%` is integer modulo.
- Dividing or taking modulo by zero throws (`division by zero` /
  `modulo by zero`).

### Comparison and equality

- `<`, `<=`, `>`, `>=` compare numbers numerically, and strings
  lexicographically.
- `==` / `!=` compare numbers by value, strings by content, and arrays/dicts
  by identity (same underlying container). `null == null` is true.

## 8. Compile-time errors

- **Lexer / parser**: reported as
  `[line N, col N] Error at 'tok': message` with the error count, e.g.
  `3 syntax error(s) in 'file.dn'.`
- **Semantic / codegen**: reported as
  `Codegen error (line N, col N): message`. This covers:
  - unknown `console.*` method, or a wrong argument count for a built-in;
  - an **unknown identifier** used as a value — e.g. a bare `stefan;` where no
    `var`/`const`/parameter named `stefan` is in scope
    (`Unknown identifier 'stefan' (declare it with var/const first)`);
  - an invalid assignment target (not a variable or an index);
  - `++`/`--` applied to something that is not a variable;
  - class misuse: a bare class name (without `.member`), an unknown class
    member, reading a method as a value, or calling a field as a method;
  - calling the result of a call (a stray `()`, e.g. `console.error()("x")`).
- **Scope rules** mirror the generated C: a variable is visible from its
  declaration to the end of the enclosing `{ ... }` block, and `for`-init
  variables are visible for the whole `for` statement only, so using `i` after
  the loop is reported. Functions cannot see top-level `main` variables.
- A C compile failure is reported with the failing compiler's output; the
  generated C is still left at `CCode/<file>.c` for inspection.

## 9. Generated C

The output C starts with a small runtime preamble implementing the dynamic
value model:

- `DinoValue` — the tagged union holding `null` / bool / int / float / string /
  array / dict, with constructors (`_dino_int`, `_dino_str`, …).
- `_dino_array_*` / `_dino_dict_*` — growable array and dictionary support,
  plus `_dino_get` / `_dino_set` / `_dino_len` / `_dino_keys` / `_dino_values`.
- `_dino_add`, `_dino_sub`, `_dino_eq`, `_dino_truthy`, … — the operator
  helpers used by generated expressions.
- `_dino_fmt(fmt, ...)` — formatting used by interpolated strings.
- `_dino_delay(seconds)` — `nanosleep` helper.
- `_dino_input(prompt)` — prompt + `getline` helper returning a value.
- `_dino_printv` / `_dino_eprintv` — value printing for
  `console.log` / `warn` / `error` (ANSI colour when the stream is a terminal,
  disabled by `NO_COLOR` or redirection).
- `_dino_throw` + a `setjmp`/`longjmp` frame stack — `throw` / `try` / `catch`.

The runtime is generated from
[`src/codegen/runtime_template.c`](../src/codegen/runtime_template.c) by
[`src/codegen/gen_runtime.py`](../src/codegen/gen_runtime.py) into
`src/codegen/runtime.c`.

`func` declarations are emitted ahead of `main()` (hoisted), so calls from
`main` (or other functions) resolve even when the definition comes later in
the file. A `class` lowers to a `static void Class_method(...)` per method and
a global `DinoValue Class_field` per field; `Class.member` becomes the mangled
symbol. Prototypes for every function and method are emitted before the
definitions, so declaration order never matters. Programs are compiled with
`gcc` (no extra flags).
