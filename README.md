# Auto Typer

[![Build](../../actions/workflows/build.yml/badge.svg)](../../actions/workflows/build.yml)

A small, dependency-free Windows utility that types text for you, one real keystroke at a time, into applications that do not accept clipboard paste.

Useful for remote desktop sessions, virtual machine consoles, KVM/IPMI web consoles, locked-down terminals or any input field where Ctrl+V is disabled, so you no longer have to retype long queries, scripts or commands by hand.

## How it works

1. Paste or write the text you want to send.
2. Set the **delay** (countdown before typing starts), the **interval** between characters and an optional extra **line pause** after each Enter.
3. Press **Start** and, during the countdown, click inside the target text field.
4. Release the mouse and keyboard. Auto Typer sends the text through the Windows `SendInput` API, character by character.
5. Optionally copy the result back and use **Compare output** to confirm it arrived intact.

Press **Esc** (or **F8**) at any time to stop.

## Features

- **Native Win32, no dependencies**: a single static `.exe`, no installer, no runtime, works offline.
- **Full Unicode**: accents, symbols, CJK and emoji (surrogate pairs) are sent as Unicode keystrokes, independent of the keyboard layout.
- **Tunable pacing**: 1–1000 ms per character plus 0–5000 ms extra after each line, for slow remote consoles or editors.
- **Compare output**: paste the result back and get an exact comparison against the text from the last run. Only line-ending conventions (CRLF / CR / LF) are treated as equivalent; the first mismatch is reported by line and column.
- **Safe by design**:
  - stops if the foreground window or focused control changes while typing;
  - stops if you press Ctrl, Alt, Shift, Win or a mouse button;
  - refuses to start into its own window or while modifier keys are held;
  - never retries partially sent input and never touches your clipboard.
- **Private**: text is kept in memory only; nothing is saved or transmitted.
- Per-monitor DPI aware, progress bar and character count.

## Tips

- **Code editors**: disable auto-indent, auto-closing brackets and autocomplete in the receiving editor before typing code, otherwise the editor itself will add or change text. Restore them afterwards.
- Start with **50 ms** per character and **250 ms** per line. On slow remote connections try 100 ms / 500 ms. Test a small sample first.
- **Enter** and **Tab** are sent as real keys, so they may submit a form, run a command or move focus.
- Windows blocks input to applications running with higher privileges (UIPI). If the target runs as administrator, run Auto Typer as administrator too.
- `SendInput` confirms the keystrokes were queued, not that the target kept them. That is why a run ends with "Sent", and why Compare output exists.

## Download

Get `AutoTyper.exe` from the [Releases](../../releases) page. Requires Windows 10 or 11 (x64).

The executable is built by GitHub Actions from this source code, but it is not code-signed, so Windows SmartScreen may warn the first time you run it (**More info → Run anyway**). You can also build it yourself.

## Build from source

The release is built with [Zig](https://ziglang.org/) 0.13.0 as a C++17 toolchain, targeting Windows x64:

```bash
cd src
zig rc /fo app.res app.rc
cd ..
zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -municode -Wl,--subsystem,windows -static src/main.cpp src/app.res -lcomctl32 -lgdi32 -luser32 -o AutoTyper.exe
```

## Tests

Text planning and comparison tests are portable and run with any C++17 compiler:

```bash
g++ -std=c++17 -O2 tests/input_plan_test.cpp -o input_plan_test && ./input_plan_test
g++ -std=c++17 -O2 tests/text_compare_test.cpp -o text_compare_test && ./text_compare_test
```

`tests/integration.cpp` is a Windows harness that launches the app, opens test windows and drives real keyboard input (delay, pacing, Unicode, Esc cancellation, focus changes, input validation). Run it on a dedicated desktop:

```bash
zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -static tests/integration.cpp -luser32 -lgdi32 -o integration.exe
integration.exe AutoTyper.exe
```

## Project structure

```
src/main.cpp          Win32 UI, timing loop, focus tracking and SendInput delivery
src/input_plan.h      UTF-16 text -> keystroke plan (pure, testable)
src/text_compare.h    Exact output comparison with newline normalization (pure, testable)
src/app.rc            Version info and manifest (DPI awareness, common controls)
tests/                Unit tests and Windows integration harness
licenses/             Notices for runtime libraries linked by the toolchain
```

## License

[MIT](LICENSE) © 2026 Eduard Serra Tuneu
