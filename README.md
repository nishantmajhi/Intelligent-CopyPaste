# Intelligent Copy Paste

A small always-on-top Windows widget with two buttons, **Copy** and **Paste**,
for people who keep hitting the wrong chord.

In a console, `Ctrl+C` does not copy. It sends an interrupt. Terminals moved
copy to `Ctrl+Shift+C` instead, so the correct keystroke for "copy the
selection" depends on which window you are in — and getting it wrong in a
terminal kills whatever was running. This widget decides for you: it looks at
the window that has focus, and sends `Ctrl+Shift+C` if that is a terminal or
plain `Ctrl+C` if it is not.

It also shows what is currently on the clipboard, so you can tell whether the
copy actually happened.

## How it works

The widget never takes focus. Its window is created `WS_EX_NOACTIVATE`, so
clicking Copy leaves the foreground window exactly where it was — which is
what makes "whichever window has focus" a question worth asking at the moment
the button is pressed. Everything else follows from that:

- **Copy** looks at the foreground window's class name and the file name of
  the process that owns it, checks both against an allowlist of known terminal
  hosts, and sends the matching chord with `SendInput`.
- **Paste** always sends `Ctrl+Shift+V`. There is no interrupt to avoid on the
  paste side, and `Ctrl+Shift+V` is honoured by the standard Windows edit
  control as well as by terminals, so one chord covers both.
- **The "Recently copied" panel** follows the clipboard itself, through
  `WM_CLIPBOARDUPDATE`. It updates whenever the clipboard changes, whether
  that was this widget's doing or a `Ctrl+C` you typed yourself.

### When it cannot do it

Two situations produce a visible message in the panel rather than a button
that quietly does nothing.

**The focused window is elevated.** Windows will not let a normal program
inject keystrokes into a window belonging to an administrator process — User
Interface Privilege Isolation blocks it, and `SendInput` reports that it
inserted nothing. An administrator console is exactly where "use
`Ctrl+Shift+C`" matters most, so this is worth knowing about: **to drive
elevated windows, the widget has to be running elevated too.** Right-click
the executable and Run as administrator.

**There is no window to act on.** If the widget itself is in the foreground —
which happens after you right-click its tray icon — it restores the window you
were last using before sending the chord, and says so if it cannot.

## Building

Needs a C compiler targeting Win32/Win64 and a resource compiler to embed the
manifest. No external dependencies beyond the Windows SDK libraries.

```bat
src\build.bat
```

That runs `windres` over `app.rc` and then compiles the six source files. The
manifest `app.rc` embeds is what asks for Common Controls v6: without it the
group box and the preview text still work, they just render in the Windows 95
style.

By hand, with MinGW-w64:

```bash
windres app.rc -O coff -o app_res.o
gcc -std=c11 -Wall -Wextra -municode -mwindows -o IntelligentCopyPaste.exe \
    src/intelligent_copy_paste.c \
    src/terminal.c src/preview.c src/keys.c src/target.c src/tray.c \
    app_res.o \
    -luser32 -lgdi32 -lcomctl32 -lshell32 -ladvapi32 -static
```

- `-municode` selects the wide entry point, `wWinMain`
- `-mwindows` builds a GUI application, with no console window attached
- `-static` links the MinGW runtime in, so the `.exe` runs on its own

With MSVC, from a Developer Command Prompt:

```bat
rc app.rc
cl src\intelligent_copy_paste.c src\terminal.c src\preview.c src\keys.c ^
   src\target.c src\tray.c app.res ^
   /link user32.lib gdi32.lib comctl32.lib shell32.lib advapi32.lib
```

## Testing

```bat
tests\build.bat
```

203 checks, no framework, no dependencies. They cover the parts of the program
that decide something: the terminal allowlist, the preview text, the keystroke
sequence and what happens when it is refused, which window a chord is aimed
at, and the tray icon's presence. Those live in their own files precisely so
they can be tested without a window — `src/intelligent_copy_paste.c` is the
only file that touches the Win32 API for real.

There is a second, opt-in suite that drives the built executable:

```bat
src\build.bat
tests\drive.bat
```

It launches the real widget, finds its window, changes the clipboard from
outside and reads the preview control back. **It replaces the contents of your
clipboard** several times and leaves the last value there, which is why it is
separate.

## Running

Double-click `IntelligentCopyPaste.exe`. The widget appears near the bottom
right of the screen and stays on top of other windows.

1. Select text in any application.
2. Click **Copy**. The panel shows what landed on the clipboard.
3. Click into where you want it, then click **Paste**.

Drag the widget by its titlebar. The minimize box shrinks it to a compact view
with just the two buttons, rather than sending it to the taskbar — a real
iconify would hide the buttons, which is the opposite of useful for a widget
you want in reach. Closing it with the X sends it to the notification area
instead of quitting; left-click the tray icon to bring it back, right-click
for a real Exit.

For terminals, the widget assumes `Ctrl+Shift+C` and `Ctrl+Shift+V` are
already wired to copy and paste, which is true out of the box for Windows
Terminal, ConEmu and most others; some older console hosts need it enabled in
their properties first. The widget only sends the keystroke — what it does
when it arrives is up to the target application.

## Project layout

```
.
├── src/
│   ├── intelligent_copy_paste.c  # the window, the message loop, the Win32 calls
│   ├── terminal.c/.h             # is this window a terminal?
│   ├── preview.c/.h              # clipboard payload -> the "Recently copied" line
│   ├── keys.c/.h                 # the Ctrl(+Shift)+letter chord, and whether it landed
│   ├── target.c/.h               # which window a chord should go to
│   ├── tray.c/.h                 # the notification-area icon's presence
│   ├── winver.h                  # the Windows API version everything targets
│   └── build.bat
├── tests/
│   ├── copypaste_test.c          # the 203 checks
│   ├── drive_test.c              # drives the real executable (opt-in)
│   ├── build.bat
│   └── drive.bat
├── docs/
│   └── aero_clipboard_mockup.html  # an early visual mockup, kept for reference
├── app.manifest                  # asks for Common Controls v6
├── app.rc                        # embeds app.manifest
├── review.md                     # what was wrong with this, and what was done
└── LICENSE
```

## Known limitations

- **Terminal detection is an allowlist**, of window classes and process names.
  A terminal emulator that is on neither list is treated as an ordinary
  application and gets plain `Ctrl+C`. That is the deliberate direction to
  fail in: a missed terminal means a keystroke that does not copy, while a
  false positive means sending a chord the target may have bound to something
  else entirely. The lists are in `src/terminal.c`.
- **The colours are fixed**, a white background with grey buttons, and do not
  follow the Windows light/dark app theme. Dark mode in Win32 is not something
  that falls out of using native controls — it needs per-control
  `DarkMode_Explorer` theming and theme-aware colours in place of the fixed
  ones.
- **There is no global hotkey.** The widget has to be clicked, which means
  reaching for the mouse.
- **The clipboard panel shows text only**, and the first 512 characters of it.
  A copied image or file list leaves the previous text in place.

## Licence

Free for personal, non-commercial use. You may use, study and modify this
software for yourself, but you may not use it commercially, and you may not
share or publish it, in its original form or modified. See [LICENSE](LICENSE)
for the full terms.
