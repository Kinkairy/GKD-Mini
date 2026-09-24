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

    def test_each_production_root_is_scanned(self) -> None:
        for name in ("tools/build.py", "build/prepare.py", "kernel/current/build.sh",
                     "system/lane/scripts/build.sh", "system/lane/source/module.c"):
            with self.subTest(path=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                path = root / name
                path.parent.mkdir(parents=True)
                path.write_text('INPUT="archive/retired/input.bin"\n')
                self.assertEqual(1, len(MODULE.audit(root)))

    def test_fixtures_and_pattern_vocabulary_are_not_build_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("tools/tests/fixture.py", "system/lane/tests/fixture.c",
                         "docs/example.py", "tools/gkd-source-closure.py"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('INPUT="archive/retired/input.bin"\n')
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
