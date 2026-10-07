# The pattern library

The File menu has two pattern menus:

- **JuggleSim Patterns**: the patterns that come with JuggleSim.
- **My Patterns**: the patterns you've saved, and the ones you've loaded recently.

## Finding a pattern

Patterns are grouped by the number of jugglers, the number of props, and the period, all worked
out from the siteswap, and every group shows how many patterns it holds:
*JuggleSim Patterns > 1 Juggler (59) > 3 Props (14)*.

The menus adapt to how many patterns there are, so you don't click through levels that don't
help:

- A level with only one choice is skipped. If all your patterns are for one juggler, *My
  Patterns* doesn't ask how many jugglers.
- A group of 20 patterns or fewer is listed directly, with headings such as *3 props, period 3*
  instead of more submenus. With a handful of saved patterns, *My Patterns* is a single list.

As a group grows past 20 it gets its own submenus, so expect the layout of *My Patterns* to
change now and then as you save more.

Each menu starts with a **Find** box: type part of a name or siteswap and the matching patterns
are listed right there (under the same headings, so the 3-, 4- and 5-ball Showers can be told
apart).

**Recent**, at the top of *My Patterns*, lists the last 8 patterns you loaded from either menu,
most recent first. Your own patterns are shown in their own color wherever the two are mixed.

Choosing a pattern loads it, exactly as if you'd typed it. **Ctrl+Z** puts back what you had
before, including the props, tempo and dwell if loading changed them. Where a pattern has a
name, its siteswap is shown beside it in grey. Hovering over a pattern that carries its own
props, tempo or dwell shows them.

*2 Jugglers* to *4 Jugglers* in *JuggleSim Patterns* are placeholders for now: they'll fill in
when passing patterns are supported.

## Saving your own patterns

*File > Save to My Patterns...* saves the current pattern under a name you choose (the siteswap,
unless you change it). It's saved together with the current props, tempo and dwell, and loading
it later sets all of them again. If you already have a pattern with that name, you're asked
whether to replace it.

## Managing your patterns

*File > Manage My Patterns...* lists your patterns in a table. Click a column heading to sort by
it. Each row has buttons to:

- **Load** the pattern.
- **Rename** it (press Enter to accept the new name, Esc to keep the old one).
- **Delete** it. You're asked to confirm, and deleting can't be undone.

## Where they're kept

Your patterns are in `my_patterns.txt` in `%APPDATA%\JuggleSim` (the Recent list is next to it,
in `recent_patterns.txt`), one pattern per line:

```
siteswap ; name ; settings
531 ; My clubs 531 ; props=clubs tempo=120 dwell=1.4
```

It's plain text, so you can back it up, copy it to another computer, or edit it by hand
(JuggleSim reads it when it starts). The settings are optional:
`props=balls|clubs|rings`, `tempo=` (beats per minute) and `dwell=` (beats).

A pattern written in notation this version of JuggleSim can't read yet (for example sync
notation, before it's supported) is kept in the file and shown in *Manage My Patterns* (greyed
out), but not offered in the menu.

The patterns that come with JuggleSim are built into the program, so they're updated by
updating JuggleSim.
