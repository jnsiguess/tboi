package org.json;

/*
 * desk only: android.jar's org.json is a stub that throws. This one reads JSON as Android's does
 * for what the installer asks of it: objects keep their keys' order (Android: LinkedHashMap), a
 * repeated key takes the last value, numbers come as Integer, Long or Double, null as NULL, and
 * the text after the first value is not looked at.
 */
public class JSONObject {
    public static final Object NULL = new Object() {
        @Override public String toString() { return "null"; }
        @Override public boolean equals(Object o) { return o == this || o == null; }
        @Override public int hashCode() { return 0; }
    };

    final java.util.LinkedHashMap<String, Object> v = new java.util.LinkedHashMap<>();

    JSONObject() { }

    public JSONObject(String s) throws JSONException {
        Reader r = new Reader(s);
        Object o = r.value();
        if (!(o instanceof JSONObject)) throw new JSONException("Value " + o + " cannot be converted to JSONObject");
        v.putAll(((JSONObject) o).v);
    }

    public Object opt(String key) { return v.get(key); }
    public boolean has(String key) { return v.containsKey(key); }
    public java.util.Iterator<String> keys() { return v.keySet().iterator(); }
    public int length() { return v.size(); }

    public String optString(String key) { return optString(key, ""); }
    public String optString(String key, String def) {
        Object o = v.get(key);
        return o != null && o != NULL ? o.toString() : def;
    }
    public JSONObject optJSONObject(String key) {
        Object o = v.get(key);
        return o instanceof JSONObject ? (JSONObject) o : null;
    }
    public JSONArray optJSONArray(String key) {
        Object o = v.get(key);
        return o instanceof JSONArray ? (JSONArray) o : null;
    }

    @Override public String toString() {
        StringBuilder b = new StringBuilder("{");
        for (java.util.Map.Entry<String, Object> e : v.entrySet()) {
            if (b.length() > 1) b.append(',');
            b.append(quote(e.getKey())).append(':').append(e.getValue() instanceof String ? quote((String) e.getValue()) : String.valueOf(e.getValue()));
        }
        return b.append('}').toString();
    }

    static String quote(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c == '\n') b.append("\\n");
            else if (c < 0x20) b.append(String.format("\\u%04x", (int) c));
            else b.append(c);
        }
        return b.append('"').toString();
    }

    /* the reader: strict JSON plus the comments Android's tokenizer forgives */
    static final class Reader {
        final String s;
        int i;
        Reader(String s) { this.s = s; }

        JSONException bad(String what) { return new JSONException(what + " at character " + i); }

        void skip() throws JSONException {
            while (i < s.length()) {
                char c = s.charAt(i);
                if (Character.isWhitespace(c)) { i++; continue; }
                if (c == '/' && i + 1 < s.length() && s.charAt(i + 1) == '*') {
                    int e = s.indexOf("*/", i + 2);
                    if (e < 0) throw bad("Unterminated comment");
                    i = e + 2;
                    continue;
                }
                if ((c == '/' && i + 1 < s.length() && s.charAt(i + 1) == '/') || c == '#') {
                    while (i < s.length() && s.charAt(i) != '\n' && s.charAt(i) != '\r') i++;
                    continue;
                }
                return;
            }
        }

        Object value() throws JSONException {
            skip();
            if (i >= s.length()) throw bad("End of input");
            char c = s.charAt(i);
            if (c == '{') return object();
            if (c == '[') return array();
            if (c == '"') return string();
            int j = i;
            while (j < s.length() && ",:]}/\\\"[{;=# \t\r\n".indexOf(s.charAt(j)) < 0) j++;
            String w = s.substring(i, j);
            if (w.isEmpty()) throw bad("Expected literal value");
            i = j;
            if (w.equals("true")) return Boolean.TRUE;
            if (w.equals("false")) return Boolean.FALSE;
            if (w.equals("null")) return NULL;
            try {
                if (w.matches("-?\\d+")) {
                    long l = Long.parseLong(w);
                    return l == (int) l ? (Object) (int) l : (Object) l;
                }
                return Double.parseDouble(w);
            } catch (NumberFormatException e) {
                throw bad("Unexpected literal " + w);
            }
        }

        String string() throws JSONException {
            StringBuilder b = new StringBuilder();
            i++;
            while (i < s.length()) {
                char c = s.charAt(i++);
                if (c == '"') return b.toString();
                if (c == '\\') {
                    if (i >= s.length()) break;
                    char e = s.charAt(i++);
                    switch (e) {
                        case 'n': b.append('\n'); break;
                        case 't': b.append('\t'); break;
                        case 'r': b.append('\r'); break;
                        case 'b': b.append('\b'); break;
                        case 'f': b.append('\f'); break;
                        case 'u':
                            if (i + 4 > s.length()) throw bad("Unterminated escape sequence");
                            b.append((char) Integer.parseInt(s.substring(i, i + 4), 16));
                            i += 4;
                            break;
                        default: b.append(e);
                    }
                } else b.append(c);
            }
            throw bad("Unterminated string");
        }

        JSONObject object() throws JSONException {
            JSONObject o = new JSONObject();
            i++;
            skip();
            if (i < s.length() && s.charAt(i) == '}') { i++; return o; }
            while (true) {
                skip();
                if (i >= s.length() || s.charAt(i) != '"') throw bad("Expected a name");
                String k = string();
                skip();
                if (i >= s.length() || s.charAt(i) != ':') throw bad("Expected ':' after " + k);
                i++;
                o.v.put(k, value());
                skip();
                if (i >= s.length()) throw bad("Unterminated object");
                char c = s.charAt(i++);
                if (c == '}') return o;
                if (c != ',' && c != ';') throw bad("Unterminated object");
            }
        }

        JSONArray array() throws JSONException {
            JSONArray a = new JSONArray();
            i++;
            skip();
            if (i < s.length() && s.charAt(i) == ']') { i++; return a; }
            while (true) {
                a.v.add(value());
                skip();
                if (i >= s.length()) throw bad("Unterminated array");
                char c = s.charAt(i++);
                if (c == ']') return a;
                if (c != ',' && c != ';') throw bad("Unterminated array");
            }
        }
    }
}
