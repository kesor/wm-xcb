# Configuration

*Written 2026-11-03.*

The window manager reads one text file at startup. A `bind` line changes which
keys are bound, and a `float` line changes which windows start out of the
tiled layout, without a rebuild. `tags` lines are read too; what they do not
yet change is under that directive below.

The compiled configuration (`src/config/config.def.h`, `src/config/config.wm.def.h`) stays in place
for a directive the file does not use. A machine with no file at all runs the
compiled one. A `bind` line replaces the compiled bindings, and a `float` line
replaces the compiled float rules: the file is the whole list, not an addition
to it.

## Where the file is

The path is the first of these whose variable is set and non-empty:

1. `$WM_CONFIG`
2. `$XDG_CONFIG_HOME/wm/config`, when `$XDG_CONFIG_HOME` is set
3. `$HOME/.config/wm/config`, when `$HOME` is set

A variable that is set wins even when that path names no file. The WM does not
fall through to the next one; a missing file means the compiled configuration.

The file is read before the components start, and read again on SIGHUP. The
first read decides which path the WM uses; a reload reads the same path, even
when no file was there at startup. See "Reloading without a restart" below.

## Default bindings

A machine with no file gets these, wired in `src/config/config.wm.def.h`. The primary
modifier is `Mod`, Super/Windows.

| Chord | Action | What it does |
|-------|--------|--------------|
| `Mod+Return` | `terminal.spawn` | starts a terminal |
| `Mod+Shift+Return` | `focus.focus-prev` | focuses the previous client |
| `Mod+p` | `app.launch` | opens dmenu, runs the line you pick |
| `Mod+1` … `Mod+9` | `tag-manager.view` | shows tag 1 to 9 |
| `Mod+Shift+1` … `Mod+Shift+9` | `tag-manager.toggle` | adds or removes a tag from the view |
| `Mod+Shift+Mod5+1` … `…+9` | `tag-manager.client-move` | moves the focused client onto tag 1 to 9, replacing the tags it was on |
| `Mod+f` | `fullscreen.toggle` | fullscreen for the focused client, and back |
| `Mod+Shift+space` | `floating.toggle` | takes the focused client out of the layout, and back |
| `Mod+i` | `monitor.tile` | tiles the monitor again |

`tag-manager.client-move` replaces the client's tag set with the named tag, the
way dwm's tag function does. Moving a client to another tag takes it off the
view it was on; moving it again leaves it on the new tag only. A binding that
adds a tag without removing the others is not wired.

A command set with `terminal_set_command()` is used as given, and is not looked
up on `$PATH`. Otherwise `terminal.spawn` runs the first of these that this
machine has installed: `$TERMINAL`, `x-terminal-emulator`, then `alacritty`,
`kitty`, `foot`, `urxvt`, `st` and `xterm`. The program is looked up on
`$PATH`, so `$TERMINAL="alacritty -e tmux"` is used when alacritty is installed
and skipped when it is not. With none of them installed the WM
prints one line at startup naming every name it tried, and `Mod+Return` reports
the same line instead of forking a shell.

## Format

One directive per line. A line starting with `#` is a comment, and so is
everything after a `#` outside quotes. Blank lines are ignored. Fields are
separated by whitespace; double quotes keep the spaces in a field.

```
# the keys this machine uses
bind Mod+Return   terminal.spawn
bind Mod+F5       tag-manager.view 2
bind Mod+Shift+F6 tag-manager.client-move 3

float class=Pavucontrol
float title=" - Preferences"

tags 1 2 3 4 dev chat
```

### `bind <chord> <action> [argument]`

Binds a key chord to an action from the action registry. The action names are
the ones a component registers, for example `tag-manager.view`,
`tag-manager.toggle`, `tag-manager.client-move`, `fullscreen.toggle`,
`floating.toggle`, `monitor.tile`, `focus.focus-prev`, `app.launch`,
`terminal.spawn`. The
`[argument]` is a number the action receives; the tag actions take the tag
number, 1-based.

The chord is a key plus at least one modifier, joined with `+`:

| Modifier | Means |
|----------|-------|
| `Mod`, `Mod4`, `Super`, `Win` | the primary modifier, Super/Windows |
| `Shift` | Shift |
| `Ctrl`, `Control` | Control |
| `Alt`, `Mod1` | Alt |
| `Mod2`, `Mod3`, `Mod5` | the other modifier bits |

A key is a single printable character (`1`, `f`, `/`) or one of the names
`Return`, `Enter`, `space`, `Tab`, `Escape`, `BackSpace`, `Delete`, `Insert`,
`Home`, `End`, `Page_Up`, `Page_Down`, `Left`, `Right`, `Up`, `Down`, `Print`,
`Pause`, `Menu`, `F1` to `F12`. Names are not case-sensitive, and a capital
letter names the same key as the lowercase one: write `Shift` when the chord
needs it.

A chord with no modifier is rejected. A grab on a plain key takes that key from
every client, so `bind Return terminal.spawn` would stop every application from
seeing Return.

### `float class=<name>`, `float instance=<name>`, `float title=<text>`

Windows that match the rule start floating instead of tiled. `class` and
`instance` are substrings of `WM_CLASS`, `title` a substring of the window
title. Fields on one line all have to match; a field left out does not take
part. Quote a value that has spaces in it.

A file with any `float` line replaces the compiled float rules, including the
one for class `steam` and the ones whose title contains ` - Preferences` or
` - Settings`. A file with none leaves those compiled rules in place.

The rule is matched once, when the window is managed, against the WM_CLASS and
WM_NAME the WM reads from the server for that window. A window it names never
enters the layout: it keeps the size and the position it asked for, and keeps
that rectangle while tags are switched. Every other window is tiled as before.

A window the WM adopted at startup goes through the same manage path, so a
rule names it too. Moving a window that is already managed is what
`MOD+Shift+space` is for; a reload does not re-decide it.

### `tags <name> ...`

Names the tags in order: the first name is tag 1, the second tag 2, up to nine.
A tag with no name keeps the number the user types. Quoted names may contain
spaces. A second `tags` line replaces the first and warns about it.

The names reach a panel through `_NET_DESKTOP_NAMES`: a tag with no name is
published as the number the user types. The nine tags exist from startup: the
tag-manager component creates them, `tag_get_name()` answers the name the file
gave that tag and the number for one it left unnamed, and a reload renames them
where they stand, so a tag keeps its identity and only its name changes. There
is still no bar to show a name on.

## What the file does not decide yet

- gaps and border widths and colours are compiled in
- the tag a particular application lands on (`TagRule` in `src/config/config.def.h`)

## Reloading without a restart

`kill -HUP <wm pid>` rereads the file the WM started with. A clean read replaces
the bindings, the float rules and the tag names, and grabs the keys again on the
root window, so a `bind` line an edit adds or removes works without a restart.
Nothing else changes: every client keeps its tags, its focus and its geometry,
because no client is recreated.

The parser reads the whole file before anything is replaced. One line it cannot
use refuses the reload: the running configuration stays in place, and one ERROR
names the line to fix.

```
ERROR: config $HOME/.config/wm/config:4: unknown directive 'bogus': use bind, float or tags (and 2 more rejected lines); the running configuration is unchanged
```

A reload that applies says what it took, on stdout:

```
INFO:  config $HOME/.config/wm/config: reloaded N binding(s), N float rule(s), tag names
```

The binding count is what reached the table, not what the file named: a bind line
the table refused is reported in its own ERROR and is not counted here.

Removing the file and sending SIGHUP brings the compiled configuration back. A
file that cannot be read is treated the way it is at startup: the WM logs why
and the compiled configuration applies.

A reload does not move a window that is already managed. The new float rules are
what the next window the WM manages is matched against.

## When the file says something wrong

A line the parser cannot read is reported once, at startup, on stderr:

```
ERROR: config $HOME/.config/wm/config:4: unknown directive 'bogus': use bind, float or tags
```

Past the errors the parser keeps, one more line says how many further lines
were rejected, without their numbers.

The line number is the one to open the file at. The line is skipped and the rest
of the file still applies, so one typo costs one line, not the configuration.

Two fallbacks keep a mistake from leaving the WM without keys:

- a file with no `bind` line leaves every compiled binding in place, so adding
  one `float` rule does not unbind every key
- a file whose bindings all fail (no modifier, unknown key, a key this keyboard
  cannot produce) also leaves the compiled bindings in place, and says so

A `bind` line the parser accepts can still be refused by the binding table it is
registered into, and that message names the line and which of the two causes it
was, because they need different edits:

```
ERROR: config $HOME/.config/wm/config:33: cannot bind 'F5' to 'tag-manager.view': the chord is already bound to 'fullscreen.toggle'
```

The second cause reads `the table holds all <n> bindings`. It cannot happen to a
file: the parser keeps no more bind lines than the table holds, so the line past
the limit is rejected as `more than <n> bindings` with its number, and
`src/config/config.c` fails the build when the two numbers disagree. The first
one is a chord the file binds twice; change or remove one of the two lines.

The table is the table, not the compiled set: a file that names a binding the
compiled bindings do not have, or more of them than `src/config/config.wm.def.h`
wires, has all of them, and `tests/x/test-58-bind-table-capacity.sh` starts a WM
on such a file to prove it.

When the file does name bindings, they **replace** the compiled ones rather than
adding to them. That is what makes a key removable: `MOD+2` does nothing in a
session whose file does not bind it.

A binding that names an action no component registered is reported once at
startup, by the same check that reports a compiled binding naming one: the line
names the chord and the action, not the line of the file it came from.

```
ERROR: keybinding: Mod+keycode 41 is bound to action 'nope.nothing', which no component registered; the key does nothing
```
