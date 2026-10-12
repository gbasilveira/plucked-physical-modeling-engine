# Where your instruments, presets and cache live

This page explains where the plug-in keeps its files, how to bring instruments and presets in and out,
and what to do if it reports a damaged file. (The folder name `pme` and the preset file ending
`.pmepreset` are working names and will change with the final product name.)

## Three places

| | What it holds | Can I change it? |
| --- | --- | --- |
| **Factory** | The instruments and presets that come with the plug-in | No. The installer owns it. |
| **User** | Instruments and presets you import or save | Yes. Back it up. |
| **Cache** | Pre-computed body data, so an instrument loads fast the second time | Yes. You can delete it at any time. |

### Windows

- Factory: `C:\ProgramData\pme`
- User: `C:\Users\<you>\AppData\Roaming\pme`
- Cache: `C:\Users\<you>\AppData\Local\pme\Cache`

### macOS

- Factory: `/Library/Application Support/pme`
- User: `~/Library/Application Support/pme`
- Cache: `~/Library/Caches/pme`

### Linux

- Factory: `/usr/share/pme` or `/usr/local/share/pme`
- User: `~/.local/share/pme` (or `$XDG_DATA_HOME/pme` if you set it)
- Cache: `~/.cache/pme` (or `$XDG_CACHE_HOME/pme`)

Inside Factory and User, instruments are in `instruments/<name>/` (a file called `instrument.json` and,
for some instruments, a `blobs` folder with binary body data) and presets are single `.pmepreset` files
in `presets/`.

## Importing an instrument

Choose the instrument's `instrument.json` file. The plug-in checks it, brings an older file up to date,
and stores a clean copy (with the body data files next to it) under your User folder. If you import the
same instrument twice, nothing changes. If a different instrument has the same name, the new one is stored
as `name-2`, `name-3`, and so on. A file with a mistake is refused with a message that says where the
mistake is, and nothing is stored.

## Exporting an instrument

Choose a destination folder. The plug-in writes a folder with the instrument's name containing
`instrument.json` and its body data files. Copy the whole folder to share it. Importing that folder on
another computer gives the same instrument. (There is no single-file archive yet.)

## Presets

A preset remembers which instrument it belongs to and the settings you changed. Importing a preset
copies it into your User folder (older presets are brought up to date); exporting writes a copy wherever
you choose. A preset from a newer version of the plug-in is refused rather than half-loaded. If a preset's
instrument is not installed, the preset still imports, but you cannot load it until you import that
instrument. If the instrument has changed since the preset was saved, the preset loads and you are told.

## Backing up

Copy the User folder. That is everything that is yours. Factory files come back with a reinstall, and the
Cache is rebuilt automatically.

## The cache

The Cache folder only speeds up loading. It is always safe to delete it, completely or in part; the next
load simply takes longer once. The plug-in also keeps it below about 1 GB by removing the entries you
used least recently.

## "Damaged file" messages

- **Instrument: a body data file is missing or does not match** (the message names the file). The file
  was changed, truncated or not copied along with the instrument. Copy the whole instrument folder again
  or reinstall the instrument. The plug-in does not load an instrument with damaged data.
- **Cache entry discarded.** The plug-in found a damaged entry in the Cache, deleted it and rebuilt it.
  You do not need to do anything.
