"""
Append an Ed25519 signature trailer to a firmware.bin so the OTA path on the
device can verify it before flashing. SD-card update path keeps accepting
unsigned binaries; this trailer is only enforced by `OtaUpdater::installUpdate`.

Trailer layout (68 bytes appended at EOF):

    [ ... original firmware bytes ... ]   <- bootloader sees this
    [ Ed25519 signature (64 bytes)    ]   <- signature over original bytes
    [ magic 'CPSG' (4 bytes ASCII)    ]   <- sentinel for the verifier

The device-side verifier reads the last 4 bytes, confirms the magic, takes the
preceding 64 bytes as signature, then runs Ed25519 verify over the remaining
file. On success the cached download is truncated to (file_size - 68) before
`flashFromSdPath` runs, so existing image-validation logic keeps working
unchanged.

Usage:

    # CI: read PEM-encoded private key from env, sign in place.
    ED25519_PRIVATE_KEY="$(cat key.pem)" \
        python scripts/sign_firmware.py firmware.bin

    # Local: sign with explicit key file, write to a different output.
    python scripts/sign_firmware.py firmware.bin \
        --key /path/to/ed25519_private.pem \
        --output firmware-signed.bin

The script is idempotent — refuses to sign a file that already carries the
trailer (detected via the 'CPSG' magic) unless --force is passed.
"""

import argparse
import os
import sys
from pathlib import Path

try:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
except ImportError:
    sys.stderr.write(
        "error: this script requires the 'cryptography' package.\n"
        "  pip install cryptography\n"
    )
    sys.exit(1)


SIGNATURE_LEN = 64
MAGIC = b"CPSG"
TRAILER_LEN = SIGNATURE_LEN + len(MAGIC)


def load_private_key(key_pem: bytes) -> Ed25519PrivateKey:
    """Accept a PEM-encoded Ed25519 private key. PKCS#8 or OpenSSH formats both
    parsable by the cryptography library."""
    key = serialization.load_pem_private_key(key_pem, password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise SystemExit(
            f"error: expected Ed25519 key, got {type(key).__name__}"
        )
    return key


def is_already_signed(data: bytes) -> bool:
    return len(data) >= TRAILER_LEN and data[-len(MAGIC):] == MAGIC


def sign_firmware(input_path: Path, output_path: Path, key: Ed25519PrivateKey,
                  force: bool) -> None:
    data = input_path.read_bytes()

    if is_already_signed(data):
        if not force:
            raise SystemExit(
                f"error: {input_path} already carries a 'CPSG' trailer. "
                "Pass --force to re-sign (existing trailer will be stripped)."
            )
        # strip and re-sign over the original bytes
        data = data[:-TRAILER_LEN]

    signature = key.sign(data)
    if len(signature) != SIGNATURE_LEN:
        raise SystemExit(
            f"internal error: Ed25519 signature length {len(signature)} != {SIGNATURE_LEN}"
        )

    signed = data + signature + MAGIC
    output_path.write_bytes(signed)

    pub = key.public_key().public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    )
    print(
        f"signed: {input_path}\n"
        f"  size:    {len(data):>10d} bytes\n"
        f"  signed:  {len(signed):>10d} bytes (+{TRAILER_LEN})\n"
        f"  output:  {output_path}\n"
        f"  pubkey:  {pub.hex()}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("firmware", type=Path, help="firmware.bin to sign")
    parser.add_argument(
        "--key",
        type=Path,
        help="PEM-encoded Ed25519 private key (default: ED25519_PRIVATE_KEY env)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="output path (default: overwrite input in place)",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="re-sign even if a 'CPSG' trailer already exists",
    )
    args = parser.parse_args()

    if args.key:
        key_pem = args.key.read_bytes()
    else:
        env = os.environ.get("ED25519_PRIVATE_KEY")
        if not env:
            raise SystemExit(
                "error: --key not provided and ED25519_PRIVATE_KEY env is unset"
            )
        key_pem = env.encode("utf-8")

    key = load_private_key(key_pem)
    output = args.output or args.firmware
    sign_firmware(args.firmware, output, key, args.force)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
