#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from package import sha256_file, verify

parser = argparse.ArgumentParser()
parser.add_argument("package", type=Path)
parser.add_argument("public_key", type=Path)
args = parser.parse_args()
manifest = verify(args.package, args.public_key)
print("GKDSU_VERIFY=PASS from=" + manifest["release"]["from_version"] +
      " to=" + manifest["release"]["to_version"] +
      " sha256=" + sha256_file(args.package))
