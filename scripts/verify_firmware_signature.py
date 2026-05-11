"""
Mirror the on-device Ed25519 verification of a signed firmware.bin.

Use this before uploading a release artifact to confirm CI signed correctly,
or to inspect a downloaded artifact you suspect of being tampered with. Reads
the same trailer layout that `OtaUpdater::installUpdate` reads at runtime
(see sign_firmware.py for the exact format).

Usage:

    # Verify against an explicit public key file (raw 32 bytes or PEM).
    python scripts/verify_firmware_signature.py firmware.bin --pubkey ed25519_public.pem

    # Verify against the embedded public key constant in the firmware repo
    # (extracted from src/network/CrosspointPubKey.h).
    python scripts/verify_firmware_signature.py firmware.bin --embedded
"""

import argparse
import re
import sys
from pathlib import Path

try:
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
except ImportError:
    sys.stderr.write(
        "error: this script requires the 'cryptography' package.\n"
        "  pip install cryptography\n"
    )
    sys.exit(1)


SIGNATURE_LEN = 64
MAGIC = b"CPSG"
TRAILER_LEN = SIGNATURE_LEN + len(MAGIC)
EMBEDDED_HEADER = Path(__file__).parent.parent / "src/network/CrosspointPubKey.h"


def load_public_key(pubkey_path: Path) -> Ed25519PublicKey:
    raw = pubkey_path.read_bytes()
    # 32 raw bytes is the canonical wire format
    if len(raw) == 32:
        return Ed25519PublicKey.from_public_bytes(raw)
    # otherwise assume PEM
    key = serialization.load_pem_public_key(raw)
    if not isinstance(key, Ed25519PublicKey):
        raise SystemExit(f"error: expected Ed25519 key, got {type(key).__name__}")
    return key


def load_embedded_public_key() -> Ed25519PublicKey:
    """Parse the C header that ships the public key into the firmware. Expects a
    declaration like:

        constexpr uint8_t kCrosspointOtaPubKey[32] = {
            0x12, 0x34, ...
        };
    """
    if not EMBEDDED_HEADER.exists():
        raise SystemExit(
            f"error: {EMBEDDED_HEADER} not found — generate it from the keypair first"
        )
    text = EMBEDDED_HEADER.read_text()
    match = re.search(
        r"kCrosspointOtaPubKey\s*\[\s*32\s*\]\s*=\s*\{([^}]+)\}",
        text,
        re.DOTALL,
    )
    if not match:
        raise SystemExit(
            f"error: couldn't locate kCrosspointOtaPubKey[32] in {EMBEDDED_HEADER}"
        )
    bytes_text = match.group(1)
    hex_values = re.findall(r"0[xX]([0-9a-fA-F]{1,2})", bytes_text)
    if len(hex_values) != 32:
        raise SystemExit(
            f"error: expected 32 hex bytes in pubkey, got {len(hex_values)}"
        )
    raw = bytes(int(h, 16) for h in hex_values)
    return Ed25519PublicKey.from_public_bytes(raw)


def verify(firmware_path: Path, pubkey: Ed25519PublicKey) -> None:
    data = firmware_path.read_bytes()
    if len(data) < TRAILER_LEN:
        raise SystemExit(
            f"error: file too small ({len(data)} bytes) — no signature trailer"
        )

    if data[-len(MAGIC):] != MAGIC:
        raise SystemExit(
            f"error: missing 'CPSG' magic at EOF (got {data[-len(MAGIC):]!r})"
        )

    signature = data[-TRAILER_LEN:-len(MAGIC)]
    body = data[:-TRAILER_LEN]

    try:
        pubkey.verify(signature, body)
    except InvalidSignature:
        raise SystemExit("error: signature verification FAILED")

    print(
        f"OK: {firmware_path}\n"
        f"  body:      {len(body):>10d} bytes\n"
        f"  signature: {len(signature):>10d} bytes\n"
        f"  trailer:   {len(MAGIC):>10d} bytes ('CPSG')\n"
        f"  total:     {len(data):>10d} bytes"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("firmware", type=Path)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--pubkey", type=Path, help="raw 32B or PEM public key file")
    group.add_argument("--embedded", action="store_true",
                       help="read the public key constant from CrosspointPubKey.h")
    args = parser.parse_args()

    if args.embedded:
        pubkey = load_embedded_public_key()
    else:
        pubkey = load_public_key(args.pubkey)

    verify(args.firmware, pubkey)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
