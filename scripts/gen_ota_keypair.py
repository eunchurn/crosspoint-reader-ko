"""
Generate the Ed25519 keypair used to sign OTA firmware images.

This is a one-shot, MAINTAINER-LOCAL operation. Run once, then:

  1. Save private key to a password manager (1Password / Bitwarden / etc.)
  2. Register private key as the `ED25519_PRIVATE_KEY` GitHub Actions secret
  3. Commit the generated public-key header (`src/network/CrosspointPubKey.h`)
     to the firmware repo
  4. Delete the private-key file from disk

If the public-key header already exists, the script refuses to overwrite it
unless --force is passed — re-keying is a deliberate event that requires
coordinating with already-deployed devices (see firmware-signature-migration.md
"키 회전 절차").

Usage:

    # Default: writes ed25519_private.pem to cwd, header to src/network/.
    python scripts/gen_ota_keypair.py

    # Custom output paths:
    python scripts/gen_ota_keypair.py \
        --out-private /secure/path/key.pem \
        --out-header src/network/CrosspointPubKey.h
"""

import argparse
import os
import stat
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
except ImportError:
    sys.stderr.write(
        "error: this script requires the 'cryptography' package.\n"
        "  pip install cryptography  (or run inside a venv)\n"
    )
    sys.exit(1)


REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PRIVATE = Path("ed25519_private.pem")
DEFAULT_HEADER = REPO_ROOT / "src/network/CrosspointPubKey.h"


def format_header(pubkey_bytes: bytes, generated_at: str) -> str:
    if len(pubkey_bytes) != 32:
        raise ValueError(f"expected 32-byte Ed25519 pubkey, got {len(pubkey_bytes)}")
    rows = []
    for i in range(0, 32, 8):
        chunk = pubkey_bytes[i : i + 8]
        rows.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    body = "\n".join(rows)
    return f"""// Ed25519 public key for CrossPoint OTA firmware-signature verification.
//
// Pairs with the private key registered as ED25519_PRIVATE_KEY in GitHub
// Actions. See docs/firmware-signature-migration.md for rotation procedure.
//
// Generated: {generated_at}
// DO NOT EDIT BY HAND. Regenerate via scripts/gen_ota_keypair.py.

#pragma once

#include <cstdint>

constexpr uint8_t kCrosspointOtaPubKey[32] = {{
{body}
}};
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument(
        "--out-private",
        type=Path,
        default=DEFAULT_PRIVATE,
        help=f"private key PEM output (default: {DEFAULT_PRIVATE})",
    )
    parser.add_argument(
        "--out-header",
        type=Path,
        default=DEFAULT_HEADER,
        help=f"public key C header output (default: {DEFAULT_HEADER})",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="overwrite existing private key or header",
    )
    args = parser.parse_args()

    if args.out_private.exists() and not args.force:
        raise SystemExit(
            f"error: {args.out_private} exists. Refusing to overwrite without --force."
        )
    if args.out_header.exists() and not args.force:
        raise SystemExit(
            f"error: {args.out_header} exists. "
            "Re-keying needs the rotation procedure — pass --force only if intentional."
        )

    key = Ed25519PrivateKey.generate()
    pem = key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    )
    pub_raw = key.public_key().public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    )

    args.out_private.parent.mkdir(parents=True, exist_ok=True)
    args.out_private.write_bytes(pem)
    # Mode 0600 — only owner readable. Posix only; on Windows this no-ops.
    try:
        os.chmod(args.out_private, stat.S_IRUSR | stat.S_IWUSR)
    except OSError:
        pass

    args.out_header.parent.mkdir(parents=True, exist_ok=True)
    generated_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    args.out_header.write_text(format_header(pub_raw, generated_at))

    print(f"private key: {args.out_private} (mode 0600)")
    print(f"public hex:  {pub_raw.hex()}")
    print(f"header:      {args.out_header}")
    print()
    print("Next steps:")
    print(f"  1. cat {args.out_private} | pbcopy   # macOS — copy to clipboard")
    print( "     gh secret set ED25519_PRIVATE_KEY --repo <owner>/crosspoint-reader-ko < " + str(args.out_private))
    print( "     (or paste into GitHub UI: Settings → Secrets and variables → Actions)")
    print( "  2. Save the private key file to your password manager (1Password etc.)")
    print(f"  3. Securely delete the local copy: shred -u {args.out_private}")
    print(f"     (or `rm -P {args.out_private}` on macOS)")
    print(f"  4. Commit {args.out_header} to the firmware repo")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
