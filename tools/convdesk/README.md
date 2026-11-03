# convdesk — converter parity (04.10.2026)

A reference for what the PC mod converter must produce, and a check of the third converter
(`ModPort.java` in the installer) against it. The reference is `tools/modport.py`; its web twin
`modport.js` is run alongside it, and any disagreement between it and Python is recorded in the reference.

## Contents

| file | what |
|---|---|
| `corpus.py` | the corpus, how input is fed, how output is described (shared by both scripts) |
| `make_expected.py` | runs Python and the page (node), writes `expected/<label>.json` |
| `compare.py` | checks the third converter's output against the reference, prints the exact difference |
| `expected/<label>.json` | reference per mod; `expected/_index.json` — summary and tool hashes |

**Corpus — 29 mods:** 25 Workshop folders (`label` = Workshop ID), `reflashed-ported`
(`other stuff/usermods/ported/reflashed.zip` — already converted on 28.09: a second pass),
`example-mod` (`example-mod.zip`, our own format — the converter writes nothing), `synthetic`
(the synthetic mod from the `synthetic()` function in `tools/umdesk/test_converters.py`: rules
that no Workshop mod reaches — merging an old table, a bare `&`, a repeated attribute,
a room from a different version, palette and interlaced images, an image wider than 4096).
`cooler-3361519802` (hotfix 1.71, 05.10.2026) — Cooler Completion Marks from
`reports/2026-10-05/hotfix171/`, PC layout: a folder without the `mod.json` that the player added
(corpus kind `pcfolder`); its own `completion_widget.anm2` and 393×500 sheets for it — stay in the mod as they are.

**Every converter gets the same zip as input.** A Workshop folder is packed once,
deterministically (names in order, no compression, a single date) into `%TEMP%/convdesk/inputs/<label>.zip`;
a zip from the corpus is fed as is. The reference holds `input_digest` (sha256 of the input contents):
`compare.py --run` warns if the input is no longer the same.

## Reference: `expected/<label>.json`

```
label, title, source, input, input_digest
tools        sha256 of modport.py, modport.js, gameref.bin, modport_node.js at the time of capture
zip          whether a zip was written (script mod: no, report only)
id           id from mod.json
report       Python report.txt line by line (split('\n'), the last element is an empty string)
files        {name: size, sha256, and by kind:
                .png        wh [w,h], pixels = sha256(b"<w>x<h>" + RGBA)      (PIL convert('RGBA'))
                .xml/.anm2  tree = sha256 of the tree (tag, attributes in order, text/tail with whitespace stripped)
                mod.json    json — the value itself}
js           {agrees, differences} — the page against Python
```

## Comparison rules (`compare.py`)

1. zip present or absent;
2. report — line for line (unified diff);
3. file names — exact, case-sensitive (with a hint if they match case-insensitively);
4. per file: `mod.json` — by value, key by key; `.png` — by RGBA pixels (PNG encoders
   differ); `.xml/.anm2` — **by bytes** (Python and Java write identically), with `--tree` — by
   tree (for the page's serializer); everything else — by bytes.

If Python's output from `make_expected.py` is at hand (`%TEMP%/convdesk/py/<label>.zip`, and the file
in it is still the one in the reference), the difference is exact: how many pixels, where, by how much, the first
differing pixels; for xml — the first differing elements by path
(`AnimatedActor/Animations/Animation[3]/…`, `-` reference, `+` output); for anything else — the first
differing byte. Without it — only the fact, by hash and size. Exit code 0 — everything matched.

## What is needed from Java: CLI

Already exists and fits as is — `tools/umdesk/java/ru/isaac/port/ConvertDesk.java`:

```
ConvertDesk <gameref.bin> <mod zip> <out zip>
```

- writes `<out zip>` (writes nothing if the mod has nothing for the port) and **always**
  `<out zip>.report.txt` — the report text exactly as `rep.text()` in `modport.py`, UTF-8, `\n`;
- `mod.json` — the `modport.py` card (without the installer label: `label = null`);
- exit code 0 also when no zip is written; non-zero — only on failure.

Run over the whole corpus and compare with one command (classes as in `test_converters.py`):

```bash
J="C:/Program Files/Android/Android Studio/jbr/bin"
AJ="$(cygpath -m "${ANDROID_HOME:-$LOCALAPPDATA/Android/Sdk}")/platforms/android-35/android.jar"
CL="$TEMP/convdesk/jclasses"; mkdir -p "$CL"
"$J/javac.exe" -Xlint:none -encoding UTF-8 -d "$CL" -cp "$AJ" \
  tools/umdesk/java/org/json/*.java port/app/src/main/java/ru/isaac/port/{UserMods,ModPort,ModPortXml,ModPortPng}.java \
  tools/umdesk/java/ru/isaac/port/ConvertDesk.java
py -3 tools/convdesk/compare.py --run "'$J/java.exe' -Xmx96m -cp '$CL;$AJ' ru.isaac.port.ConvertDesk \
  'other stuff/usermods/modport-web/src/gameref.bin' {in} {out}"
```

`{in}` — the input from the corpus, `{out}` — `%TEMP%/convdesk/third/<label>.zip`. The command string
is parsed as in a POSIX shell: paths with spaces in single quotes, forward slashes.
`--only <label> …` — part of the corpus; `--max N` — length of the difference block.

Other modes:

```bash
py -3 tools/convdesk/compare.py <label> <out.zip | folder>      # one output (report — <out>.report.txt or --report)
py -3 tools/convdesk/compare.py --dir <folder>                   # ready-made <label>.zip (+ .report.txt) across the whole corpus
py -3 tools/convdesk/compare.py <label> <mods/<folder>> --adapted   # a mod installed by the installer
```

`--adapted`: the output is what the installer itself put in place (`ModPort.convert` with the label
`UserMods.adaptedLabel`): the description in `mod.json` is `{en, ru, de, es: label + "\n\n" + description}`,
which is not a difference; the report is taken from `<folder>/modport-report.txt` and is not counted among the mod's files.

## When to recapture the reference

After changing the rules in `modport.py`/`modport.js` or `gameref.bin` (`modport_ref.py`):

```bash
py -3 tools/convdesk/make_expected.py            # whole corpus, ~3 min; --only <label> …; --no-run
```

`compare.py` itself says `NOTE: … changed since expected/ was made` if the tool hashes
no longer match. `make_expected.py` returns 1 if the page and Python disagreed anywhere —
that is a converter bug; the disagreements are in the reference's `js.differences`.

## Snapshot 04.10.2026, 23:30–23:40

- Python and the page: **28 of 28 matched** (reports line for line, files, pixels, trees).
- `reflashed-ported` and `2934904934` give the same contents for 5329 files (a second pass over
  the already converted Reflashed changes nothing; the reports differ — as they should).
- Java (`ConvertDesk`, `ModPort.java` from the same hour) via `--run`: **28 of 28 matched**, xml
  byte for byte; images — by pixels (18 Reflashed files have different PNG bytes).
