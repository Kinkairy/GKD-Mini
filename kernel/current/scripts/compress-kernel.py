#!/usr/bin/env python3
"""Build the pinned upstream gzip encoder and verify exact kernel round-trip."""
import gzip
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[3]
ARCHIVE_NAME = 'zopfli-1.0.3.tar.gz'


def compress(source: Path, output: Path, archive: Path) -> dict:
    if output.exists() or source.is_symlink() or archive.is_symlink():
        raise ValueError('unsafe input or output already exists')
    contract = json.loads((ROOT / 'build/rc3.6-inputs.json').read_text())['files'][ARCHIVE_NAME]
    archived = archive.read_bytes()
    if len(archived) != contract['bytes'] or hashlib.sha256(archived).hexdigest() != contract['sha256']:
        raise ValueError('encoder source integrity mismatch')
    raw = source.read_bytes()
    if not raw or len(raw) >= 0x800000:
        raise ValueError('kernel raw size outside boot contract')
    with tempfile.TemporaryDirectory(prefix='gzip-encoder-', dir=output.parent) as tmp:
        root = Path(tmp)
        with tarfile.open(archive, 'r:gz') as bundle:
            for member in bundle.getmembers():
                parts = PurePosixPath(member.name)
                if parts.is_absolute() or '..' in parts.parts or not (member.isfile() or member.isdir()):
                    raise ValueError('unsafe encoder source archive')
                relative = Path(*parts.parts[1:])
                target = root / relative
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with target.open('xb') as f:
                        f.write(bundle.extractfile(member).read())
        sources = sorted((root / 'src/zopfli').glob('*.c'))
        if not sources or not (root / 'COPYING').is_file():
            raise ValueError('missing encoder sources or license')
        encoder = root / 'zopfli'
        subprocess.run(['cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Wno-unused-function',
                        *map(str, sources), '-lm', '-o', str(encoder)], check=True)
        packed = subprocess.check_output([str(encoder), '--i5', '--gzip', '-c', str(source)])
    # The output stays standard gzip. Neither the raw kernel nor its layout
    # may be adjusted to fit; the slot assembler still enforces the boundary.
    if gzip.decompress(packed) != raw or packed[4:8] != bytes(4):
        raise ValueError('gzip parity or timestamp mismatch')
    with output.open('xb') as f:
        f.write(packed)
    receipt = {'encoder': 'zopfli-1.0.3', 'iterations': 5,
               'source_sha256': contract['sha256'], 'raw_bytes': len(raw),
               'raw_sha256': hashlib.sha256(raw).hexdigest(), 'gzip_bytes': len(packed),
               'gzip_sha256': hashlib.sha256(packed).hexdigest(), 'round_trip': True}
    output.with_name(output.name + '.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('usage: compress-kernel.py RAW OUTPUT PINNED_ARCHIVE')
    print('GKD_KERNEL_GZIP=PASS ' + json.dumps(compress(*map(Path, sys.argv[1:]))))
