"""A tiny key-value store with planted bugs (STITCH++ example)."""


class Store:
    def __init__(self):
        self._d = {}
        self._order = []

    def put(self, key, value):
        if not key or "\n" in key or "\n" in value:
            raise ValueError("keys and values are single non-empty lines")
        if key not in self._d:
            self._order.append(key)
        self._d[key] = value

    def get(self, key):
        return self._d.get(key)

    def remove(self, key):
        if key in self._d:
            del self._d[key]
            if not key.startswith("tmp"):  # BUG: temporary keys stay in the order list
                self._order.remove(key)

    def items(self):
        return [(k, self._d[k]) for k in self._order]  # KeyError after removing a tmp key

    def dump(self):
        return "\n".join(f"{k}={v}" for k, v in self.items())


def load(text):
    st = Store()
    for line in text.split("\n"):
        if not line:
            continue
        if "=" not in line:
            raise ValueError(f"line without '=': {line!r}")
        k, v = line.split("=", 1)  # BUG: keys containing '=' do not round-trip
        if v.startswith("#!"):
            v = v[2:].split()[1]  # BUG: IndexError on "#!x"
        st.put(k, v)
    return st
