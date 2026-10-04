# Stress-testing FastBasic with fbp

This is an opening prompt for an AI coding session (for example Claude Code)
working on a fork of [dmsc/fastbasic](https://github.com/dmsc/fastbasic), to
look for bugs in FastBasic itself. It was written while building fbp, and
includes leads found then, verified at upstream commit `212de72`.

fbp's own tests don't find FastBasic bugs, as fbp treats FastBasic as the
reference. But fbp is useful as a tool: it rewrites programs into equivalent
forms that must produce the same output when compiled and run, and its
minimized output exercises the native (6502) parser in ways normal programs
don't.

Copy everything below the line into the session, with both the FastBasic fork
and this repository available to it.

---

# Goal

This repository is a fork of dmsc/fastbasic (FastBasic, a BASIC compiler and
interpreter for the Atari 8-bit). Stress-test FastBasic for language, logic and
compilation bugs: the cross-compiler parser, the native (6502) IDE parser, the
peephole optimizer and the runtime. Work only in this fork, on a new branch. Do
not open issues or PRs upstream; I'll decide what to send to dmsc.

Judge behavior against `manual.md`, and against consistency between the ways a
program can be compiled: native vs cross compiler, integer vs floating point
target, optimizer on vs off. Separate real bugs from documented or intentional
quirks. When unsure, mark it as a question for the author rather than "fixing" it.

# Setup

- `make` builds everything (C and C++ compilers only; cc65 sources are in `cc65/`).
- `make test` runs the test suite (it fetches the `testsuite/mini65` submodule,
  a 6502/Atari emulator). The runner is `testsuite/src/fbtest.c`.
- Each test is `testsuite/tests/NAME.bas` plus `NAME.chk`, with fields Name, Test,
  optional Input (keyboard input) and Output (expected screen output).
- Test types: `run` compiles with BOTH the native compilers (`build/bin/fbc.xex`
  and `fbci.xex`, running inside the emulator) and the cross-compiler, for both
  the integer and FP targets, then runs each result and compares output. Also
  `run-cross`, `run-fp`, `compile-error` and `compile-error-native`. Read
  `fbtest.c` and `tests.mak` before adding tests.

# Techniques, in this order

1. Run `make test` and record the baseline.
2. Differential parser testing (native vs cross). Write new `run` tests aimed at
   parser edge cases: abbreviations (`PRI.`, `A.`/`O.`/`M.`/`E.` for
   AND/OR/MOD/EXOR, `F.` for `FRE()`), missing spaces (`FORI=0TO10`, `I.A>0A.B<1`),
   identifiers that start with keywords (`PRINTED`, `ABSA`, `INCX`, `DATAVAR`),
   negative and hex numbers, FP formats (`.5`, `1.E5`, `-0.25`), string escapes
   (`""` and `"A"$9B"B"`), `'` vs `.` comments, multi-line DATA ending in `,`,
   DLI SET continuation, and functions without parentheses (`RAND 10`, `P.712`).
   Any case where native and cross disagree is a finding.
3. Optimizer on/off: compile with the cross-compiler with and without `-n` (no
   optimizer); runtime output must be identical. Also diff the generated `.asm`
   to understand each difference.
4. Metamorphic testing with fbp (github.com/kimslawson/fastbasic-parser; build
   with `make`, binary `build/fbp`). fbp rewrites FastBasic programs into
   equivalent forms, and checks every rewrite by re-parsing it with FastBasic's
   own grammar and comparing bytecode. For each `run`/`run-fp` test, generate
   variants and run them with the SAME `.chk`:
   - `fbp -l` (expanded), `fbp -s -e` (minified), `fbp -s -e -O`
     (identical-code optimizations), `fbp -S -e` (also rewrites that change the
     bytecode but not the meaning: `INC`/`DEC`, constants in variables, `CHR$`
     as strings).
   - Use `-e` (ASCII line endings) so the test pipeline treats variants like the
     originals. Use `-t atari-int` for integer-only tests, default target
     otherwise.
   - fbp checks against ONE target grammar. If a variant fails only in int or
     only in FP, regenerate it with the other target before blaming FastBasic:
     a few abbreviations mean different keywords in the two grammars.
   - Skip compile-error tests; copy any data files a test loads.
   - A variant that prints different output means a bug in FastBasic or in
     fbp. Minimize it to decide which, and list fbp bugs separately.
5. Targeted code review, looking for rules or paths that can never trigger or
   that disagree with the runtime:
   - every `src/compiler/peephole.cc` rule, especially constant folding and
     division/MOD by zero (see the "Probably a bug in the division routine"
     comments)
   - integer edge cases: -32768, /-1, MOD with negative operands, 16-bit overflow
   - FP edge cases
   - strings: 255-character limit, `[start,len]` bounds, `=+` overflow
   - PRINT TAB/RTAB/COLOR and INPUT
   - FOR with negative or zero STEP, and EXIT inside nested loops and PROCs
   - PROC parameters, and the native compiler's per-statement limits
6. Optional: a random program generator driven by `src/syntax/*.syn`, compared
   across native/cross and optimizer on/off.

# Leads from building fbp (at upstream commit 212de72)

1. CONFIRMED, block layout. `src/compiler/parser.h:616` places PROC/DATA/DLI
   blocks with `std::sort` by source line. Ties (two blocks on one line) keep
   std::map order, which compares label names as strings, so `jump_lbl_10` comes
   before `jump_lbl_9`. `std::sort` is also not stable in general (libstdc++
   above 16 elements, and other C++ libraries differ), so memory layout can vary
   between platforms. Repro: the second array is placed BEFORE the first in the
   `.asm`:

       FOR I=1 TO 2:NEXT I
       FOR I=1 TO 2:NEXT I
       FOR I=1 TO 2:NEXT I
       FOR I=1 TO 2:NEXT I
       DATA FIRST() BYTE = 1 : DATA SECOND() BYTE = 2
       ? ADR(FIRST) < ADR(SECOND)

   Decide whether that is a bug (programs indexing past one DATA array into
   the next would break) and propose a fix, e.g. stable sort by (line,
   numeric label).
2. CONFIRMED, dead optimizer rule. In `peephole.cc`, the rule at ~line 1050
   (`TOK_VAR x / TOK_PUSH / TOK_NUM y / TOK_ADD -> TOK_NUM y / TOK_ADD_VAR x`)
   rewrites the code before the `VAR = VAR + 1 ==> INC VAR` rule at ~line 1116
   can match. So `X=X+1` compiles to `TOK_1, TOK_ADD_VAR X, TOK_VAR_STORE X`,
   not `TOK_INCVAR X`, while `INC X` does get `TOK_INCVAR`. The code is still
   correct, just bigger and slower. Check the rule at ~1133 and the DEC
   equivalents, then look for other rules that earlier rules make unreachable.
3. Typo in `parser.h:377`, `peek()`: `if(p >= 'a' && p >= 'z')` (should be
   `<=`). Currently harmless because `peek` is only called with `':'`.
4. Variables and labels created by failed parse attempts are never removed
   (`parser-actions.cc:441-451`, `E_VAR_CREATE`; `restore()` in `parser.h:296`
   does not undo them). `INCX=5` creates an unused `X`, and `DATAVAR=1`
   creates a label `VAR`. Check whether the native parser does the same, and
   whether this can cause errors (variable limits, later `PROC VAR` or `DATA VAR()`).
5. `CHR$` and `STR$` results share one temporary buffer: `CHR$(65)=CHR$(66)` is
   true, and `testsuite/tests/func-chr.chk` expects that, but the manual only
   documents it for `[]` slicing. Probably a documentation gap.
6. CONFIRMED, rejects valid code. `INPUT` into a byte or floating point DATA
   array fails with "parse error, expected: ','", in the integer and FP
   targets, while byte and FP `DIM` arrays and word DATA arrays work:

       DATA D() BYTE = 1, 2, 3
       INPUT D(0)

   Also `INPUT "?", D(0)` and `DATA D%() = 1.5 : INPUT D%(0)`. Cause (see
   lead 4): `INPUT_VAR` in `basic.syn` tries `VAR_WORD_LVALUE_SADDR` first,
   whose `E_VAR_CREATE` creates a variable `D` and then accepts it as a word
   variable; `INPUT_VAR` returns, the `(` doesn't match `INPUT_VAR_MORE`, and
   the `ARRAY_BYTE_ADDR` (or `float.syn`) alternatives are never tried.
   Assignments (`D(0)=1`) work because `LINE_ASSIGNMENT` has the `=` inside
   each alternative. Check the native parser too, and look for other tables
   that call `VAR_WORD_LVALUE_SADDR` before an array alternative and then
   continue outside it.

# Deliverables

- `FINDINGS.md`. For each finding: summary, minimal `.bas` repro, expected vs
  actual, which paths show it (native/cross, int/fp, optimizer on/off),
  severity (wrong output > crash > rejects valid code > missed optimization >
  docs), and a proposed fix. Keep fbp bugs in a separate section.
- New regression tests in `testsuite/tests/` for confirmed bugs.
- Fixes on separate commits with `make test` passing, and a note on any change
  in generated code size.
- Ask me before large refactors or anything that changes language behavior.
