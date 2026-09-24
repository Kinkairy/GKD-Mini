#!/usr/bin/env python3
"""Build a private, offline GKD Mini card image from sealed inputs.

The output is a hardware-test candidate, not a redistributable release.
"""

import argparse
import hashlib
import json
import os
import shutil
import struct
import stat
import tempfile
import subprocess
from pathlib import Path


MIB = 1024 * 1024
GIB = 1024 * MIB
IMAGE_BYTES = 4 * GIB
PREFIX_BYTES = 20 * MIB
P1_START = 40960 * 512
P1_BYTES = 805306880
P2_START = 1615872 * 512
P2_BYTES = 2 * GIB
P3_START = (P2_START + P2_BYTES + MIB - 1) // MIB * MIB
P3_BYTES = GIB
R_OFFSET = 3 * MIB
A_OFFSET = 9 * MIB
SLOT_BYTES = 6 * MIB
EXPECTED_OLD_PARTITIONS = ((0x83, 40960, 1572865),
                           (0x83, 1615872, 57726976),
                           (0x82, 59342848, 2097152))
# The A service's offline-load opens P2/local/etc before USB or UI startup.
# The current A loader also requires the original device identity for its USB
# bootstrap. These private images must not be mistaken for public releases.
STORAGE_POLICY = 'system-core-gamecard-v1'
SYSTEM_CONFIG_FILES = frozenset('local/etc/gkd-mini/' + name for name in (
    'gdkmini.conf', 'gdkmini.override.conf', 'input-routing.conf'))
FRONTEND_HOME = 'local/home/.simplemenu'
# SimpleMenu skips ALL default config setup when this directory already exists.
# Seeding section_groups therefore requires its packaged defaults and work dirs.
FRONTEND_DEFAULT_FILES = frozenset(FRONTEND_HOME + '/' + name for name in (
    'config.ini', 'alias.txt', 'favorites.sav', 'round61-performance.conf'))
REQUIRED_P2_DIRECTORIES = frozenset(('local/etc', 'bios') + tuple(
    FRONTEND_HOME + '/' + name for name in ('apps', 'games', 'tmp')))
BOOT_IDENTITY_FILES = {
    'local/home/.ssh/authorized_keys': (1, 1048576),
    'local/etc/shadow': (1, 4096),
    'local/etc/gkd-mini/dropbear_rsa_host_key': (64, 1048576),
}
PRIVATE_IDENTITY_POLICY = 'existing-device-private'
PRIVATE_ROOT = Path('/opt/gkd-build/private-state/gkd-mini-system-rebuild')
BUILDER_IMAGE = 'local/c-builder:2026.08.02-kernel'
SWAP_REGION_HASHES = (
    (0, 1024, ('5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef',)),
    (1024, 12, ('f128380dea7f568df2edeb74f23aea99498ea3e40f5b0ebad3c83f9d640c633b',)),
    (1052, 16, ('8ee37f8cb4ee7808070f9aada7a2c6111b782f4dbc64c81c117879468193cbf1',
                '374708fff7719dd5979ec875d56cd2286f6d3cf7ec317a3b25632aab28ec37bb')),
    (1068, 3018, ('1c36d6033bf5dc378c31e3f25af3ddcffbb30eda6abbf6aca9943a7be1db316c',)),
    (4096, 4096, ('ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7',)),
)


def require_private_path(path, root=PRIVATE_ROOT):
    if root.resolve() not in path.resolve().parents:
        raise ValueError('device identity inputs/output must stay in controlled private storage')


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(4 * MIB), b''):
            result.update(chunk)
    return result.hexdigest()


def checked_file(item, expected_bytes):
    path = Path(item['path'])
    if path.is_symlink() or not path.is_file() or path.stat().st_size != expected_bytes:
        raise ValueError(f'input size/type mismatch: {path}')
    if digest(path) != item['sha256']:
        raise ValueError(f'input digest mismatch: {path}')
    return path


def part(entry):
    return entry[4], *struct.unpack_from('<II', entry, 8)


def validate_prefix(path, manifest):
    with path.open('rb') as stream:
        mbr = stream.read(512)
        if mbr[510:512] != b'\x55\xaa':
            raise ValueError('missing MBR signature')
        actual = tuple(part(mbr[446 + 16 * i:462 + 16 * i]) for i in range(3))
        if actual != EXPECTED_OLD_PARTITIONS or mbr[494:510] != bytes(16):
            raise ValueError('source partition layout mismatch')
        for name, offset in (('accepted_r_sha256', R_OFFSET),
                             ('accepted_a_sha256', A_OFFSET)):
            stream.seek(offset)
            if hashlib.sha256(stream.read(SLOT_BYTES)).hexdigest() != manifest[name]:
                raise ValueError(f'source prefix {name} mismatch')
        stream.seek(20967424)
        if stream.read(4096) != bytes(4096):
            raise ValueError('source prefix update request is not empty')
    return mbr


def validate_tree(root, manifest):
    expected = manifest['p2_files']
    if not isinstance(expected, dict) or not expected:
        raise ValueError('empty P2 manifest')
    if manifest.get('identity_policy') != PRIVATE_IDENTITY_POLICY:
        raise ValueError('explicit existing-device-private identity policy required')
    missing_identity = BOOT_IDENTITY_FILES.keys() - expected.keys()
    if missing_identity:
        raise ValueError('missing boot identity files: ' + ', '.join(sorted(missing_identity)))
    missing_defaults = FRONTEND_DEFAULT_FILES - expected.keys()
    if missing_defaults:
        raise ValueError('missing frontend defaults: ' + ', '.join(sorted(missing_defaults)))
    allowed_directories = set(REQUIRED_P2_DIRECTORIES)
    for name in (*expected, *REQUIRED_P2_DIRECTORIES):
        path = Path(name)
        allowed_directories.update(parent.as_posix() for parent in path.parents
                                   if parent.as_posix() != '.')
    found = {}
    found_directories = set()
    for path in root.rglob('*'):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            raise ValueError(f'symlink in P2 input: {relative}')
        if path.is_dir():
            if relative not in allowed_directories:
                raise ValueError(f'unapproved P2 directory: {relative}')
            found_directories.add(relative)
            continue
        if not path.is_file() or not (
            relative == 'apps/SimpleMenu-OD-v1.1.opk'
            or relative.startswith('local/home/.simplemenu/section_groups/')
            and relative.count('/') == 4 and relative.endswith('.ini')
            or relative in BOOT_IDENTITY_FILES or relative in FRONTEND_DEFAULT_FILES
            or relative in SYSTEM_CONFIG_FILES
        ):
            raise ValueError(f'unapproved P2 path: {relative}')
        size = path.stat().st_size
        if relative in FRONTEND_DEFAULT_FILES and stat.S_IMODE(path.stat().st_mode) != 0o644:
            raise ValueError('frontend default permissions must be 0644: ' + relative)
        if relative in BOOT_IDENTITY_FILES:
            minimum, maximum = BOOT_IDENTITY_FILES[relative]
            if not minimum <= size <= maximum:
                raise ValueError('boot identity size rejected: ' + relative)
            if stat.S_IMODE(path.stat().st_mode) != 0o600:
                raise ValueError('boot identity permissions must be 0600: ' + relative)
        found[relative] = {'bytes': size, 'sha256': digest(path)}
    if found != expected:
        raise ValueError('P2 file manifest mismatch')
    missing = REQUIRED_P2_DIRECTORIES - found_directories
    if missing:
        raise ValueError('missing required P2 directory: ' + ', '.join(sorted(missing)))
    required = expected.get('apps/SimpleMenu-OD-v1.1.opk')
    if not required or required['sha256'] != manifest['simplemenu_sha256']:
        raise ValueError('required frontend OPK mismatch')
    if len([name for name in found if name.startswith('apps/')]) != 1:
        raise ValueError('system image must contain only the frontend application')
    return found


def run(command):
    subprocess.run(command, check=True)


def validate_frontend_defaults(tree, scratch):
    # Derive the required bytes from the very same SHA-checked frontend OPK;
    # a self-consistent manifest must not bless empty or unrelated config files.
    with tempfile.TemporaryDirectory(prefix='frontend-defaults-', dir=scratch) as tmp:
        unpacked = Path(tmp) / 'opk'
        members = ['config/' + Path(name).name for name in sorted(FRONTEND_DEFAULT_FILES)]
        result = subprocess.run(
            ['unsquashfs', '-no-progress', '-no-xattrs', '-d', str(unpacked),
             str(tree / 'apps/SimpleMenu-OD-v1.1.opk'), *members],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        if result.returncode:
            raise ValueError('cannot extract frontend packaged defaults')
        for name in sorted(FRONTEND_DEFAULT_FILES):
            packaged = unpacked / 'config' / Path(name).name
            supplied = tree / name
            if (packaged.is_symlink() or not packaged.is_file()
                    or packaged.stat().st_size != supplied.stat().st_size
                    or digest(packaged) != digest(supplied)):
                raise ValueError('frontend default differs from OPK: ' + name)


def validate_image_boot_files(p2, files, scratch):
    # Extract only into a private temporary directory; never print key contents.
    with tempfile.TemporaryDirectory(prefix='identity-readback-', dir=scratch) as tmp:
        for index, name in enumerate(sorted(set(BOOT_IDENTITY_FILES) | FRONTEND_DEFAULT_FILES)):
            details = subprocess.check_output(
                ['debugfs', '-R', 'stat /' + name, str(p2)],
                text=True, stderr=subprocess.DEVNULL)
            mode = '0600' if name in BOOT_IDENTITY_FILES else '0644'
            if not ('Type: regular' in details and 'Mode:  ' + mode in details
                    and 'User:     0' in details and 'Group:     0' in details):
                raise ValueError('P2 boot file owner/type/mode rejected: ' + name)
            target = Path(tmp) / str(index)
            subprocess.run(['debugfs', '-R', 'dump /' + name + ' ' + str(target), str(p2)],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if (not target.is_file() or target.stat().st_size != files[name]['bytes']
                    or digest(target) != files[name]['sha256']):
                raise ValueError('P2 boot file readback mismatch: ' + name)


def build_p2(tree, scratch, builder_digest):
    actual = subprocess.check_output(
        ['docker', 'image', 'inspect', BUILDER_IMAGE, '--format', '{{.Id}}'],
        text=True).strip()
    if actual != builder_digest:
        raise ValueError('builder image mismatch')
    p2 = scratch / 'p2.ext3'
    with p2.open('xb') as stream:
        p2.chmod(0o600)
        stream.truncate(P2_BYTES)
    command = (
        'cp -R /inputs/. /stage/ && chown -R 0:0 /stage && '
        'mke2fs -q -F -t ext3 -b 4096 -L GKD-DATA -d /stage '
        '/out/p2.ext3 524288 && chown %d:%d /out/p2.ext3'
        % (os.getuid(), os.getgid()))
    run(['docker', 'run', '--rm', '--network', 'none', '--read-only',
         '--security-opt', 'no-new-privileges', '--cap-drop', 'ALL',
         '--cap-add', 'CHOWN', '--cap-add', 'DAC_OVERRIDE',
         '--tmpfs', '/stage:rw,size=1073741824,mode=0755',
         '--mount', f'type=bind,src={tree},dst=/inputs,readonly',
         '--mount', f'type=bind,src={scratch},dst=/out',
         '--entrypoint', 'sh', BUILDER_IMAGE, '-ec', command])
    if p2.stat().st_size != P2_BYTES:
        raise ValueError('P2 image size mismatch')
    check = subprocess.run(['e2fsck', '-fn', str(p2)], capture_output=True, text=True)
    if check.returncode != 0:
        raise ValueError('P2 filesystem check failed: ' + check.stdout[-500:])
    features = subprocess.check_output(['dumpe2fs', '-h', str(p2)],
                                       text=True, stderr=subprocess.DEVNULL)
    feature_line = next(line for line in features.splitlines()
                        if line.startswith('Filesystem features:'))
    if any(name in feature_line for name in ('extent', '64bit', 'metadata_csum')):
        raise ValueError('P2 is not ext3-compatible')
    frontend = subprocess.check_output(
        ['debugfs', '-R', 'stat /apps/SimpleMenu-OD-v1.1.opk', str(p2)],
        text=True, stderr=subprocess.DEVNULL)
    if 'User:     0' not in frontend and 'User: 0' not in frontend:
        raise ValueError('frontend OPK is not root-owned')
    for name in sorted(REQUIRED_P2_DIRECTORIES):
        directory = subprocess.check_output(
            ['debugfs', '-R', 'stat /' + name, str(p2)],
            text=True, stderr=subprocess.DEVNULL)
        if 'Type: directory' not in directory:
            raise ValueError('P2 boot-required directory is missing: ' + name)
    return p2, feature_line


def copy_at(output, source, offset):
    output.seek(offset)
    with source.open('rb') as stream:
        shutil.copyfileobj(stream, output, 4 * MIB)


def partition_entry(kind, start, sectors):
    return bytes((0, 0xfe, 0xff, 0xff, kind, 0xfe, 0xff, 0xff)) + struct.pack(
        '<II', start, sectors)


def validate_swap_header(header):
    if len(header) != 8192 or header[4086:4096] != b'SWAPSPACE2':
        raise ValueError('invalid P3 swap signature')
    for offset, size, allowed in SWAP_REGION_HASHES:
        if hashlib.sha256(header[offset:offset + size]).hexdigest() not in allowed:
            raise ValueError(f'P3 swap header incompatible at {offset}')


def validate_storage_runtime(manifest):
    if manifest.get('storage_policy') != STORAGE_POLICY:
        raise ValueError('storage isolation runtime required; old firmware is incompatible')
    receipt = manifest.get('runtime_build', {})
    if not receipt:
        raise ValueError('missing storage runtime build receipt')
    path = checked_file(receipt, receipt['bytes'])
    plan = json.loads(path.read_text())
    if plan.get('status') != 'BUILD_PASS' or manifest['a_slot']['sha256'] not in plan.get('outputs', {}).values():
        raise ValueError('storage runtime A artifact mismatch')
    root = Path(__file__).resolve().parents[1]
    for name in ('system/application-core/include/gkd-app-storage.h',
                 'system/application-core/source/gkd-app-host.c',
                 'system/application-core/source/gkd-app-launcher.c',
                 'system/application-core/source/gkd-app-media.c',
                 'system/application-core/device/gkd-simplemenu-opkrun',
                 'system/rc33-system-update/scripts/build-recovery-capsule.py'):
        if plan.get('source', {}).get(name) != digest(root / name):
            raise ValueError('storage runtime source mismatch: ' + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--p2-tree', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    for private_path in (args.manifest, args.p2_tree, args.output, args.scratch):
        require_private_path(private_path)
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    if manifest.get('distribution') != 'private-hardware-test':
        raise ValueError('only private hardware-test inputs are supported')
    if args.output.exists() or args.evidence.exists() or args.scratch.exists():
        raise ValueError('output, evidence and scratch must not exist')
    if not args.p2_tree.is_dir() or args.p2_tree.is_symlink():
        raise ValueError('invalid P2 tree')
    inputs = {name: checked_file(manifest[name], size)
              for name, size in (('prefix', PREFIX_BYTES), ('p1', P1_BYTES),
                                 ('a_slot', SLOT_BYTES), ('r_slot', SLOT_BYTES))}
    validate_prefix(inputs['prefix'], manifest)
    validate_storage_runtime(manifest)
    files = validate_tree(args.p2_tree, manifest)
    if P3_START + P3_BYTES > IMAGE_BYTES:
        raise ValueError('image layout exceeds output size')
    args.scratch.mkdir(parents=True, mode=0o700)
    try:
        validate_frontend_defaults(args.p2_tree, args.scratch)
        p2, feature_line = build_p2(args.p2_tree, args.scratch,
                                    manifest['builder_image'])
        validate_image_boot_files(p2, files, args.scratch)
        p3 = args.scratch / 'p3.swap'
        with p3.open('xb') as stream:
            stream.truncate(P3_BYTES)
        p3.chmod(0o600)
        run(['mkswap', '-L', 'GKD-SWAP', str(p3)])
        with p3.open('rb') as swap:
            swap_header = swap.read(8192)
        validate_swap_header(swap_header)
        with args.output.open('xb') as output:
            args.output.chmod(0o600)
            output.truncate(IMAGE_BYTES)
            for name, offset in (('prefix', 0), ('p1', P1_START),
                                 ('r_slot', R_OFFSET), ('a_slot', A_OFFSET)):
                copy_at(output, inputs[name], offset)
            copy_at(output, p2, P2_START)
            output.seek(P3_START)
            output.write(swap_header)
            output.seek(446 + 16)
            output.write(partition_entry(0x83, P2_START // 512,
                                         P2_BYTES // 512))
            output.write(partition_entry(0x82, P3_START // 512,
                                         P3_BYTES // 512))
            output.flush()
            os.fsync(output.fileno())
        with args.output.open('rb') as output:
            mbr = output.read(512)
            expected = ((0x83, 40960, 1572865),
                        (0x83, P2_START // 512, P2_BYTES // 512),
                        (0x82, P3_START // 512, P3_BYTES // 512))
            if tuple(part(mbr[446 + 16 * i:462 + 16 * i])
                     for i in range(3)) != expected:
                raise ValueError('output MBR mismatch')
            for name, offset in (('r_slot', R_OFFSET), ('a_slot', A_OFFSET)):
                output.seek(offset)
                if hashlib.sha256(output.read(SLOT_BYTES)).hexdigest() != manifest[name]['sha256']:
                    raise ValueError(f'output {name} mismatch')
            output.seek(P3_START)
            validate_swap_header(output.read(8192))
        receipt = {
            'status': 'OFFLINE_IMAGE_PASS_NOT_DEVICE_ACCEPTED',
            'distribution': 'private-hardware-test',
            'identity_policy': PRIVATE_IDENTITY_POLICY,
            'storage_policy': STORAGE_POLICY,
            'contains_existing_device_identity': True,
            'image_bytes': IMAGE_BYTES, 'image_sha256': digest(args.output),
            'source_commit': manifest['source_commit'],
            'builder_script_sha256': digest(Path(__file__)),
            'p2_file_count': len(files), 'p2_features': feature_line,
            'p2_required_directories': sorted(REQUIRED_P2_DIRECTORIES),
            'frontend_defaults': {name: files[name] for name in sorted(FRONTEND_DEFAULT_FILES)},
            'frontend_defaults_match_packaged_opk': True,
            'partitions': expected, 'input_hashes': {
                name: manifest[name]['sha256'] for name in inputs},
            'physical_restore_test': 'NOT_PERFORMED',
            'image_boot_test': 'NOT_PERFORMED',
        }
        args.evidence.write_text(json.dumps(receipt, indent=2) + '\n',
                                 encoding='utf-8')
        print(json.dumps(receipt))
    except BaseException:
        if args.output.exists():
            args.output.unlink()
        raise


if __name__ == '__main__':
    main()
