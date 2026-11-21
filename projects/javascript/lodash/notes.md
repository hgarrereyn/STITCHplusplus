# lodash (lodash/lodash @ 4.17.15)

13 stubs over deep paths on plain objects: `new_obj`, `set`, `set_with`, `update`, `unset`,
`get_has`, `zip_object_deep`, `from_pairs_set`, `merge` (`merge`, `mergeWith`, `defaultsDeep`,
`assign`, `defaults`, `assignIn`), `parse_json_into`, `clone` (shallow and deep), `pick_omit`
(`pick`, `omit`, `at`, `invoke`, `result`) and `to_path_roundtrip`. Object: `Obj`. Decoder:
`PropPath` (dotted and bracketed paths); its dictionary has `__proto__`, `constructor`,
`prototype`, `toString`, `valueOf`, `hasOwnProperty`, `length`. Oracles: the runtime's
prototype-pollution check (any property added to, replaced on or deleted from
`Object.prototype`, its methods, `Array.prototype`, `Function.prototype` or `Object` during a
call), `has()` implies `get()` finds the value, `get` finds what `set` wrote, and clones are
`isEqual` to the original.

Campaign: 15 min, 2 workers: 1.2M execs (1.4k/s), 786 edges, 14 buckets.

## Library bugs

Checked against 4.17.21, 4.17.23 and 4.18.0 (the latest) with `stitch exec <id> --rev`.

1. **Writes into shared built-in methods** (still in 4.18.0). `_.set({}, "toString.a", 1)` walks
   into the inherited `Object.prototype.toString` function and adds `a` to it, so every object
   now has `({}).toString.a === 1`. The same happens through `setWith`, `pick`, `defaultsDeep`,
   `merge`, `toPath` + `set`, and with `valueOf` and `hasOwnProperty`. The 4.17.21 hardening only
   blocks the keys `__proto__`, `constructor` and `prototype`. Reproducer:
   `bugs/builtin-method-write/`.
2. **`_.unset(obj, "__proto__.constructor")` deletes `Object.prototype.constructor`.** Present in
   4.17.21, fixed in 4.17.23. Reproducer: `bugs/unset-proto-constructor/`.
3. **Prototype pollution through `set`, `setWith`, `update` and `zipObjectDeep`**
   (CVE-2020-8203 and related): `_.set({}, "[__proto__].a", 1)`, `_.zipObjectDeep(["__proto__.a"],
   [1])`, ... set `Object.prototype.a`. Fixed in 4.17.21 (6 buckets). Reproducer:
   `bugs/cve-2020-8203/`.
4. **`clone`/`cloneDeep` throw on an array with an own `constructor` property** (`a.constructor
   = 7` gives "array.constructor is not a constructor"). lodash builds the copy with `new
   value.constructor`. Still in 4.17.21 when the property is assigned directly (the minimized
   programs set it with `setWith(..., "constructor")`, which 4.17.21 refuses). A minor robustness
   issue.
5. **`set` and `pick` through `hasOwnProperty.length.__proto__`** threw "Object.defineProperty
   called on non-object". Fixed in 4.17.21.

## Harness bugs found and fixed

- **The pollution oracle only looked at new keys of `Object.prototype`.** Writes into its
  methods (`toString.a`) went unseen, and replacing a built-in (`__proto__.valueOf = 7`) broke the
  runtime itself ("crash (no sanitizer report)", 8 buckets). The JavaScript runtime now snapshots
  the shared built-ins, checks them right after each call, restores them, and reports what
  changed.
- **Huge array indices.** A path segment like `777757777` makes a sparse array that long, and
  later `merge`/`cloneDeep` walk it element by element until the worker runs out of memory.
  Paths with 5-digit or longer numeric segments now bail.
- `has()` is true for a key that holds `undefined`, while `get(o, p, "dflt")` returns the default:
  the check accounts for it.
- An empty path never round-trips (`set` with no path writes nothing).
- `invoke` on a value that is not a function is the caller's error, and assigning an array's
  `length` a non-length value is a `RangeError` in JavaScript itself: both are bails.
