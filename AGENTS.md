# AGENTS.md — Sunsetter

Guidance for AI agents (and humans) working in this repository.

## What this project is

**Sunsetter** (V.8, version string "9a") is a C++ **Crazyhouse / Bughouse
chess engine** by Ben Dean-Kawamura & Georg v. Zimmermann, released under
LGPL (see `COPYING`). It is a late-1990s codebase that was maintained and
partially bugfixed later (notably by "angrim" and Lichess contributors).

- Plays **Crazyhouse** (piece drops, `P@f7` notation) and **Bughouse**
  (dual-board, "sit" mechanics, partner protocol).
- Communicates via the **XBoard** protocol, a custom **"tellics" partner
  protocol** (for Bughouse), and an **analysis mode**.
- Target platforms: **MSVC** (the original and primary compiler) and
  **Linux/GCC** (via `Makefile`). Also has `__EMSCRIPTEN__` hooks.

## Build

### Linux (primary for development here)

```sh
make            # builds ./sunsetter
make clean
```

`Makefile` compiles all 17 `.cpp` files at once with `-O3 -DNDEBUG`.
There are **no object-level dependencies declared by hand** — `.depend` is
generated with `$(CXX) -MM`. If you add/remove a source file, update the
`OBJECTS` list in the Makefile **and** `ClCompile Include=...` in
`msvc.vcxproj`.

### Windows / MSVC

`MSVC.sln` → `msvc.vcxproj` (plus legacy `MSVC.dsw` / `msvc.dsp` for
ancient Visual Studio). The original code assumes MSVC idioms; the
README notes non-MSVC compilers may need `_win32_` / `__forceinline`
shims, but modern GCC works via the fallbacks already in the code.

### Tests / regression tools (no unit test framework exists)

There is **no unit test suite**. Instead there are two CLI modes
(dispatched in `main()` in `bughouse.cpp` before the normal game loop):

```sh
./sunsetter test bench.bpgn        # analyzes a Crazyhouse bpgn game (or one
                                   # position, e.g. "test bench.bpgn 34 WHITE"),
                                   # exercising make/unmake, movegen and eval
./sunsetter speedtest bench.bpgn   # measures make/unmake + movegen speed
```

`bench.bpgn` is the sample game; `bench.bat` / `speedtest.bat` are the
Windows equivalents. **Use these as sanity checks after changes to board
state, move generation, or evaluation** — a bad make/unmake will corrupt
the position sequence in `test` output.

`tests.cpp` also contains `testbpgn()`/`speedtest()` and a bpgn tokenizer
(`nextToken`) shared with the book/test code paths.

## Architecture

One flat directory. All files are siblings; a few headers act as
"namespaces" for groups of functions. Global state is the norm.

### Data flow (from header comments in `bughouse.cpp`)

```
     ---> |   AI   | -----> | main() | -----------|
     |     --------          --------             |
     |       /|\             |                  |
     |       |   |----------|-----------|      |
    \|/      |              | Board  | <---- | Xboard |
     --------             --------         --------
    /|\     --------        --------
     |---> |quiesce()| <--> | eval() |
          --------        --------
```

1. `main()` in `bughouse.cpp` runs an infinite loop: if it's the
   engine's turn, call `findMove()`; otherwise ponder or wait; then
   `checkInput()` for XBoard commands.
2. `findMove()` (`search.cpp`) deep-copies the position into `AIBoard`
   (a second `boardStruct` global) and runs iterative deepening.
   **`gameBoard` is the real game board; `AIBoard` is the search
   board.** Never make game moves on `AIBoard`.
3. `searchRoot()` picks the best root move from
   `searchFirstMove()`/`searchMove()` (each pre-searches with a guess
   to enable time management).
4. `search()` is negamax alpha-beta with null-move pruning, check/capture/
   forcing extensions, and a 4-tier "PMG" (Plausible Move Generation):
   captures → mate tries → check evasions → quiet moves. Because
   Crazyhouse has huge branching and frequent deep mates, this goes
   beyond the usual 2-step (search + quiesce) scheme.
5. `quiesce()` (in `quiescense.cpp` — **the misspelling is original,
   do not "fix" the filename** unless you update the Makefile, vcxproj,
   and all references) searches captures/promotions only.
6. `eval()` (`evaluate.cpp`) returns centipawns, from side-to-move
   perspective; static tables (development, king safety, material-in-hand)
   live in `evaluate.h` and `brain.h` externs.

### Key types (in `board.h`)

- `boardStruct` — the entire game state. Contains `position[64]`, per-piece
  and per-color **bitboards**, `hand[2][7]` (hand counts for Crazyhouse
  drops), castling, en passant, clock, and full make/unmake history
  (`moveHistory`, `takeBackHistory`, `checkHistory`).
  `playMove()`/`unplayMove()` are the public API; `changeBoard()`/
  `unchangeBoard()` are the low-level ones, dispatched via a
  **pointer-to-member** (`currentChangeBoard`) that selects
  `bugChangeBoard` vs `ZHChangeBoard` (bug vs crazyhouse rules).
- `move` — packed 32-bit value: `from`(6) `to`(6) `moved`(3)
  `promotion`(3) + `isBad` flag. Packed in its constructor via shifts;
  access only through the inline accessors.
- `bitboard` — 64-bit wrapper with overloaded operators;
  `firstSquare()` is `bsf` inline assembly (MSVC + GCC inline asm
  variants; **do not remove without a portable replacement**).
- `transpositionEntry` — 16 bytes, `#pragma pack(4)` on MSVC only;
  48-bit hash bits. Hash is computed incrementally
  (`initHash`/`addToHash`/...) in `board.cpp`.
- Square numbers: `A1`=0 … `H8`=63, rank-major (rank 1 first).
  `IN_HAND`=64 and `OFF_BOARD`=65 are pseudo-squares (Crazyhouse drops
  originate from `IN_HAND`).
- Piece codes: NONE=0, PAWN=1, ROOK=2, KNIGHT=3, BISHOP=4, QUEEN=5,
  KING=6; `WHITE`=0, `BLACK`=1.

### Search-value scale (`brain.h`)

`INFINITY`=32000, `MATE`=20000, `MATE_IN_ONE`=30000,
`ALMOST_MATE`=18000 (Crazyhouse "sit" — no legal move but can sit for a
piece), `EXTREME_EVAL`=10000. These are interlinked; the comments warn
not to change them casually. `MAX_SEARCH_DEPTH`=48, `DEPTH_LIMIT`=64
(the `FractionalDeep[]` table in `search.cpp` implements sub-ply
fractional deepening, `ONE_PLY`=4).

### Move generation files (all operate on `move` arrays, MAX_MOVES=512)

- `moves.cpp` — all legal moves
- `capture_moves.cpp` — captures (+ `captureGain`, `orderCaptures` for
  MVV/LVA-ish ordering; `bestCaptureGain` must be set per node)
- `check_moves.cpp` — checks / mate tries / check evasions
- `aimoves.cpp` — "AI moves" (full generation for the search)
- `order_moves.cpp` — move ordering (hash move, killers via
  `initializeHistory`/`updateHistory`/`makeHistoryOld` in `board.cpp`)

### Other subsystems

- `interface.cpp` — all protocol I/O: XBoard command handling
  (`parseOption`), `tellics` partner protocol (see also `partner.cpp`),
  analysis mode updates, input polling (`checkInput`, `pollForInput`).
  `output()` is the single print function.
- `notation.cpp` — algebraic ↔ `move` conversion, including Crazyhouse
  drop syntax (`P@f7`); `rawAlgebraicMoveToDBMove`/
  `algebraicMoveToDBMove` on `boardStruct`.
- `transposition.cpp` — TT lookup/store + a "learn table" (4 MB,
  `checkLearnTable`/`saveLearnTable`/save-load to disk) for
  self-improvement scoring.
- `tests.cpp` — bpgn-based test/speedtest mode (see Build section).
- `validate.cpp` — board sanity checks / move validation helpers.
- `bitboard.cpp` — table initialization (`initBitboards`,
  attack/ray tables: `squaresTo`, `squaresPast`, `directionPiece`,
  `nearSquares`, etc.).
- `variables.h` — tunable size constants (hash size, MAX_MOVES, ...) and
  debug `#define`s (`LOG`, `DEBUG_HASH`, `DEBUG_STATS`, `GAMETREE`, ...).

## Conventions & gotchas

- **C++98-era style**: globals, `char*`, `sprintf`, no RAII, minimal
  inline functions. Match the existing style; don't modernize in drive-by
  edits.
- **Compiler conditionals**: `definitions.h` branches on `_win32_`.
  `qword` is `unsigned __int64` on MSVC / `long long` elsewhere;
  `qword(x)` macro builds 64-bit literals portably (always use it for
  64-bit constants, e.g. `A_FILE`).
- **`__forceinline`** is defined as `inline` off-MSVC (see `definitions.h`).
- **`#pragma pack(4)`** is applied to `transpositionEntry` only under
  `_win32_`; keep struct size (16 bytes) in mind for hash sizing
  (`MIN_HASH_SIZE`, `LEARN_SIZE` in `variables.h` are computed from it).
- **Debug/asserts**: `-DNDEBUG` is on in the Makefile. `#ifndef NDEBUG`
  code (`badMove`, `stopIfBad`, `showDebugInfo`) is build-gated;
  rebuild without `-DNDEBUG` (e.g. `make CXXFLAGS=-O2`) when debugging.
- **Volatile search abort flags** in `search.cpp` (`stopThinking`,
  `reSearch`, `forceMove`) are toggled by interface commands
  (`stopThought()`, `startSearchOver()`, `forceDeepBugToMove()`); check
  them if touching the search loop.
- **Bughouse "sit" state machine**: `sitting`, `toldpartisit`,
  `toldparttosit`, `parttoldgo`, `partsitting`, `psittinglong`
  (`bughouse.h`) — this is the dual-board partner protocol; the
  `ALMOST_MATE` score in `brain.h` relates to "sit for a piece"
  positions.
- **Time management**: `millisecondsPerMove` (global) is recomputed per
  depth; `timeToMove()` checks clocks. The main loop sleeps ~10 ms
  between iterations to avoid hogging CPU (and `usleep`/`sleep(10)`
  differs by platform).
- **Emscripten**: there are `__EMSCRIPTEN__` paths (Web build); keep
  them compiling if you touch those files.
- The opening **book** code is referenced but was still being integrated
  at this snapshot (see README "change notes" — partially commented out
  in `bughouse.cpp`/`board.h`).
- `README.txt` is the authoritative history/change-notes document;
  `COPYING` is LGPL.

## Where to look first

| Task | File(s) |
|---|---|
| "Where does the program start?" | `bughouse.cpp` (`main`) → `interface.cpp` loop helpers |
| "How is a move chosen?" | `search.cpp`: `findMove` → `searchRoot` → `search` → `quiesce()` |
| "How is a position scored?" | `evaluate.cpp` (`eval()`) + tables in `evaluate.h` |
| "How does board state change?" | `board.cpp`: `playMove`/`unplayMove`, `ZHChangeBoard`/`bugChangeBoard` |
| "How are moves generated?" | `aimoves.cpp`, `capture_moves.cpp`, `check_moves.cpp`, `moves.cpp`, `order_moves.cpp` |
| "How do I talk to the engine?" | `interface.cpp` (`parseOption`, `tellics` handling), `partner.cpp` |
| "What is this move encoding?" | `struct move` in `board.h` (bit layout documented there) |
| "How do I sanity-test my change?" | `./sunsetter test bench.bpgn` and `./sunsetter speedtest bench.bpgn` |

## Notes for modifications

1. Any new source file must be added to **both** `Makefile` `OBJECTS`
   and `msvc.vcxproj` `ClCompile`.
2. Keep the 16-byte `transpositionEntry` size stable if you touch it
   (hash sizes are derived from it).
3. `orderCaptures` must remain called on every node (see
   `bestCaptureGain` comment in `board.h`).
4. Preserve platform parity: MSVC and GCC paths exist side by side
   (`firstSquare`, `qword` literal macro, `#pragma` warnings). Test
   `make` (Linux) after any change you can; the README describes the
   intended non-MSVC shim behavior.
5. When in doubt about intended behavior, the block comments at the top of
   each file are unusually good and are the project's documentation.
