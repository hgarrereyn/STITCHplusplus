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
