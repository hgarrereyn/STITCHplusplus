"""`stitch write` for Rust: repro.rs, a binary of the harness crate that calls each stub."""
import json
import shlex
from pathlib import Path

from ..standalone import read_plan

FILENAME = "repro.rs"
SHIM = "stitch::"  # the stitch crate in the image (stitch::run / stitch::call)


def install_shim(d: Path):
    pass


def _literal(a, draws):
    if a.get("rust"):
        comment = None
        if a["kind"] == "source" and draws:
            comment = "draws: " + "; ".join(f"{d['op']} -> {d.get('v', d.get('hex', ''))}" for d in draws)[:300]
        return a["rust"], comment
    return "Default::default()", f"{a.get('name', '?')} was {a.get('v')}: fill it in"


def write(p, plan: Path, header):
    calls, draws, done, skips, drops, ended = read_plan(plan)
    order = sorted(calls)
    crash_node = next((n for n in order if n not in done), None)
    vars_, mods, body = {}, [], []
    for n in order:
        c = calls[n]
        m = c.get("module", "stitch_harness")
        if m.endswith("::" + c["ep"]):  # recorded inside the stub's registration module
            m = m[: -len(c["ep"]) - 2]
        if m not in mods:
            mods.append(m)
        d = done.get(n)
        status = "crash" if n == crash_node else d["status"]
        reason = (d or {}).get("reason", "")
        args, summ, si = [], [], 0
        for a in c.get("args") or []:
            name = a.get("name", "")
            if "obj" in a:
                v = vars_.get(tuple(a["obj"]))
                if v is None:
                    args.append("todo!(\"missing object\")")
                else:
                    args.append(f"stitch::take({v})" if a.get("take") else f"stitch::borrow(&mut {v})")
                summ.append(f"{name}={v}")
            else:
                dr = draws.get(n, {}).get(si, []) if a["kind"] == "source" else []
                if a["kind"] == "source":
                    si += 1
                lit, comment = _literal(a, dr)
                if comment:
                    body.append(f"        // {comment}")
                args.append(lit)
                summ.append(name)
        note = {"crash": "  <-- crashes here in the original run",
                "bail": f' (bailed in the original run: "{reason}")'}.get(status, "")
        body.append(f"        // #{n} {c['ep']}({', '.join(summ)}){note}")
        call = f"|| {c['ep']}({', '.join(args)})"
        if status == "bail":
            body.append(f"        stitch::rt::call_bails({n}, {json.dumps(c['ep'])}, {call});")
            continue
        types = c.get("outs", [])
        outs = (d or {}).get("outs", [])
        if not types or status == "crash":
            body.append(f"        stitch::call({n}, {json.dumps(c['ep'])}, {call});")
        else:
            body.append(f"        let mut r{n} = stitch::call({n}, {json.dumps(c['ep'])}, {call});")
            for k in outs:
                v = f"{types[k].lower()}{n}" + (f"_{k}" if len(types) > 1 else "")
                vars_[(n, k)] = v
                body.append(f"        let mut {v} = r{n}.get::<{types[k]}>({k});")
        if status == "crash":
            break
    if crash_node is not None:
        body.append(f"        // the original run crashed in call #{crash_node}; nothing after it ran")
    head = ["// " + h if h else "//" for h in header]
    text = "\n".join(head + ["#![allow(unused_mut, unused_variables, unused_imports)]", "use stitch::*;",
                             *[f"use {m}::*;" for m in mods], "", "fn main() {", "    stitch_harness::__stitch_register();",
                             "    stitch::run(|| {", *body, "    });", "}", ""])
    return text, {"calls": len(order), "crash_node": crash_node, "skipped": len(skips)}


def run_script(p, src_rel, timeout=60):
    crate = "/project/.stitch/build/app/crate"
    return (f"t=$(rustc -vV | sed -n 's/host: //p') && cp {shlex.quote(src_rel)} {crate}/src/bin/stitchtry.rs && cd {crate} && "
            f"CARGO_HOME=/project/.stitch/cargo RUSTC_WRAPPER=/stitch/bin/stitch-rustc STITCH_SANCOV=1 "
            f"CARGO_TARGET_DIR=/project/.stitch/cargo-target/fuzz RUSTFLAGS=\"$(grep -o \"'.*'\" /project/.stitch/build/repro | tr -d \"'\" | head -1)\" "
            f"cargo build -q --release --target $t --bin stitchtry 2>/tmp/build.log "
            f"|| {{ grep -v '^ *\\(Compiling\\|Finished\\)' /tmp/build.log >&2; rm -f src/bin/stitchtry.rs; exit 201; }}; "
            f"rm -f src/bin/stitchtry.rs; echo '[repro] ---- run ----' >&2; "
            f"RUST_BACKTRACE=1 ASAN_OPTIONS=abort_on_error=1:detect_leaks=0:symbolize=1:allocator_may_return_null=1 "
            f"timeout {timeout} /project/.stitch/cargo-target/fuzz/$t/release/stitchtry >&2")


def build_hint(src_rel):
    return "a binary of the harness crate in .stitch/build/app/crate (stitch try builds and runs it)"
