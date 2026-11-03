# -*- coding: utf-8 -*-
"""Arabic for the descriptions: the "Arabic EID" mod made into an EID language.

The mod (other stuff/arabic-eid/Arabic EID, from the owner 26.10.2026) is not a
language pack. It is a Lua mod for EID 5.03 that overwrites EID's ENGLISH tables
at runtime: every row is either an item (id, name, description) or a leaf of
EID.descriptions.en_us named by its path - "n:1/s:name" is [1]["name"]. Its
text is already shaped (Arabic presentation forms) and laid in visual order for
EID's left-to-right renderer.

So the language is made the way the mod makes it: the English files of the EID
copy the port is built from (eid-mod/descriptions/{ab+,rep}/en_us.lua) with
each string the mod names replaced by its Arabic, everything else left English
- which is also what the mod shows for a line it has no translation for. The
result is an ordinary language pair, descriptions/{ab+,rep}/ar.lua, in a copy of
the mod folder (work/eid-ar), and the port's own tools build the sections from
it exactly as for every other language:

    py -3 tools/eid_arabic.py                 # writes work/eid-ar, says what it matched
    py -3 tools/eid_arabic.py --into-bin      # and lays the "ar" block into assets/eid.bin

--into-bin works on a copy of the working eid.bin: the "ar" block is added there
(a copy of en_us with the Arabic rows), eid_more.py and eid_wisps.py build its
other sections from work/eid-ar, and only that one block is then carried into
the working file. Every other language stays byte for byte in meaning.
"""
import io
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import eid_more                                     # noqa: E402
from eid_more import strip_comments, read_bin, write_bin   # noqa: E402

ROOT = os.path.dirname(HERE)
MOD_SRC = os.path.join(ROOT, 'other stuff', 'arabic-eid', 'Arabic EID', 'main.lua')
EID_MOD = os.path.join(ROOT, 'eid-mod')
OUT_MOD = os.path.join(ROOT, 'work', 'eid-ar')
WORKING_BIN = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'eid.bin')
CODE = 'ar'
FOLDERS = ('ab+', 'rep')
# the item tables: rows {"id", "name", "description"}, found by their first string
ITEM_TABLES = ('collectibles', 'trinkets', 'cards', 'pills', 'horsepills')
ROW_TABLES = ('sacrifice', 'dice')                  # rows {"id", "", "description"}


# ------------------------------------------------------------ a Lua reader
class Str(object):
    def __init__(self, value, start, end):
        self.value, self.start, self.end = value, start, end


class Table(object):
    def __init__(self, items):
        self.items = items                          # [(key, value)], key ('n', k) or ('s', name)

    def get(self, key):
        for k, v in self.items:
            if k == key:
                return v
        return None


class Other(object):
    pass


def skip(text, i):
    while i < len(text) and text[i] in ' \t\r\n':
        i += 1
    return i


def string_at(text, i):
    quote = text[i]
    j = i + 1
    buf = []
    while j < len(text) and text[j] != quote:
        if text[j] == '\\' and j + 1 < len(text):
            buf.append({'n': '\n', 't': '\t'}.get(text[j + 1], text[j + 1]))
            j += 2
            continue
        buf.append(text[j])
        j += 1
    return Str(''.join(buf), i, j + 1), j + 1


def expression_end(text, i):
    """past the rest of an expression the reader does not take apart"""
    depth = 0
    while i < len(text):
        c = text[i]
        if c in '"\'':
            i = string_at(text, i)[1]
            continue
        if c in '({[':
            depth += 1
        elif c in ')}]':
            if depth == 0:
                return i
            depth -= 1
        elif c in ',;' and depth == 0:
            return i
        elif c == '\n' and depth == 0 and text[i + 1:i + 2] == 'E':
            return i                                # the next statement
        i += 1
    return i


def value_at(text, i):
    i = skip(text, i)
    c = text[i]
    if c in '"\'':
        v, j = string_at(text, i)
    elif c == '{':
        items, n = [], 0
        j = i + 1
        while True:
            j = skip(text, j)
            if text[j] == '}':
                j += 1
                break
            key = None
            if text[j] == '[':
                k, j = value_at(text, j + 1)
                j = skip(text, j)
                assert text[j] == ']', text[j - 40:j + 40]
                j = skip(text, j + 1)
                assert text[j] == '=', text[j - 40:j + 40]
                j += 1
                if isinstance(k, Str):
                    key = ('s', k.value)
                elif isinstance(k, Other) and getattr(k, 'number', None) is not None:
                    key = ('n', k.number)
                else:
                    key = ('?', None)
            else:
                m = re.match(r'([A-Za-z_]\w*)\s*=(?!=)', text[j:])
                if m:
                    key = ('s', m.group(1))
                    j += m.end()
            val, j = value_at(text, j)
            if key is None:
                n += 1
                key = ('n', n)
            items.append((key, val))
            j = skip(text, j)
            if text[j] in ',;':
                j += 1
        v = Table(items)
    else:
        m = re.match(r'-?\d+(?:\.\d+)?', text[i:])
        v = Other()
        if m and '.' not in m.group(0):
            v.number = int(m.group(0))
        j = expression_end(text, i)
        return v, j
    # a value followed by more of an expression ("a" .. b) is not a plain value
    k = skip(text, j)
    if text[k:k + 2] == '..':
        return Other(), expression_end(text, k)
    return v, j


def assignments(text):
    """[(category, (index, ...), value)] in file order: every
    EID.descriptions[languageCode].X = ..., and every local table the file lays
    over one with EID:updateDescriptionsViaTable(local, EID.descriptions[languageCode].X)"""
    out = []
    merged = dict(re.findall(r'updateDescriptionsViaTable\((\w+),\s*EID\.descriptions\[languageCode\]\.(\w+)\)', text))
    pattern = r'^(?:EID\.descriptions\[languageCode\]\.(\w+)((?:\[\d+\])*)|local (\w+))\s*=\s*'
    for m in re.finditer(pattern, text, re.M):
        if text[m.end():m.end() + 8] == 'function':
            continue
        if m.group(3):
            if m.group(3) not in merged or text[m.end()] != '{':
                continue
            cat, idx = merged[m.group(3)], ()
        else:
            cat = m.group(1)
            idx = tuple(('n', int(x)) for x in re.findall(r'\[(\d+)\]', m.group(2)))
        val, _ = value_at(text, m.end())
        out.append((cat, idx, val))
    return out


def leaf(tables, category, path):
    """the string a path names, through the last assignment that holds it"""
    for cat, idx, val in reversed(tables):
        if cat != category or tuple(path[:len(idx)]) != idx:
            continue
        node = val
        for step in path[len(idx):]:
            if not isinstance(node, Table):
                node = None
                break
            node = node.get(step)
        if isinstance(node, Str):
            return node
    return None


# ------------------------------------------------------------ the mod's rows
def mod_rows():
    text = io.open(MOD_SRC, encoding='utf-8').read()
    m = re.search(r'local translations = ', text)
    val, _ = value_at(text, m.end())
    out = {}
    for key, part in val.items:
        out[key[1]] = part
    return out


def plain(row):
    return dict((k[1], v.value if isinstance(v, Str) else getattr(v, 'number', None)) for k, v in row.items)


def path_of(text):
    parts = []
    for part in text.split('/'):
        if re.match(r'[sn]:', part) or not parts:
            parts.append(part)
        else:
            parts[-1] += '/' + part                 # a key with a slash in it: "s:20/20"
    steps = []
    for part in parts:
        kind, value = part.split(':', 1)
        steps.append(('n', int(value)) if kind == 'n' else ('s', value))
    return tuple(steps)


def lua_quote(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'


# ------------------------------------------------------------ the language pair
def make_pair():
    rows = mod_rows()
    texts = {}
    tables = {}
    for folder in FOLDERS:
        raw = io.open(os.path.join(EID_MOD, 'descriptions', folder, 'en_us.lua'), encoding='utf-8').read()
        texts[folder] = strip_comments(raw)
        tables[folder] = assignments(texts[folder])
    edits = {f: {} for f in FOLDERS}               # start -> (end, new)
    missed = []

    def put(spot, folder, new):
        edits[folder][spot.start] = (spot.end, lua_quote(new))

    def find_leaf(category, path):
        for folder in ('rep', 'ab+'):             # rep is laid over ab+ by the mod
            s = leaf(tables[folder], category, path)
            if s is not None:
                return folder, s
        return None, None

    def item_tables(category):
        for folder in ('rep', 'ab+'):
            for cat, idx, val in reversed(tables[folder]):
                if cat == category and not idx and isinstance(val, Table):
                    yield folder, val

    counts = {}
    # the items: the last row of an id wins (the mod adds them in order)
    for table in ITEM_TABLES + ROW_TABLES:
        last = {}
        for _, row in rows[table].items:
            r = plain(row)
            if r['layer'] == 'rep+':
                continue
            last[r['id']] = r
        done = 0
        for ident, r in sorted(last.items()):
            hit = None
            for folder, t in item_tables(table):
                for _, entry in t.items:
                    if isinstance(entry, Table) and isinstance(entry.get(('n', 1)), Str) \
                            and entry.get(('n', 1)).value == str(ident):
                        hit = (folder, entry)
                if hit:
                    break
            if not hit:
                missed.append('%s %s' % (table, ident))
                continue
            folder, entry = hit
            if table in ITEM_TABLES and r.get('name'):
                put(entry.get(('n', 2)), folder, r['name'])
            put(entry.get(('n', 3)), folder, r['desc'])
            done += 1
        counts[table] = done
    # transformation names: the mod's list counts from the first real one (Guppy)
    done = 0
    for i, (_, name) in enumerate(rows['transformations'].items):
        folder, s = find_leaf('transformations', (('n', i + 2),))
        if s is None:
            missed.append('transformation %d' % i)
            continue
        put(s, folder, name.value)
        done += 1
    counts['transformations'] = done
    # every other leaf, by its path
    done = 0
    for _, row in rows['auxiliary'].items:
        r = plain(row)
        if r['layer'] == 'rep+':
            continue
        path = path_of(r['path']) if r['path'] else ()
        folder, s = find_leaf(r['category'], path)
        if s is None:
            missed.append('%s %s' % (r['category'], r['path']))
            continue
        put(s, folder, r['text'])
        done += 1
    counts['auxiliary'] = done

    os.makedirs(OUT_MOD, exist_ok=True)
    for folder in FOLDERS:
        t = texts[folder]
        out, at = [], 0
        for start in sorted(edits[folder]):
            end, new = edits[folder][start]
            out.append(t[at:start])
            out.append(new)
            at = end
        out.append(t[at:])
        t = ''.join(out).replace('local languageCode = "en_us"', 'local languageCode = "%s"' % CODE, 1)
        d = os.path.join(OUT_MOD, 'descriptions', folder)
        os.makedirs(d, exist_ok=True)
        io.open(os.path.join(d, CODE + '.lua'), 'w', encoding='utf-8', newline='\n').write(t)
        shutil.copyfile(os.path.join(EID_MOD, 'descriptions', folder, 'en_us.lua'), os.path.join(d, 'en_us.lua'))
    os.makedirs(os.path.join(OUT_MOD, 'features'), exist_ok=True)
    for f in ('eid_data.lua', 'eid_xmldata.lua'):
        shutil.copyfile(os.path.join(EID_MOD, 'features', f), os.path.join(OUT_MOD, 'features', f))
    print('matched:', ', '.join('%s %d' % kv for kv in counts.items()))
    print('not in the EID the port carries (stay English): %d' % len(missed))
    for m in missed:
        print('   ', m)
    return rows


# ------------------------------------------------------------ into eid.bin
def into_bin():
    work = os.path.join(ROOT, 'work', 'eid-ar.bin')
    shutil.copyfile(WORKING_BIN, work)
    blocks, pool, version = read_bin(work)
    blocks = [b for b in blocks if b[0] != CODE]
    known = {}
    at = 0
    while at < len(pool):
        end = pool.find(b'\0', at)
        known[bytes(pool[at:end])] = at
        at = end + 1

    def put(s):
        b = s.encode('utf-8')
        if b not in known:
            known[b] = len(pool)
            pool.extend(b)
            pool.append(0)
        return known[b]

    english = dict(blocks)[eid_more.FALLBACK]
    # sections 1..7 from the pair: ab+ first, then rep and its local tables over it
    pair = []
    for folder in FOLDERS:
        pair += assignments(io.open(os.path.join(OUT_MOD, 'descriptions', folder, CODE + '.lua'),
                                    encoding='utf-8').read())
    kinds = {'collectibles': 1, 'trinkets': 2, 'cards': 3, 'pills': 4, 'horsepills': 5,
             'dice': 6, 'sacrifice': 7}
    ours = []
    for kind, rows in english:
        name = [k for k, v in kinds.items() if v == kind]
        if not name:
            ours.append((kind, rows))
            continue
        said = {}
        for cat, idx, val in pair:
            if cat != name[0] or idx or not isinstance(val, Table):
                continue
            for _, entry in val.items:
                parts = [entry.get(('n', j)) for j in (1, 2, 3)] if isinstance(entry, Table) else []
                if all(isinstance(p, Str) for p in parts) and parts[0].value.lstrip('-').isdigit():
                    # EID's pill rows say the PillEffect (Bad Gas "0"); the database counts
                    # them from 1, as the game's lookup does (effect + 1, eid_pack)
                    shift = 1 if kind in (4, 5) else 0
                    said[int(parts[0].value) + shift] = (parts[1].value, parts[2].value)
        new = []
        for ident, n, t in rows:
            got = said.get(ident)
            new.append((ident, put(got[0]) if got and got[0] else n, put(got[1]) if got else t))
        ours.append((kind, new))
    blocks.append((CODE, ours))
    blocks.sort(key=lambda b: b[0])
    write_bin(work, blocks, pool, version)
    # the other sections, by the port's own tools, from the pair
    eid_more.main(OUT_MOD, work)
    import eid_wisps
    eid_wisps.main(OUT_MOD, work, os.path.join(ROOT, 'work', 'eid-ar-wisps.h'))
    # and only the Arabic block into the working file
    built, built_pool, _ = read_bin(work)
    base, base_pool, version = read_bin(WORKING_BIN)
    ar = dict(built)[CODE]

    def text(p, off):
        return bytes(p[off:p.find(b'\0', off)])
    known = {}
    at = 0
    while at < len(base_pool):
        end = base_pool.find(b'\0', at)
        known[bytes(base_pool[at:end])] = at
        at = end + 1

    def again(off):
        b = text(built_pool, off)
        if b not in known:
            known[b] = len(base_pool)
            base_pool.extend(b)
            base_pool.append(0)
        return known[b]
    ar = [(kind, [(i, again(n), again(t)) for i, n, t in rows]) for kind, rows in ar]
    base = [b for b in base if b[0] != CODE] + [(CODE, ar)]
    base.sort(key=lambda b: b[0])
    keep = os.path.join(ROOT, 'work', 'eid.bin.before-arabic')     # not beside it: assets/ ships
    if not os.path.exists(keep):
        shutil.copyfile(WORKING_BIN, keep)
    size = write_bin(WORKING_BIN, base, base_pool, version)
    print('%s: "%s" laid in, %d sections, %.2f MB' % (WORKING_BIN, CODE, len(ar), size / 1048576.0))


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    make_pair()
    if '--into-bin' in sys.argv:
        into_bin()
