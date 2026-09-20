#!/usr/bin/env python3
"""Run inside the pinned offline container with fake config/exec endpoints mounted."""
import os
from pathlib import Path
import subprocess
import unittest

OUT = Path('/out')
BASE = """volume_step=2
brightness_steps=2,10,20,30,40,50,60,70,80,90,100
volume_osd_timeout_ms=5000
volume_osd=enabled
ui_dynamic_effects=enabled
volume_curve=perceptual
input_map_volume_up=KEY_KPPLUS
input_map_volume_down=KEY_KPMINUS
input_map_brightness=KEY_END
"""
MANAGED = BASE + """hardware_state_persistence=enabled
hardware_state_save_delay_ms=1000
volume_default_percent=50
brightness_default_percent=70
"""
class Launcher(unittest.TestCase):
    def launch(self, snapshot=BASE, dump_result=0, arguments=()):
        (OUT/'config.snapshot').write_text(snapshot)
        (OUT/'dump-count').write_text('')
        (OUT/'arguments').unlink(missing_ok=True)
        env = dict(os.environ, DUMP_RESULT=str(dump_result))
        result = subprocess.run(['/bin/sh', '/lane/device/gkd-controls-start', *arguments], env=env, capture_output=True)
        self.assertEqual((OUT/'dump-count').read_text(), '1\n')
        return result
    def test_one_snapshot_and_exact_arguments(self):
        self.assertEqual(self.launch().returncode, 0)
        self.assertEqual((OUT/'arguments').read_text().splitlines(),
            ['2','2,10,20,30,40,50,60,70,80,90,100','5000','enabled','enabled','KEY_KPPLUS','KEY_KPMINUS','KEY_END'])
    def test_failed_dump_does_not_launch_even_with_complete_output(self):
        self.assertNotEqual(self.launch(dump_result=1).returncode, 0)
        self.assertFalse((OUT/'arguments').exists())
    def test_incomplete_or_unsupported_snapshot(self):
        for missing in BASE.splitlines():
            with self.subTest(missing=missing):
                self.assertNotEqual(self.launch(BASE.replace(missing+'\n','')).returncode,0)
                self.assertFalse((OUT/'arguments').exists())
        self.assertNotEqual(self.launch(BASE.replace('perceptual','linear')).returncode,0)
    def test_duplicate_rejected(self):
        for item in BASE.splitlines():
            with self.subTest(item=item):
                self.assertNotEqual(self.launch(BASE+item+'\n').returncode,0)
                self.assertFalse((OUT/'arguments').exists())
    def test_config_is_data_not_shell(self):
        marker = OUT/'injected'
        marker.unlink(missing_ok=True)
        self.assertEqual(self.launch(BASE+'unrelated=$(touch /out/injected)\n').returncode,0)
        self.assertFalse(marker.exists())
    def test_explicit_disabled_policy(self):
        self.assertEqual(self.launch(BASE.replace('volume_osd=enabled','volume_osd=disabled')).returncode,0)
        self.assertEqual((OUT/'arguments').read_text().splitlines()[3],'disabled')
    def test_effects_policy_is_required_and_explicit(self):
        self.assertNotEqual(self.launch(BASE.replace('ui_dynamic_effects=enabled\n','')).returncode, 0)
        self.assertEqual(self.launch(BASE.replace('ui_dynamic_effects=enabled','ui_dynamic_effects=disabled')).returncode, 0)
        self.assertEqual((OUT/'arguments').read_text().splitlines()[4], 'disabled')
    def test_managed_exact_arguments(self):
        self.assertEqual(self.launch(MANAGED, arguments=('--managed','3','4')).returncode, 0)
        self.assertEqual((OUT/'arguments').read_text().splitlines()[8:],
            ['--managed','3','4','enabled','1000','50','70'])
    def test_managed_missing_or_duplicate_policy(self):
        for row in MANAGED[len(BASE):].splitlines():
            with self.subTest(row=row):
                for snapshot in (MANAGED.replace(row+'\n',''), MANAGED+row+'\n'):
                    self.assertNotEqual(self.launch(snapshot, arguments=('--managed','3','4')).returncode, 0)
                    self.assertFalse((OUT/'arguments').exists())
    def test_managed_disabled_persistence(self):
        self.assertEqual(self.launch(MANAGED.replace('persistence=enabled','persistence=disabled'),
            arguments=('--managed','3','4')).returncode, 0)
        self.assertEqual((OUT/'arguments').read_text().splitlines()[11], 'disabled')
if __name__ == '__main__': unittest.main()
