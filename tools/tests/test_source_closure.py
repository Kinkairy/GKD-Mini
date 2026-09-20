#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / "gkd-source-closure.py"
SPEC = importlib.util.spec_from_file_location("gkd_source_closure", TOOL)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SourceClosureTest(unittest.TestCase):
    def test_current_formal_tree_has_no_historical_build_inputs(self) -> None:
        root = TOOL.parents[1]
        self.assertEqual([], MODULE.audit(root))

    def test_clean_current_source_passes_and_historical_input_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            current = root / "system/lane/scripts/build.sh"
            current.parent.mkdir(parents=True)
            current.write_text("#!/bin/sh\nprintf current\n", encoding="utf-8")
            self.assertEqual([], MODULE.audit(root))
            current.write_text("#!/bin/sh\ncp archive/old/input .\n", encoding="utf-8")
            self.assertEqual(1, len(MODULE.audit(root)))
            current.write_text('#include "../../../archive/old/backend.c"\n', encoding="utf-8")
            self.assertEqual(1, len(MODULE.audit(root)))


if __name__ == "__main__":
    unittest.main()
