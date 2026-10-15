# Code Review — Intelligent Copy Paste

First reviewed 2026-09-04. **Remediated 2026-09-18** on branch
`cleanup/2026-09-18`. This file is now a record rather than a to-do list:
what was wrong, what was fixed, what was deliberately left, and what still
needs a person.

## Corrections to the first review

Two findings in the first pass were wrong. Both are recorded here with the
evidence, because a review that sends the next reader to fix the wrong thing
is worse than one that misses something.

**1. "`UpdateRecentTextFromClipboard` ignores `CF_TEXT`" — not a bug.** The
first review said that reading only `CF_UNICODETEXT` leaves the preview stale
when copying from an application that offers only ANSI text. Windows
synthesises the clipboard text formats from one another, so it does not.
Verified rather than reasoned about: a probe that put `CF_TEXT` on the
clipboard and nothing else, then asked for `CF_UNICODETEXT`, got the text
back.

```
IsClipboardFormatAvailable(CF_TEXT)        = 1
IsClipboardFormatAvailable(CF_UNICODETEXT) = 1
GetClipboardData(CF_UNICODETEXT) SUCCEEDED -> "ANSI only, no wide copy"
```

This is the same conclusion `C/clipboard_history` reached on 2026-09-17, where
the first review made the mirror-image mistake and assumed a live `CF_TEXT`
fallback branch. A `CF_TEXT` path in a program that already asks for
`CF_UNICODETEXT` is dead code. No fallback was added; a comment in
`UpdateRecentTextFromClipboard` now says why.

**2. "The tray icon uses `IDI_APPLICATION` even though the project ships
`app.rc` with its own icon" — there is no icon.** `app.rc` is three lines and
contains only the manifest:

```rc
#include <winuser.h>

1 RT_MANIFEST "app.manifest"
```

There is no `.ico` anywhere in the repository and never was, so "load the app
icon resource instead" had nothing to load. The tray icon is still the system
application icon. What was changed is that it is now requested at the
small-icon metric (`SM_CXSMICON`) instead of at `0, 0`, which asked for the
32×32 version and left the shell to scale it down.

## What this project is

An always-on-top Win32 widget with Copy and Paste buttons. Copy detects
whether the foreground window is a terminal and sends `Ctrl+Shift+C` there and
plain `Ctrl+C` everywhere else; Paste always sends `Ctrl+Shift+V`. It shows a
preview of the clipboard and hides to the tray.

## Verdict, revisited

The first review's assessment stands: this is well-built native Win32 code,
and `WS_EX_NOACTIVATE` so the buttons do not steal focus is the load-bearing
insight the whole tool rests on. The fonts are created once and freed, the
allowlist errs conservatively and explains why, and the comments explain
*why* rather than *what* throughout — the `WS_EX_TOOLWINDOW` note and the
owner-drawn button rationale are both things a reader could not recover from
the code.

What the first review did not say is that **the "Recently copied" panel had
never once updated from a clipboard change it did not itself cause**, and that
several of the failure paths could not be distinguished from success. The
README's own admission — "tested by cross-compiling for correctness" — turned
out to be the tell. Nine bugs were found, of which the first review had
identified three.

The pass added 203 automated checks in `tests/copypaste_test.c` and 15
end-to-end checks in `tests/drive_test.c` that drive the real executable.
Against the pre-pass code the unit checks fail 51 times and segfault twice;
the end-to-end checks fail 7 times.

## What was fixed

### 1. The preview was read on a 60 ms sleep, and never followed the clipboard (P1, in the first review)

`DoCopy` sent the keystroke, called `Sleep(60)` on the UI thread, then read
the clipboard. Three consequences: the widget froze for that period; if the
source application took longer than 60 ms the panel showed the *previous*
clipboard content with no indication it was stale; and a copy made with the
keyboard instead of the button never updated the panel at all.

Replaced with `AddClipboardFormatListener` and a `WM_CLIPBOARDUPDATE` handler,
which is what the sibling `C/clipboard_history` already did. No sleep, no
race, and the panel now tracks the clipboard however it changed.

Confirmed end to end. `tests/drive_test.c` launches the real executable,
changes the clipboard from another process with no button pressed, and reads
the preview control back. Against the fixed build all 15 checks pass; against
the pre-pass build seven fail and the panel stays on "Nothing copied yet"
throughout.

### 2. `SendInput`'s return value was discarded (P1, in the first review)

`SendKeyCombo` returned `void`. When User Interface Privilege Isolation blocks
the injection — which it does whenever the foreground window belongs to a
higher-integrity process, so any elevated console — `SendInput` inserts
nothing and returns 0. The button appeared to work and the panel went on
showing the old clipboard content.

`src/keys.c` now reports `KEYS_SENT`, `KEYS_BLOCKED` or `KEYS_PARTIAL`, and
`DoCopy`/`DoPaste` put an explanation in the panel when it is not the first.
`SendInput` arrives as a function pointer so the refusal path is testable at
all; provoking it for real would need an elevated window to hand.

A partial insert now also releases Ctrl and Shift. The key-down events reach
the target and the key-up events do not, so without that the application
believes the user is still holding Ctrl and every subsequent keystroke is a
chord.

The elevation limitation is documented in the README, under "When it cannot
do it", as the first review asked.

### 3. The widget could send its keystrokes to itself (new — the first review missed this)

`DoCopy` used `GetForegroundWindow()` on the reasoning that
`WS_EX_NOACTIVATE` guarantees it is never the widget. It does not:
`WS_EX_NOACTIVATE` only prevents activation *by clicking*. It does not
prevent `SetForegroundWindow` — and the tray handler calls
`SetForegroundWindow(hwnd)` on this very window every time the icon is
right-clicked, because a tray popup menu will not dismiss without it.

So after any right-click on the tray icon the widget itself held the
foreground, and the next Copy sent `Ctrl+C` to a window with no text and no
selection. Nothing was copied, the clipboard kept whatever it had, and the
panel showed that stale content.

Confirmed with a probe before fixing, since the claim rests entirely on
whether `SetForegroundWindow` is really allowed on such a window:

```
after SW_SHOWNOACTIVATE: is foreground = 0
SetForegroundWindow on the visible NOACTIVATE window returned 1; is foreground = 1
DoCopy's target would be our own process: 1 (target pid 6332, ours 6332)
```

`src/target.c` now remembers the last foreground window that was not ours, and
`targetDecide` answers both halves of the question. The second half matters
because **`SendInput` has no target parameter** — it delivers to whatever holds
the foreground, so knowing which window should receive the chord is useless
unless that window is put in front first. `AcquireTarget` does that, and
refuses with a message when there is nowhere to send.

The foreground history comes from a `SetWinEventHook` on
`EVENT_SYSTEM_FOREGROUND`, out-of-context so the callback arrives on this
thread through the existing message loop. A remembered handle is checked with
`IsWindow` before use, because handles get reused.

### 4. The clipboard payload was read with `wcslen` (new — the first review missed this)

Nothing obliges the process that writes to the clipboard to NUL-terminate its
block: `SetClipboardData` takes a handle and a format, and `GlobalSize` is the
only thing that is certain. The original called `wcslen` on the locked
pointer, which reads until it happens to find a zero somewhere past the end of
the allocation.

`previewBuild` in `src/preview.c` takes the character count as a required
argument and stops at the first NUL *or* the end of the block, whichever comes
first. `UpdateRecentTextFromClipboard` passes
`GlobalSize(hData) / sizeof(wchar_t)`.

The test constructs a block with sixteen characters of payload followed by
sixteen more the caller never offers, so the over-read is visible rather than
merely undefined, and asserts both that the new code stops and that the
reproduced original does not.

Worth knowing for next time: Windows itself terminates the block on the way
through the clipboard. The end-to-end test puts eight unterminated characters
on and gets seven back. So the over-read is harder to trigger through the real
clipboard than through the function — but the function is also called with
whatever a future caller hands it, and a bound that is a parameter cannot be
forgotten.

### 5. A failed tray add hid the window anyway (new — the first review missed this)

```c
Shell_NotifyIconW(NIM_ADD, &g_nid);
ShowWindow(g_hwnd, SW_HIDE);
g_hiddenToTray = TRUE;
```

`NIM_ADD` can fail — while Explorer is restarting, or when the notification
area is not accepting icons at that moment. When it did, the window was hidden
regardless and there was no icon to click. The process stayed running, holding
its clipboard listener, unreachable short of Task Manager.

Related and with the same ending: an icon that *was* added successfully is
thrown away when Explorer restarts, and the shell broadcasts `TaskbarCreated`
so applications can add theirs back. The original never registered for it.

`src/tray.c` is now a small state machine. `trayShow` returns whether the icon
is actually there and `WM_CLOSE` hides the window only if it is, saying so in
the panel otherwise. `trayRestore` handles `TaskbarCreated`, and falls back to
showing the window if the icon cannot be re-added. `Shell_NotifyIconW` arrives
as a function pointer, because nothing here can make a real notification area
refuse on demand.

This also resolves the first review's P2 note that `HideToTray` re-zeroed and
re-added `g_nid` every time: the struct is built once in `wWinMain` and
`trayShow` is a no-op when the icon is already present, which the tests pin.

### 6. `OpenClipboard` was not retried (P2, in the first review)

`if (!OpenClipboard(g_hwnd)) return;` — one attempt, and a silent stale panel
on failure. The first review was right that this matters more here than in the
sibling project, because the moment it runs is right after another application
has written to the clipboard.

Reproduced before fixing. A helper process that holds the clipboard open with
a real window as its owner makes every attempt fail:

```
attempt 0: FAILED, err 5    (ERROR_ACCESS_DENIED)
...
-> 5 of 5 single attempts failed
```

Worth recording the trap: a first attempt at this probe called
`OpenClipboard(NULL)` in the holder, and then all five attempts *succeeded* —
the exclusion did not appear to exist. The holder needs a real window handle
before the lock behaves as documented.

Now ten attempts with a 15 ms gap, and a message in the panel if they all
fail.

### 7. Truncation split surrogate pairs (new — the first review missed this)

Cutting at 512 characters can land between a high surrogate and its low half.
Half a pair is not a character and a `STATIC` control draws it as a
replacement glyph. `previewBuild` drops the orphaned high surrogate along with
its partner.

The first review checked the buffer arithmetic here and called it correct,
which it was — 512 characters plus ellipsis plus NUL into a 516-element
buffer. It just was not the only thing that could go wrong at that boundary.

### 8. `QueryFullProcessImageNameW` was given a fixed `MAX_PATH` buffer (new — the first review missed this)

A path longer than `MAX_PATH` makes it fail with
`ERROR_INSUFFICIENT_BUFFER`, and the original treated any failure as "not a
terminal". So a terminal installed somewhere deep — a portable build inside a
nest of project folders — silently got plain `Ctrl+C`, which in a console
means interrupt. `ProcessImagePath` now grows its buffer to 32768.

### 9. The process-name allowlist could not match a console window (new — the first review missed this)

The list had `cmd.exe` and `powershell.exe` on it, but the window a console
application appears in belongs to the console *host* process, not to the
command interpreter. Neither `conhost.exe` nor `openconsole.exe` was on the
list, so the image check could never fire for a console window — it only ever
worked because `ConsoleWindowClass` caught it first.

Harmless as it stood, because the class check is the reliable half and does
catch it. Both host names are on the list now, and the comment in
`src/terminal.c` says which half does the work and why the other two entries
are there.

## Restructuring

The project was a single 516-line file in which every decision was tangled
with the Win32 call that acted on it, which is why none of the above was
caught. Following the convention in `CLAUDE.md`, the decisions were pulled out
into files with no window, socket or clipboard in them:

| File | What it decides |
| --- | --- |
| `src/terminal.c` | is this a terminal, from a class name and an image path |
| `src/preview.c` | what text goes in the panel, given a payload and its length |
| `src/keys.c` | the chord's `INPUT` array, and whether the send was accepted |
| `src/target.c` | which window a chord goes to, and whether to restore it first |
| `src/tray.c` | whether the notification icon is actually present |

`src/intelligent_copy_paste.c` keeps the window, the controls, the message
loop and the real API calls, and is the only file the test suite does not
touch. Every UI line in it — the layout constants, `CreateChildControls`,
`ApplyLayout`, `WM_CTLCOLORSTATIC`, `WM_DRAWITEM`, the window creation in
`wWinMain` — is unchanged from the original.

`aero_clipboard_mockup.html` moved to `docs/` and is now referenced from the
README, as the first review suggested. The stale `IntelligentCopyPaste.exe`
and `app_res.o` were deleted from disk; both were already gitignored. A
`LICENSE` was added — MIT, matching the sibling projects, since the README had
said "add a license of your choice here" since the first commit.

One trap for anyone restructuring this further: `preview.h`'s include guard
was originally `PREVIEW_H`, which collides with the layout constant
`#define PREVIEW_H 96` in the main file. gcc warns, but only because the
constant is defined second; the other way round it would have silently
suppressed the header. All five guards are `ICP_`-prefixed now.

## Deliberately not done

**No global hotkey.** The first review listed this as P3 and it is a fair
observation — a tool whose value is "one click instead of remembering the
chord" still needs the mouse. But adding one is a change to how the program is
used, not to whether it works, and the standing constraint for this archive is
business logic only. It is listed in the README's known limitations instead.
If it is wanted, `RegisterHotKey` plus a `WM_HOTKEY` case calling `DoCopy` is
perhaps fifteen lines, and `AcquireTarget` already does the right thing when
the widget is not in the foreground.

**No dark mode.** Explicitly UI, and the first review did not ask for it. The
README says what it would take.

**The tray popup menu can be left stuck.** `SetForegroundWindow` is documented
to fail on a hidden window, and the widget is hidden whenever the tray icon is
clickable — so the call that is meant to let the menu dismiss when you click
away may not be doing so. A probe here found `SetForegroundWindow` returning
non-zero and taking the foreground even while hidden, so the problem may not
arise on this Windows build; the usual remedy is to post `WM_NULL` to the
window after `TrackPopupMenu` returns. Not changed: it is interaction rather
than logic, nothing here can confirm the symptom, and the fix would be a
guess. Worth a look during the manual pass below.

**The terminal allowlist was not expanded** beyond the two console-host names
in item 9. The first review called the conservative list the right call and it
is; keeping it current is a maintenance job, not a fix.

## Verifying

```bat
tests\build.bat
```

Expected: `203 checks, 0 failed`. Needs gcc only — `D:\TDM-GCC-64\bin` is
where it is on this machine.

The opt-in end-to-end suite, which **replaces the clipboard contents**:

```bat
src\build.bat
tests\drive.bat
```

Expected: `15 checks, 0 failed`.

## Still needs a person

Two things no test here can reach, both about ten minutes.

1. **The Copy and Paste buttons have never been clicked.** Pressing them sends
   real keystrokes to whatever window is in front, so an automated test would
   type into the terminal running it. The chord construction, the refusal
   handling and the target choice are all covered by unit checks, and the
   window, message loop and clipboard listener are covered end to end — but
   nothing joins the two. Worth doing:

   - Select text in Notepad, click **Copy**, confirm the panel updates.
   - Select text in Windows Terminal, click **Copy**, confirm the panel
     updates and that whatever was running was *not* interrupted. This is the
     one that matters: it is the whole point of the program.
   - Click into Notepad, click **Paste**, confirm the text arrives.
   - Right-click the tray icon, choose Open, then click **Copy** — this is the
     path that used to send `Ctrl+C` to the widget itself. It should now
     restore the window you were last in and copy from there.
   - While it is in the tray, check the popup menu dismisses when you click
     away from it (see "Deliberately not done").

2. **The elevation path.** Open an administrator PowerShell, select some text,
   and click **Copy** with the widget running unelevated. Expected: the panel
   says Windows refused the keystroke and names elevation as the cause. Then
   run the widget as administrator and confirm it works. This is the
   `KEYS_BLOCKED` path, which is unit-tested through an injected function but
   has never been produced by a real UIPI refusal.

## Notes for a future session

- `tests/drive_test.c` is the reusable piece here: a harness that launches a
  real Win32 GUI executable, finds its window by class, and reads and asserts
  on its controls from outside. If another Win32 project in this archive needs
  an end-to-end test, start from that rather than from scratch.
- The `_wcsicmp`-to-`CompareStringOrdinal` change in `src/terminal.c` is
  hardening, not a bug fix, and is described that way on purpose. The theory
  was that `_wcsicmp` folds case through the CRT locale, so `MINTTY` against
  `mintty` would stop matching in a Turkish locale. It does not, on this
  toolchain — MinGW's `towlower(L'I')` returns `i` even after
  `setlocale(LC_ALL, "Turkish")` succeeds. There is no regression check for
  it, because a test that passes against both implementations is testing
  nothing. `CompareStringOrdinal` was kept anyway: it is the right API and
  removes the dependency on process-wide locale state.
- `app.manifest` is what gives the native controls their v6 comctl32 theming.
  If the buttons ever revert to Windows 95 styling, that manifest stopped
  being embedded.

## Licence, 2026-10-02

The author chose one licence for all of their projects: free for personal,
non-commercial use, modifiable for the user's own use, and not to be
distributed, original or modified. No standard licence says that, so `LICENSE`
is a short custom text, the same in every project apart from the year and what
it excludes. The README's licence section says the same. Anything above that
calls the licence an open question, or calls the project MIT, is superseded.

## CI, 2026-10-02

`.github/workflows/ci.yml` runs `tests\build.bat` with the runner's MinGW gcc
on `windows-latest`. `tests\drive.bat`, which replaces the clipboard, stays
manual. Nothing has been pushed, so it has not run on GitHub yet. Every job
was run before committing, from a fresh clone of this branch, the way a runner
would run it: Windows jobs here, in PowerShell 7 or the shell the step names,
with only the tools the job sets up on `PATH`. 203.
