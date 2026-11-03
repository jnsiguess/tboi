# -*- coding: utf-8 -*-
"""The port's additions to the game's .anm2 files, kept as PATCHES (plan 30.09, 4.9 M8).

The game's own .anm2 files stay as the IPA has them. What the port adds - its
straps, its menu rows, the MODS item, the D-pad and twin buttons - is a patch
laid over whatever .anm2 the game is about to load: the game's own, or a mod's
(portpatch.c does it at run time, this file is its twin and the desk's maker).
A patch names everything BY NAME - sheets by path, layers and nulls by name,
animations by name - so a mod's file with another structure still takes it,
and the numbers the port's code needs are read back from the result
(portpatch.c shim_port_layer / shim_port_null), never assumed.

---------------------------------------------------------------- the format
    <PortPatch Version="1">
      <Spritesheet Path="port_strap_credits.png" />
      <Layer Name="PortStrap0" Spritesheet="port_strap_credits.png" />
      <Layer Name="Item6" Spritesheet="GameMenu.png" SheetOf="Item5" />
      <Null Name="Area6" Before="AreaDailyRun" />
      <Animation Name="Idle" FrameNum="6" Loop="false">
        <RootAnimation> <Frame .../> </RootAnimation>
        <LayerAnimation Layer="PortStrap0" After="" Visible="true"> <Frame .../> </LayerAnimation>
        <NullAnimation Null="Area6" After="Area5" Visible="true"> ... </NullAnimation>
        <Triggers> ... </Triggers>
      </Animation>
    </PortPatch>

Every operation is idempotent: the patch over its own result changes nothing.
  Spritesheet  added when no sheet has that Path (case ignored); Id = max + 1.
  Layer        added when no layer has that Name; Id = max + 1. Its sheet is
               the one with the Path given, else the sheet of the layer named
               by SheetOf; neither found - the layer is refused (report line).
               An existing layer of that name is bound to that sheet.
               Other attributes are copied onto a new layer.
  Null         added when no null has that Name. With Before: it takes the Id
               of that null, which and every null after it move up by one (the
               NullIds of the animations follow). Without: Id = max + 1.
  Animation    found by Name; attributes other than Name are set. Missing: it
               is created (FrameNum/Loop from the patch, empty parts).
    RootAnimation / Triggers   the base's children are replaced.
    LayerAnimation  of the layer named: present - its Visible and frames are
               replaced in place; absent - inserted right after the
               LayerAnimation of the layer named After ("" = first; After not
               present - appended, with a report line). Unknown layer -
               refused (report line).
    NullAnimation   the same for nulls.

-------------------------------------------------------------- serialization
The result is written afresh, in the game's own style: the bytes before the
root element as they were, one tab per depth, `<Tag a="v" />` for an empty
element, the base's line break (CRLF if its first one is). Attribute values are
copied raw, quotes kept. A patch that changes nothing (the result writes
out exactly as the base parsed and written out) returns the base's bytes
untouched. portpatch.c writes exactly the same bytes (tools/portdesk checks).

Usage:
  py -3 tools/anm2patch.py --selftest     every registered anm2: vanilla + its
                                          patch == the port's file, twice ==
                                          once, the port's file + patch == itself
  py -3 tools/anm2patch.py --make VANILLA PORT [--out PATCH]
  py -3 tools/anm2patch.py --apply BASE PATCH [--out FILE]
"""
import os
import sys

# ======================================================================= reader


class Node:
    __slots__ = ("tag", "attrs", "kids")

    def __init__(self, tag, attrs=None, kids=None):
        self.tag = tag
        self.attrs = attrs if attrs is not None else []      # [name, raw value, quote byte]
        self.kids = kids if kids is not None else []

    def get(self, name):
        for a in self.attrs:
            if a[0] == name:
                return a[1].decode("utf-8", "replace")
        return None

    def set(self, name, value):
        raw = value.encode("utf-8") if isinstance(value, str) else value
        for a in self.attrs:
            if a[0] == name:
                a[1] = raw
                return
        self.attrs.append([name, raw, b'"'])

    def find(self, tag):
        for k in self.kids:
            if k.tag == tag:
                return k
        return None

    def copy(self):
        return Node(self.tag, [list(a) for a in self.attrs], [k.copy() for k in self.kids])


class Doc:
    __slots__ = ("prolog", "root", "nl")


class ParseError(Exception):
    pass


def _space(c):
    return c in (0x20, 0x09, 0x0D, 0x0A)


def _attrs(b, p, end):
    out = []
    while True:
        while p < end and _space(b[p]):
            p += 1
        if p >= end:
            break
        ns = p
        while p < end and b[p] != 0x3D and not _space(b[p]):
            p += 1
        name = b[ns:p].decode("latin-1")
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
        out.append([name, b[vs:p], bytes([q])])
        p += 1
    return out


def _tag_end(b, p, lim):
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
    """Doc of an .anm2 (or a patch): one root element; text, comments, <? ?> and <! > skipped"""
    lim = len(b)
    p = 0
    stack = []
    root = None
    root_at = -1
    while p < lim:
        lt = b.find(b"<", p)
        if lt < 0:
            break
        if b.startswith(b"<!--", lt):
            e = b.find(b"-->", lt + 4)
            if e < 0:
                raise ParseError("comment not closed at %d" % lt)
            p = e + 3
            continue
        if lt + 1 < lim and b[lt + 1] == 0x3F:
            e = b.find(b"?>", lt + 2)
            if e < 0:
                raise ParseError("<? not closed at %d" % lt)
            p = e + 2
            continue
        if lt + 1 < lim and b[lt + 1] == 0x21:
            e = _tag_end(b, lt, lim)
            if e < 0:
                raise ParseError("<! not closed at %d" % lt)
            p = e + 1
            continue
        if lt + 1 < lim and b[lt + 1] == 0x2F:
            e = b.find(b">", lt)
            if e < 0:
                raise ParseError("end tag not closed at %d" % lt)
            if not stack:
                raise ParseError("end tag without a start at %d" % lt)
            stack.pop()
            p = e + 1
            if not stack:
                break                               # the root is closed: the rest is ignored
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
        selfc = b[gt - 1] == 0x2F and gt - 1 >= q
        n = Node(b[lt + 1:q].decode("latin-1"), _attrs(b, q, gt - 1 if selfc else gt))
        if stack:
            stack[-1].kids.append(n)
        elif root is None:
            root, root_at = n, lt
        else:
            raise ParseError("a second root at %d" % lt)
        if not selfc:
            if len(stack) == 64:
                raise ParseError("nesting deeper than 64")
            stack.append(n)
        elif not stack and root is n:
            break
        p = gt + 1
    if root is None:
        raise ParseError("no element")
    if stack:
        raise ParseError("%d elements not closed" % len(stack))
    d = Doc()
    d.prolog = b[:root_at]
    d.root = root
    cr = b.find(b"\n")
    d.nl = b"\r\n" if cr > 0 and b[cr - 1] == 0x0D else b"\n"
    return d


def serialize(d):
    out = [d.prolog]

    def w(n, depth):
        out.append(b"\t" * depth + b"<" + n.tag.encode("latin-1"))
        for name, raw, q in n.attrs:
            out.append(b" " + name.encode("latin-1") + b"=" + q + raw + q)
        if not n.kids:
            out.append(b" />" + d.nl)
            return
        out.append(b">" + d.nl)
        for k in n.kids:
            w(k, depth + 1)
        out.append(b"\t" * depth + b"</" + n.tag.encode("latin-1") + b">" + d.nl)

    w(d.root, 0)
    return b"".join(out)


def canon(n):
    """a comparable form: tag, attributes as a sorted tuple (order of attributes is not meaning), kids in order"""
    return (n.tag, tuple(sorted((a[0], a[1]) for a in n.attrs)), tuple(canon(k) for k in n.kids))


# ======================================================================= model


def _num(s, default=-1):
    try:
        return int(s)
    except (TypeError, ValueError):
        return default


def _lower(s):
    """ASCII only, as portpatch.c folds"""
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in (s or ""))


class Model:
    """the parts of an .anm2 a patch touches, as live nodes of its Doc"""

    def __init__(self, doc):
        r = doc.root
        self.doc = doc
        self.content = r.find("Content")
        self.anims = r.find("Animations")
        if self.content is None or self.anims is None:
            raise ParseError("not an anm2: no Content/Animations")
        self.sheets = self._part("Spritesheets")
        self.layers = self._part("Layers")
        self.nulls = self._part("Nulls")

    def _part(self, tag):
        x = self.content.find(tag)
        if x is None:
            x = Node(tag)
            # the game writes Spritesheets, Layers, Nulls, Events in this order
            order = ["Spritesheets", "Layers", "Nulls", "Events"]
            at = len(self.content.kids)
            for i, k in enumerate(self.content.kids):
                if k.tag in order and order.index(k.tag) > order.index(tag):
                    at = i
                    break
            self.content.kids.insert(at, x)
        return x

    def sheet_by_path(self, path):
        for s in self.sheets.kids:
            if s.tag == "Spritesheet" and _lower(s.get("Path")) == _lower(path):
                return s
        return None

    def layer_by_name(self, name):
        for l in self.layers.kids:
            if l.tag == "Layer" and l.get("Name") == name:
                return l
        return None

    def null_by_name(self, name):
        for x in self.nulls.kids:
            if x.tag == "Null" and x.get("Name") == name:
                return x
        return None

    def layer_name(self, lid):
        for l in self.layers.kids:
            if l.tag == "Layer" and _num(l.get("Id")) == lid:
                return l.get("Name")
        return None

    def null_name(self, nid):
        for x in self.nulls.kids:
            if x.tag == "Null" and _num(x.get("Id")) == nid:
                return x.get("Name")
        return None

    def anim(self, name):
        for a in self.anims.kids:
            if a.tag == "Animation" and a.get("Name") == name:
                return a
        return None

    @staticmethod
    def max_id(part, tag):
        m = -1
        for x in part.kids:
            if x.tag == tag:
                m = max(m, _num(x.get("Id")))
        return m


def _anim_part(a, tag):
    x = a.find(tag)
    if x is None:
        x = Node(tag)
        order = ["RootAnimation", "LayerAnimations", "NullAnimations", "Triggers"]
        at = len(a.kids)
        for i, k in enumerate(a.kids):
            if k.tag in order and order.index(k.tag) > order.index(tag):
                at = i
                break
        a.kids.insert(at, x)
    return x


# ======================================================================= apply


def apply(base, patch, report=None):
    """base .anm2 bytes + patch bytes -> result bytes (the base itself when nothing changes)"""
    say = report.append if report is not None else (lambda s: None)
    doc = parse(base)
    m = Model(doc)
    p = parse(patch).root
    before = serialize(doc)
    for op in p.kids:
        if op.tag == "Spritesheet":
            path = op.get("Path")
            if path and m.sheet_by_path(path) is None:
                nid = Model.max_id(m.sheets, "Spritesheet") + 1
                m.sheets.kids.append(Node("Spritesheet", [["Id", str(nid).encode(), b'"'], ["Path", path.encode("utf-8"), b'"']]))
        elif op.tag == "Layer":
            name = op.get("Name")
            s = m.sheet_by_path(op.get("Spritesheet")) if op.get("Spritesheet") else None
            if s is None and op.get("SheetOf"):
                o = m.layer_by_name(op.get("SheetOf"))
                if o is not None:
                    sid = o.get("SpritesheetId")
                    for x in m.sheets.kids:
                        if x.tag == "Spritesheet" and x.get("Id") == sid:
                            s = x
            if s is None:
                say("layer %s: no sheet %s - refused" % (name, op.get("Spritesheet") or ""))
                continue
            l = m.layer_by_name(name)
            if l is None:
                nid = Model.max_id(m.layers, "Layer") + 1
                l = Node("Layer", [["Id", str(nid).encode(), b'"'], ["Name", name.encode("utf-8"), b'"'],
                                   ["SpritesheetId", s.get("Id").encode(), b'"']])
                for a in op.attrs:
                    if a[0] not in ("Name", "Spritesheet", "SheetOf", "Id", "SpritesheetId"):
                        l.attrs.append(list(a))
                m.layers.kids.append(l)
            elif l.get("SpritesheetId") != s.get("Id"):
                l.set("SpritesheetId", s.get("Id"))
        elif op.tag == "Null":
            name = op.get("Name")
            if m.null_by_name(name) is not None:
                continue
            before_n = m.null_by_name(op.get("Before")) if op.get("Before") else None
            if op.get("Before") and before_n is None:
                say("null %s: no null %s to go before - appended" % (name, op.get("Before")))
            if before_n is not None:
                nid = _num(before_n.get("Id"))
                for x in m.nulls.kids:
                    if x.tag == "Null" and _num(x.get("Id")) >= nid:
                        x.set("Id", str(_num(x.get("Id")) + 1))
                for a in m.anims.kids:
                    na = a.find("NullAnimations")
                    for x in (na.kids if na is not None else []):
                        if x.tag == "NullAnimation" and _num(x.get("NullId")) >= nid:
                            x.set("NullId", str(_num(x.get("NullId")) + 1))
                at = m.nulls.kids.index(before_n)
            else:
                nid = Model.max_id(m.nulls, "Null") + 1
                at = len(m.nulls.kids)
            n = Node("Null", [["Id", str(nid).encode(), b'"'], ["Name", name.encode("utf-8"), b'"']])
            for a in op.attrs:
                if a[0] not in ("Name", "Before", "Id"):
                    n.attrs.append(list(a))
            m.nulls.kids.insert(at, n)
        elif op.tag == "Animation":
            _apply_anim(m, op, say)
        else:
            say("unknown operation <%s> - skipped" % op.tag)
    after = serialize(doc)
    return base if after == before else after


def _apply_anim(m, op, say):
    name = op.get("Name")
    a = m.anim(name)
    if a is None:
        a = Node("Animation", [["Name", name.encode("utf-8"), b'"']])
        for k in ("RootAnimation", "LayerAnimations", "NullAnimations", "Triggers"):
            a.kids.append(Node(k))
        m.anims.kids.append(a)
    for at in op.attrs:
        if at[0] != "Name":
            a.set(at[0], at[1])
    for part in op.kids:
        if part.tag in ("RootAnimation", "Triggers"):
            _anim_part(a, part.tag).kids = [k.copy() for k in part.kids]
        elif part.tag in ("LayerAnimation", "NullAnimation"):
            is_layer = part.tag == "LayerAnimation"
            key = "Layer" if is_layer else "Null"
            idattr = "LayerId" if is_layer else "NullId"
            box = _anim_part(a, "LayerAnimations" if is_layer else "NullAnimations")
            target = part.get(key)
            node = m.layer_by_name(target) if is_layer else m.null_by_name(target)
            if node is None:
                say("%s: %s %s is not in the file - refused" % (name, key.lower(), target))
                continue
            tid = node.get("Id")
            cur = None
            for x in box.kids:
                if x.tag == part.tag and x.get(idattr) == tid:
                    cur = x
                    break
            if cur is not None:
                for at in part.attrs:
                    if at[0] not in (key, "After"):
                        cur.set(at[0], at[1])
                cur.kids = [k.copy() for k in part.kids]
                continue
            new = Node(part.tag, [[idattr, tid.encode(), b'"']])
            for at in part.attrs:
                if at[0] not in (key, "After"):
                    new.attrs.append(list(at))
            new.kids = [k.copy() for k in part.kids]
            after = part.get("After")
            pos = None
            if after == "" or after is None:
                pos = 0
            else:
                an = m.layer_by_name(after) if is_layer else m.null_by_name(after)
                if an is not None:
                    for i, x in enumerate(box.kids):
                        if x.tag == part.tag and x.get(idattr) == an.get("Id"):
                            pos = i + 1
                            break
                if pos is None:
                    say("%s: %s %s - %s is not drawn here, appended" % (name, key.lower(), target, after))
                    pos = len(box.kids)
            box.kids.insert(pos, new)
        else:
            say("%s: unknown part <%s> - skipped" % (name, part.tag))


# ======================================================================== make


def _id_names(model):
    return ({x.get("Id"): x.get("Name") for x in model.layers.kids if x.tag == "Layer"},
            {x.get("Id"): x.get("Name") for x in model.nulls.kids if x.tag == "Null"})


def make(vanilla, port, retarget=None):
    """the patch that turns VANILLA into PORT (bytes -> bytes).

    retarget: optional {"sheets": [(path, port_path)], "layers": {layer name: port sheet path},
    "clones": {(anim, layer name): (clone name, port sheet path, [frame index moved])}} -
    the port's pictures moved off a game sheet (effective.py / port_patch.py decide it)."""
    v, c = Model(parse(vanilla)), Model(parse(port))
    out = Node("PortPatch", [["Version", b"1", b'"']])
    vl, vn = _id_names(v)
    cl, cn = _id_names(c)
    if len(set(vl.values())) != len(vl) or len(set(cl.values())) != len(cl):
        raise ValueError("two layers share a name - a patch by name cannot say which")
    if len(set(vn.values())) != len(vn) or len(set(cn.values())) != len(cn):
        raise ValueError("two nulls share a name")
    # sheets
    vsheets = {x.get("Id"): x.get("Path") for x in v.sheets.kids if x.tag == "Spritesheet"}
    csheets = {x.get("Id"): x.get("Path") for x in c.sheets.kids if x.tag == "Spritesheet"}
    for sid in sorted(csheets, key=_num):
        if sid in vsheets and _lower(vsheets[sid]) != _lower(csheets[sid]):
            raise ValueError("sheet %s changed its path %s -> %s" % (sid, vsheets[sid], csheets[sid]))
        if sid not in vsheets:
            out.kids.append(Node("Spritesheet", [["Path", csheets[sid].encode("utf-8"), b'"']]))
    # layers
    vlayers = {x.get("Name"): x for x in v.layers.kids if x.tag == "Layer"}
    for x in sorted((x for x in c.layers.kids if x.tag == "Layer"), key=lambda x: _num(x.get("Id"))):
        name = x.get("Name")
        sid = x.get("SpritesheetId")
        old = vlayers.get(name)
        if old is not None:
            if old.get("Id") != x.get("Id"):
                raise ValueError("layer %s moved from %s to %s" % (name, old.get("Id"), x.get("Id")))
            if old.get("SpritesheetId") == sid:
                continue
        op = Node("Layer", [["Name", name.encode("utf-8"), b'"'], ["Spritesheet", csheets[sid].encode("utf-8"), b'"']])
        if sid in vsheets:
            same = [l for l in sorted(vlayers.values(), key=lambda l: _num(l.get("Id"))) if l.get("SpritesheetId") == sid]
            if same:
                op.attrs.append(["SheetOf", same[-1].get("Name").encode("utf-8"), b'"'])
        for a in x.attrs:
            if a[0] not in ("Id", "Name", "SpritesheetId"):
                op.attrs.append(list(a))
        out.kids.append(op)
    # nulls: the port's order, each new one before the next old one
    cnulls = [x for x in c.nulls.kids if x.tag == "Null"]
    vnames = [x.get("Name") for x in v.nulls.kids if x.tag == "Null"]
    for i, x in enumerate(cnulls):
        if x.get("Name") in vnames:
            continue
        op = Node("Null", [["Name", x.get("Name").encode("utf-8"), b'"']])
        nxt = [y.get("Name") for y in cnulls[i + 1:] if y.get("Name") in vnames]
        if nxt:
            op.attrs.append(["Before", nxt[0].encode("utf-8"), b'"'])
        for a in x.attrs:
            if a[0] not in ("Id", "Name"):
                op.attrs.append(list(a))
        out.kids.append(op)
    # animations
    for ca in c.anims.kids:
        if ca.tag != "Animation":
            continue
        name = ca.get("Name")
        va = v.anim(name)
        op = Node("Animation", [["Name", name.encode("utf-8"), b'"']])
        for a in ca.attrs:
            if a[0] != "Name" and (va is None or va.get(a[0]) != a[1].decode("utf-8", "replace")):
                op.attrs.append(list(a))
        for tag in ("RootAnimation", "Triggers"):
            cp = ca.find(tag)
            vp = va.find(tag) if va is not None else None
            if cp is not None and (vp is None or canon(cp) != canon(vp)):
                op.kids.append(Node(tag, [], [k.copy() for k in cp.kids]))
        for box, idattr, key, cnames, vnames_ in (("LayerAnimations", "LayerId", "Layer", cl, vl),
                                                  ("NullAnimations", "NullId", "Null", cn, vn)):
            cb = ca.find(box)
            vb = va.find(box) if va is not None else None
            vitems = {}
            vorder = []
            for x in (vb.kids if vb is not None else []):
                nm = vnames_.get(x.get(idattr))
                vitems[nm] = x
                vorder.append(nm)
            corder = [cnames.get(x.get(idattr)) for x in (cb.kids if cb is not None else [])]
            kept = [nm for nm in corder if nm in vitems]
            if kept != [nm for nm in vorder if nm in corder]:
                raise ValueError("%s: the port reorders the game's %s" % (name, box))
            if len(kept) != len(vorder):
                raise ValueError("%s: the port drops some of the game's %s" % (name, box))
            prev = ""
            for x in (cb.kids if cb is not None else []):
                nm = cnames.get(x.get(idattr))
                old = vitems.get(nm)
                xc = Node(x.tag, [a for a in x.attrs if a[0] != idattr], x.kids)
                if old is not None:
                    oc = Node(old.tag, [a for a in old.attrs if a[0] != idattr], old.kids)
                    if canon(xc) != canon(oc):
                        part = Node(x.tag, [[key, nm.encode("utf-8"), b'"']] + [list(a) for a in x.attrs if a[0] != idattr],
                                    [k.copy() for k in x.kids])
                        op.kids.append(part)
                else:
                    part = Node(x.tag, [[key, nm.encode("utf-8"), b'"'], ["After", prev.encode("utf-8"), b'"']]
                                + [list(a) for a in x.attrs if a[0] != idattr], [k.copy() for k in x.kids])
                    op.kids.append(part)
                prev = nm
        if va is None or op.kids or len(op.attrs) > 1:
            out.kids.append(op)
    if retarget:
        _retarget(out, v, c, retarget)
    d = Doc()
    d.prolog = b""
    d.root = out
    d.nl = b"\n"
    return serialize(d)


def _retarget(out, v, c, rt):
    """move the port's pictures onto port sheets (see make)"""
    have = {_lower(k.get("Path")) for k in out.kids if k.tag == "Spritesheet"}
    first_layer = next((i for i, k in enumerate(out.kids) if k.tag != "Spritesheet"), len(out.kids))
    for game_path, port_path in rt.get("sheets", []):
        if _lower(port_path) not in have:
            out.kids.insert(first_layer, Node("Spritesheet", [["Path", port_path.encode("utf-8"), b'"']]))
            first_layer += 1
            have.add(_lower(port_path))
    # port layers bound to a port sheet
    for name, port_path in rt.get("layers", {}).items():
        for k in out.kids:
            if k.tag == "Layer" and k.get("Name") == name:
                k.set("Spritesheet", port_path)
                k.attrs = [a for a in k.attrs if a[0] != "SheetOf"]
                break
        else:
            raise ValueError("retarget: %s is not a layer the port adds" % name)
    # clones of game layers on a port sheet, placed right after them in the animations
    made = set()
    for (anim, lname), (clone, port_path, moved) in sorted(rt.get("clones", {}).items()):
        if clone not in made:
            made.add(clone)
            at = max(i for i, k in enumerate(out.kids) if k.tag in ("Spritesheet", "Layer", "Null")) + 1
            out.kids.insert(at, Node("Layer", [["Name", clone.encode("utf-8"), b'"'],
                                               ["Spritesheet", port_path.encode("utf-8"), b'"']]))
        op = next((k for k in out.kids if k.tag == "Animation" and k.get("Name") == anim), None)
        if op is None:
            raise ValueError("retarget: animation %s is not the port's" % anim)
        part = next((k for k in op.kids if k.tag == "LayerAnimation" and k.get("Layer") == lname), None)
        if part is None:
            raise ValueError("retarget: %s/%s is not changed by the port" % (anim, lname))
        frames = [k for k in part.kids if k.tag == "Frame"]
        cl = Node("LayerAnimation", [["Layer", clone.encode("utf-8"), b'"'], ["After", lname.encode("utf-8"), b'"']]
                  + [list(a) for a in part.attrs if a[0] not in ("Layer", "After")], [])
        for i, f in enumerate(frames):
            g = f.copy()
            if i not in moved:
                g.set("Visible", "false")
            else:
                f.set("Visible", "false")
            cl.kids.append(g)
        op.kids.insert(op.kids.index(part) + 1, cl)
        # what the port drew right after the game layer now comes after its clone
        for k in op.kids[op.kids.index(cl) + 1:]:
            if k.tag == "LayerAnimation" and k.get("After") == lname:
                k.set("After", clone)


# ======================================================================= tests


def _registered_anm2():
    """(path, vanilla bytes, port bytes) of every registered anm2 the port changed or added over a game base"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import port_additions as pa
    out = []
    for e in pa.entries():
        if not e["path"].endswith(".anm2"):
            continue
        cur = pa.port_bytes(e["path"])
        base = pa.base_of(e["path"])
        if cur is None or base is None:
            continue
        out.append((e["path"], pa.vanilla_bytes(base), cur))
    return out


def selftest():
    ok = True
    for path, van, cur in _registered_anm2():
        rep = []
        try:
            patch = make(van, cur)
            got = apply(van, patch, rep)
            again = apply(got, patch, rep)
            over_port = apply(cur, patch, rep)
            good = (canon(parse(got).root) == canon(parse(cur).root) and again == got
                    and canon(parse(over_port).root) == canon(parse(cur).root) and not rep)
        except (ValueError, ParseError) as e:
            good, patch = False, b""
            rep.append(str(e))
        ok &= good
        print("  %-72s %s %6d B%s" % (path, "ok  " if good else "FAIL", len(patch), ("  " + "; ".join(rep)) if rep else ""))
    print("SELFTEST", "OK" if ok else "FAILED")
    return ok


def main(argv):
    def arg(k):
        return argv[argv.index(k) + 1] if k in argv and argv.index(k) + 1 < len(argv) else None
    if "--selftest" in argv:
        return 0 if selftest() else 1
    if "--make" in argv:
        i = argv.index("--make")
        b = make(open(argv[i + 1], "rb").read(), open(argv[i + 2], "rb").read())
    elif "--apply" in argv:
        i = argv.index("--apply")
        rep = []
        b = apply(open(argv[i + 1], "rb").read(), open(argv[i + 2], "rb").read(), rep)
        for r in rep:
            print("report:", r, file=sys.stderr)
    else:
        print(__doc__)
        return 0
    o = arg("--out")
    if o:
        open(o, "wb").write(b)
    else:
        sys.stdout.buffer.write(b)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
