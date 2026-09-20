#!/usr/bin/env python3
import copy
import importlib.util
import json
from pathlib import Path
import unittest
LANE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("manifest", LANE / "scripts/manifest-header.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
class Manifest(unittest.TestCase):
    def setUp(self):
        self.data = json.loads((LANE / "config/minimal-a-manifest.json").read_text())
    def test_accepted_asset_is_bound(self):
        rendered = module.render(self.data)
        self.assertIn('#define APP_ANIMATION_PATH "/boot/gkd-mini/startup-animation.rgb565"', rendered)
        self.assertIn(self.data["boot_animation"]["sha256"], rendered)
    def test_missing_animation_refused(self):
        del self.data["boot_animation"]
        with self.assertRaises(KeyError): module.render(self.data)
    def test_unsafe_animation_path_refused(self):
        self.data["boot_animation"]["path"] = "/boot/../old.rgb565"
        with self.assertRaises(ValueError): module.render(self.data)
    def test_bad_animation_hash_refused(self):
        self.data["boot_animation"]["sha256"] = "bad"
        with self.assertRaises(ValueError): module.render(self.data)
    def test_duplicate_library_refused(self):
        self.data["dependencies"].append(copy.deepcopy(self.data["dependencies"][0]))
        with self.assertRaises(ValueError): module.render(self.data)
if __name__ == "__main__": unittest.main()
