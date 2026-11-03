package org.json;

/* desk only: see JSONObject */
public class JSONArray {
    final java.util.ArrayList<Object> v = new java.util.ArrayList<>();

    public int length() { return v.size(); }
    public Object opt(int i) { return i >= 0 && i < v.size() ? v.get(i) : null; }
    public JSONObject optJSONObject(int i) {
        Object o = opt(i);
        return o instanceof JSONObject ? (JSONObject) o : null;
    }
    public String optString(int i, String def) {
        Object o = opt(i);
        return o != null && o != JSONObject.NULL ? o.toString() : def;
    }

    @Override public String toString() {
        StringBuilder b = new StringBuilder("[");
        for (Object o : v) {
            if (b.length() > 1) b.append(',');
            b.append(o instanceof String ? JSONObject.quote((String) o) : String.valueOf(o));
        }
        return b.append(']').toString();
    }
}
