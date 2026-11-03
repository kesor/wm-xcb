# Configuration

*Written 2026-11-03.*

The window manager reads one text file at startup. It changes which keys are
bound, which windows float, and what the tags are called, without a rebuild.

The compiled configuration (`config.def.h`, `config.wm.def.h`) stays in place
wherever the file says nothing. A machine with no file at all runs the compiled
one.

## Where the file is

The first of these that exists is used:

1. `$WM_CONFIG`, when the variable is set to a path
2. `$XDG_CONFIG_HOME/wm/config`
3. `$HOME/.config/wm/config`

The file is read once, before the components start. A change takes effect on the
next start; the running WM does not reread it. Reloading without a restart is
not implemented yet.

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
`monitor.tile`, `focus.focus-prev`, `app.launch`, `terminal.spawn`. The
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

These rules are what `config_should_float()` answers with. Nothing in the
manage path asks the question yet, so a `float` line currently changes no
window's placement; it is read, checked, and kept for the code that will.

### `tags <name> ...`

Names the tags in order: the first name is tag 1, the second tag 2, up to nine.
A tag with no name keeps the number the user types. Quoted names may contain
spaces. A second `tags` line replaces the first and warns about it.

`tag_get_name()` returns the name for a tag that exists. Two things are missing
before a name is visible: nothing creates the tag list at startup today
(`tag_list_init()` is called only from the tests), and there is no bar to show a
name on.

## What the file does not decide yet

- gaps and border widths and colours are compiled in
- the tag a particular application lands on (`TagRule` in `config.def.h`)
- reloading without a restart

## When the file says something wrong

Every line the parser cannot read is reported once, at startup, on stderr:

```
ERROR: config $HOME/.config/wm/config:4: unknown directive 'bogus': use bind, float or tags
```

The line number is the one to open the file at. The line is skipped and the rest
of the file still applies, so one typo costs one line, not the configuration.

Two fallbacks keep a mistake from leaving the WM without keys:

- a file with no `bind` line leaves every compiled binding in place, so adding
  one `float` rule does not unbind every key
- a file whose bindings all fail (no modifier, unknown key, a key this keyboard
  cannot produce) also leaves the compiled bindings in place, and says so

When the file does name bindings, they **replace** the compiled ones rather than
adding to them. That is what makes a key removable: `MOD+2` does nothing in a
session whose file does not bind it.

A binding that names an action nothing registered is still silent. Startup does
not check the name against the action registry yet.
