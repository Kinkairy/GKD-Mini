#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import os
import re
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


LANE = Path(__file__).resolve().parents[1]
PROJECT = LANE.parents[1]
DEFAULTS = LANE / "config/gdkmini-ui.defaults.conf"
SCHEMA = PROJECT / "system/config-core/schema/gdkmini.schema"
EXPECTED_FRAMES = {
    ("menu", 0): "5bc4be496b5b4768e15b60468930549217536051c887ab118cb2272f1a24db57",
    ("menu", 1): "1be90a011e576f2812e123991ee7688b400a24b5db8f2fdd28d831518ae330b6",
    ("menu", 2): "649058401b55de6a8f16860e5c578e54de8bcd8559db2ee49497e06a4333d75b",
    ("menu", 3): "580f5cd1dca587622f636dc12667de434f5379e0406fdbbfcafa136f3944a886",
    ("confirmation", 1): "7595863458e0c3f28b9bf7d89d85962ebcc6283e9eaacfdc8a92d8758608aaeb",
    ("status", 0): "49f06fcc8deb002adfd57115642b0099f06de08dcd11f73f88871031fa6f26ca",
    ("failure", 0): "f957a198594263f2a83b3788e91e398bd88cf413d7e6250d381ecbdc3bebb337",
    ("loading", 0): "b9b94a6d1a98a6006ec04e5dd7001cc8a9e5082f4cb10aacc6377c008065cf6d",
    ("loading", 1): "59647ec089dcfc282aa5dab3da100bd641b66efd17f1d8d15f15b1326e62f874",
    ("loading", 2): "75e3e55fb5b6a32b9bb6df53bfe0b8dd3ba8a9c1507562887c845ba2f199d5f9",
    ("loading", 3): "7aa13c06e38ad6497b2342a2e3684e287097f5864e55d7ce592e46c4f0aebf9e",
    ("loading", 4): "15d1b7c664b78deeae0f8762a62dd8f3431a311f556930fd77c410ee27ee5426",
    ("loading", 5): "9e1a616908e9d58e5422e02541d08ea2e7e1979bb26540c8a61b9f0abcd605c0",
    ("loading", 6): "fb0bfa54a5ba2eb40ffda46a3ba2dda4712e0f1a525be477607615d9d4dd16d7",
    ("loading", 7): "3f8cf5a2ee6fa0709ad17340692fc342a43143bacda8761eb1806eeb768905f5",
}
EXPECTED_OSD = {
    "battery": "aa3a366e88c31457cb46984c726f84d48314f7219c7896cc4a49d1d0bd8ab7d6",
    "brightness": "98d3e14d764607b7ef4ae72290cc2b64d48d605348d66c6d120f79c713cb2edc",
    "charge": "f1950bb79f1fb831c70ed5d33aec8f47124d1ff937a0935f6ff5aef63c1e378d",
    "debug": "dec477bf53791b7d18180dd8ab614e8b74f4f71efe85d8da5618606176793ef0",
    "low-battery": "e47da0bf98cc0b34d2edca92ccdd01645dbcd944439069d6455adbf5ada73b0c",
    "network": "0814d6d018393e8239d6f943b0f2f3aa70acf7cfcae06819edd059c42a398338",
    "screenshot": "1cb0242a1c027378a982537a2f19678f7becae2518d73c7737c04a79a91cd6f0",
    "storage": "a15541589be23b372acb01fb0c267639033a41e7fb1abcc3e7fe355c6abc732c",
    "volume": "3f8a5dd67cd65649b4594dbdb879376ea3ec5d8a733860442c6f4459422a95e0",
}


def values(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            key, value = line.split("=", 1)
            if key in result:
                raise AssertionError(f"duplicate key: {key}")
            result[key] = value
    return result


class UiCoreTest(unittest.TestCase):
    def test_rndis_configfs_two_digit_parser_contract(self) -> None:
        if not os.environ.get("GKD_UI_TEST_BINARY"):
            self.skipTest("host build environment not supplied")
        script = (PROJECT / "kernel/current/initramfs/gkd-recovery-usb").read_text()
        assignments = dict(re.findall(
            r'write_attr "\$gadget/functions/rndis\.usb0/(class|subclass|protocol)" (\S+)',
            script))
        self.assertEqual({"class": "ef", "subclass": "04", "protocol": "01"}, assignments)
        with tempfile.TemporaryDirectory(prefix="gkd-rndis-parser-") as directory:
            root = Path(directory)
            source = root / "parser.c"
            source.write_text('#include <stdio.h>\nint main(int argc, char **argv) {\n'
                              'unsigned char value = 255;\n'
                              'if (argc != 2 || sscanf(argv[1], "%02hhx", &value) != 1) return 2;\n'
                              'printf("%02x", value); return 0; }\n')
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(source),
                            "-o", str(root / "parser")], check=True)
            for value in assignments.values():
                result = subprocess.run([str(root / "parser"), value], check=True,
                                        capture_output=True, text=True)
                self.assertEqual(value, result.stdout)
            wrong = subprocess.run([str(root / "parser"), "0xef"], check=True,
                                   capture_output=True, text=True)
            self.assertEqual("00", wrong.stdout)
        for key, value in assignments.items():
            self.assertIn(f'[ "$(cat "$gadget/functions/rndis.usb0/{key}")" = {value} ] || return 1', script)

    def test_locked_palette_and_geometry_are_in_unified_schema(self) -> None:
        defaults = values(DEFAULTS)
        self.assertEqual(36, len(defaults))
        self.assertEqual("#76FF89", defaults["normal"])
        self.assertEqual("#000502", defaults["dark"])
        self.assertEqual("#36AD4E", defaults["highlight"])
        self.assertEqual("#BEFFB9", defaults["white"])
        self.assertEqual("#FF5A67", defaults["button_b"])
        self.assertEqual("14", defaults["menu_icon_size"])
        self.assertEqual("12", defaults["action_px"])
        self.assertEqual("14", defaults["loading_icon_size"])
        self.assertEqual("12", defaults["loading_px"])
        self.assertEqual("LOADING", defaults["loading_label"])
        schema = {
            row[0]: row[2]
            for row in (
                line.split("|")
                for line in SCHEMA.read_text(encoding="utf-8").splitlines()
                if line and not line.startswith("#")
            )
        }
        self.assertEqual(defaults, {key: schema[key] for key in defaults})

    def test_runtime_assets_are_procedural_and_psf2_based(self) -> None:
        sources = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (
                LANE / "source/gkd-ui.c",
                LANE / "source/gkd-recovery-ui.c",
            )
        )
        self.assertNotIn(".png", sources.lower())
        self.assertIn("draw_icon", sources)
        self.assertIn("PSF2_MAGIC", sources)
        self.assertIn("gkd_ui_render_confirmation", sources)
        self.assertIn("gkd_ui_render_loading", sources)
        self.assertIn("run_program_loading", sources)
        self.assertIn("(void)gkd_ui_config_load", sources)
        self.assertIn("gkd_ui_font_load(&runtime->font, RAM_FONT) == 0", sources)

    def test_host_renderer_produces_distinct_complete_frames(self) -> None:
        binary = os.environ.get("GKD_UI_TEST_BINARY")
        font = os.environ.get("GKD_UI_TEST_FONT")
        if not binary or not font:
            self.skipTest("host renderer not supplied")
        scenes = [
            *(('menu', selected) for selected in range(4)),
            ('confirmation', 1), ('status', 0), ('failure', 0),
            *(('loading', frame) for frame in range(8)),
        ]
        digests: set[str] = set()
        with tempfile.TemporaryDirectory(prefix="gkd-ui-test-") as directory:
            root = Path(directory)
            for scene, frame in scenes:
                output = root / f"{scene}-{frame}.rgb565"
                result = subprocess.run(
                    [binary, "--render-test", str(DEFAULTS), font,
                     str(output), scene, str(frame)],
                    check=False, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, text=True,
                )
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertEqual(320 * 240 * 2, output.stat().st_size)
                digest = hashlib.sha256(output.read_bytes()).hexdigest()
                if os.environ.get("GKD_UI_PRINT_HASHES"):
                    print(f"{scene}:{frame}={digest}")
                self.assertEqual(EXPECTED_FRAMES[(scene, frame)], digest)
                digests.add(digest)
        self.assertEqual(len(scenes), len(digests))

    def test_four_row_mapping_and_visual_overrides(self) -> None:
        binary = os.environ.get("GKD_UI_TEST_BINARY")
        font = os.environ.get("GKD_UI_TEST_FONT")
        if not binary or not font:
            self.skipTest("host renderer not supplied")
        with tempfile.TemporaryDirectory(prefix="gkd-ui-mapping-") as directory:
            root = Path(directory)
            source = root / "mapping.c"
            source.write_text('#include "gkd-ui.h"\nint main(void) {\n'
                              'return !(GKD_UI_MENU_ITEMS == 4 && '
                              'gkd_ui_menu_action(0) == 0 && gkd_ui_menu_action(1) == 1 && '
                              'gkd_ui_menu_action(2) == 3 && gkd_ui_menu_action(3) == 4);}\n')
            subprocess.run(["cc", "-DGKD_DEDICATED_RECOVERY=1", "-I", str(LANE / "include"),
                            str(source), "-o", str(root / "mapping")], check=True)
            subprocess.run([str(root / "mapping")], check=True)
            command = [binary, "--render-test", str(DEFAULTS), font, str(root / "frame.raw")]
            self.assertEqual(2, subprocess.run(command + ["menu", "4"]).returncode)
            defaults = DEFAULTS.read_text()
            for key, value in (("menu_text_shift", "2"), ("menu_content_shift", "6"),
                               ("accent", "#76FF89"), ("accent_alpha", "0")):
                conf = root / "override.conf"
                conf.write_text(defaults + f"{key}={value}\n")
                subprocess.run([binary, "--render-test", str(conf), font,
                                str(root / "frame.raw"), "menu", "0"], check=True)
                self.assertNotEqual(EXPECTED_FRAMES[("menu", 0)],
                                    hashlib.sha256((root / "frame.raw").read_bytes()).hexdigest())
            for extra in ("accent_width=160\n", "accent_period=10\n", "accent_height=19\naction_button_height=15\n"):
                conf.write_text(defaults + extra)
                self.assertEqual(2, subprocess.run([binary, "--render-test", str(conf), font,
                                                   str(root / "bad.raw"), "menu", "0"]).returncode)

    def test_action_labels_keep_every_native_ink_pixel(self) -> None:
        binary = os.environ.get("GKD_UI_TEST_BINARY")
        font_path = os.environ.get("GKD_UI_TEST_FONT")
        if not binary or not font_path:
            self.skipTest("host renderer not supplied")
        font = Path(font_path).read_bytes()
        _, _, header_size, _, _, char_size, height, width = struct.unpack("<8I", font[:32])
        self.assertEqual((width, height), (8, 16))
        with tempfile.TemporaryDirectory(prefix="gkd-ui-native-ink-") as directory:
            output = Path(directory) / "confirmation.raw"
            subprocess.run([binary, "--render-test", str(DEFAULTS), font_path,
                            str(output), "confirmation", "1"], check=True)
            pixels = struct.unpack("<76800H", output.read_bytes())
        normal = ((0x76 >> 3) << 11) | ((0xff >> 2) << 5) | (0x89 >> 3)
        for panel_x, label in ((164, "YES"), (241, "NO")):
            text_width = len(label) * 9 - 1
            text_x = panel_x + (71 - (11 + 7 + text_width)) // 2 + 11 + 7
            text_y = 206 + (19 - 12) // 2
            for index, letter in enumerate(label):
                glyph = font[header_size + ord(letter) * char_size:
                             header_size + (ord(letter) + 1) * char_size]
                rows = [y for y in range(16) if glyph[y]]
                first, last = min(rows), max(rows) + 1
                self.assertLessEqual(last - first, 12)
                top = (12 - (last - first)) // 2
                expected = {(x, top + y - first) for y in range(first, last)
                            for x in range(8) if glyph[y] & (0x80 >> x)}
                actual = {(x, y) for y in range(12) for x in range(8)
                          if pixels[(text_y + y) * 320 + text_x + index * 9 + x] == normal}
                self.assertEqual(expected, actual, letter)

    def test_application_renderer_matches_accepted_osd_and_contracts(self) -> None:
        font = os.environ.get("GKD_UI_TEST_FONT")
        accepted = os.environ.get("GKD_UI_ACCEPTED_OSD")
        if not font:
            self.skipTest("application fixture environment not supplied")
        with tempfile.TemporaryDirectory(prefix="gkd-ui-a-render-") as directory:
            root = Path(directory)
            fixture = root / "a-ui-fixture"
            source = [str(LANE / "source/gkd-ui.c"), str(LANE / "tests/a_ui_fixture.c")]
            subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-Wformat=2", "-Wshadow", "-Wconversion", "-DGKD_APPLICATION_UI=1",
                            "-I", str(LANE / "include"), *source, "-o", str(fixture)], check=True)
            for name, digest in EXPECTED_OSD.items():
                output = root / f"{name}.rgb565"
                subprocess.run([str(fixture), font, str(output), name, "255"], check=True)
                reference = bytearray(output.read_bytes())
                if name in ("battery", "low-battery"):
                    # Normalize only the six-column interior to the accepted
                    # fixed two-cell glyph. Everything else keeps its golden hash.
                    ink = struct.unpack_from("<H", reference, 2 * (210 * 320 + 15))[0]
                    columns = 5 if name == "battery" else 1
                    for y in range(212, 218):
                        empty = struct.unpack_from("<H", reference, 2 * (y * 320 + 16))[0]
                        for x in range(17, 23):
                            at = 2 * (y * 320 + x)
                            self.assertEqual(ink if x - 17 < columns else empty,
                                             struct.unpack_from("<H", reference, at)[0])
                            struct.pack_into("<H", reference, at, ink if x in (17, 18, 20, 21) else empty)
                self.assertEqual(digest, hashlib.sha256(reference).hexdigest())
                if accepted:
                    self.assertEqual(bytes(reference), (Path(accepted) / f"{name}.rgb565").read_bytes())
            base = root / "base.rgb565"
            opaque = root / "opaque.rgb565"
            middle = root / "middle.rgb565"
            subprocess.run([str(fixture), font, str(base), "volume", "0"], check=True)
            subprocess.run([str(fixture), font, str(opaque), "volume", "255"], check=True)
            subprocess.run([str(fixture), font, str(middle), "volume", "127"], check=True)
            base_pixels = struct.unpack("<76800H", base.read_bytes())
            opaque_pixels = struct.unpack("<76800H", opaque.read_bytes())
            middle_pixels = struct.unpack("<76800H", middle.read_bytes())
            for y in range(240):
                for x in range(320):
                    index = y * 320 + x
                    if not (8 <= x < 156 and 206 <= y < 225):
                        self.assertEqual(base_pixels[index], middle_pixels[index])
                    else:
                        under, over = base_pixels[index], opaque_pixels[index]
                        expected = sum((((((under >> shift) & mask) * 128 +
                                           ((over >> shift) & mask) * 127 + 127) // 255) << shift)
                                       for shift, mask in ((11, 31), (5, 63), (0, 31)))
                        self.assertEqual(expected, middle_pixels[index])
            contract = root / "renderer-contract"
            subprocess.run(["cc", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wformat=2", "-Wshadow", "-Wconversion", "-fsanitize=address,undefined",
                            "-DGKD_APPLICATION_UI=1", "-I", str(LANE / "include"),
                            str(LANE / "source/gkd-ui.c"),
                            str(LANE / "tests/renderer_contract_fixture.c"), "-o", str(contract)], check=True)
            subprocess.run([str(contract), font], check=True)
            export_fixture = root / "osd-export-fixture"
            subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-Wformat=2", "-Wshadow", "-Wconversion", "-DGKD_APPLICATION_UI=1",
                            "-I", str(LANE / "include"), str(LANE / "source/gkd-ui.c"),
                            str(LANE / "tests/osd_export_fixture.c"), "-o", str(export_fixture)], check=True)
            export_result = subprocess.run([str(export_fixture), font], check=True,
                                           capture_output=True, text=True)
            self.assertIn("summary cases=144 nonzero=0 zero=144", export_result.stdout)

    def test_root_b_is_exactly_the_enabled_button_dimmed_toward_dark(self) -> None:
        binary = os.environ.get("GKD_UI_TEST_BINARY")
        font = os.environ.get("GKD_UI_TEST_FONT")
        if not binary or not font:
            self.skipTest("host renderer not supplied")
        dark = 0x0020
        colors = (0x77f1, 0x3569, 0xbff7, 0xfacc)
        def blend(color, alpha):
            return sum((((((dark >> shift) & mask) * (255 - alpha) +
                          ((color >> shift) & mask) * alpha + 127) // 255) << shift)
                       for shift, mask in ((11, 31), (5, 63), (0, 31)))
        with tempfile.TemporaryDirectory(prefix="gkd-ui-action-context-") as directory:
            output = Path(directory) / "frame.raw"
            def render(scene, conf=DEFAULTS, frame=0):
                subprocess.run([binary, "--render-test", str(conf), font,
                                str(output), scene, str(frame)], check=True)
                return struct.unpack("<76800H", output.read_bytes())
            def button(pixels, x):
                return [pixels[y * 320 + column] for y in range(206, 225)
                        for column in range(x, x + 71)]
            active = render("confirmation")
            self.assertIn(colors[3], button(active, 241))
            for alpha in (0, 72, 255):
                conf = Path(directory) / "intensity.conf"
                conf.write_text(DEFAULTS.read_text() + f"action_disabled_alpha={alpha}\n")
                palette = {color: blend(color, alpha) for color in colors}
                expected = [palette.get(pixel, pixel) for pixel in button(active, 241)]
                for frame in range(4):
                    root = render("menu", conf, frame)
                    self.assertEqual(expected, button(root, 241))
                    self.assertEqual(button(active, 164), button(root, 164))
            conf.write_text(DEFAULTS.read_text() + "action_disabled_alpha=256\n")
            self.assertEqual(2, subprocess.run([binary, "--render-test", str(conf), font,
                                               str(output), "menu", "0"]).returncode)

    def test_psf2_rejection_and_unicode_mapping(self) -> None:
        binary = os.environ.get("GKD_UI_TEST_BINARY")
        font = os.environ.get("GKD_UI_TEST_FONT")
        if not binary or not font:
            self.skipTest("host renderer not supplied")
        with tempfile.TemporaryDirectory(prefix="gkd-ui-font-") as directory:
            root = Path(directory)
            invalid = root / "invalid.psf"
            invalid.write_bytes(b"not-a-font")
            rejected = subprocess.run(
                [binary, "--render-test", str(DEFAULTS), str(invalid),
                 str(root / "invalid.rgb565"), "menu", "0"], check=False,
            )
            self.assertEqual(2, rejected.returncode)
            custom = root / "unicode.psf"
            header = struct.pack("<8I", 0x864AB572, 0, 32, 1, 2, 16, 16, 8)
            glyphs = bytes([0x7E, 0x42, 0x04, 0x08, 0x10, 0x10, 0, 0x10,
                            0, 0, 0, 0, 0, 0, 0, 0]) + bytes(
                [0x18, 0x18, 0x7E, 0x18, 0x18, 0x7E, 0x18, 0x18,
                 0x7E, 0x18, 0x18, 0x18, 0, 0, 0, 0]
            )
            custom.write_bytes(header + glyphs + b"?\xff" + "中".encode() + b"\xff")
            custom_config = root / "unicode.conf"
            custom_config.write_text(
                DEFAULTS.read_text(encoding="utf-8").replace(
                    "recovery_export_label=EXPORT SYSTEM CARD",
                    "recovery_export_label=中",
                ), encoding="utf-8",
            )
            output = root / "unicode.rgb565"
            accepted = subprocess.run(
                [binary, "--render-test", str(custom_config), str(custom),
                 str(output), "menu", "0"], check=False,
            )
            self.assertEqual(0, accepted.returncode)
            self.assertEqual(320 * 240 * 2, output.stat().st_size)


if __name__ == "__main__":
    unittest.main()
