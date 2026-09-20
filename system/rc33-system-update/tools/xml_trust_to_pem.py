#!/usr/bin/env python3
import argparse, base64, re
from pathlib import Path

def length(n):
    if n < 128: return bytes([n])
    raw = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(raw)]) + raw

def item(tag, body): return bytes([tag]) + length(len(body)) + body
def integer(raw):
    raw = raw.lstrip(b"\0") or b"\0"
    if raw[0] & 0x80: raw = b"\0" + raw
    return item(2, raw)

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path); parser.add_argument("output", type=Path)
args = parser.parse_args()
text = args.source.read_text(encoding="utf-8").strip()
match = re.fullmatch(r"<RSAKeyValue><Modulus>([A-Za-z0-9+/=]+)</Modulus><Exponent>AQAB</Exponent></RSAKeyValue>", text)
if not match or args.output.exists(): raise SystemExit("GKDSU_TRUST=BLOCKED")
rsa = item(0x30, integer(base64.b64decode(match.group(1))) + integer(b"\x01\x00\x01"))
algorithm = bytes.fromhex("300d06092a864886f70d0101010500")
spki = item(0x30, algorithm + item(3, b"\0" + rsa))
encoded = base64.b64encode(spki).decode("ascii")
lines = [encoded[i:i+64] for i in range(0, len(encoded), 64)]
args.output.write_text("-----BEGIN PUBLIC KEY-----\n" + "\n".join(lines) +
                       "\n-----END PUBLIC KEY-----\n", encoding="ascii")
print("GKDSU_TRUST=PASS")
