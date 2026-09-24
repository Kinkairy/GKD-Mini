#!/usr/bin/env python3
"""Safety checks for the offline private card-image assembler."""

import hashlib
import importlib.util
import struct
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / 'gkd-build-card-image.py'
SPEC = importlib.util.spec_from_file_location('gkd_card_image', SCRIPT)
CARD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CARD)


class CardImageSafety(unittest.TestCase):
    def make_tree(self, root):
        app = root / 'apps' / 'SimpleMenu-OD-v1.1.opk'
        app.parent.mkdir()
        app.write_bytes(b'frontend')
        for name, (minimum, _) in CARD.BOOT_IDENTITY_FILES.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'x' * max(minimum, 64))
            path.chmod(0o600)
        for name in CARD.FRONTEND_DEFAULT_FILES:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('packaged fixture: ' + path.name).encode())
            path.chmod(0o644)
        for name in CARD.REQUIRED_P2_DIRECTORIES:
            (root / name).mkdir(parents=True, exist_ok=True)
        files = {p.relative_to(root).as_posix(): {
            'bytes': p.stat().st_size,
            'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
            for p in root.rglob('*') if p.is_file()}
        return {'p2_files': files,
                'identity_policy': CARD.PRIVATE_IDENTITY_POLICY,
                'simplemenu_sha256': files['apps/SimpleMenu-OD-v1.1.opk']['sha256']}

    def test_p2_exact_allowlist(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.make_tree(root)
            self.assertEqual(CARD.validate_tree(root, manifest), manifest['p2_files'])
            extra = root / 'local' / 'home' / '.ssh' / 'unapproved_key'
            extra.write_bytes(b'not authorized by the exact manifest')
            with self.assertRaisesRegex(ValueError, 'unapproved P2 path'):
                CARD.validate_tree(root, manifest)
            extra.unlink()
            (root / 'apps' / 'leak.opk').symlink_to(root / 'apps' / 'SimpleMenu-OD-v1.1.opk')
            with self.assertRaisesRegex(ValueError, 'symlink'):
                CARD.validate_tree(root, manifest)

    def test_game_content_is_rejected_even_with_matching_manifest(self):
        for name in ('apps/emulator.opk', 'bios/scph1001.bin',
                     'local/home/.pcsx4all/memcard.mcd', 'roms/game.bin'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self.make_tree(root)
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'game-data')
                manifest['p2_files'][name] = {'bytes': 9, 'sha256': CARD.digest(path)}
                with self.assertRaisesRegex(ValueError, 'unapproved P2 path'):
                    CARD.validate_tree(root, manifest)

    def test_missing_identity_is_rejected_before_build(self):
        for name in CARD.BOOT_IDENTITY_FILES:
            with self.subTest(path=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self.make_tree(root)
                (root / name).unlink()
                del manifest['p2_files'][name]
                with self.assertRaisesRegex(ValueError, 'missing boot identity files'):
                    CARD.validate_tree(root, manifest)

    def test_private_identity_requires_explicit_policy_and_safe_permissions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.make_tree(root)
            del manifest['identity_policy']
            with self.assertRaisesRegex(ValueError, 'identity policy required'):
                CARD.validate_tree(root, manifest)
            manifest['identity_policy'] = CARD.PRIVATE_IDENTITY_POLICY
            key = root / 'local/etc/gkd-mini/dropbear_rsa_host_key'
            key.chmod(0o644)
            with self.assertRaisesRegex(ValueError, 'permissions must be 0600'):
                CARD.validate_tree(root, manifest)
            key.chmod(0o600)
            key.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'size rejected'):
                CARD.validate_tree(root, manifest)

    def test_preseeded_home_requires_all_frontend_defaults(self):
        for name in CARD.FRONTEND_DEFAULT_FILES:
            with self.subTest(path=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self.make_tree(root)
                (root / name).unlink()
                del manifest['p2_files'][name]
                with self.assertRaisesRegex(ValueError, 'missing frontend defaults'):
                    CARD.validate_tree(root, manifest)

    def test_missing_frontend_work_directory_is_rejected(self):
        for name in CARD.REQUIRED_P2_DIRECTORIES - {'local/etc'}:
            with self.subTest(path=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self.make_tree(root)
                (root / name).rmdir()
                with self.assertRaisesRegex(ValueError, 'missing required P2 directory'):
                    CARD.validate_tree(root, manifest)

    def test_defaults_are_checked_against_the_actual_opk(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            root = work / 'input'
            root.mkdir()
            manifest = self.make_tree(root)
            package = work / 'package' / 'config'
            package.mkdir(parents=True)
            for name in CARD.FRONTEND_DEFAULT_FILES:
                shutil.copy2(root / name, package / Path(name).name)
            opk = root / 'apps/SimpleMenu-OD-v1.1.opk'
            opk.unlink()
            subprocess.run(['mksquashfs', str(package.parent), str(opk),
                            '-noappend', '-processors', '1', '-quiet'],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            manifest['p2_files']['apps/SimpleMenu-OD-v1.1.opk'] = {
                'bytes': opk.stat().st_size, 'sha256': CARD.digest(opk)}
            manifest['simplemenu_sha256'] = CARD.digest(opk)
            CARD.validate_tree(root, manifest)
            CARD.validate_frontend_defaults(root, work)
            config = root / 'local/home/.simplemenu/config.ini'
            config.write_bytes(b'')
            manifest['p2_files']['local/home/.simplemenu/config.ini'] = {
                'bytes': 0, 'sha256': CARD.digest(config)}
            # A coherent tree manifest alone cannot detect invalid defaults.
            CARD.validate_tree(root, manifest)
            with self.assertRaisesRegex(ValueError, 'frontend default differs from OPK'):
                CARD.validate_frontend_defaults(root, work)

    def test_identity_storage_boundary_resolves_symlinks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'private'
            root.mkdir()
            CARD.require_private_path(root / 'candidate.img', root)
            with self.assertRaisesRegex(ValueError, 'controlled private storage'):
                CARD.require_private_path(Path(temporary) / 'shared.img', root)
            (root / 'escape').symlink_to(Path(temporary), target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'controlled private storage'):
                CARD.require_private_path(root / 'escape' / 'candidate.img', root)

    def test_old_runtime_cannot_build_system_only_image(self):
        with self.assertRaisesRegex(ValueError, 'old firmware is incompatible'):
            CARD.validate_storage_runtime({})
        with self.assertRaisesRegex(ValueError, 'missing storage runtime'):
            CARD.validate_storage_runtime({'storage_policy': CARD.STORAGE_POLICY})

    def test_prefix_layout_and_slot_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary) / 'prefix.bin'
            with prefix.open('wb') as stream:
                stream.truncate(CARD.PREFIX_BYTES)
            with prefix.open('r+b') as stream:
                stream.seek(446)
                for kind, start, count in CARD.EXPECTED_OLD_PARTITIONS:
                    stream.write(CARD.partition_entry(kind, start, count))
                stream.seek(510)
                stream.write(b'\x55\xaa')
            zero_slot = hashlib.sha256(bytes(CARD.SLOT_BYTES)).hexdigest()
            manifest = {'accepted_a_sha256': zero_slot,
                        'accepted_r_sha256': zero_slot}
            CARD.validate_prefix(prefix, manifest)
            with prefix.open('r+b') as stream:
                stream.seek(446 + 16 + 8)
                stream.write(struct.pack('<I', 123))
            with self.assertRaisesRegex(ValueError, 'partition layout'):
                CARD.validate_prefix(prefix, manifest)

    def test_swap_header_rejects_signature_only(self):
        header = bytearray(8192)
        header[4086:4096] = b'SWAPSPACE2'
        with self.assertRaisesRegex(ValueError, 'incompatible at 1024'):
            CARD.validate_swap_header(header)


if __name__ == '__main__':
    unittest.main()
