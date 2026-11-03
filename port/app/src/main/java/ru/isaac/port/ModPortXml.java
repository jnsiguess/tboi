package ru.isaac.port;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/*
 * XML AS tools/modport.py SEES IT (ModPort, the converter inside the installer, 04.10.2026).
 *
 * The converter's output must be the very bytes modport.py writes, so this is Python's
 * xml.etree.ElementTree in miniature, not a DOM: an element keeps its attributes in the order they
 * were written, its text and its tail; comments, processing instructions and the DOCTYPE are dropped
 * (ET's TreeBuilder drops them); a file that expat refuses (a bare &, a < inside a value, a repeated
 * attribute, an undefined entity, junk after the root...) is refused here too, so the converter takes
 * the same lenient path. tostring() writes what ET.tostring(root, encoding='utf-8',
 * xml_declaration=True) writes: the declaration in single quotes, " />" for an empty element, the
 * attribute escapes of Python 3.13 (&#10; &#13; &#09; for the line breaks and tabs a value carries).
 * Measured against Python on every .anm2 and .xml of the game and of the 25 workshop mods
 * (tools/umdesk/test_converters.py).
 */
final class ModPortXml {
    private ModPortXml() { }

    static final class Bad extends Exception {
        Bad(String why) { super(why); }
    }

    /* an element: ET's Element */
    static final class El {
        String tag;
        final List<String> keys = new ArrayList<>();
        final List<String> vals = new ArrayList<>();
        String text, tail;                       /* null: none, as ET */
        final List<El> kids = new ArrayList<>();

        El(String tag) { this.tag = tag; }

        String get(String k) {
            int i = keys.indexOf(k);
            return i < 0 ? null : vals.get(i);
        }
        String get(String k, String def) {
            String v = get(k);
            return v == null ? def : v;
        }
        void set(String k, String v) {
            int i = keys.indexOf(k);
            if (i < 0) { keys.add(k); vals.add(v); } else vals.set(i, v);
        }
        /* ET's find(tag): the first child of that tag */
        El find(String t) {
            for (El e : kids) if (e.tag.equals(t)) return e;
            return null;
        }
        /* ET's findall("a/b"): children by a path of tags */
        List<El> findall(String path) {
            List<El> cur = new ArrayList<>();
            cur.add(this);
            for (String step : path.split("/")) {
                List<El> next = new ArrayList<>();
                for (El e : cur) for (El k : e.kids) if (k.tag.equals(step)) next.add(k);
                cur = next;
            }
            return cur;
        }
        /* ET's SubElement */
        El sub(String t, String... kv) {
            El e = new El(t);
            for (int i = 0; i + 1 < kv.length; i += 2) e.set(kv[i], kv[i + 1]);
            kids.add(e);
            return e;
        }
        /* ET's iter(): this element and every one below, in document order */
        void iter(List<El> out) {
            out.add(this);
            for (El k : kids) k.iter(out);
        }
        List<El> iter() {
            List<El> out = new ArrayList<>();
            iter(out);
            return out;
        }
        /* copy.deepcopy: the tail goes with it */
        El deepCopy() {
            El e = new El(tag);
            e.keys.addAll(keys);
            e.vals.addAll(vals);
            e.text = text;
            e.tail = tail;
            for (El k : kids) e.kids.add(k.deepCopy());
            return e;
        }
    }

    /* ------------------------------------------------------------------ reading */

    private static final Pattern DECL_ENC = Pattern.compile("^<\\?xml[^>]*?encoding\\s*=\\s*[\"']([A-Za-z0-9._-]+)[\"']");

    static String decode(byte[] b) throws Bad {
        int at = 0;
        Charset cs = StandardCharsets.UTF_8;
        if (b.length >= 3 && (b[0] & 0xff) == 0xef && (b[1] & 0xff) == 0xbb && (b[2] & 0xff) == 0xbf) at = 3;
        else if (b.length >= 2 && (b[0] & 0xff) == 0xff && (b[1] & 0xff) == 0xfe) { at = 2; cs = StandardCharsets.UTF_16LE; }
        else if (b.length >= 2 && (b[0] & 0xff) == 0xfe && (b[1] & 0xff) == 0xff) { at = 2; cs = StandardCharsets.UTF_16BE; }
        else {
            String head = new String(b, 0, Math.min(b.length, 200), StandardCharsets.ISO_8859_1);
            Matcher m = DECL_ENC.matcher(head);
            if (m.find()) {
                String name = m.group(1).toLowerCase(java.util.Locale.ROOT);
                if (!name.equals("utf-8") && !name.equals("utf8")) {
                    try { cs = Charset.forName(name); } catch (RuntimeException e) { throw new Bad("unknown encoding"); }
                }
            }
        }
        try {
            CharBuffer c = cs.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(b, at, b.length - at));
            return c.toString();
        } catch (CharacterCodingException e) {
            throw new Bad("not well-formed (invalid token)");
        }
    }

    static El parse(byte[] b) throws Bad { return new Reader(decode(b)).document(); }

    static El parse(String s) throws Bad { return new Reader(s).document(); }

    private static final class Reader {
        final String s;
        int p;
        Reader(String s) { this.s = s; }

        Bad bad(String why) { return new Bad(why + " at " + p); }

        boolean at(String t) { return s.startsWith(t, p); }

        static boolean nameStart(char c) {
            return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c != 0xD7 && c != 0xF7 && c < 0xFFFE);
        }
        static boolean nameChar(char c) {
            return nameStart(c) || c == '-' || c == '.' || (c >= '0' && c <= '9') || c == 0xB7;
        }
        static boolean xmlChar(int c) {
            return c == 9 || c == 10 || c == 13 || (c >= 0x20 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0x10FFFF);
        }

        String name() throws Bad {
            int a = p;
            if (p >= s.length() || !nameStart(s.charAt(p))) {
                if (p < s.length() && s.charAt(p) == ':') throw bad("unbound prefix");
                throw bad("not well-formed (invalid token)");
            }
            p++;
            while (p < s.length() && (nameChar(s.charAt(p)) || s.charAt(p) == ':')) p++;
            String n = s.substring(a, p);
            if (n.indexOf(':') >= 0) throw bad("unbound prefix");
            return n;
        }

        void ws() { while (p < s.length() && isWs(s.charAt(p))) p++; }
        static boolean isWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

        /* a reference after '&': its characters */
        String ref() throws Bad {
            int semi = s.indexOf(';', p);
            if (semi < 0) throw bad("not well-formed (invalid token)");
            String body = s.substring(p + 1, semi);
            if (body.startsWith("#")) {
                int cp;
                try {
                    if (body.startsWith("#x")) {
                        if (body.length() < 3 || !body.substring(2).matches("[0-9A-Fa-f]+")) throw bad("not well-formed (invalid token)");
                        cp = Integer.parseInt(body.substring(2), 16);
                    } else {
                        if (body.length() < 2 || !body.substring(1).matches("[0-9]+")) throw bad("not well-formed (invalid token)");
                        cp = Integer.parseInt(body.substring(1));
                    }
                } catch (NumberFormatException e) { throw bad("reference to invalid character number"); }
                if (!xmlChar(cp)) throw bad("reference to invalid character number");
                p = semi + 1;
                return new String(Character.toChars(cp));
            }
            if (body.isEmpty() || !nameStart(body.charAt(0))) throw bad("not well-formed (invalid token)");
            for (int i = 1; i < body.length(); i++) if (!nameChar(body.charAt(i))) throw bad("not well-formed (invalid token)");
            String v;
            switch (body) {
                case "amp": v = "&"; break;
                case "lt": v = "<"; break;
                case "gt": v = ">"; break;
                case "quot": v = "\""; break;
                case "apos": v = "'"; break;
                default: throw bad("undefined entity");
            }
            p = semi + 1;
            return v;
        }

        void misc() throws Bad {
            for (;;) {
                ws();
                if (at("<!--")) {
                    int e = s.indexOf("-->", p + 4);
                    if (e < 0) throw bad("unclosed token");
                    p = e + 3;
                } else if (at("<?")) {
                    int e = s.indexOf("?>", p + 2);
                    if (e < 0) throw bad("unclosed token");
                    p = e + 2;
                } else if (at("<!DOCTYPE")) {
                    int depth = 0;
                    while (p < s.length()) {
                        char c = s.charAt(p++);
                        if (c == '[') depth++;
                        else if (c == ']') depth--;
                        else if (c == '>' && depth <= 0) break;
                    }
                } else return;
            }
        }

        El document() throws Bad {
            misc();
            if (p >= s.length()) throw bad("no element found");
            if (s.charAt(p) != '<') throw bad("not well-formed (invalid token)");
            El root = element();
            misc();
            if (p < s.length()) throw bad("junk after document element");
            return root;
        }

        /* the start tag at p ('<'), its content and its end tag; returns the element */
        El element() throws Bad {
            List<El> stack = new ArrayList<>();
            List<StringBuilder> data = new ArrayList<>();       /* the pending text of each open element */
            El root = null;
            for (;;) {
                if (p >= s.length()) throw bad(stack.isEmpty() ? "no element found" : "unclosed token");
                char c = s.charAt(p);
                if (c == '<') {
                    if (at("</")) {
                        p += 2;
                        String n = name();
                        ws();
                        if (p >= s.length() || s.charAt(p) != '>') throw bad("not well-formed (invalid token)");
                        p++;
                        El top = stack.get(stack.size() - 1);
                        if (!top.tag.equals(n)) throw bad("mismatched tag");
                        flush(stack, data);
                        stack.remove(stack.size() - 1);
                        data.remove(data.size() - 1);
                        if (stack.isEmpty()) return root;
                        continue;
                    }
                    if (at("<!--")) {
                        int e = s.indexOf("-->", p + 4);
                        if (e < 0) throw bad("unclosed token");
                        p = e + 3;
                        continue;
                    }
                    if (at("<![CDATA[")) {
                        if (stack.isEmpty()) throw bad("not well-formed (invalid token)");
                        int e = s.indexOf("]]>", p + 9);
                        if (e < 0) throw bad("unclosed CDATA section");
                        String raw = s.substring(p + 9, e);
                        for (int i = 0; i < raw.length(); i++) if (!xmlChar(raw.codePointAt(i))) throw bad("not well-formed (invalid token)");
                        data.get(data.size() - 1).append(lines(raw));
                        p = e + 3;
                        continue;
                    }
                    if (at("<?")) {
                        int e = s.indexOf("?>", p + 2);
                        if (e < 0) throw bad("unclosed token");
                        p = e + 2;
                        continue;
                    }
                    if (at("<!")) throw bad("not well-formed (invalid token)");
                    p++;
                    El e = new El(name());
                    for (;;) {
                        int before = p;
                        ws();
                        if (p >= s.length()) throw bad("unclosed token");
                        char d = s.charAt(p);
                        if (d == '>' || d == '/') break;
                        if (p == before) throw bad("not well-formed (invalid token)");
                        String k = name();
                        ws();
                        if (p >= s.length() || s.charAt(p) != '=') throw bad("not well-formed (invalid token)");
                        p++;
                        ws();
                        if (p >= s.length()) throw bad("unclosed token");
                        char q = s.charAt(p);
                        if (q != '"' && q != '\'') throw bad("not well-formed (invalid token)");
                        p++;
                        StringBuilder v = new StringBuilder();
                        for (;;) {
                            if (p >= s.length()) throw bad("unclosed token");
                            char x = s.charAt(p);
                            if (x == q) { p++; break; }
                            if (x == '<') throw bad("not well-formed (invalid token)");
                            if (x == '&') { v.append(ref()); continue; }
                            int cp = s.codePointAt(p);
                            if (!xmlChar(cp)) throw bad("not well-formed (invalid token)");
                            if (x == '\r') {
                                v.append(' ');
                                p++;
                                if (p < s.length() && s.charAt(p) == '\n') p++;
                                continue;
                            }
                            if (x == '\n' || x == '\t') { v.append(' '); p++; continue; }
                            v.appendCodePoint(cp);
                            p += Character.charCount(cp);
                        }
                        if (e.keys.contains(k)) throw bad("duplicate attribute");
                        e.keys.add(k);
                        e.vals.add(v.toString());
                    }
                    boolean empty = s.charAt(p) == '/';
                    if (empty) {
                        p++;
                        if (p >= s.length() || s.charAt(p) != '>') throw bad("not well-formed (invalid token)");
                    }
                    p++;
                    if (stack.isEmpty()) root = e;
                    else {
                        flush(stack, data);
                        stack.get(stack.size() - 1).kids.add(e);
                    }
                    if (empty) {
                        if (stack.isEmpty()) return root;
                        continue;
                    }
                    stack.add(e);
                    data.add(new StringBuilder());
                    continue;
                }
                if (stack.isEmpty()) throw bad("not well-formed (invalid token)");
                if (c == '&') { data.get(data.size() - 1).append(ref()); continue; }
                if (at("]]>")) throw bad("not well-formed (invalid token)");
                int cp = s.codePointAt(p);
                if (!xmlChar(cp)) throw bad("not well-formed (invalid token)");
                StringBuilder d = data.get(data.size() - 1);
                if (c == '\r') {
                    d.append('\n');
                    p++;
                    if (p < s.length() && s.charAt(p) == '\n') p++;
                    continue;
                }
                d.appendCodePoint(cp);
                p += Character.charCount(cp);
            }
        }

        /* the open element's pending text: its text before its first child, the last child's tail after it */
        static void flush(List<El> stack, List<StringBuilder> data) {
            StringBuilder d = data.get(data.size() - 1);
            if (d.length() == 0) return;
            El top = stack.get(stack.size() - 1);
            if (top.kids.isEmpty()) top.text = top.text == null ? d.toString() : top.text + d;
            else {
                El last = top.kids.get(top.kids.size() - 1);
                last.tail = last.tail == null ? d.toString() : last.tail + d;
            }
            d.setLength(0);
        }

        static String lines(String raw) {
            return raw.replace("\r\n", "\n").replace('\r', '\n');
        }
    }

    /* ------------------------------------------------------------------ writing */

    static byte[] tostring(El root) {
        StringBuilder b = new StringBuilder("<?xml version='1.0' encoding='utf-8'?>\n");
        write(b, root);
        return b.toString().getBytes(StandardCharsets.UTF_8);
    }

    private static void write(StringBuilder b, El e) {
        b.append('<').append(e.tag);
        for (int i = 0; i < e.keys.size(); i++) b.append(' ').append(e.keys.get(i)).append("=\"").append(escAttr(e.vals.get(i))).append('"');
        if (e.text != null && !e.text.isEmpty() || !e.kids.isEmpty()) {
            b.append('>');
            if (e.text != null) b.append(escText(e.text));
            for (El k : e.kids) write(b, k);
            b.append("</").append(e.tag).append('>');
        } else {
            b.append(" />");
        }
        if (e.tail != null && !e.tail.isEmpty()) b.append(escText(e.tail));
    }

    static String escText(String t) {
        if (t.indexOf('&') >= 0) t = t.replace("&", "&amp;");
        if (t.indexOf('<') >= 0) t = t.replace("<", "&lt;");
        if (t.indexOf('>') >= 0) t = t.replace(">", "&gt;");
        return t;
    }

    static String escAttr(String t) {
        t = escText(t);
        if (t.indexOf('"') >= 0) t = t.replace("\"", "&quot;");
        if (t.indexOf('\r') >= 0) t = t.replace("\r", "&#13;");
        if (t.indexOf('\n') >= 0) t = t.replace("\n", "&#10;");
        if (t.indexOf('\t') >= 0) t = t.replace("\t", "&#09;");
        return t;
    }

    /* ------------------------------------------------------------------ the game's parser's forgiveness */

    /* modport.py lenient_xml: a bare &, a < inside a value, a repeated attribute (the first wins) - on the
       bytes, as Python's bytes regexes (\w is [A-Za-z0-9_], \s is [ \t\n\r\f\v]) */
    private static final Pattern BARE_AMP = Pattern.compile("&(?!(?:[A-Za-z][A-Za-z0-9]*|#[0-9]+|#x[0-9A-Fa-f]+);)");
    private static final Pattern ATTR_VALUE = Pattern.compile("=\"([^\"]*)\"");
    private static final Pattern TAG = Pattern.compile("<([A-Za-z_][\\w.:-]*)((?:\\s+[\\w.:-]+\\s*=\\s*\"[^\"]*\")+)(\\s*/?>)");
    private static final Pattern ATTR = Pattern.compile("\\s+([\\w.:-]+)\\s*=\\s*\"[^\"]*\"");

    static final class Fixed {
        byte[] data;
        final List<String> fixes = new ArrayList<>();
    }

    static Fixed lenient(byte[] raw) {
        Fixed f = new Fixed();
        String t = new String(raw, StandardCharsets.ISO_8859_1);
        Matcher m = BARE_AMP.matcher(t);
        int n = 0;
        while (m.find()) n++;
        if (n > 0) {
            t = BARE_AMP.matcher(t).replaceAll("&amp;");
            f.fixes.add(n + " bare &");
        }
        StringBuilder out = new StringBuilder();
        m = ATTR_VALUE.matcher(t);
        int lt = 0, last = 0;
        while (m.find()) {
            String v = m.group(1);
            out.append(t, last, m.start());
            if (v.indexOf('<') >= 0) {
                for (int i = 0; i < v.length(); i++) if (v.charAt(i) == '<') lt++;
                v = v.replace("<", "&lt;");
            }
            out.append("=\"").append(v).append('"');
            last = m.end();
        }
        out.append(t.substring(last));
        t = out.toString();
        if (lt > 0) f.fixes.add(lt + " < inside a value");
        out = new StringBuilder();
        m = TAG.matcher(t);
        int dups = 0;
        last = 0;
        while (m.find()) {
            out.append(t, last, m.start());
            List<String> names = new ArrayList<>();
            List<String> parts = new ArrayList<>();
            Matcher a = ATTR.matcher(m.group(2));
            while (a.find()) { names.add(a.group(1)); parts.add(a.group(0)); }
            if (new java.util.HashSet<>(names).size() == names.size()) {
                out.append(m.group(0));
            } else {
                java.util.Set<String> seen = new java.util.HashSet<>();
                StringBuilder kept = new StringBuilder();
                for (int i = 0; i < names.size(); i++) {
                    if (!seen.add(names.get(i))) { dups++; continue; }
                    kept.append(parts.get(i));
                }
                out.append('<').append(m.group(1)).append(kept).append(m.group(3));
            }
            last = m.end();
        }
        out.append(t.substring(last));
        t = out.toString();
        if (dups > 0) f.fixes.add(dups + " repeated attribute");
        f.data = t.getBytes(StandardCharsets.ISO_8859_1);
        return f;
    }

    static byte[] utf8(String s) { return s.getBytes(StandardCharsets.UTF_8); }

    static byte[] concat(ByteArrayOutputStream b) { return b.toByteArray(); }
}
