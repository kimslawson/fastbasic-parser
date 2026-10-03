# fbp - The missing parser for FastBasic

`fbp` is a parsing, minimizing and pretty-printing tool for
[FastBasic](https://github.com/dmsc/fastbasic) programs, made to ease
sizecoding (like the BASIC 10-liner contest) and to make obfuscating and
de-obfuscating FastBasic programs easy.

It is the spiritual successor of
[tbxl-parser](https://github.com/dmsc/tbxl-parser), the Turbo-Basic XL and
Atari BASIC parser tool, adapted to the FastBasic language.

**Credits:** FastBasic and tbxl-parser are both by Daniel Serpell
([dmsc](https://github.com/dmsc)). `fbp` uses the FastBasic grammar files,
syntax table loader and peephole optimizer unmodified, and its parsing engine
is a port of the FastBasic compiler parser. Thanks dmsc for both tools!

`fbp` produces two kinds of output:

- **A minimized listing** (`-s`, the default): keywords abbreviated,
  variables and procedures renamed to one letter, comments removed, the
  minimum number of spaces and as many statements per line as fit in the
  line length. Uses Atari end of lines, ready to be loaded in the FastBasic
  IDE. With `-S` all optimizations that make the listing shorter are also
  applied.

- **A readable listing** (`-l`): one statement per line, indentation, all
  keywords expanded, spaces around operators, parenthesis around function
  arguments and the variable name added to `NEXT`. Comments and blank lines
  from the source are kept. Use it to de-obfuscate programs, including your
  own minimized ones.

**Every output is verified**: each written statement is parsed again with the
FastBasic grammar and checked to compile to exactly the same bytecode as the
original, and after writing, the complete output is compiled and compared
with the original program. If something would change the program, `fbp`
writes it in a safer form instead.


## Example

The sample program `samples/stars.bas` (61 lines, 1254 bytes of readable
source) becomes 5 lines of 80 columns (385 bytes), with
`fbp -S -n 80 samples/stars.bas`:

```
G.0:P.752,1:SE.2,0,0:DI.D(4),C(4):F=0:E=3:B=20:F.A=0T.4:D(A)=R.38+1:C(A)=R.10:N.
PR.A:POS.B-1,22:?" ^ ";:ENDP.:R.:G=S.0:I.G=11A.B>1T.DE.B:I.G=7A.B<38T.INCB:@A
F.A=0T.4:POS.D(A),C(A):?" ";:INCC(A):I.C(A)=22:I.A.(D(A)-B)<=1:F=F+10
S.0,100,10,8:EL.:DE.E:S.0,200,12,8:E.:D(A)=R.38+1:C(A)=0:E.:POS.D(A),C(A):?"*";
N.:POS.0,0:?"Score: "F"  Lives: "E" ";:PA.4:S.:U.E=0:POS.15,10:?"GAME OVER"
```

And `fbp -l` on that minimized listing gives back a readable program:

```
repeat
  G = stick(0)
  if G = 11 and B > 1 then dec B
  if G = 7 and B < 38 then inc B
  exec A
  for A = 0 to 4
    position D(A), C(A)
    print " ";
    inc C(A)
    if C(A) = 22
      if abs(D(A) - B) <= 1
        F = F + 10
  ...
```


## Results

Measured on the FastBasic samples and on two finished PUR-120 ten-liners,
with FastBasic 4.7. The numbers are the minimized program's length with each
line break counted as one character (the program as one line), so line
breaking doesn't affect them. Every `fbp -O` output compiles to exactly the
same XEX as the original with the FastBasic 4.7 cross-compiler.

| Program | FastBasic `-l:min` | `fbp -O` | Saved | `fbp -O -f` (names kept) |
|---|---|---|---|---|
| PUR-120 ten-liner A (hand-golfed) | 1195 | 1170 | 25 (2%) | 1177 |
| PUR-120 ten-liner B (hand-golfed) | 1154 | 1121 | 33 (3%) | 1134 |
| FastBasic sample `ahlbench.bas` | 211 | 169 | 42 (20%) | 203 |
| FastBasic sample `carrera3d.bas` | 1602 | 1186 | 416 (26%) | 1546 |
| FastBasic sample `dli.bas` | 646 | 588 | 58 (9%) | 632 |
| FastBasic sample `draw.bas` | 107 | 103 | 4 (4%) | 103 |
| FastBasic sample `fedora.bas` | 643 | 463 | 180 (28%) | 619 |
| FastBasic sample `iospeed.bas` | 671 | 563 | 108 (16%) | 663 |
| FastBasic sample `joyas.bas` | 2442 | 1827 | 615 (25%) | 2407 |
| FastBasic sample `mastodon.bas` | 834 | 591 | 243 (29%) | 835 |
| FastBasic sample `nc.bas` | 683 | 547 | 136 (20%) | 670 |
| FastBasic sample `pi.bas` | 1330 | 1068 | 262 (20%) | 1321 |
| FastBasic sample `pmtest.bas` | 416 | 272 | 144 (35%) | 401 |
| FastBasic sample `sieve.bas` | 281 | 208 | 73 (26%) | 269 |
| `samples/stars.bas` | 593 | 391 | 202 (34%) | 561 |

On ordinary programs most of the saving is renaming. On hand-golfed
ten-liners, where the names are already short, the rest still counts:
parenthesis around function arguments (`P.(712)` becomes `P.712`, 2
characters each), parenthesis made redundant by precedence
(`((R+M)&-8)*5` is `(R+M)&-8*5`, as `&` binds tighter than `*`), one-statement
`IF` blocks, and names assigned by frequency, including `_` and labels that
share a variable's letter. That was 25 and 33 characters on listings already
packed into 1,200, enough to buy back a feature. (`mastodon.bas` is one
character longer with `-f` because of the end-of-line escape, below.)


## Usage

    fbp [options] [-o output] filenames

Options:

- `-l`  Output long (readable) listing, with one statement per line,
        indentation and all keywords expanded. Uses standard (ASCII) end of
        lines.

- `-s`  Output short (minimized) listing, with abbreviated keywords, short
        variable names and Atari end of lines. This is the default.

- `-S`  Like `-s`, also enables all optimizations that make the listing
        shorter, including the ones that change the compiled code (the
        result is verified to be equivalent).

- `-n num`  In short listing, sets the maximum line length (default 120). If
        a single statement is longer, it is written in its own line with a
        warning. Note that the FastBasic IDE can't edit lines longer than
        255 characters.

- `-f`  In short listing, keep the full variable and PROC names instead of
        renaming them to one letter. Needed if the program is linked with
        assembly code that uses the variable names.

- `-e`  In short listing, use standard (ASCII) end of lines instead of
        Atari end of lines.

- `-u`  In long listing, write keywords in uppercase (the default is
        lowercase).

- `-O`  Optimize the program. Without an argument enables all the
        optimizations that produce the same compiled code; an argument can
        be given to enable (`+name` or `name`) or disable (`-name`) one
        specific optimization. The option can be given multiple times,
        processed in order. Use `-O help` for a list. Examples:
        `-O` (all same-code optimizations),
        `-O -O -parens` (all, except removing parenthesis),
        `-O -O +inc_dec` (all, plus `INC`/`DEC` conversion),
        `-S -O -const_replace` (everything, except constant replacement).

- `-t tgt`  Selects the FastBasic target, as in the compiler `-t:` option.
        The default target is `default` (Atari 8-bit with floating point),
        use `atari-int` for integer-only programs and `atari-5200` for the
        Atari 5200. Use `-t help` for a list.

- `-v`  Shows more information (verbose mode): renamed variables, constants
        replaced and verification results. Repeat for even more.

- `-q`  Don't show any information, only errors (quiet mode).

- `-o out`  Sets the output file name. By default the output is the input
        file name with the `.lst` extension. If the given name starts with a
        dot, it is used as the extension.

- `-c`  Output to standard output instead of a file.

- `-h`  Shows help and exit.


## Optimizations

Optimizations enabled by a plain `-O`, the compiled code is exactly the same
as the original (after the FastBasic optimizer, as the cross-compiler does):

| Name         | Effect |
|--------------|--------|
| `parens`     | Remove parenthesis not needed, including around function arguments: `PEEK(712)` becomes `P.712`. |
| `print_sep`  | Remove `;` separators in `PRINT` when not needed: `?"A=";A` becomes `?"A="A`. |
| `print_join` | Join constant strings and `CHR$` in `PRINT`: `? "A";CHR$(66)` becomes `?"AB"`. |
| `next_var`   | Remove the variable after `NEXT`, FastBasic does not use it. |
| `defaults`   | Remove `STEP 1`, `PAUSE 0` and the `WORD` type in `DIM` and `DATA`. |
| `const_fold` | Compute integer operations on constants: `POKE 704+4,15*16` becomes `P.708,240`. |
| `cmp_zero`   | Remove comparisons with zero in conditions: `IF X<>0` becomes `IF X`. |
| `if_then`    | Replace `IF`/`ENDIF` blocks with only one statement by `IF`/`THEN`. |
| `elif`       | Replace `ELSE` followed by an `IF` block with `ELIF`, removing one `ENDIF`; also `ELSE` / `IF c THEN s` / `ENDIF` becomes `ELIF c` / `s` / `ENDIF`. Done by default in the long listing. |
| `end`        | Remove `END` at the end of the main program. |

Optimizations that change the compiled code, enabled with `-O +name` or with
`-S`. Those are verified by checking that the only difference is the
substitution done:

| Name            | Effect |
|-----------------|--------|
| `inc_dec`       | Replace `X=X+1` with `INC X` and `X=X-1` with `DEC X`, also on array elements and after `THEN`. The code is smaller and faster. |
| `const_replace` | Replace numbers and strings used many times with a new variable assigned at the start of the program, if the listing gets shorter. The code is a little bigger and slower. Not done if the program uses `CLR`. |
| `chr_str`       | Replace `CHR$(n)` with a string constant containing the character. Not done in string comparisons (see below). |

For de-obfuscating, there is one more optimization, only for the long listing
and only enabled explicitly with `-O +fixed_vars`:

| Name         | Effect |
|--------------|--------|
| `fixed_vars` | The reverse of `const_replace`: variables assigned only once, with a number or a string, at the start of the program, are replaced by the value. The assignment is replaced by a comment like `' fbp: fixed B = 53248`. Not done if the assignment is inside a loop, condition or PROC, if a PROC is called before it, or if the program uses `CLR`. |

With `-l`, only `const_fold`, `cmp_zero`, `defaults`, `elif` and
`fixed_vars` are applied, as the other optimizations make the listing less
readable. The `elif` conversion is done by default in the long listing (use
`-O -elif` to disable it), but only when no comment would be lost.

For example, `fbp -l -O +fixed_vars` on this minimized code:

```
B=53248:C=704:POKE C+1,15:F.I=0T.3:P.B+I,I*16:N.:I.B=1:?1:EL.:I.C:?2:E.:E.
```

gives:

```
' fbp: fixed B = 53248
' fbp: fixed C = 704
poke 704 + 1, 15
for I = 0 to 3
  poke 53248 + I, I * 16
next I
if 53248 = 1
  print 1
elif 704
  print 2
endif
```


## FastBasic notes for sizecoders

Some things work differently from Turbo-Basic XL, and `fbp` takes care of
them:

- **No line numbers.** Statements can be freely moved between lines, so
  `fbp` packs as many statements per line as possible.

- **`IF ... THEN` applies to only one statement.** In FastBasic,
  `IF X THEN A=1:B=2` always executes `B=2`, unlike Turbo-Basic XL. Because
  of this, joining lines never changes the meaning of an `IF`, and multi-line
  `IF`/`ELIF`/`ELSE`/`ENDIF` blocks can be split across lines anywhere.
  With `-v`, `fbp` shows a note when a source line has statements after an
  `IF ... THEN statement`, as this is a common mistake when coming from
  Turbo-Basic XL.

- **Comments** are `'` (anywhere, up to the end of the line, including any
  `:` after it) and `.` (only at the start of a statement). Short listings
  have no comments, long listings keep them.

- **PROC and DATA placement.** `PROC`, `DATA` and `DLI SET` code is placed
  by the compiler outside the main program, sorted by source line number.
  `PROC`/`ENDPROC` can be put anywhere in a line, but when two of those
  definitions share a line, their order in memory can change. `fbp` only
  puts them in the same line when the resulting order is the same as in the
  original, so the compiled program is identical (this matters for programs
  that read past the end of a `DATA` array into the next).

- **Statements can't be split across lines.** Loops, `IF` blocks and `PROC`s
  can span lines, but a single statement can't. A `DATA` statement ending
  in `,` continues in the next `DATA` statement. A statement longer than the
  maximum line length is written in its own line.

- **One name space for all variable types.** `A`, `A$`, `A%` and `A()` are
  the same name, so there are only 27 one-letter names (`A` to `Z` and `_`).
  PROC, DATA and DLI names are labels, in a separate name space, and can
  repeat variable names, with three catches: `&X` and `ADR(X)` take the
  address of DATA `X` before the address of variable `X`; `X(n)` reads an
  array variable `X` before DATA `X`; and assigning to a DATA element
  (`X(0)=1`, `GET`, `INPUT`) makes the parser create a variable `X` while it
  tries other rules, which changes the code if `X` already exists. `fbp`
  gives labels their own one-letter names and steers clear of those three.

- **EOL inside strings.** FastBasic's own `-l:min` writes an ATASCII end of
  line inside a string as the raw $9B byte (the compiler keeps reading a line
  inside quotes). That is 2 characters shorter than `"$9B`, but it shows as a
  line break in the IDE and in the listing, so `fbp` always uses the `$9B`
  escape: every line of its output is a real line.

- **Abbreviations and parenthesis.** All statements and functions can be
  abbreviated, including `AND`, `OR`, `MOD` and `EXOR` (`A.`, `O.`, `M.`,
  `E.`), and functions with one argument don't need parenthesis
  (`RAND 10`, `P.712`). Functions without arguments lose the parenthesis
  when abbreviated (`K.` for `KEY()`).

- **Floating point numbers** must have a decimal point before an exponent
  when an integer could be read (`1.E5`, not `1E5`), `fbp` keeps that.

- **String functions use a shared buffer.** `CHR$(65)=CHR$(66)` is true in
  FastBasic, so `fbp` does not replace `CHR$` in string comparisons.

- **Variables created while parsing.** FastBasic can create unused
  variables while trying alternative parsings (for example, `INCX=5`
  creates a variable `X` before assigning `INCX`). Those are not created in
  the minimized output, so `fbp` checks the code ignoring them.

- **Limits of the FastBasic IDE.** The IDE editor accepts lines up to 255
  characters, and the IDE compiler accepts up to 255 bytes of bytecode per
  statement. `fbp` never makes a statement bigger, except when joining an
  `IF` block into `IF`/`THEN`, where it checks the size.


## Differences with tbxl-parser

- There is no tokenized (binary) output, as FastBasic programs are always
  plain text, and compiled with the FastBasic compiler.
- The optimizations are new, made for FastBasic. The ones about line
  numbers and `%` constants don't apply.
- The parser directives (`$define`, `$incbin`, ...) are not supported, as
  the input must be a valid FastBasic program.
- The default operation is the short listing.


## Building

The only requirement is a C++17 compiler; type `make` and the program is
built as `build/fbp`. To cross-compile, for example for Windows:

    make CXX=x86_64-w64-mingw32-g++ EXT=.exe

The FastBasic syntax files are embedded into the executable at build time,
by a small tool compiled with `HOSTCXX` (default: same as `CXX`).

To run the tests, type `make test`.


## Keeping up to date with FastBasic

The files from FastBasic are in `vendor/fastbasic`, see the `VERSION` file
there for the FastBasic version. To update to a new FastBasic version:

    tools/sync-fastbasic.sh /path/to/fastbasic-source
    make clean test
    tests/crosscheck.sh /path/to/fastbasic-source

The `crosscheck.sh` script builds the FastBasic cross-compiler from the given
source and verifies that all `fbp` outputs of the FastBasic test suite and
samples compile to the same code as the originals.

The syntax tables and the optimizer are used unmodified, but the parsing
engine in `src/engine.cc` is a port of the FastBasic parser; if the files in
`vendor/fastbasic/reference` changed, port the changes to `src/engine.cc`.


## Testing FastBasic itself

`docs/fastbasic-stress-test.md` describes how to use `fbp` to look for bugs in
FastBasic (comparing the native and cross compilers with rewritten programs),
with some leads found while writing `fbp`.


## License

`fbp` is free software, under the GNU General Public License version 2 or
later, the same as FastBasic and tbxl-parser. See the `LICENSE` file.
