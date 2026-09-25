# Default BASIC USB/BLE keyboard mapping

Both USB and BLE keyboards feed the same HID usage-code mapping.

## Main typing

| PC key | Bally BASIC |
|---|---|
| A-Z | matching BASIC letter through GREEN/RED/BLUE prefix |
| 0-9 | matching digit |
| Shift+1 | `!` |
| Shift+2 | `@` |
| Shift+3 | `#` |
| Shift+4 | `$` |
| Shift+5 | `%` |
| Shift+6 | not mapped; `^` is not on the current BASIC overlay |
| Shift+7 | `&` |
| Shift+8 | `*` |
| Shift+9 | `(` |
| Shift+0 | `)` |
| Space | SPACE |
| Enter | GO |
| Shift+Enter | GO+10 |
| Escape | HALT, held until Escape is released |
| Pause | PAUSE |
| Backspace | Left arrow, then ERASE |
| Delete | ERASE |
| - | MINUS |
| + | PLUS |
| = | EQUALS |
| Arrow keys | matching colored arrow symbol |

## WORDS keys

| PC key | BASIC token |
|---|---|
| F1 | FOR |
| F2 | TO |
| F3 | STEP |
| F4 | NEXT |
| F5 | GOSUB |
| F6 | RETURN |
| F7 | RND |
| F8 | IF |
| F9 | CLEAR |
| F10 | LINE |
| F11 | BOX |
| F12 | GOTO |
| Ctrl+F1 | GO+10 |
| Ctrl+F2 | RUN |
| Ctrl+F3 | LIST |
| Ctrl+F4 | INPUT |
| Ctrl+F5 | PRINT |

## Numeric keypad operators

| PC key | BASIC function |
|---|---|
| Numpad / | DIVIDE |
| Numpad * | MULTIPLY |
| Numpad - | MINUS |
| Numpad + | PLUS |
| Numpad Enter | GO |
| Numpad decimal | BASIC period character |
| Numpad 0-9 | matching digit |

The main-keyboard `/` and `Shift+8` remain available as the colored literal
`/` and `*` characters shown on the BASIC overlay.
