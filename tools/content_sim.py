# -*- coding: utf-8 -*-
"""The reference model of content.c: how a data table of Repentance is built
from the game's file and the enabled mods (plan 30.09, 4.9 M3/M4, 4.10.3).

This file is the SPEC. The C code (content.c content_build) is checked against
it byte for byte: same input files -> same output bytes and the same report
lines. Everything here is deterministic; nothing depends on dict order of
Python, the clock or the file system order.

------------------------------------------------------------------ the order
For one file F (data/*.xml of repentance-resources, or stringtable.sta):

  1. BASE. The strongest enabled mod (top of MODS) that brings a whole F in
     resources/ (a "legacy" file, as on PC) gives the base, otherwise the
     game's file. A legacy file that does not read as XML, or that has a
     record outside the loader's table (BOUNDS below), is refused - the next
     weaker legacy file is tried, then the game's. Two or more legacy files:
     the top one wins and every other gets a "not used" line (4.10.3).
  1a. VERSION (04.10.2026). Each legacy candidate, before its refusal check,
     is merged three ways against the other version's own table
     (Repentance+'s, OTHER): what of it is Repentance+'s and not the game's
     goes back to the game's; the mod's own changes stay (_version).
  2. COMPLETION. A legacy base gets the game's top-level records it lacks
     (by the file's key), each written before its root's end tag - the guard
     of 02.10 (usermods.c guard_table), now with the key table below.
  3. CONTENT. content/F of every enabled mod, strongest first: new records
     where the table allows it (today only entities2.xml: a new variant of a
     type the game has; the strongest mod's entry wins on a shared key).
  4. PATCH. patch/F of every enabled mod, weakest first, each on top of what
     is there: the strongest wins on every attribute. A record the base has
     not got is refused; if the game's own file has it, the line says the
     legacy base of <mod> took it away ("overridden by legacy <mod>").
  5. PORT. The port's own additions to F (M8), "fill" mode: only what is
     empty or missing is filled (today: stringtable.sta keys, see
     port_additions.py).

  Why patches go over a stronger legacy file (and not "a legacy file of mod
  k wipes the patches of weaker mods"): the owner, 01.10 (4.10.3): "a whole
  file is a REPLACEMENT; replacement + append -> the append lies on top of
  the replaced one". The audit of 02.10 states the same chain.

  No mod touches F (no legacy, no content, no patch) -> the output IS the
  game's file, byte for byte (verified for every file by --verify).

------------------------------------------------------------- patch/ syntax
A patch file has the game file's name and root; inside, records with their
key attributes and only what changes:

  <players>
    <player id="13" items-add="412"/>                 add to a list
    <player id="31" portrait="null.png"/>             set (replace in place)
    <player id="3" unset="costume"/>                  remove attributes
  </players>
  <ItemPools>
    <Pool Name="treasure">
      <Item Id="1" Weight="0.5"/>                     change a pool entry
      <Item Id="118" Weight="1" DecreaseBy="1" RemoveOn="0.1"/>  new entry
      <Item Id="7" remove="true"/>                    remove an entry
    </Pool>
  </ItemPools>

  - key attributes select the record (numbers compare as numbers: "07"=="7");
    they are never changed; costumes2 is keyed by type+id (type default
    "none"; passive/active/familiar are one id space), fxParams params by
    stage+altStages+backdrop (defaults ""), entities by id+variant+subtype;
  - a key the base has twice (the game's own items.xml has null 124 twice):
    every such record gets the change; several patch elements of one file
    naming one record: their operations in document order, as one element;
  - <attr>="v"          set; every occurrence of attr in the record gets v
                        (a base that already has the attribute twice keeps
                        both, both get v; nothing new is ever duplicated);
  - <attr>-add="a,b"    list attribute: append; "multiset" lists (item
                        numbers: startingitems, items...) take duplicates as
                        given, "set" lists (tags, cache) skip present values;
  - <attr>-remove="a,b" list attribute: a multiset loses the first
                        occurrence of each value, a set every occurrence;
  - unset="a b"         the attributes are removed;
  - remove="true"       the element is removed - only where the table says
                        removable (pool entries);
  - within one element the order is: remove, unset, set, -remove, -add;
  - a patch never adds or removes a top-level record (fixed tables keep their
    count; costumes2/music keep theirs - Specialist Dance counts them);
    children are added only where the table says addable (pool entries,
    stringtable keys);
  - unknown attribute (not read by the loader and not in the game's file):
    applied, with a warning line;
  - a value that names another table's record (an item in startingitems...)
    must exist in the game's table, or the attribute change is refused.

  stringtable.sta patch:
  <stringtable>
    <category name="Default">
      <key name="MY_KEY">
        <string lang="English">Text</string>    by language name (or its id)
        <string>Text</string>                   or by position (English first)
      </key>
    </category>
  </stringtable>
  A key the category has not got is added (keys are looked up by name);
  a new category is refused.

-------------------------------------------------------- serialization rules
Output = the input bytes with span edits; everything not edited is copied
byte for byte. The edits:

  S1 set value      the bytes between the quotes are replaced, the quote
                    character stays. A value is copied raw from the patch
                    file (no entity decoding or encoding - both sides are the
                    same XML). If it contains the base's quote character:
                    the other quote character is used for this attribute
                    (both quotes replaced); if it contains both, the base's
                    quote char inside the value becomes &quot; / &apos;.
  S2 new attribute  ' name="value"' (one space, double quotes, S1 escaping)
                    right after the end of the record's last attribute (or
                    after the tag name when it has none).
  S3 unset          the attribute and the whitespace run right before it.
  S4 list value     tokens split on ',' and whitespace; survivors keep their
                    text; joined by the list's separator: "," for multiset
                    lists unless the old value had ", " (then ", "), " " for
                    set lists. An empty result writes "" (the attribute
                    stays; use unset to drop it).
  S5 new child      NL + IND + the patch element's own bytes, inserted at the
                    end of the line of the parent's last child element (the
                    end of line = first CR/LF after it, when only spaces,
                    tabs and comments come between; else right after the
                    element). IND = the last child's leading whitespace
                    (spaces/tabs from the line start). NL = the file's first
                    line break (CRLF or LF). A parent without element
                    children: NL + IND(parent) + TAB + element, right after
                    the parent's start tag. Several new children keep their
                    order.
  S6 remove child   the element; if it stands alone on its line (only
                    spaces/tabs before it, only spaces/tabs and one comment
                    after it), the whole line including its line break.
  S7 completion     (step 2, = guard_table) "\n" + record bytes for every
                    missing game record, then "\n", inserted right before
                    the root's end tag.
  S8 content        (step 3, entities2) for each mod with at least one new
                    entry: "\n<!-- <id> (content/entities2.xml, merged by the
                    port) -->\n" then each entry's bytes + "\n" (anm2path
                    prefixed when the mod's anm2root differs), all before the
                    root's end tag, and one "\n" after the last mod.
  S9 strings        stringtable: <string>TEXT</string> - the inner bytes are
                    replaced; <string/> becomes <string>TEXT</string>;
                    missing positions are added as S5 lines after the last
                    <string> (empty <string></string> for gaps); a new key:
                    S5 after the category's last key, its strings one per
                    line indented as the sibling key's first string (else
                    key indent + 4 spaces), then NL + IND + </key>.

The reader is the rapidxml-like one of usermods.c (xml_parse/xml_attr):
quotes respected, <!-- --> / <![CDATA[ ]]> / <? ?> / <! > skipped, a bare '<'
in text forgiven, end-tag names not checked (bosspools.xml ends with
</pool>), several roots allowed (ambush, fxlayers, seedmenu).

Report lines (the C writes them with the prefix "content: "):
  <file>: base <game|mod id>[ ...]       one summary line
  <file>: legacy <mod>: ...              refusals/notes of legacy files
  <file>: patch <mod>: <rec> <what>      every refused or warned change
  <file>: conflict <rec> <attr>: <a> and <b> - <b> wins
  <file>: content <mod>: ...             content/ merges

Usage:
  py -3 tools/content_sim.py --verify     empty set == game bytes for every
                                          file + set/restore round trips
  py -3 tools/content_sim.py --test       synthetic mods
  py -3 tools/content_sim.py --table      the key table
  py -3 tools/content_sim.py --build FILE --mods DIR[,DIR...] [--out PATH]
        (DIRs strongest first, each a mod folder with resources/ content/
         patch/; prints the report lines)
"""
import os, re, sys, json, hashlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, "work", "app", "repentance-resources", "data")
# the other version of the game's tables (VERSION, step 1a): Repentance+'s, as the port carries them
# (tools/port_patch.py copies them from work/pc-resources-repplus/resources into the assets; portpatch.c
# lays them into the pack under OTHER_INNER, content.c reads them there)
OTHER_INNER = "port-version-ref/repplus/"
OTHER = os.path.join(ROOT, "port", "app", "src", "main", "assets", "port_patch", "port-version-ref", "repplus")
OTHER_NAME = "Repentance+"

# ======================================================================= reader


class El:
    __slots__ = ("start", "tag_end", "ns", "ne", "as_", "ae", "inner_end", "stop", "depth", "parent", "idx")


class ParseError(Exception):
    pass


def _space(c):
    return c in b" \t\r\n"


def _tag_end(b, p, lim):
    """the '>' that ends the tag starting at p, outside quotes (xml_tag_end)"""
    q = 0
    p += 1
    while p < lim:
        c = b[p]
        if q:
            if c == q:
                q = 0
        elif c == 0x22 or c == 0x27:
            q = c
        elif c == 0x3E:
            return p
        p += 1
    return -1


def parse(b):
    """every element in document order (xml_parse); ParseError when it breaks off"""
    lim = len(b)
    v, stack = [], []
    p = 0
    while p < lim:
        lt = b.find(b"<", p)
        if lt < 0:
            break
        left = lim - lt
        if left >= 4 and b.startswith(b"<!--", lt):
            e = b.find(b"-->", lt + 4)
            if e < 0:
                raise ParseError("comment not closed at %d" % lt)
            p = e + 3
            continue
        if left >= 9 and b.startswith(b"<![CDATA[", lt):
            e = b.find(b"]]>", lt + 9)
            if e < 0:
                raise ParseError("CDATA not closed at %d" % lt)
            p = e + 3
            continue
        if left >= 2 and b[lt + 1] == 0x3F:
            e = b.find(b"?>", lt + 2)
            if e < 0:
                raise ParseError("<? not closed at %d" % lt)
            p = e + 2
            continue
        if left >= 2 and b[lt + 1] == 0x21:
            e = _tag_end(b, lt, lim)
            if e < 0:
                raise ParseError("<! not closed at %d" % lt)
            p = e + 1
            continue
        if left >= 2 and b[lt + 1] == 0x2F:
            e = b.find(b">", lt)
            if e < 0:
                raise ParseError("end tag not closed at %d" % lt)
            if stack:
                x = v[stack.pop()]
                x.inner_end = lt
                x.stop = e + 1
            p = e + 1
            continue
        q = lt + 1
        while q < lim and not _space(b[q]) and b[q] != 0x2F and b[q] != 0x3E:
            q += 1
        if q == lt + 1:
            p = lt + 1
            continue
        gt = _tag_end(b, lt, lim)
        if gt < 0:
            raise ParseError("start tag not closed at %d" % lt)
        x = El()
        x.idx = len(v)
        selfc = b[gt - 1] == 0x2F and gt - 1 >= q
        x.start, x.tag_end, x.ns, x.ne, x.as_ = lt, gt, lt + 1, q, q
        x.ae = gt - 1 if selfc else gt
        x.depth = len(stack)
        x.parent = stack[-1] if stack else -1
        if selfc:
            x.inner_end = x.stop = gt + 1
        else:
            if len(stack) == 64:
                raise ParseError("nesting deeper than 64")
            stack.append(x.idx)
            x.inner_end = x.stop = None
        v.append(x)
        p = gt + 1
    if stack:
        raise ParseError("%d elements not closed" % len(stack))
    return v


def attrs(b, x):
    """[(name, vs, ve, quote, ns)] of a start tag, stopping where xml_attr stops"""
    out = []
    p, end = x.as_, x.ae
    while True:
        while p < end and _space(b[p]):
            p += 1
        if p >= end:
            break
        ns = p
        while p < end and b[p] != 0x3D and not _space(b[p]):
            p += 1
        name = b[ns:p]
        while p < end and _space(b[p]):
            p += 1
        if p >= end or b[p] != 0x3D:
            break
        p += 1
        while p < end and _space(b[p]):
            p += 1
        if p >= end or b[p] not in (0x22, 0x27):
            break
        q = b[p]
        p += 1
        vs = p
        while p < end and b[p] != q:
            p += 1
        if p >= end:
            break
        out.append((name.decode("latin-1"), vs, p, q, ns))
        p += 1
    return out


def name_of(b, x):
    return b[x.ns:x.ne].decode("latin-1")


def get(b, x, attr):
    """first occurrence, as the port's reader (xml_get) does"""
    for n, vs, ve, q, ns in attrs(b, x):
        if n == attr:
            return b[vs:ve].decode("utf-8", "replace")
    return None


def get_last(b, x, attr):
    """last occurrence: the game's loaders strcmp every attribute in turn, the last one sticks"""
    r = None
    for n, vs, ve, q, ns in attrs(b, x):
        if n == attr:
            r = b[vs:ve].decode("utf-8", "replace")
    return r


def kids(v, xi):
    out = []
    x = v[xi]
    for j in range(xi + 1, len(v)):
        y = v[j]
        if y.start >= x.stop:
            break
        if y.parent == xi:
            out.append(y)
    return out


def roots(v):
    return [x for x in v if x.depth == 0]


# ======================================================================= edits


class Edits:
    """[at, end) replaced by text, applied in one pass; same-place inserts keep their order (ed_apply)"""

    def __init__(self):
        self.v = []

    def add(self, at, end, text):
        self.v.append((at, end, len(self.v), text))

    def apply(self, b):
        out, at = [], 0
        for e_at, e_end, seq, text in sorted(self.v, key=lambda e: (e[0], e[2])):
            if e_at < at:
                raise AssertionError("overlapping edits at %d" % e_at)
            out.append(b[at:e_at])
            out.append(text)
            at = e_end
        out.append(b[at:])
        return b"".join(out)


def newline_of(b):
    i = b.find(b"\n")
    return b"\r\n" if i > 0 and b[i - 1] == 0x0D else b"\n"


def indent_of(b, pos):
    """spaces/tabs from the line start to pos; None when pos is not first on its line"""
    i = pos
    while i > 0 and b[i - 1] in (0x20, 0x09):
        i -= 1
    if i == 0 or b[i - 1] in (0x0A, 0x0D):
        return b[i:pos]
    return None


def eol(b, pos):
    """the index of the CR/LF (or the file's end) that ends pos's line, when only spaces, tabs and
    comments come between; -1 when something else does"""
    i = pos
    while i < len(b):
        c = b[i]
        if c in (0x20, 0x09):
            i += 1
        elif b.startswith(b"<!--", i):
            e = b.find(b"-->", i + 4)
            if e < 0:
                return -1
            i = e + 3
        elif c in (0x0A, 0x0D):
            return i
        else:
            return -1
    return i


def line_end_after(b, pos):
    """S5's insertion point after an element ending at pos"""
    e = eol(b, pos)
    return pos if e < 0 else e


def quote_value(raw, q):
    """S1: the value bytes and the quote char to write them in"""
    other = 0x27 if q == 0x22 else 0x22
    if bytes([q]) not in raw:
        return raw, q
    if bytes([other]) not in raw:
        return raw, other
    return raw.replace(bytes([q]), b"&quot;" if q == 0x22 else b"&apos;"), q


# ======================================================================= key table


class Rec:
    """a record class: its tags, key, known attributes, lists, references, children"""

    def __init__(self, tags, key=(), attrs="", lists=None, refs=None, kids=(), bounds=None,
                 add=False, remove=False, note=""):
        self.tags = tuple(tags.split()) if isinstance(tags, str) else tuple(tags)
        self.cls = self.tags[0]
        self.key = tuple(k if isinstance(k, tuple) else (k, None) for k in key)  # (attr, default)
        self.attrs = set(attrs.split())
        self.lists = lists or {}          # attr -> "multiset" | "set"
        self.refs = refs or {}            # attr -> table name ("collectible", ...)
        self.kids = list(kids)            # child Recs
        self.bounds = list(bounds or [])  # [(attr, lo, hi, kind, when)] - the loader's table
        self.add = add                    # a patch may add this element (as a child)
        self.remove = remove              # a patch may remove it (remove="true")
        self.note = note


class Table:
    def __init__(self, name, loader, roots, fixed=True, read=True, content=False, note=""):
        self.name = name
        self.loader = loader              # the Repentance loader (image address)
        self.roots = roots                # root tag -> (root attrs, [Rec])
        self.fixed = fixed                # the record count is the save's/loader's (never changes)
        self.read = read                  # does the Repentance engine read the file at all
        self.content = content            # content/<file> adds records (step 3)
        self.note = note


def _norm_key(v):
    if v is None:
        return None
    s = v.strip()
    if re.fullmatch(r"[+-]?\d+", s):
        return str(int(s))
    return s


# The key table. Attribute names are the loader's own strcmp() strings (see the
# research notes of 02.10.2026, batch 5 content spec); those found only in the
# game's file are added at run time (Game.attrs_seen) - they are not warned about.
#
# BOUNDS: (attr, lo, hi, kind, when) - what the Repentance loader does with the
# record's key, measured in the image (base mode, mod pointer 0 as the startup
# list calls it). lo/hi None = unbounded on that side.
#   "danger"  the loader writes or dereferences through an unchecked index:
#             a legacy file with such a record is refused, content/ skips it;
#   "ignored" the loader checks and skips the record: a warning line only;
#   "ascii"   every byte of the value must be < 0x80 (else as "danger").
# when: None, ("attr", (values...)) - the bound holds for those records only,
# or ("prefix", "s") - for values written "sN" (wisps' special ids).
D, I = "danger", "ignored"
T = {}


def _t(t):
    T[t.name] = t


_t(Table("players.xml", 0x78555c, {"players": ("root portraitroot nameimageroot", [
    Rec("player", ["id"], "id name skin nameimage portrait extraportrait costume hp armor black coins bombs keys card pill "
        "items trinket skinColor canShoot costumeSuffix achievement broken pocketActive birthright bSkinParent hidden",
        lists={"items": "multiset"}, refs={"items": "collectible", "pocketActive": "collectible", "trinket": "trinket",
                                           "card": "card", "pill": "pill", "costume": "costume"},
        kids=[Rec("hair", [], "gfx"), Rec("Background", [], "gfx")],
        bounds=[("id", 0, 40, D, None)])])},
   note="vector resize(41) 0x7855dc, record 0x178; store 0x7862b0..0x7862d0 skips only id<0"))

_t(Table("items.xml", 0x73857c, {"items": ("version gfxroot", [
    Rec("passive active familiar", ["id"],
        "id name description gfx achievement cache maxhearts hearts soulhearts blackhearts bombs keys coins maxcharges "
        "cooldown special devilprice shopprice initcharge chargetype passivecache addcostumeonpickup cleareffectsonremove "
        "hidden persistent quality craftquality tags", lists={"cache": "set", "tags": "set"},
        bounds=[("id", 0, 732, D, None)]),
    Rec("trinket", ["id"], "id name description gfx achievement cache hidden persistent addcostumeonpickup quality tags",
        lists={"cache": "set", "tags": "set"}, bounds=[("id", 0, 189, D, None)]),
    Rec("null", ["id"], "id name cache persistent cooldown hidden", lists={"cache": "set"},
        bounds=[("id", 0, 131, D, None)]),
])}, note="ItemConfig vectors resize(733)/(190)/(132) at 0x737eac/0x737ed4/0x737efc; base mode stores vec[id] with no "
          "check (0x739200, 0x7392dc); mod mode appends"))

_t(Table("items_metadata.xml", 0x73b150, {"items": ("", [
    Rec("item", ["id"], "id quality craftquality tags", lists={"tags": "set"}, bounds=[("id", 0, 732, D, None)]),
    Rec("trinket", ["id"], "id quality tags", lists={"tags": "set"}, bounds=[("id", 0, 189, D, None)]),
])}, note="0x73b4a0/0x73b4d0 log 'Collectible/Trinket ID out of bounds' and still read vec[id] (0x73b4f4), "
          "then write +0xc0"))

_t(Table("pocketitems.xml", 0x73db80, {"pocketitems": ("", [
    Rec("card rune", ["id"], "id achievement name description hud greedmode type pickup announcer announcerdelay mimiccharge",
        bounds=[("id", 0, 97, D, None)]),
    Rec("pilleffect", ["id"], "id achievement name description greedmode announcer announcer2 announcerdelay class mimiccharge",
        bounds=[("id", 0, 49, D, None)]),
])}, note="cards ItemConfig+0x48 resize(98), pills +0x60 resize(50); base mode stores [vec, id, uxtw #3] with no "
          "upper check (0x73e3dc, 0x73e438); a negative id is logged and skipped"))

_t(Table("challenges.xml", 0x66f898, {"challenges": ("version", [
    Rec("challenge", ["id"], "id name startingitems startingitems2 startingtrinkets achievements endstage roomfilter "
        "cursefilter getcurse startingpill startingcard soulhp blackhp redhp maxhp coins canshoot altpath maxdamage "
        "adddamage minfirerate minshotspeed bigrange playertype difficulty megasatan secretpath",
        lists={"startingitems": "multiset", "startingitems2": "multiset", "startingtrinkets": "multiset",
               "achievements": "set", "roomfilter": "set"},
        refs={"startingitems": "collectible", "startingitems2": "collectible", "startingtrinkets": "trinket",
              "startingcard": "card", "startingpill": "pill"},
        bounds=[("id", 0, 45, D, None)])])},
   note="vector resize(46) 0x66f9a0, record 0xf0; umaddl id*0xf0 at 0x67069c with no check (unsigned: negative too)"))

_t(Table("entities2.xml", 0x782360, {"entities": ("version anm2root", [
    Rec("entity", ["id", ("variant", "0"), ("subtype", "0")],
        "id variant subtype shadowSize name collisionDamage boss bossID champion collisionRadius collisionRadiusXMulti "
        "collisionRadiusYMulti collisionMass numGridCollisionPoints friction baseHP stageHP anm2path portrait shutdoors "
        "gridCollision reroll hasFloorAlts collisionInterval shieldStrength shieldFrames tags bestiary bestiaryTransform "
        "bestiaryAnim bestiaryOverlay gibAmount gibFlags", lists={"tags": "set"},
        kids=[Rec("gibs", [], "amount blood bone gut eye large poop worm rock rock_small sound_baby sound_bone chain dust huge"),
              Rec("bestiary", [], "transform parent anim overlay alt anm2path"),
              Rec("preload", ["id", ("variant", "0"), ("subtype", "0")], "id variant subtype path"),
              Rec("preload-snd", ["id"], "id"),
              Rec("devolve", ["id"], "id weight")],
        bounds=[("id", 0, 4095, I, None), ("variant", 0, 4095, I, None), ("subtype", 0, 255, I, None)])])},
   fixed=False, content=True,
   note="a map keyed type<<20|variant<<8|subtype (0x783f50): wider values alias another key (no memory harm); new "
        "records only through content/ (a new variant of a type the game has)"))

_t(Table("costumes2.xml", 0x73a790, {"costumes": ("anm2root", [
    Rec("costume", [("type", "none"), "id"], "id type anm2path priority hasOverlay hasSkinAlt skinColor isFlying overwriteColor "
        "forceHeadColor forceBodyColor",
        bounds=[("id", 0, 732, D, ("type", ("passive", "active", "familiar"))),
                ("id", 0, 189, D, ("type", ("trinket",))),
                ("id", 0, 131, D, ("type", ("none",)))])])},
   note="the id is the item's (passive/active/familiar: log 0x73ae18 then read 0x73ae3c; trinket 0x73af74) or a null "
        "costume's (none: ItemConfig+0x30, 132 entries, 0x73af0c unchecked); 132 is Specialist Dance's GAME_NULLS - "
        "a patch never changes the count"))

_t(Table("music.xml", 0x75fc64, {"music": ("root", [
    Rec("track", ["id"], "id name path intro loop layer layerintro layermode mul layermul layerfadespeed",
        kids=[Rec("layer", [], "path mul")], bounds=[("id", 1, 118, I, None)])])},
   note="resize(119) 0x75fdbc; id-1 cmp #0x75 b.hi skips (0x7608d4); 119 = Specialist Dance's GAME_TRACKS"))

_t(Table("sounds.xml", 0x7efedc, {"sounds": ("root", [
    Rec("sound", ["id"], "id name", bounds=[("id", 0, None, D, None), ("id", None, 832, I, None)])])},
   note="resize(833) 0x7eff74; id>832 logged and skipped (0x7f01a0); a negative id passes the signed check and is "
        "written before the array (madd 0x7f0220); <sample> children are positional - not patchable"))

_t(Table("babies.xml", 0x786e24, {"babies": ("root", [Rec("baby", ["id"], "id name skin achievement")])},
   note="a growing vector: a gap is padded with id -1 entries, negative ids skipped (0x7871b4) - no bound"))

_t(Table("bossportraits.xml", 0x7874f4, {"bosses": ("root anm2", [
    Rec("boss", ["id"], "id name nameimage portrait pivotX pivotY achievement",
        kids=[Rec("alt", ["stage"], "stage portrait")], bounds=[("id", 1, 103, I, None)])])},
   note="resize(104) 0x787698, record 0x78; id-1 cmp #0x67 b.hs skips (0x7879b4)"))

_t(Table("bosscolors.xml", 0x784c50, {"bosscolors": ("version anm2root portraitroot", [
    Rec("boss", ["id", ("variant", "0")], "id variant")])},
   note="colours hang on the entity found by 0x7822ac (absent: skipped); the colour index is checked (0x78508c); "
        "<color> children are positional - not patchable"))

_t(Table("achievements.xml", 0x56e964, {"achievements": ("gfxroot", [
    Rec("achievement", ["id"], "id text gfx", bounds=[("id", 0, 637, I, None)])])},
   note="a fixed array of 638; cmp #0x27e b.hs skips (0x56ec50, unsigned)"))

_t(Table("wisps.xml", 0x73b628, {"wisps": ("gfxroot", [
    Rec("color", ["name"], "name r g b a cr cg cb ca or og ob"),
    Rec("wisp", ["id"], "id hp damage stageDamage shotSpeed fireDelay procChance canShoot tearFlags tearFlags2 "
        "tearVariant tearVariant2 tearScale tearScale2 damageMultiplier damageMultiplier2 tearColor tearColor2 "
        "flameColor coreColor flameGfx coreGfx bloodColor poofColor layer priority count",
        bounds=[("id", 0, 732, D, None), ("id", 0, 18, D, ("prefix", "s"))])])},
   note="a wisp hangs on its collectible: 0x73bc50 logs 'Collectible ID out of bounds' and still reads vec[id] "
        "(0x73bc70); 'sN' ids: ItemConfig+0x1d8 resize(19), no check (0x73b89c)"))

_t(Table("locusts.xml", 0x73c758, {"locusts": ("gfxroot", [
    Rec("color", ["name"], "name r g b a cr cg cb ca or og ob"),
    Rec("locust", ["id"], "id color speed scale procChance tearFlags tearFlags2 damageMultiplier damageMultiplier2 "
        "backColor overlayColor gfx backGfx overlayGfx count collisionInterval", bounds=[("id", 0, 732, D, None)])])},
   note="as wisps: 0x73c9e8 logs, 0x73ca08 still reads"))

_t(Table("recipes.xml", 0x73d76c, {"recipes": ("", [
    Rec("recipe", ["input"], "input output outputName", refs={"output": "collectible"},
        bounds=[("input", None, None, "ascii", None)])])},
   note="std::map, the FIRST of two equal inputs wins (0x7419c8); an input byte >=0x80 is read signed (0x73d9b4) and "
        "indexes a table backwards - refused (the harm is not pinned, it depends on bss)"))

_t(Table("playerforms.xml", 0x73e6d4, {"playerforms": ("", [
    Rec("form", ["id"], "id name tags cache costume", lists={"tags": "set", "cache": "set"}, refs={"costume": "costume"},
        bounds=[("id", 0, 14, I, None)])])}, note="resize(15) 0x737f74; 0x73e994 b.ge skips with a warning"))

_t(Table("bombcostumes.xml", 0x73eb24, {"bombcostumes": ("anm2root", [Rec("bomb", ["variant"], "variant")])},
   note="std::map by variant (any int); <rule> children are positional (evaluated top to bottom) - not patchable"))

_t(Table("nightmares.xml", 0x7ec490, {"nightmares": ("root progressAnm2 backgroundAnm2", [])},
   note="a growing vector; <nightmare> records have no key (by position) - only the root's attributes are patchable"))

_t(Table("cutscenes.xml", 0x755dd8, {"cutscenes": ("root", [
    Rec("cutscene", ["id"], "id name fadeout fadecolor", bounds=[("id", 0, 26, D, None)])])},
   note="a fixed array of 27 x 0x60: madd id*0x60 at 0x7560f4 with no check; <anm2part>/<videopart> positional"))

_t(Table("stages.xml", 0x751880, {"stages": ("root greedroot bossgfxroot", [
    Rec("stage", ["id"], "id name path playerspot bossspot music backdrop suffix", bounds=[("id", 0, 36, D, None)])])},
   note="a fixed array of 37 x 0x100: base+id<<8 at 0x751ec8 with no check"))

_t(Table("curses.xml", 0x75219c, {"curses": ("", [
    Rec("curse", ["id"], "id name", bounds=[("id", 0, 31, I, None)])])},
   note="a growing vector (0x752578), safe in the loader; the game makes 1<<id into a 32-bit mask - 32+ is meaningless"))

_t(Table("minibosses.xml", 0x752688, {"minibosses": ("", [
    Rec("miniboss", ["id"], "id name", bounds=[("id", 0, 15, I, None)])])}, note="fixed 16; cmp #0x10 b.hs 0x752860"))

_t(Table("giantbook.xml", 0x5744e0, {"giantbook": ("anm2root", [
    Rec("entry", ["id"], "id anm2 anim gfx", kids=[Rec("layer", ["name"], "name r g b a")],
        bounds=[("id", 0, 46, I, None)])])}, note="fixed 47; cmp #0x2f b.hs 0x574abc"))

_t(Table("bossoverlays.xml", 0x76b1a4, {"bossoverlay": ("anm2root", [
    Rec("entry", ["id"], "id anm2 anim persist", bounds=[("id", 0, 4, I, None)])])}, note="fixed 5; 0x76b4c8"))

_t(Table("ambush.xml", 0x634144, {"bossrush": ("numWaves spawnsPerWave", []),
                                   "ambushRoom": ("", []), "bossAmbushRoom": ("", [])},
   note="Repentance reads only <bossrush> (0x634144, growing vectors); <spawn> groups are positional; "
        "ambushRoom/bossAmbushRoom are read only by the AB/AB+ copies"))

_t(Table("preload.xml", 0x635280, {"preloads": ("", [Rec("preload", ["id"], "id")])},
   note="a map by id string + growing vectors; <png> children are positional - not patchable"))

_t(Table("itempools.xml", 0x87a518, {"ItemPools": ("", [
    Rec("Pool", ["Name"], "Name", kids=[
        Rec("Item", ["Id"], "Id Weight DecreaseBy RemoveOn Name", refs={"Id": "collectible"}, add=True, remove=True)])])},
   note="31 pools by Name (table 0xa90f80; unknown: 'Pool %s not found', skipped); items a growing vector (0x87a9b8)"))

_t(Table("bosspools.xml", 0x675528, {"bosspools": ("", [
    Rec("pool", ["name"], "name doubletrouble",
        kids=[Rec("boss", ["id"], "id weight room", add=True)])])},
   note="26 floors by name (unknown: 'Boss pool: Unknown stage', skipped); bosses a growing vector, the boss id is "
        "checked (0x787f3c)"))

_t(Table("backdrops.xml", 0x705638, {"backdrops": ("gfxroot gridgfxroot effectsgfxroot", [
    Rec("backdrop", ["id"], "id gfx walls wallvariants floors floorvariants lfloorgfx nfloorgfx props rocks pit "
        "waterpit waterpitsmode bridge door holeinwall spikes watergfx reversewatergfx fullimage",
        bounds=[("id", 1, 60, I, None)])])}, note="fixed 60; id-1 cmp #0x3c b.hs 0x705504"))

_t(Table("fxlayers.xml", 0x8590e4, {
    "fxLayers": ("gfxroot", [Rec("fx", ["id"], "id path numLayers xMin xMax yMin yMax layer stage altStages "
                                               "onlyDefaultBackdrop blendMode parallax backdrop useBGShader")]),
    "fxRays": ("", []),
    "fxParams": ("", [Rec("params", ["stage", ("altStages", ""), ("backdrop", "")], "stage altStages shadowAlpha waterv2 backdrop",
                          kids=[Rec(t, [], "r g b a rMul gMul bMul brightness contrast")
                                for t in ("colorModifier", "shadowColor", "waterColor", "lightColor", "waterEffectColor")])])},
   note="growing vectors filtered by stage (safe); <fx> <gfx> children and <rayGroup>s are positional - not patchable"))

_t(Table("seedmenu.xml", 0x5d2dc4, {"entryDialog": ("unlock enable", []),
                                     "seedmenu": ("", [Rec("seed", ["id"], "id name implemented",
                                                           bounds=[("id", 0, 79, D, None)]),
                                                       Rec("confirm", [], "name"), Rec("reset", [], "name")])},
   note="the seed id indexes SaveState+0xe94 bytes (0x7036cc) and manager+0x23640 (0x5d3228) with no check; the "
        "tables' length is not pinned - 79 (the game's highest id) is the conservative bound"))

_t(Table("menu_sounds.xml", None, {}, read=False, note="no loader in the image reads it"))
_t(Table("translations.xml", None, {}, read=False, note="no loader in the image reads it"))

STRINGTABLE = "stringtable.sta"


def rec_of(table, root, tag):
    for r in table.roots.get(root, ("", []))[1]:
        if tag in r.tags:
            return r
    return None


def kid_of(rec, tag):
    for k in rec.kids:
        if tag in k.tags:
            return k
    return None


def key_of(b, x, rec):
    """the record's key as a string 'cls[k1.k2]', None when a required key attribute is missing"""
    parts = []
    for a, d in rec.key:
        v = get_last(b, x, a)
        if v is None:
            v = d
        if v is None:
            return None
        v = _norm_key(v)
        parts.append(KEY_CLASS.get((rec.cls, a), {}).get(v, v))
    return "%s[%s]" % (rec.cls, ".".join(parts))


# key values that name one id space: costumes of passive/active/familiar items share the collectibles' ids
KEY_CLASS = {("costume", "type"): {"passive": "item", "active": "item", "familiar": "item"}}


# ======================================================================= the game's tables (refs)


class Game:
    def __init__(self, data_dir=GAME, other_dir=None):
        self.dir = data_dir
        self._files = {}
        self._ids = None
        # Repentance's own tables only: an older edition's data has no "other version" here
        self.other_dir = other_dir if other_dir is not None else (OTHER if os.path.normcase(os.path.abspath(data_dir))
                                                                  == os.path.normcase(os.path.abspath(GAME)) else "")
        self._other = {}

    def other(self, name):
        """the other version's (Repentance+'s) table of this name, or None"""
        if not self.other_dir:
            return None
        if name not in self._other:
            p = os.path.join(self.other_dir, name)
            self._other[name] = open(p, "rb").read() if os.path.isfile(p) else None
        return self._other[name]

    def file(self, name):
        if name not in self._files:
            p = os.path.join(self.dir, name)
            self._files[name] = open(p, "rb").read() if os.path.exists(p) else None
        return self._files[name]

    def ids(self):
        if self._ids is None:
            ids = {k: set() for k in ("collectible", "trinket", "card", "pill", "costume")}
            for fname, root, tag, kind in (("items.xml", "items", None, None), ("pocketitems.xml", "pocketitems", None, None),
                                           ("costumes2.xml", "costumes", "costume", "costume")):
                b = self.file(fname)
                v = parse(b)
                for x in v:
                    if x.depth != 1:
                        continue
                    t = name_of(b, x)
                    i = _norm_key(get_last(b, x, "id"))
                    if i is None:
                        continue
                    k = {"passive": "collectible", "active": "collectible", "familiar": "collectible",
                         "trinket": "trinket", "card": "card", "rune": "card", "pilleffect": "pill",
                         "costume": "costume"}.get(t)
                    if k:
                        ids[k].add(i)
            self._ids = ids
        return self._ids

    def parsed(self, name, b):
        cache = self.__dict__.setdefault("_parsed", {})
        if name not in cache or cache[name][0] is not b:
            cache[name] = (b, parse(b))
        return cache[name][1]

    def attrs_seen(self, table, root, rec):
        """attributes the game's own file uses on this record class (never warned about)"""
        ck = (table.name, rec.tags)
        cache = self.__dict__.setdefault("_seen", {})
        if ck not in cache:
            cache[ck] = self._attrs_seen(table, rec)
        return cache[ck]

    def _attrs_seen(self, table, rec):
        b = self.file(table.name)
        seen = set()
        if not b:
            return seen
        try:
            v = parse(b)
        except ParseError:
            return seen
        for x in v:
            if name_of(b, x) in rec.tags:
                seen.update(a[0] for a in attrs(b, x))
        return seen


def ref_ok(game, kind, value):
    v = value.strip()
    if not re.fullmatch(r"[+-]?\d+", v):
        return False
    n = abs(int(v))                       # challenges: -584 = the item as a pocket active
    if kind == "pill":
        return n in range(0, 14) or str(n) in game.ids()["pill"]   # startingpill is a pill colour; pill= a colour too
    return str(n) in game.ids()[kind]


# ======================================================================= mods


class Mod:
    """a mod: id and its files, relative paths ('resources/players.xml', 'patch/players.xml', ...)"""

    def __init__(self, mid, files=None, folder=None):
        self.id = mid
        self.files = dict(files or {})
        self.folder = folder

    def get(self, rel):
        if rel in self.files:
            return self.files[rel]
        if self.folder:
            p = os.path.join(self.folder, *rel.split("/"))
            if os.path.isfile(p):
                return open(p, "rb").read()
        return None

    @staticmethod
    def from_dir(folder):
        return Mod(os.path.basename(os.path.normpath(folder)), folder=folder)


# ======================================================================= build


class Result:
    def __init__(self, data, lines, base):
        self.data = data
        self.lines = lines
        self.base = base


def build(fname, mods, game=None, port=None):
    """F's bytes for this set of mods (strongest first); port = the port's fill patch bytes or None"""
    game = game or Game()
    g = game.file(fname)
    lines = []
    say = lambda s: lines.append("%s: %s" % (fname, s))
    if g is None:
        return Result(None, ["%s: the game has no such file" % fname], "none")
    table = T.get(fname)
    if fname == STRINGTABLE:
        return _build_stringtable(fname, mods, game, port, lines)
    legacy = [m for m in mods if m.get("resources/" + fname) is not None]
    contents = [m for m in mods if table and table.content and m.get("content/" + fname) is not None]
    patches = [m for m in reversed(mods) if m.get("patch/" + fname) is not None]
    if table is not None and not table.read:
        if legacy:
            say("legacy %s: the game does not read this file - handed out as it is" % legacy[0].id)
            return Result(legacy[0].get("resources/" + fname), lines, legacy[0].id)
        for m in patches:
            say("patch %s: refused - the game does not read this file" % m.id)
        return Result(g, lines, "game")
    # 1. base
    cur, base_id = g, "game"
    ob = game.other(fname) if table is not None else None
    for i, m in enumerate(legacy):
        mb = m.get("resources/" + fname)
        if ob is not None:
            mb = _version(table, mb, g, ob, say, m.id)
        why = _legacy_refusal(table, mb, say, m.id)
        if why:
            say("legacy %s: refused - %s; the next one below is used" % (m.id, why))
            continue
        cur, base_id = mb, m.id
        for o in legacy[i + 1:]:
            say("legacy %s: not used - %s replaces the file (the top of MODS wins)" % (o.id, m.id))
        break
    # 1b. the base's lists of item ids, mended (M1, the owner 02.10.2026)
    if base_id != "game" and table is not None:
        cur = _mend_lists(table, fname, cur, game, say, base_id)
    # 2. completion
    if base_id != "game" and table is not None:
        cur, n = _complete(table, cur, g)
        if n:
            say("legacy %s: an older table - %d records of the game's were missing, added" % (base_id, n))
    # 3. content
    if contents:
        cur = _content_entities(fname, cur, contents, say)
    # 4. patch
    owners = {}
    for m in patches:
        if table is None:
            say("patch %s: refused - not a data table the port knows" % m.id)
            continue
        cur = _apply_patch(table, cur, g, m.get("patch/" + fname), m.id, base_id, owners, game, say)
    # 5. port: data tables carry no port additions today
    say("base %s, content from %d mods, patches from %d mods" % (base_id, len(contents), len(patches)))
    if not legacy and not contents and not patches:
        lines.pop()
    return Result(cur, lines, base_id)


# ======================================================================= 1a. version (04.10.2026)
# A WHOLE TABLE MADE FOR ANOTHER VERSION OF THE GAME (the owner approved, 04.10.2026). Sam & Max's players.xml
# is Repentance+'s: kept whole it takes the starting items of Cain, Eve, Samson, Lazarus II, Lilith and Bethany
# away, since Repentance+ gives them differently. A three-way merge against the other version's own table
# (Repentance+'s, the port carries it) tells the mod's own changes from the version's:
#   per record (by the file's key, under the root of the same name) and attribute -
#     mod == game                          nothing;
#     mod == Repentance+ != game           the version's value: the game's is put back (set, unset or added);
#     otherwise                            the mod's own change: kept (its texts, costumeSuffix...);
#   a record the mod and Repentance+ have and the game has not       Repentance+'s: left out (S6);
#   a record only the mod has                                       the mod's own: kept (the bounds still apply);
#   a record the game has and the mod lacks                         completion (step 2) puts it back, as before;
#   a record's children (Eden's <hair>, a sound's <sample>s...), compared as a whole (names, attributes sorted,
#     their own children; text ignored) - the mod's == Repentance+'s != the game's: the game's inner bytes are
#     put back; otherwise the mod's stay;
#   the roots' own attributes: as a record's.
# A key the game, Repentance+ or the mod has twice is left as the mod has it (nothing to pair). Values compare
# as the bytes between the quotes. Each put-back is a line; a summary line counts them and the mod's own changes
# kept. No difference of the version's: no line, the mod's bytes as they are.

def _canon(b, v, x):
    return (name_of(b, x), tuple(sorted((a[0], b[a[1]:a[2]]) for a in attrs(b, x))),
            tuple(_canon(b, v, k) for k in kids(v, x.idx)))


def _val(b, x, a):
    """the last occurrence's raw bytes, or None"""
    r = None
    for n, vs, ve, q, ns in attrs(b, x):
        if n == a:
            r = b[vs:ve]
    return r


def _shown(raw):
    return "none" if raw is None else '"%s"' % raw.decode("utf-8", "replace")


def _version_attrs(mb, x, gb, gx, ob, ox, ed, say, mid, who):
    """the attribute rule on one element; (put back, own kept)"""
    names = []
    for a in attrs(mb, x):
        if a[0] not in names:
            names.append(a[0])
    for a in attrs(gb, gx):
        if a[0] not in names:
            names.append(a[0])
    have = attrs(mb, x)
    back = own = 0
    for a in names:
        m, g = _val(mb, x, a), _val(gb, gx, a)
        if m == g:
            continue
        o = _val(ob, ox, a) if ox is not None else None
        if ox is None or m != o or o == g:
            own += 1
            continue
        back += 1
        occ = [h for h in have if h[0] == a]
        if g is None:
            for n, vs, ve, q, ns in occ:
                i = ns
                while i > x.as_ and _space(mb[i - 1]):
                    i -= 1
                ed.add(i, ve + 1, b"")
        elif m is None:
            val, q2 = quote_value(g, 0x22)
            at = have[-1][2] + 1 if have else x.ne
            ed.add(at, at, b" " + a.encode("latin-1") + b"=" + bytes([q2]) + val + bytes([q2]))
        else:
            for n, vs, ve, q, ns in occ:
                if mb[vs:ve] == g:
                    continue
                val, q2 = quote_value(g, q)
                if q2 == q:
                    ed.add(vs, ve, val)
                else:
                    ed.add(vs - 1, ve + 1, bytes([q2]) + val + bytes([q2]))
        say("legacy %s: %s %s=%s is %s's - the game's %s kept" % (mid, who, a, _shown(m), OTHER_NAME, _shown(g)))
    return back, own


def _keyed(table, b, v):
    """{(root name, key): [element]} of the depth-1 records with a key"""
    out = {}
    for x in v:
        if x.depth != 1:
            continue
        rn = name_of(b, v[x.parent])
        r = rec_of(table, rn, name_of(b, x))
        if not r or not r.key:
            continue
        k = key_of(b, x, r)
        if k:
            out.setdefault((rn, k), []).append(x)
    return out


def _first_roots(b, v):
    out = {}
    for x in roots(v):
        out.setdefault(name_of(b, x), x)
    return out


def _version(table, mb, gb, ob, say, mid):
    try:
        mv, gv, ov = parse(mb), parse(gb), parse(ob)
    except ParseError:
        return mb
    ed = Edits()
    back = own = 0
    groot, oroot = _first_roots(gb, gv), _first_roots(ob, ov)
    gk, ok, mk = _keyed(table, gb, gv), _keyed(table, ob, ov), _keyed(table, mb, mv)
    for x in mv:
        if x.depth == 0:
            rn = name_of(mb, x)
            if rn in groot:
                b2, o2 = _version_attrs(mb, x, gb, groot[rn], ob, oroot.get(rn), ed, say, mid, "<%s>" % rn)
                back += b2
                own += o2
            continue
        if x.depth != 1:
            continue
        rn = name_of(mb, mv[x.parent])
        if rn not in groot:
            continue
        r = rec_of(table, rn, name_of(mb, x))
        if not r or not r.key:
            continue
        k = key_of(mb, x, r)
        if not k or len(mk.get((rn, k), [])) != 1:
            continue
        gs, os_ = gk.get((rn, k), []), ok.get((rn, k), [])
        if not gs:
            if len(os_) == 1:
                ind = indent_of(mb, x.start)
                e = eol(mb, x.stop)
                if ind is not None and e >= 0 and e < len(mb):
                    ed.add(x.start - len(ind), e + (2 if mb.startswith(b"\r\n", e) else 1), b"")
                else:
                    ed.add(x.start, x.stop, b"")
                back += 1
                say("legacy %s: %s is %s's (the game has none) - left out" % (mid, k, OTHER_NAME))
            else:
                own += 1
            continue
        if len(gs) != 1:
            continue
        gx = gs[0]
        ox = os_[0] if len(os_) == 1 else None
        b2, o2 = _version_attrs(mb, x, gb, gx, ob, ox, ed, say, mid, k)
        back += b2
        own += o2
        cm = tuple(_canon(mb, mv, c) for c in kids(mv, x.idx))
        cg = tuple(_canon(gb, gv, c) for c in kids(gv, gx.idx))
        if cm == cg:
            continue
        co = tuple(_canon(ob, ov, c) for c in kids(ov, ox.idx)) if ox is not None else None
        if co is None or cm != co or x.inner_end == x.stop:
            own += 1
            continue
        inner = gb[gx.tag_end + 1:gx.inner_end] if gx.inner_end != gx.stop else b""
        ed.add(x.tag_end + 1, x.inner_end, inner)
        back += 1
        tags = sorted(set(name_of(mb, c) for c in kids(mv, x.idx)) | set(name_of(gb, c) for c in kids(gv, gx.idx)))
        say("legacy %s: %s: its <%s> are %s's - the game's kept" % (mid, k, "> <".join(tags), OTHER_NAME))
    if not back:
        return mb
    say("legacy %s: a table of %s - %d of its differences are %s's, the game's kept; %d changes of the mod's own taken"
        % (mid, OTHER_NAME, back, OTHER_NAME, own))
    return ed.apply(mb)


def bound_check(b, x, r):
    """[(kind, text)] for the record's values outside the loader's table"""
    out = []
    for attr, lo, hi, kind, when in r.bounds:
        raw = get_last(b, x, attr)
        if raw is None:
            continue
        if when and when[0] != "prefix":
            w = get_last(b, x, when[0])
            if w is None or w.strip() not in when[1]:
                continue
        s = raw.strip()
        if kind == "ascii":
            if any(ord(c) >= 0x80 for c in raw):
                out.append(("danger", "%s %s=%s (a byte >= 0x80)" % (name_of(b, x), attr, raw)))
            continue
        pre = when[1] if when and when[0] == "prefix" else ""
        if pre:
            if not s.startswith(pre):
                continue
            s = s[len(pre):]
        elif not re.match(r"[+-]?\d", s):
            if re.fullmatch(r"[A-Za-z]\d+", s):
                continue                     # another prefix form (wisps 's3' against the numeric bound)
        m = re.match(r"[+-]?\d+", s)
        n = int(m.group(0)) if m else 0      # atoi
        if (lo is not None and n < lo) or (hi is not None and n > hi):
            out.append((kind, "%s %s=%s outside %s..%s" % (name_of(b, x), attr, raw, "" if lo is None else lo,
                                                          "" if hi is None else hi)))
    return out


def _legacy_refusal(table, mb, say=None, mid=""):
    try:
        v = parse(mb)
    except ParseError as e:
        return "it does not read as XML (%s)" % e
    if table is None:
        return None
    bad, skipped = [], []
    for x in v:
        if x.depth != 1:
            continue
        r = rec_of(table, name_of(mb, v[x.parent]), name_of(mb, x))
        if not r or not r.bounds:
            continue
        for kind, text in bound_check(mb, x, r):
            (bad if kind == "danger" else skipped).append(text)
    if skipped and say:
        say("legacy %s: %d records the loader skips (outside its table): %s" % (
            mid, len(skipped), ", ".join(skipped[:5]) + (" ..." if len(skipped) > 5 else "")))
    if bad:
        return "records outside the loader's table, the game would write past it: %s" % (
            ", ".join(bad[:5]) + (" ..." if len(bad) > 5 else ""))
    return None


# M1 extended (the owner, 02.10.2026): a mod's WHOLE table that hands the game a list of item ids - players.xml
# items=, challenges.xml startingitems/startingitems2/startingtrinkets - is MENDED, not refused and not passed as
# it is: the ids the game's own rule (0x73f904: 0 < id < the item count, not 43/61/235, an entry exists) does not
# take are left out of the list, everything else of the file stays. The loaders copy such lists unchecked and
# AddCollectible takes any number (the batch 5 notes, M1-M5). A negative id is a pocket active only in challenges.xml.
NOT_ITEMS = (43, 61, 235)


def _item_ok(game, fname, kind, t):
    s = t.strip()
    if not re.fullmatch(r"[+-]?[0-9]+", s):
        return False
    if kind != "collectible":
        return ref_ok(game, kind, s)
    n = int(s)
    if n < 0 and fname == "players.xml":
        return False
    n = abs(n)
    if n == 0 or n in NOT_ITEMS:
        return False
    return str(n) in game.ids()["collectible"]


def _mend_lists(table, fname, mb, game, say, mid):
    try:
        v = parse(mb)
    except ParseError:
        return mb
    ed = Edits()
    for x in v:
        if x.depth != 1:
            continue
        r = rec_of(table, name_of(mb, v[x.parent]), name_of(mb, x))
        if not r:
            continue
        have = attrs(mb, x)
        done = set()
        for a, _vs, _ve, _q, _ns in have:
            if a in done:
                continue
            done.add(a)
            kind = r.refs.get(a)
            if a not in r.lists or kind not in ("collectible", "trinket"):
                continue
            occ = [h for h in have if h[0] == a]
            old = mb[occ[-1][1]:occ[-1][2]].decode("utf-8", "replace")
            bad = [t for t in _tokens(old.encode("utf-8")) if not _item_ok(game, fname, kind, t)]
            if not bad:
                continue
            who = get_last(mb, x, "name")
            if who is None or not who.strip():
                who = key_of(mb, x, r) or name_of(mb, x)
            for t in bad:
                say("legacy %s: %s: %s %s does not exist - left out" % (mid, who, a, t))
            new = _list_value(old, r.lists[a], [], bad).encode("utf-8")
            for n, vs, ve, q, ns in occ:
                if mb[vs:ve] == new:
                    continue
                val, q2 = quote_value(new, q)
                if q2 == q:
                    ed.add(vs, ve, val)
                else:
                    ed.add(vs - 1, ve + 1, bytes([q2]) + val + bytes([q2]))
    return ed.apply(mb) if ed.v else mb


def _all_recs(table):
    for root, (ra, recs) in sorted(table.roots.items()):
        for r in recs:
            yield r


def _complete(table, mb, gb):
    """S7: the game's top-level records the base lacks, before each root's end (guard_table)"""
    try:
        mv, gv = parse(mb), parse(gb)
    except ParseError:
        return mb, 0
    ed = Edits()
    added = 0
    for gr in roots(gv):
        rname = name_of(gb, gr)
        mr = next((x for x in roots(mv) if name_of(mb, x) == rname), None)
        if mr is None or mr.inner_end == mr.stop:
            continue
        have = set()
        for x in kids(mv, mr.idx):
            r = rec_of(table, rname, name_of(mb, x))
            if r and r.key:
                k = key_of(mb, x, r)
                if k:
                    have.add(k)
        buf = []
        for x in kids(gv, gr.idx):
            r = rec_of(table, rname, name_of(gb, x))
            if not r or not r.key:
                continue
            k = key_of(gb, x, r)
            if not k or k in have:
                continue
            buf.append(b"\n" + gb[x.start:x.stop])
            added += 1
        if buf:
            ed.add(mr.inner_end, mr.inner_end, b"".join(buf) + b"\n")
    return (ed.apply(mb), added) if added else (mb, 0)


def _content_entities(fname, cur, mods, say):
    """S8: content/entities2.xml of every mod, strongest first"""
    v = parse(cur)
    root = next((x for x in roots(v) if name_of(cur, x) == "entities" and x.inner_end != x.stop), None)
    if root is None:
        say("content: the base has no whole <entities> root - nothing merged")
        return cur
    base_root = get(cur, root, "anm2root") or "gfx/"
    keys, types = set(), set()
    for x in kids(v, root.idx):
        if name_of(cur, x) == "entity":
            t, va, st = (_norm_key(get(cur, x, a) or d) for a, d in (("id", "-1"), ("variant", "0"), ("subtype", "0")))
            keys.add((t, va, st))
            types.add(t)
    out = []
    for m in mods:
        mb = m.get("content/" + fname)
        try:
            mv = parse(mb)
        except ParseError as e:
            say("content %s: does not read as XML (%s) - skipped" % (m.id, e))
            continue
        mr = next((x for x in roots(mv) if name_of(mb, x) == "entities" and x.inner_end != x.stop), None)
        if mr is None:
            say("content %s: no whole <entities> root - skipped" % m.id)
            continue
        mod_root = get(mb, mr, "anm2root") or "gfx/"
        prefix = b""
        if mod_root.lower() != base_root.lower():
            if mod_root.lower().startswith(base_root.lower()):
                prefix = mod_root[len(base_root):].encode("utf-8")
            else:
                say('content %s: anm2root "%s" is not under the game\'s "%s" - its paths are kept as they are'
                    % (m.id, mod_root, base_root))
        added, dup, newtype, wide, chunk = 0, 0, 0, 0, []
        erec = rec_of(T[fname], "entities", "entity")
        for x in kids(mv, mr.idx):
            if name_of(mb, x) != "entity":
                continue
            t, va, st = (_norm_key(get(mb, x, a) or d) for a, d in (("id", "-1"), ("variant", "0"), ("subtype", "0")))
            if t not in types or not re.fullmatch(r"\d+", t):
                newtype += 1
                continue
            if bound_check(mb, x, erec):
                wide += 1                    # a key the loader's packed map would alias onto another
                continue
            if (t, va, st) in keys:
                dup += 1
                continue
            keys.add((t, va, st))
            ap = next(((vs, ve) for n, vs, ve, q, ns in attrs(mb, x) if n == "anm2path"), None)
            if prefix and ap:
                chunk.append(mb[x.start:ap[0]] + prefix + mb[ap[0]:x.stop] + b"\n")
            else:
                chunk.append(mb[x.start:x.stop] + b"\n")
            added += 1
        say("content %s: %d new entities merged; %d the game or a stronger mod has already, %d of a type the game "
            "does not have (a script's creature) - left out%s" % (m.id, added, dup, newtype,
                                                                  "; %d with a variant/subtype past the loader's key "
                                                                  "(variant 0..4095, subtype 0..255) - left out" % wide
                                                                  if wide else ""))
        if added:
            out.append(b"\n<!-- " + m.id.encode("utf-8") + b" (content/entities2.xml, merged by the port) -->\n" + b"".join(chunk))
    if not out:
        return cur
    ed = Edits()
    ed.add(root.inner_end, root.inner_end, b"".join(out) + b"\n")
    return ed.apply(cur)


# ------------------------------------------------------------------ patch


DIRECTIVES = ("unset", "remove")


def _parse_ops(pb, px):
    """a patch element's operations, in document order: [(op, attr, raw value bytes)]"""
    ops = []
    for n, vs, ve, q, ns in attrs(pb, px):
        raw = pb[vs:ve]
        if n == "remove":
            ops.append(("remove", None, raw))
        elif n == "unset":
            for a in raw.decode("utf-8", "replace").split():
                ops.append(("unset", a, None))
        elif n.endswith("-add"):
            ops.append(("add", n[:-4], raw))
        elif n.endswith("-remove"):
            ops.append(("del", n[:-7], raw))
        else:
            ops.append(("set", n, raw))
    return ops


def _tokens(raw):
    return [t for t in re.split(r"[,\s]+", raw.decode("utf-8", "replace").strip()) if t != ""]


def _list_value(old, kind, adds, dels):
    """S4"""
    toks = [t for t in re.split(r"\s*,\s*|\s+", old.strip()) if t != ""] if old else []
    if kind == "multiset":
        sep = ", " if old and ", " in old else ","
        for d in dels:
            for i, t in enumerate(toks):
                if _norm_key(t) == _norm_key(d):
                    del toks[i]
                    break
        toks += adds
    else:
        sep = " " if not old or "," not in old else ","
        dn = set(_norm_key(d) for d in dels)
        toks = [t for t in toks if _norm_key(t) not in dn]
        for a in adds:
            if _norm_key(a) not in set(_norm_key(t) for t in toks):
                toks.append(a)
    return sep.join(toks)


class _Ctx:
    def __init__(self, table, cur, g, mid, base_id, owners, game, say):
        self.table, self.cur, self.g, self.mid, self.base_id = table, cur, g, mid, base_id
        self.owners, self.game, self.say = owners, game, say
        self.ed = Edits()
        self.nl = newline_of(cur)
        self.changed = 0
        self.refused = 0


def _apply_patch(table, cur, g, pb, mid, base_id, owners, game, say):
    try:
        pv = parse(pb)
    except ParseError as e:
        say("patch %s: refused - the file does not read as XML (%s)" % (mid, e))
        return cur
    v = parse(cur)
    c = _Ctx(table, cur, g, mid, base_id, owners, game, say)
    c.pv = pv
    c.pb = pb
    for pr in roots(pv):
        rname = name_of(pb, pr)
        if rname not in table.roots:
            say("patch %s: <%s> refused - the file has no such root" % (mid, rname))
            c.refused += 1
            continue
        br = next((x for x in roots(v) if name_of(cur, x) == rname), None)
        if br is None:
            say("patch %s: <%s> refused - the base has no such root" % (mid, rname))
            c.refused += 1
            continue
        rattrs = set(table.roots[rname][0].split())
        _apply_ops(c, v, br, _parse_ops(pb, pr), None, rname, rattrs, root=True)
        groups = _group(c, v, br, kids(pv, pr.idx), lambda tag: rec_of(table, rname, tag), "", rname)
        for x, r, k, pxs in groups:
            _apply_record(c, v, x, pxs, r, k)
    out = c.ed.apply(cur)
    say("patch %s: %d changes, %d refused" % (mid, c.changed, c.refused))
    return out


def _group(c, v, parent, pels, find, kprefix, rname, missing=None):
    """patch elements -> [(base element, rec, key, [patch elements])] in first-seen order; a base element
    named by several patch elements of one file gets their operations in document order"""
    pb, b = c.pb, c.cur
    order, by = [], {}
    index = {}                                # (rec class) -> {key: [base elements]}, built once per parent

    def lookup(r, kk):
        if r.cls not in index:
            m = {}
            for x in kids(v, parent.idx):
                if name_of(b, x) in r.tags:
                    m.setdefault(key_of(b, x, r), []).append(x)
            index[r.cls] = m
        return index[r.cls].get(kk, [])

    for px in pels:
        ptag = name_of(pb, px)
        r = find(ptag)
        if r is None:
            c.say("patch %s: %s<%s> refused - not a %s" % (c.mid, kprefix, ptag,
                                                          "record of <%s>" % rname if not kprefix else
                                                          "patchable child (positional or unknown)"))
            c.refused += 1
            continue
        if not r.key:
            hits = [x for x in kids(v, parent.idx) if name_of(b, x) in r.tags][:1]
            k = "%s%s[]" % (kprefix, r.cls)
        else:
            kk = key_of(pb, px, r)
            if kk is None:
                c.say("patch %s: %s<%s> refused - its key (%s) is missing" % (c.mid, kprefix, ptag,
                                                                             " ".join(a for a, d in r.key)))
                c.refused += 1
                continue
            k = kprefix + kk
            hits = lookup(r, kk)
        if not hits:
            if missing is not None:
                missing.append((r, k, px))
            else:
                _missing(c, rname, r, k, pb, px)
            continue
        for x in hits:
            if x.idx not in by:
                by[x.idx] = (x, r, k, [])
                order.append(x.idx)
            by[x.idx][3].append(px)
    return [by[i] for i in order]


def _missing(c, rname, r, k, pb, px):
    if "remove" in [o[0] for o in _parse_ops(pb, px)]:
        c.say("patch %s: %s remove refused - a record of a fixed table is never removed" % (c.mid, k))
    gv = c.game.parsed(c.table.name, c.g)
    gr = next((x for x in roots(gv) if name_of(c.g, x) == rname), None)
    in_game = gr is not None and any(name_of(c.g, x) in r.tags and key_of(c.g, x, r) == k for x in kids(gv, gr.idx))
    if in_game and c.base_id != "game":
        c.say("patch %s: %s refused - overridden by legacy %s (its file has no such record)" % (c.mid, k, c.base_id))
    else:
        c.say("patch %s: %s refused - no such record (a patch changes records; new ones go through content/ where "
              "the table allows it)" % (c.mid, k))
    c.refused += 1


def _ops_of(c, pxs):
    ops = []
    for px in pxs:
        ops += _parse_ops(c.pb, px)
    return ops


def _apply_record(c, v, x, pxs, r, k):
    pb = c.pb
    ops = _ops_of(c, pxs)
    if any(o[0] == "remove" for o in ops):
        if not r.remove:
            c.say("patch %s: %s remove refused - only pool entries may be removed" % (c.mid, k))
            c.refused += 1
            ops = [o for o in ops if o[0] != "remove"]
        else:
            _remove_element(c, x)        # the element goes: nothing else of it is edited
            c.changed += 1
            return
    _apply_ops(c, v, x, ops, r, k, None)
    pk_all = []
    for px in pxs:
        pk_all += kids(c.pv, px.idx)
    new = []
    groups = _group(c, v, x, pk_all, lambda tag: kid_of(r, tag), k + "/", None, missing=new)
    added = []
    for kr, kk, pk in new:
        kops = _parse_ops(pb, pk)
        if not kr.add or not kr.key:
            c.say("patch %s: %s refused - no such entry (this table adds none)" % (c.mid, kk))
            c.refused += 1
            continue
        if any(o[0] == "remove" for o in kops):
            c.say("patch %s: %s remove - no such entry, nothing to remove" % (c.mid, kk))
            continue
        if any(o[0] != "set" for o in kops):
            c.say("patch %s: %s refused - a new entry carries plain attributes only" % (c.mid, kk))
            c.refused += 1
            continue
        bad = _bad_refs(c, kr, kops)
        if bad:
            c.say("patch %s: %s refused - %s" % (c.mid, kk, bad))
            c.refused += 1
            continue
        added.append(pb[pk.start:pk.stop])
        c.changed += 1
    for y, kr, kk, pks in groups:
        _apply_record(c, v, y, pks, kr, kk)
    if added:
        _add_children(c, v, x, added)


def _bad_refs(c, r, ops):
    for op, a, raw in ops:
        kind = r.refs.get(a)
        if not kind or raw is None:
            continue
        for t in _tokens(raw) if a in r.lists or op != "set" else [raw.decode("utf-8", "replace")]:
            # M1 (the batch 5 notes, M1-M5): players.xml hands its numbers to AddCollectible as they are - a negative
            # one is a pocket active only in challenges.xml, here it is no item (the game's 0x73f904 rules)
            neg = c.table.name == "players.xml" and kind == "collectible" and t.strip().startswith("-")
            if neg or not ref_ok(c.game, kind, t):
                return "%s %s is not a %s of the game" % (a, t, kind)
    return None


def _apply_ops(c, v, x, ops, r, k, rattrs, root=False):
    b = c.cur
    if root:
        known, keyset, lists = rattrs, set(), {}
        k = "<%s>" % k
    else:
        known = r.attrs | c.game.attrs_seen(c.table, None, r)
        keyset = set(a for a, d in r.key)
        lists = r.lists
    have = attrs(b, x)
    final = {}           # attr -> ("set", raw) | ("unset",) | ("list", adds, dels)
    order = []
    for op, a, raw in ops:
        if op == "remove":
            continue
        if a in keyset:
            if op == "set" and _norm_key(raw.decode("utf-8", "replace")) == _norm_key(get_last(b, x, a) or ""):
                continue     # the key itself, as given to select the record
            c.say("patch %s: %s %s refused - a key attribute is never changed" % (c.mid, k, a))
            c.refused += 1
            continue
        if root and op != "set" and op != "unset":
            c.say("patch %s: %s %s refused - lists on a root" % (c.mid, k, a))
            c.refused += 1
            continue
        if op in ("add", "del") and a not in lists:
            c.say("patch %s: %s %s-%s refused - %s is not a list" % (c.mid, k, a, "add" if op == "add" else "remove", a))
            c.refused += 1
            continue
        if not root:
            bad = _bad_refs(c, r, [(op, a, raw)])
            if bad:
                c.say("patch %s: %s refused - %s" % (c.mid, k, bad))
                c.refused += 1
                continue
        if a not in known:
            c.say("patch %s: %s %s - the loader does not read this attribute (applied anyway)" % (c.mid, k, a))
        if a not in final:
            order.append(a)
        if op == "set":
            final[a] = ("set", raw)
        elif op == "unset":
            final[a] = ("unset",)
        else:
            prev = final.get(a)
            adds, dels = (list(prev[1]), list(prev[2])) if prev and prev[0] == "list" else ([], [])
            if prev and prev[0] == "set":
                old = prev[1].decode("utf-8", "replace")
                final[a] = ("setlist", old, [], [])
                prev = final[a]
            if prev and prev[0] == "setlist":
                if op == "add":
                    prev[2].extend(_tokens(raw))
                else:
                    prev[3].extend(_tokens(raw))
                continue
            (adds if op == "add" else dels).extend(_tokens(raw))
            final[a] = ("list", adds, dels)
    for a in order:
        f = final[a]
        occ = [h for h in have if h[0] == a]
        own_key = (c.table.name, k, a)
        prev_owner = c.owners.get(own_key)
        if f[0] in ("set", "unset", "setlist") and prev_owner and prev_owner[0] != c.mid and prev_owner[1] != "list":
            c.say("conflict %s %s: %s and %s - %s wins" % (k, a, prev_owner[0], c.mid, c.mid))
        elif f[0] in ("set", "unset", "setlist") and prev_owner and prev_owner[0] != c.mid:
            c.say("conflict %s %s: %s and %s - %s wins" % (k, a, prev_owner[0], c.mid, c.mid))
        c.owners[own_key] = (c.mid, "list" if f[0] == "list" else "set")
        if f[0] == "unset":
            for n, vs, ve, q, ns in occ:
                i = ns
                while i > x.as_ and _space(b[i - 1]):
                    i -= 1
                c.ed.add(i, ve + 1, b"")
            c.changed += bool(occ)
            continue
        if f[0] == "set":
            new = f[1]
        elif f[0] == "setlist":
            new = _list_value(f[1], lists[a], f[2], f[3]).encode("utf-8")
        else:
            old = b[occ[-1][1]:occ[-1][2]].decode("utf-8", "replace") if occ else ""
            new = _list_value(old, lists[a], f[1], f[2]).encode("utf-8")
        if occ:
            for n, vs, ve, q, ns in occ:
                if b[vs:ve] == new:
                    continue
                val, q2 = quote_value(new, q)
                if q2 == q:
                    c.ed.add(vs, ve, val)
                else:
                    c.ed.add(vs - 1, ve + 1, bytes([q2]) + val + bytes([q2]))
            c.changed += 1
        else:
            val, q2 = quote_value(new, 0x22)
            at = have[-1][2] + 1 if have else x.ne
            c.ed.add(at, at, b" " + a.encode("latin-1") + b"=" + bytes([q2]) + val + bytes([q2]))
            c.changed += 1


def _remove_element(c, x):
    """S6"""
    b = c.cur
    ind = indent_of(b, x.start)
    e = eol(b, x.stop)
    if ind is not None and e >= 0 and e < len(b):
        c.ed.add(x.start - len(ind), e + (2 if b.startswith(b"\r\n", e) else 1), b"")
    else:
        c.ed.add(x.start, x.stop, b"")


def _add_children(c, v, x, texts):
    """S5"""
    b = c.cur
    ks = kids(v, x.idx)
    if ks:
        last = ks[-1]
        ind = indent_of(b, last.start)
        at = line_end_after(b, last.stop)
        lead = c.nl + ind if ind is not None else b" "
    else:
        ind = indent_of(b, x.start)
        at = x.tag_end + 1
        lead = c.nl + ind + b"\t" if ind is not None else b" "
        if x.inner_end == x.stop:              # <Pool .../> - opened
            c.say("patch %s: a self-closing parent cannot take entries" % c.mid)
            return
    c.ed.add(at, at, b"".join(lead + t for t in texts))


# ======================================================================= stringtable


def _st_index(b, v):
    """languages [(id, name)] by index (Key column excluded), categories {name: el}, keys {(cat, key): el}"""
    langs, cats, keys = [], {}, {}
    root = next((x for x in roots(v) if name_of(b, x) == "stringtable"), None)
    if root is None:
        return None
    for x in kids(v, root.idx):
        t = name_of(b, x)
        if t == "languages":
            ls = []
            for y in kids(v, x.idx):
                if name_of(b, y) == "language":
                    ls.append((int(get(b, y, "index") or 0), get(b, y, "id") or "", get(b, y, "name") or ""))
            ls.sort()
            langs = [(i, n) for idx, i, n in ls if n != "Key"]
        elif t == "category":
            cats.setdefault(get(b, x, "name"), x)
            for y in kids(v, x.idx):
                if name_of(b, y) == "key":
                    keys.setdefault((get(b, x, "name"), get(b, y, "name")), y)
    return root, langs, cats, keys


def _st_patch(cur, pb, mid, fill, say, owners):
    """S9: one stringtable patch (fill: only empty or missing strings)"""
    v = parse(cur)
    ix = _st_index(cur, v)
    if ix is None:
        say("patch %s: refused - the base has no <stringtable>" % mid)
        return cur
    root, langs, cats, keys = ix
    try:
        pv = parse(pb)
    except ParseError as e:
        say("patch %s: refused - the file does not read as XML (%s)" % (mid, e))
        return cur
    nl = newline_of(cur)
    ed = Edits()
    changed = refused = 0
    pr = next((x for x in roots(pv) if name_of(pb, x) == "stringtable"), None)
    if pr is None:
        say("patch %s: refused - no <stringtable> root" % mid)
        return cur

    def col_of(ps, pos):
        lang = get(pb, ps, "lang")
        if lang is None:
            return pos
        for i, (lid, lname) in enumerate(langs):
            if lang.lower() == lname.lower() or lang == lid:
                return i
        return None

    new_keys = {}
    for pc in kids(pv, pr.idx):
        if name_of(pb, pc) != "category":
            continue
        cname = get(pb, pc, "name")
        if cname not in cats:
            say("patch %s: category %s refused - a new category (keys go into the game's categories)" % (mid, cname))
            refused += 1
            continue
        for pk in kids(pv, pc.idx):
            if name_of(pb, pk) != "key":
                continue
            kname = get(pb, pk, "name")
            want = {}
            pos = 0
            for ps in kids(pv, pk.idx):
                if name_of(pb, ps) != "string":
                    continue
                col = col_of(ps, pos)
                pos += 1
                if col is None:
                    say("patch %s: %s/%s refused - language %s is not in the file's <languages>"
                        % (mid, cname, kname, get(pb, ps, "lang")))
                    refused += 1
                    continue
                want[col] = pb[ps.tag_end + 1:ps.inner_end] if ps.inner_end != ps.stop else b""
            ke = keys.get((cname, kname))
            if ke is None:
                new_keys.setdefault(cname, []).append((kname, want))
                changed += 1
                continue
            strs = [y for y in kids(v, ke.idx) if name_of(cur, y) == "string"]
            extra = []
            for col in sorted(want):
                text = want[col]
                if col < len(strs):
                    s = strs[col]
                    old = cur[s.tag_end + 1:s.inner_end] if s.inner_end != s.stop else b""
                    if fill and old.strip():
                        continue
                    if old == text:
                        continue
                    own = (cname, kname, col)
                    if not fill and own in owners and owners[own] != mid:
                        say("conflict %s/%s %s: %s and %s - %s wins" % (cname, kname, langs[col][1], owners[own], mid, mid))
                    owners[own] = mid
                    if s.inner_end == s.stop:
                        ed.add(s.start, s.stop, b"<string>" + text + b"</string>")
                    else:
                        ed.add(s.tag_end + 1, s.inner_end, text)
                    changed += 1
                else:
                    extra.append((col, text))
            if extra:
                last = strs[-1] if strs else None
                ind = indent_of(cur, last.start) if last is not None else None
                if last is None or ind is None:
                    say("patch %s: %s/%s refused - the key has no <string> line to follow" % (mid, cname, kname))
                    refused += 1
                    continue
                at = line_end_after(cur, last.stop)
                parts, top = [], max(c for c, t in extra)
                got = dict(extra)
                for col in range(len(strs), top + 1):
                    parts.append(nl + ind + b"<string>" + got.get(col, b"") + b"</string>")
                ed.add(at, at, b"".join(parts))
                changed += len(extra)
    for cname in sorted(new_keys, key=lambda n: cats[n].start):
        ce = cats[cname]
        ks = [y for y in kids(v, ce.idx) if name_of(cur, y) == "key"]
        if not ks:
            say("patch %s: category %s refused - it has no key to follow" % (mid, cname))
            continue
        last = ks[-1]
        kind = indent_of(cur, last.start) or b""
        fs = next((y for y in kids(v, ks[0].idx) if name_of(cur, y) == "string"), None)
        sind = indent_of(cur, fs.start) if fs is not None else None
        if sind is None:
            sind = kind + b"    "
        at = line_end_after(cur, last.stop)
        parts = []
        for kname, want in new_keys[cname]:
            p = [nl + kind + b'<key name="' + kname.encode("utf-8") + b'">']
            for col in range(len(langs)):
                p.append(nl + sind + b"<string>" + want.get(col, b"") + b"</string>")
            p.append(nl + kind + b"</key>")
            parts.append(b"".join(p))
        ed.add(at, at, b"".join(parts))
    say("patch %s: %d strings changed, %d refused" % (mid, changed, refused) if not fill else
        "port: %d strings filled" % changed)
    return ed.apply(cur)


def _build_stringtable(fname, mods, game, port, lines):
    say = lambda s: lines.append("%s: %s" % (fname, s))
    g = game.file(fname)
    legacy = [m for m in mods if m.get("resources/" + fname) is not None]
    patches = [m for m in reversed(mods) if m.get("patch/" + fname) is not None]
    cur, base_id = g, "game"
    for i, m in enumerate(legacy):
        mb = m.get("resources/" + fname)
        try:
            v = parse(mb)
            ok = _st_index(mb, v) is not None
        except ParseError:
            ok = False
        if not ok:
            say("legacy %s: refused - it does not read as a <stringtable>; the next one below is used" % m.id)
            continue
        cur, base_id = mb, m.id
        for o in legacy[i + 1:]:
            say("legacy %s: not used - %s replaces the file (the top of MODS wins)" % (o.id, m.id))
        break
    owners = {}
    for m in patches:
        cur = _st_patch(cur, m.get("patch/" + fname), m.id, False, say, owners)
    if port is not None:
        cur = _st_patch(cur, port, "port", True, say, owners)
    if legacy or patches:
        say("base %s, patches from %d mods" % (base_id, len(patches)))
    return Result(cur, lines, base_id)


# ======================================================================= verify


def _all_data_files():
    out = []
    app = os.path.join(ROOT, "work", "app")
    for ed in sorted(os.listdir(app)):
        d = os.path.join(app, ed)
        if not os.path.isdir(d) or not ed.endswith("-resources"):
            continue
        for sub in sorted(os.listdir(d)):
            dd = os.path.join(d, sub)
            if not (sub == "data" or sub.startswith("data.")) or not os.path.isdir(dd):
                continue
            for f in sorted(os.listdir(dd)):
                if f.endswith(".xml") or f.endswith(".sta"):
                    out.append(os.path.join(dd, f))
    return out


def _roundtrip(fname, b, game):
    """set every record's first non-key attribute to a sentinel and back; add+unset a probe: bytes must return"""
    table = T[fname]
    v = parse(b)
    p_set, p_back, p_add, p_unset = [], [], [], []
    for r_el in roots(v):
        rname = name_of(b, r_el)
        if rname not in table.roots:
            continue
        sets, backs, adds, unsets = [], [], [], []
        seen = {}
        for x in kids(v, r_el.idx):
            r = rec_of(table, rname, name_of(b, x))
            if r and r.key:
                k = key_of(b, x, r)
                seen[k] = seen.get(k, 0) + 1
        for x in kids(v, r_el.idx):
            r = rec_of(table, rname, name_of(b, x))
            if not r or not r.key or key_of(b, x, r) is None or seen[key_of(b, x, r)] > 1:
                continue                     # the game's own duplicate keys (items null 124) - a patch edits both
            keyattrs = " ".join('%s="%s"' % (a, (get_last(b, x, a) or d)) for a, d in r.key
                                if get_last(b, x, a) is not None)
            if not keyattrs:
                continue
            tag = name_of(b, x)
            other = [(n, b[vs:ve]) for n, vs, ve, q, ns in attrs(b, x) if n not in [a for a, d in r.key] and q == 0x22
                     and n not in r.refs]
            if other and len(set(n for n, _ in other)) == len(other):
                n, val = other[0]
                sets.append('<%s %s %s="@@SIM@@"/>' % (tag, keyattrs, n))
                backs.append(b"<" + tag.encode() + b" " + keyattrs.encode() + b" " + n.encode() + b'="' + val + b'"/>')
            adds.append('<%s %s zzsim="1"/>' % (tag, keyattrs))
            unsets.append('<%s %s unset="zzsim"/>' % (tag, keyattrs))
        p_set.append("<%s>%s</%s>" % (rname, "".join(sets), rname))
        p_back.append(b"<" + rname.encode() + b">" + b"".join(backs) + b"</" + rname.encode() + b">")
        p_add.append("<%s zzsim=\"1\">%s</%s>" % (rname, "".join(adds), rname))
        p_unset.append("<%s unset=\"zzsim\">%s</%s>" % (rname, "".join(unsets), rname))
    m1 = Mod("sim-a", {"patch/" + fname: "".join(p_set).encode("utf-8")})
    m2 = Mod("sim-b", {"patch/" + fname: b"".join(p_back)})
    g1 = _Fixed(game, fname, b)
    r1 = build(fname, [m1], g1)
    r2 = build(fname, [m2], _Fixed(game, fname, r1.data))
    m3 = Mod("sim-c", {"patch/" + fname: "".join(p_add).encode("utf-8")})
    m4 = Mod("sim-d", {"patch/" + fname: "".join(p_unset).encode("utf-8")})
    r3 = build(fname, [m3], g1)
    r4 = build(fname, [m4], _Fixed(game, fname, r3.data))
    n_set = sum(s.count("@@SIM@@") for s in p_set)
    return r2.data == b and r4.data == b and (n_set == 0 or r1.data != b) and r3.data != b, n_set, r1, r3


class _Fixed(Game):
    """a Game whose file F is given bytes (the round trip's middle step)"""

    def __init__(self, game, fname, data):
        Game.__init__(self, game.dir)
        self._files = dict(game._files)
        self._files[fname] = data
        self._ids = game._ids


def verify():
    game = Game()
    ok = True
    files = _all_data_files()
    print("empty mod set, every data file of every edition (%d):" % len(files))
    for p in files:
        rel = os.path.relpath(p, os.path.join(ROOT, "work", "app")).replace("\\", "/")
        f = os.path.basename(p)
        gd = Game(os.path.dirname(p))
        b = open(p, "rb").read()
        try:
            v = parse(b)
            pe = "%d elements" % len(v)
        except ParseError as e:
            pe = "PARSE ERROR %s" % e
        r = build(f, [], gd)
        same = r.data == b
        ok &= same and not r.lines
        print("  %-62s %s  %s%s" % (rel, "identical" if same else "DIFFERS", pe, ("  lines: %s" % r.lines) if r.lines else ""))
    print("\nround trips on the Repentance tables (set -> restore, add -> unset):")
    for f in sorted(T):
        b = game.file(f)
        if b is None or not T[f].read:
            continue
        good, n, r1, r3 = _roundtrip(f, b, game)
        ok &= good
        print("  %-22s %s  (%d values set and restored)" % (f, "ok" if good else "FAILED", n))
        if not good:
            for l in (r1.lines + r3.lines)[:6]:
                print("      ", l)
    # stringtable: vanilla + the port's fill patch == the shipped file
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import port_additions
        for ed in ("repentance-resources", "rebirth-resources"):
            d = os.path.join(ROOT, "work", "app", ed, "data")
            # vanilla: the IPA's (work/app holds it since M8; the old .orig went to work/port-edit)
            vb = port_additions.vanilla_bytes("%s/data/stringtable.sta" % ed)
            if vb is None:
                continue
            gv = Game(d)
            gv._files[STRINGTABLE] = vb
            r = build(STRINGTABLE, [], gv, port=port_additions.stringtable_patch())
            # the port's version: work/port-edit since M8 (work/app holds the IPA's), else work/app
            shipped = port_additions.port_bytes("%s/data/stringtable.sta" % ed) or                 open(os.path.join(d, "stringtable.sta"), "rb").read()
            same = r.data == shipped
            ok &= same
            print("\n%s: vanilla .orig + port fill patch == shipped stringtable.sta: %s (%s)"
                  % (ed, "yes" if same else "NO", "; ".join(r.lines)))
    except ImportError:
        print("\nport_additions.py not found - the stringtable port check skipped")
    print("\nVERIFY", "OK" if ok else "FAILED")
    return ok


# ======================================================================= tests


def _expect(name, cond, detail=""):
    print("  %-64s %s%s" % (name, "ok" if cond else "FAILED", "" if cond else "  " + str(detail)))
    return cond


def tests():
    game = Game()
    ok = True
    P = "players.xml"
    g = game.file(P)
    print("synthetic mods:")

    # 1. two patches on one table, different records + one shared list
    a = Mod("aaa", {"patch/players.xml": b'<players><player id="13" items-add="412"/><player id="0" coins="5"/></players>'})
    bm = Mod("bbb", {"patch/players.xml": b'<players><player id="13" items-add="45"/><player id="1" bombs="3"/></players>'})
    r = build(P, [bm, a], game)                         # bbb stronger (top), aaa weaker
    v = parse(r.data)
    lil = next(x for x in v if name_of(r.data, x) == "player" and get(r.data, x, "id") == "13")
    ok &= _expect("two patches, one list: Lilith items 357,412 -> +412 (aaa), +45 (bbb)",
                  get(r.data, lil, "items") == "357,412,412,45", get(r.data, lil, "items"))
    ok &= _expect("two patches: no conflict line for list appends", not any("conflict" in l for l in r.lines), r.lines)
    isaac = next(x for x in v if name_of(r.data, x) == "player" and get(r.data, x, "id") == "0")
    ok &= _expect("two patches: new attribute coins appended once", get(r.data, isaac, "coins") == "5"
                  and r.data.count(b'coins="5"') == 1)
    # only the touched start tags differ
    ok &= _expect("two patches: bytes outside the edited tags are the game's",
                  _diff_spans(g, r.data) <= 3, "%d spans" % _diff_spans(g, r.data))

    # 2. conflict on one record attribute
    a2 = Mod("weak", {"patch/players.xml": b'<players><player id="2" coins="10"/></players>'})
    b2 = Mod("strong", {"patch/players.xml": b'<players><player id="2" coins="99"/></players>'})
    r = build(P, [b2, a2], game)
    x = next(x for x in parse(r.data) if name_of(r.data, x) == "player" and get(r.data, x, "id") == "2")
    ok &= _expect("conflict: the stronger mod wins", get(r.data, x, "coins") == "99")
    ok &= _expect("conflict: one conflict line", sum("conflict player[2] coins: weak and strong - strong wins" in l
                                                      for l in r.lines) == 1, r.lines)

    # 3. replacement (legacy) + patch: the patch lies on top; the legacy is completed from the game
    v0 = parse(g)
    pl = [x for x in v0 if name_of(g, x) == "player"]
    old = g[:pl[20].start] + g[pl[-1].stop:pl[-1].stop]   # drops players 20..40 (an "old" file, pre-tainted)
    old = old + b"\r\n</players>\r\n"
    old = old.replace(b'id="13" name="#LILITH_NAME"', b'id="13" name="#LILITH_NAME" portrait="null.png"', 1)
    leg = Mod("legacy-old", {"resources/players.xml": old})
    pat = Mod("patcher", {"patch/players.xml": b'<players><player id="13" items-add="412"/></players>'})
    r = build(P, [leg, pat], game)                      # legacy is the stronger one; the patch still lies on top
    v = parse(r.data)
    ids = [get(r.data, x, "id") for x in v if name_of(r.data, x) == "player"]
    ok &= _expect("replacement + patch: completed to 41 players", len(ids) == 41 and len(set(ids)) == 41, ids)
    lil = next(x for x in v if name_of(r.data, x) == "player" and get(r.data, x, "id") == "13")
    ok &= _expect("replacement + patch: the legacy portrait and the patch's item", get(r.data, lil, "portrait") == "null.png"
                  and get(r.data, lil, "items").endswith("412"), get(r.data, lil, "items"))
    ok &= _expect("replacement + patch: completion line", any("21 records of the game's were missing" in l for l in r.lines),
                  r.lines)

    # 4. two replacements: the top wins and says so
    leg2 = Mod("legacy-two", {"resources/players.xml": g})
    r = build(P, [leg2, leg], game)
    ok &= _expect("two replacements: the top one is the base", r.base == "legacy-two")
    ok &= _expect("two replacements: the other gets a 'not used' line",
                  any("legacy legacy-old: not used - legacy-two replaces the file" in l for l in r.lines), r.lines)

    # 5. bounds refusal (M4)
    bad = g.replace(b"</players>", b'\t<player id="41" name="Extra" skin="x.png"/>\r\n</players>')
    r = build(P, [Mod("over", {"resources/players.xml": bad})], game)
    ok &= _expect("bounds: players id 41 refused, the game's file used", r.data == g and r.base == "game", r.lines)
    ok &= _expect("bounds: the refusal line names it", any("player id=41" in l and "refused" in l for l in r.lines), r.lines)
    badi = game.file("items.xml").replace(b"</items>", b'<passive id="733" name="x" gfx="x.png"/></items>')
    r = build("items.xml", [Mod("over", {"resources/items.xml": badi})], game)
    ok &= _expect("bounds: collectible 733 refused", r.base == "game", r.lines)
    badc = game.file("challenges.xml").replace(b"</challenges>", b'<challenge id="46" name="x"/></challenges>')
    r = build("challenges.xml", [Mod("over", {"resources/challenges.xml": badc})], game)
    ok &= _expect("bounds: challenge 46 refused", r.base == "game", r.lines)

    # 5b. M1 mended (the owner, 02.10.2026): a whole players.xml / challenges.xml keeps everything but the ids the
    #     game's 0x73f904 does not take - each left-out id a line
    gp = game.file(P)
    vv = parse(gp)
    lil = next(x for x in vv if name_of(gp, x) == "player" and get(gp, x, "id") == "13")
    li_old = get(gp, lil, "items")
    li_new = li_old + ",9999,-5,43,0,12"
    mp = gp.replace(b'items="%s"' % li_old.encode(), b'items="%s"' % li_new.encode(), 1)
    r = build(P, [Mod("mend", {"resources/players.xml": mp})], game)
    L = " | ".join(r.lines)
    ok &= _expect("mend: players kept as the base", r.base == "mend", r.lines)
    ok &= _expect("mend: Lilith's list loses 9999, -5, 43, 0 and keeps 12",
                  get(r.data, next(x for x in parse(r.data) if name_of(r.data, x) == "player"
                                   and get(r.data, x, "id") == "13"), "items") == li_old + ",12", L)
    ok &= _expect("mend: the rest of the file untouched",
                  r.data == gp.replace(b'items="%s"' % li_old.encode(), b'items="%s"' % (li_old + ",12").encode(), 1), L)
    ok &= _expect("mend: a line per left-out id", all(("items %s does not exist - left out" % t) in L
                                                      for t in ("9999", "-5", "43", "0")), L)
    gc = game.file("challenges.xml")
    mc = gc.replace(b'startingitems="209,6,236,291" startingtrinkets="2"',
                    b'startingitems="209, -9999, 6,236,-584,291" startingtrinkets="2,999"', 1)
    r = build("challenges.xml", [Mod("mendc", {"resources/challenges.xml": mc})], game)
    L = " | ".join(r.lines)
    hb = next(x for x in parse(r.data) if name_of(r.data, x) == "challenge" and get(r.data, x, "id") == "2")
    ok &= _expect("mend: a challenge keeps its pocket active -584, loses -9999", get(r.data, hb, "startingitems")
                  == "209, 6, 236, -584, 291", get(r.data, hb, "startingitems"))
    ok &= _expect("mend: startingtrinkets loses 999", get(r.data, hb, "startingtrinkets") == "2", L)

    # 6. patch refusals: missing record, key change, unknown attr, bad item, list on non-list
    m = Mod("rules", {"patch/players.xml": b'<players><player id="41" coins="1"/><player id="3" id="4"/>'
                                            b'<player id="4" frobnicate="1"/><player id="5" items-add="9999"/>'
                                            b'<player id="6" coins-add="1"/></players>'})
    r = build(P, [m], game)
    L = " | ".join(r.lines)
    ok &= _expect("refusal: no such record", "player[41] refused - no such record" in L, L)
    ok &= _expect("refusal: a key attribute never changes", "player[3] id refused" in L or "player[4]" in L, L)
    ok &= _expect("warning: unknown attribute applied", "frobnicate - the loader does not read" in L, L)
    ok &= _expect("refusal: item 9999 does not exist", "items 9999 is not a collectible" in L, L)
    ok &= _expect("refusal: -add on a non-list", "coins-add refused" in L, L)

    # 7. overridden by legacy: the key exists in the game but not in the legacy base
    pr = Mod("late", {"patch/itempools.xml": b'<ItemPools><Pool Name="shellGame"><Item Id="1" Weight="2"/></Pool></ItemPools>'})
    ip = game.file("itempools.xml")
    vv = parse(ip)
    pools = [x for x in vv if name_of(ip, x) == "Pool"]
    sg = next(x for x in pools if get(ip, x, "Name") == "shellGame")
    # a legacy itempools without shellGame -> completion puts it back; so test with a non-completable record instead:
    leg_ch = game.file("challenges.xml")
    vc = parse(leg_ch)
    c7 = next(x for x in vc if name_of(leg_ch, x) == "challenge" and get(leg_ch, x, "id") == "7")
    leg_ch2 = leg_ch[:c7.start] + leg_ch[c7.stop:]
    r = build("challenges.xml", [Mod("old-ch", {"resources/challenges.xml": leg_ch2}),
                                  Mod("late", {"patch/challenges.xml": b'<challenges><challenge id="7" coins="9"/></challenges>'})],
              game)
    ok &= _expect("completion restores a record the legacy dropped", b'id="7"' in r.data, r.lines)

    # 8. itempools: add an Item, change one, remove one; then the round trip
    m = Mod("pools", {"patch/itempools.xml": b'<ItemPools><Pool Name="treasure">'
                                              b'<Item Id="118" Weight="1" DecreaseBy="1" RemoveOn="0.1"/>'
                                              b'<Item Id="1" Weight="0.5"/><Item Id="2" remove="true"/>'
                                              b'<Item Id="9999" Weight="1"/></Pool><Pool Name="nopool"/></ItemPools>'})
    r = build("itempools.xml", [m], game)
    v = parse(r.data)
    tr = next(x for x in v if name_of(r.data, x) == "Pool" and get(r.data, x, "Name") == "treasure")
    items = [(get(r.data, y, "Id"), get(r.data, y, "Weight")) for y in kids(v, tr.idx)]
    ok &= _expect("pools: Item 118 added at the end", items[-1] == ("118", "1"), items[-3:])
    ok &= _expect("pools: Item 1 weight 0.5", ("1", "0.5") in items)
    ok &= _expect("pools: Item 2 removed", all(i != "2" for i, w in items))
    ok &= _expect("pools: Item 9999 refused, unknown pool refused",
                  any("Id 9999 is not a collectible" in l for l in r.lines) and any("Pool[nopool] refused" in l for l in r.lines),
                  r.lines)
    m_back = Mod("undo", {"patch/itempools.xml": b'<ItemPools><Pool Name="treasure"><Item Id="118" remove="true"/>'
                                                  b'<Item Id="1" Weight="1"/></Pool></ItemPools>'})
    r2 = build("itempools.xml", [m_back], _Fixed(game, "itempools.xml", r.data))
    gi = parse(ip)
    t0 = next(x for x in gi if name_of(ip, x) == "Pool" and get(ip, x, "Name") == "treasure")
    i2 = next(y for y in kids(gi, t0.idx) if get(ip, y, "Id") == "2")
    expect = ip[:i2.start - 2] + ip[i2.stop + 2:]       # the game's minus the Item 2 line (CRLF, two tabs)
    ok &= _expect("pools: add+remove and set+restore give the game's bytes (minus Item 2)", r2.data == expect,
                  "%d spans differ" % _diff_spans(expect, r2.data))

    # 9. stringtable: a mod's new key, a translation, a conflict
    s1 = Mod("tr-weak", {"patch/stringtable.sta": '<stringtable><category name="Default"><key name="PROMPT_OK">'
                                                   '<string lang="Russian">Хорошо</string></key></category></stringtable>'.encode()})
    s2 = Mod("tr-strong", {"patch/stringtable.sta": '<stringtable><category name="Default"><key name="PROMPT_OK">'
                                                     '<string lang="Russian">ОК!</string></key><key name="MY_NEW">'
                                                     '<string>Hello</string></key></category></stringtable>'.encode()})
    r = build(STRINGTABLE, [s2, s1], game)
    ok &= _expect("stringtable: the stronger translation wins", "ОК!".encode() in r.data and "Хорошо".encode() not in r.data)
    ok &= _expect("stringtable: conflict line", any("conflict Default/PROMPT_OK Russian" in l for l in r.lines), r.lines)
    ok &= _expect("stringtable: new key with 8 strings", r.data.count(b'<key name="MY_NEW">') == 1, r.lines)
    vv = parse(r.data)
    nk = next(x for x in vv if name_of(r.data, x) == "key" and get(r.data, x, "name") == "MY_NEW")
    ok &= _expect("stringtable: the new key's strings", [r.data[y.tag_end + 1:y.inner_end] for y in kids(vv, nk.idx)]
                  == [b"Hello"] + [b""] * 7)

    # 10. content/entities2: a new variant, a duplicate, a new type
    ce = Mod("enemies", {"content/entities2.xml": b'<entities anm2root="gfx/"><entity id="10" variant="77" name="Red Gaper" '
                                                   b'anm2path="monsters/red.anm2"/><entity id="10" variant="0" name="dup"/>'
                                                   b'<entity id="999" name="lua"/></entities>'})
    r = build("entities2.xml", [ce], game)
    ok &= _expect("content: one new entity merged", b'variant="77" name="Red Gaper"' in r.data
                  and any("1 new entities merged; 1 the game or a stronger mod has already, 1 of a type" in l for l in r.lines),
                  r.lines)

    # 10b. the measured bounds of the other loaders
    bare = Game(other_dir="")                           # the bounds alone: no other version to pair records with
    def refused(fname, old, new_tail, mid="over"):
        b0 = bare.file(fname)
        i = b0.rfind(old)
        rr = build(fname, [Mod(mid, {"resources/" + fname: b0[:i] + new_tail + b0[i:]})], bare)
        return rr.base == "game", rr
    for fname, close, rec, why in (
            ("costumes2.xml", b"</costumes>", b'<costume id="132" anm2path="x.anm2" type="none"/>', "null costume 132"),
            ("costumes2.xml", b"</costumes>", b'<costume id="733" anm2path="x.anm2" type="passive"/>', "item costume 733"),
            ("wisps.xml", b"</wisps>", b'<wisp id="s19" hp="2"/>', "wisp s19"),
            ("wisps.xml", b"</wisps>", b'<wisp id="733" hp="2"/>', "wisp 733"),
            ("sounds.xml", b"</sounds>", b'<sound id="-1"><sample path="x.wav"/></sound>', "sound -1"),
            ("pocketitems.xml", b"</pocketitems>", b'<card id="98" name="x"/>', "card 98"),
            ("pocketitems.xml", b"</pocketitems>", b'<pilleffect id="50" name="x"/>', "pill 50"),
            ("stages.xml", b"</stages>", b'<stage id="37" name="x" path="x.stb"/>', "stage 37"),
            ("cutscenes.xml", b"</cutscenes>", b'<cutscene id="27" name="x"/>', "cutscene 27"),
            ("recipes.xml", b"</recipes>", '<recipe input="ééééé" output="1"/>'.encode(), "recipe non-ASCII input"),
            ("seedmenu.xml", b"</seedmenu>", b'<seed id="200" name="x"/>', "seed 200")):
        good, rr = refused(fname, close, rec)
        ok &= _expect("bounds: %s refused" % why, good, rr.lines)
    good, rr = refused("sounds.xml", b"</sounds>", b'<sound id="900"><sample path="x.wav"/></sound>')
    ok &= _expect("bounds: sound 900 (the loader skips it) passes with a warning",
                  not good and any("the loader skips" in l for l in rr.lines), rr.lines)
    good, rr = refused("music.xml", b"</music>", b'<track id="119" name="x" path="x.ogg"/>')
    ok &= _expect("bounds: track 119 (skipped by the loader) passes with a warning", not good, rr.lines)

    # 10c. VERSION (04.10.2026): a whole table made for Repentance+ - the three-way merge
    rp = game.other(P)
    if rp is not None:
        def recs(b, fname, root):
            v = parse(b)
            t = T[fname]
            out = {}
            for x in v:
                if x.depth == 1:
                    r = rec_of(t, root, name_of(b, x))
                    if r and r.key:
                        out[key_of(b, x, r)] = dict((a[0], b[a[1]:a[2]]) for a in attrs(b, x))
            return out
        # Sam & Max's kind: Repentance+'s players.xml with the mod's own costumeSuffix and skinColor
        sm = rp.replace(b'<player id="19" ', b'<player id="19" costumeSuffix="jacob" ', 1)
        sm = sm.replace(b'<player id="20" ', b'<player id="20" costumeSuffix="esau" ', 1)
        i = sm.find(b'<player id="20" ')
        j = sm.find(b'skinColor="', i)
        sm = sm[:j + 11] + b"0" + sm[sm.find(b'"', j + 11):]
        r = build(P, [Mod("sammax", {"resources/" + P: sm})], game)
        got, gg = recs(r.data, P, "players"), recs(g, P, "players")
        diff = sorted((k, a) for k in gg for a in set(gg[k]) | set(got.get(k, {})) if gg[k].get(a) != got.get(k, {}).get(a))
        ok &= _expect("version: a Repentance+ players.xml - the starting items back, the mod's own changes kept",
                      diff == [("player[19]", "costumeSuffix"), ("player[20]", "costumeSuffix"), ("player[20]", "skinColor")]
                      and got["player[2]"].get("items") == gg["player[2]"].get("items") and r.base == "sammax"
                      and any("a table of Repentance+" in l for l in r.lines), diff or r.lines)
        ok &= _expect("version: Eden's hair of Repentance+ - the game's", b"<hair" in r.data and
                      r.data.count(b"<hair") == g.count(b"<hair"), r.data.count(b"<hair"))
        # a mod's own value on an attribute Repentance+ changed too: the mod's
        own = rp.replace(b'<player id="2" ', b'<player id="2" items="1,2" ', 1) if b'<player id="2" items=' not in rp else rp
        r = build(P, [Mod("own", {"resources/" + P: own})], game)
        got = recs(r.data, P, "players")
        ok &= _expect("version: the mod's own items for Cain (neither the game's nor Repentance+'s) - the mod's",
                      got["player[2]"].get("items") == b"1,2", got["player[2]"])
        # the game's own file as a mod: nothing at all
        r = build(P, [Mod("same", {"resources/" + P: g})], game)
        ok &= _expect("version: the game's own table - no version line", r.data == g and
                      not any("Repentance+" in l for l in r.lines), r.lines)
        # items.xml of Repentance+: its null 132 left out before the bounds - the table is taken, not refused
        ri = game.other("items.xml")
        r = build("items.xml", [Mod("rp", {"resources/items.xml": ri.replace(b'description="#SACK_HEAD_DESCRIPTION"',
                                                                               b'description="moon gear"', 1)})], game)
        ok &= _expect("version: items.xml of Repentance+ - null 132 left out, the table taken with the mod's text",
                      r.base == "rp" and b"moon gear" in r.data and b'<null id="132"' not in r.data
                      and any("null[132] is Repentance+'s" in l for l in r.lines), r.lines[-4:])
        rg = recs(game.file("items.xml"), "items.xml", "items")
        gotI = recs(r.data, "items.xml", "items")
        diffI = sorted((k, a) for k in rg for a in set(rg[k]) | set(gotI.get(k, {})) if rg[k].get(a) != gotI.get(k, {}).get(a))
        ok &= _expect("version: items.xml - only the mod's text differs from the game's", diffI == [("passive[424]", "description")],
                      diffI[:6])
        # an older edition's data has no other version
        ok &= _expect("version: no other version outside Repentance's data", Game(os.path.join(ROOT, "work", "app",
                      "rebirth-resources", "data")).other(P) is None)

    # 11. the game does not read it
    r = build("menu_sounds.xml", [Mod("x", {"patch/menu_sounds.xml": b"<sounds/>"})], game)
    ok &= _expect("unread file: patch refused, game bytes", r.data == game.file("menu_sounds.xml")
                  and any("does not read" in l for l in r.lines))

    # 12. determinism: the same input twice -> the same bytes and lines
    r1 = build(P, [bm, a], game)
    r2 = build(P, [bm, a], game)
    ok &= _expect("deterministic", r1.data == r2.data and r1.lines == r2.lines)
    print("\nTESTS", "OK" if ok else "FAILED")
    return ok


def _diff_spans(a, b):
    """how many places differ (by lines)"""
    import difflib
    sm = difflib.SequenceMatcher(None, a.split(b"\n"), b.split(b"\n"), autojunk=False)
    return sum(1 for op in sm.get_opcodes() if op[0] != "equal")


# ======================================================================= table dump


def table_text():
    out = []
    for f in sorted(T):
        t = T[f]
        out.append("%s  loader %s%s%s" % (f, "0x%x" % t.loader if t.loader else "-", "" if t.read else "  NOT READ",
                                          "  content/ adds" if t.content else ""))
        for root, (ra, recs) in sorted(t.roots.items()):
            out.append("  <%s> %s" % (root, ra))
            for r in recs:
                key = "+".join(a if d is None else "%s=%s" % (a, d) for a, d in r.key) or "(singleton)"
                extra = []
                for attr, lo, hi, kind, when in r.bounds:
                    extra.append("%s %s %s..%s%s" % (kind, attr, "" if lo is None else lo, "" if hi is None else hi,
                                                     "" if not when else " if %s %s" % (when[0], "/".join(when[1])
                                                                                       if isinstance(when[1], tuple) else when[1])))
                if r.lists:
                    extra.append("lists " + ",".join("%s:%s" % kv for kv in sorted(r.lists.items())))
                if r.refs:
                    extra.append("refs " + ",".join("%s->%s" % kv for kv in sorted(r.refs.items())))
                out.append("    %-28s key %-26s %s" % ("/".join(r.tags), key, "; ".join(extra)))
                for k in r.kids:
                    out.append("      %-26s key %-26s %s" % ("/".join(k.tags), "+".join(a for a, d in k.key) or "(singleton)",
                                                             ("add " if k.add else "") + ("remove" if k.remove else "")))
        if t.note:
            out.append("  note: " + t.note)
    out.append("stringtable.sta  loader 0x8dffb8: category[name]/key[name]/string[position or lang]; new keys allowed")
    return "\n".join(out)


def main(argv):
    if "--verify" in argv:
        return 0 if verify() else 1
    if "--test" in argv:
        return 0 if tests() else 1
    if "--table" in argv:
        print(table_text())
        return 0
    if "--build" in argv:
        f = argv[argv.index("--build") + 1]
        mods = [Mod.from_dir(d) for d in argv[argv.index("--mods") + 1].split(",")] if "--mods" in argv else []
        r = build(f, mods)
        for l in r.lines:
            print("content:", l)
        if "--out" in argv:
            open(argv[argv.index("--out") + 1], "wb").write(r.data)
        return 0
    print(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
