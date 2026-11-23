"use strict";
// A tiny key-value store with planted bugs (STITCH++ example).

class Store {
  constructor() {
    this.data = {};
    this.order = [];
  }
  put(key, value) {
    if (!key || key.includes("\n") || value.includes("\n")) throw new RangeError("keys and values are single non-empty lines");
    if (!(key in this.data)) this.order.push(key);
    this.data[key] = value;
  }
  remove(key) {
    if (key in this.data) {
      delete this.data[key];
      if (!key.startsWith("tmp")) this.order.splice(this.order.indexOf(key), 1); // BUG: tmp keys stay
    }
  }
  merge(other) {
    for (const k of other.order) this.put(k, other.data[k]); // TypeError after removing a tmp key from other
  }
  dump() {
    return this.order.map((k) => `${k}=${this.data[k].trim()}`).join("\n"); // TypeError after removing a tmp key
  }
}

// load(text): parses dump's format. Throws RangeError for a line without '='.
function load(text) {
  const st = new Store();
  for (const line of text.split("\n")) {
    if (!line) continue;
    const eq = line.indexOf("=");
    if (eq < 0) throw new RangeError("line without '='");
    let v = line.slice(eq + 1); // BUG: keys containing '=' do not round-trip
    if (v.startsWith("#!")) v = v.slice(2).split(" ")[1].trim(); // BUG: TypeError on "#!x"
    st.put(line.slice(0, eq), v);
  }
  return st;
}

// setPath(obj, "a.b.c", v): nested assignment. BUG: "__proto__" is not filtered.
function setPath(obj, path, value) {
  const parts = path.split(".");
  let cur = obj;
  for (let i = 0; i < parts.length - 1; i++) {
    if (parts[i] === "constructor") throw new Error("forbidden key");
    if (typeof cur[parts[i]] !== "object" || cur[parts[i]] === null) cur[parts[i]] = {};
    cur = cur[parts[i]];
  }
  cur[parts[parts.length - 1]] = value;
}

module.exports = { Store, load, setPath };
