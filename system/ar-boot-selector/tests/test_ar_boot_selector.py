import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib


SCRIPT = Path(__file__).parents[1] / "scripts" / "build_test_prefix.py"
SPEC = importlib.util.spec_from_file_location("build_test_prefix", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class SelectorEnvironmentTests(unittest.TestCase):
    def test_selector_uses_x1830_gpio_b_stride_and_input_shadow(self):
        source = (
            Path(__file__).parents[1] / "source" / "gkd-ar-selector.S"
        ).read_text(encoding="ascii")
        self.assertIn("ori\t$t0, $t0, 0x7000", source)
        self.assertIn("sw\t$t2, 0x0018($t0)", source)
        self.assertIn("sw\t$t2, 0x0024($t0)", source)
        self.assertIn("sw\t$t2, 0x0034($t0)", source)
        self.assertIn("sw\t$t1, 0x00f0($t0)", source)
        self.assertIn("ori\t$t0, $t0, 0x1000", source)
        self.assertNotIn("ori\t$t0, $t0, 0x0100", source)

    def test_test_environment_uses_both_mode_and_crc_loader(self):
        text = MODULE.environment("both").rstrip(b"\0").decode("ascii")
        self.assertIn("autostart=no\n", text)
        self.assertIn("verifysel=bootm 80500000\n", text)
        self.assertIn("testboth=go 80600000 both\n", text)
        self.assertIn(
            "gkdboot=run loadsel verifysel testboth bootr;run boota;run bootr\n",
            text,
        )

    def test_final_environment_is_menu_only_delta(self):
        both = MODULE.environment("both")
        menu = MODULE.environment("menu")
        self.assertEqual(len(both), len(menu), 4096)
        self.assertIn(b"testmenu bootr", menu)
        self.assertNotEqual(both, menu)

    def test_selector_parser_accepts_valid_padded_legacy_image(self):
        payload = b"selector-payload"
        name = b"gkd-ar-selector-v1".ljust(32, b"\0")
        header = MODULE.HEADER.pack(
            0x27051956, 0, 1785448800, len(payload), 0x80600000,
            0x80600000, zlib.crc32(payload) & 0xFFFFFFFF,
            5, 5, 1, 0, name,
        )
        header = header[:4] + struct.pack(
            ">I", zlib.crc32(header) & 0xFFFFFFFF
        ) + header[8:]
        block = header + payload
        block += bytes(4096 - len(block))
        result = MODULE.validate_selector(block)
        self.assertEqual(result["load"], 0x80600000)
        self.assertEqual(result["entry"], 0x80600000)

    def test_selector_parser_rejects_crc_damage(self):
        payload = b"selector-payload"
        name = b"gkd-ar-selector-v1".ljust(32, b"\0")
        header = MODULE.HEADER.pack(
            0x27051956, 0, 1, len(payload), 0x80600000, 0x80600000,
            zlib.crc32(payload) & 0xFFFFFFFF, 5, 5, 1, 0, name,
        )
        header = header[:4] + struct.pack(
            ">I", zlib.crc32(header) & 0xFFFFFFFF
        ) + header[8:]
        block = bytearray(header + payload + bytes(4096 - len(header) - len(payload)))
        block[MODULE.HEADER.size] ^= 1
        with self.assertRaises(ValueError):
            MODULE.validate_selector(bytes(block))


if __name__ == "__main__":
    unittest.main()
