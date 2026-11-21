"""Standalone reproducer writers for worker-runtime languages (C/C++ lives in ../standalone.py).

Each module provides:
  FILENAME                        name of the generated file (e.g. repro.py)
  SHIM                            name of the stand-in for the STITCH++ runtime written next to it
  write(p, plan, header) -> (text, meta)
  install_shim(dir)               write the shim into dir
  run_script(p, src_rel, timeout) shell script that builds (if needed) and runs src_rel in the
                                  project image; exit 201 = compile error
"""
import importlib


def get(language):
    try:
        return importlib.import_module(f".{language}", __name__)
    except ModuleNotFoundError:
        raise ValueError(f"no standalone writer for {language}")


def comment_safe(text: str) -> str:
    """Text for a one-line source comment: printable ASCII, other characters escaped, backslashes
    doubled (so no `\\u` becomes a Java unicode escape)."""
    out = []
    for c in text.replace("\\", "\\\\"):
        o = ord(c)
        out.append(c if 32 <= o < 127 else (f"\\x{o:02x}" if o < 256 else f"U+{o:04X}"))
    return "".join(out)
