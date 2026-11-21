"use strict";
// A tiny key-value store with planted bugs (STITCH++ example).

class Store {
  constructor() {
    this.data = {};
    this.order = [];
  }
  put(key, value) {
    if (!key) throw new RangeError("empty key");
    if (!(key in this.data)) this.order.push(key);
    this.data[key] = value;
  }
  remove(key) {
    if (key in this.data) {
      delete this.data[key];
      if (!key.startsWith("tmp")) this.order.splice(this.order.indexOf(key), 1); // BUG: tmp keys stay
    }
  }
  dump() {
    return this.order.map((k) => `${k}=${this.data[k].trim()}`).join("\n"); // TypeError after removing a tmp key
  }
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

module.exports = { Store, setPath };
