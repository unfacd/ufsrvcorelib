#!/usr/bin/env python3
"""
VERSION 1.0

Encrypt a plaintext secret for storage in a ufsrv config file.

Key operations (mutually exclusive):

  --generate-key          generate and print a new 32-byte AES-256-GCM key (hex)
  --key-file <path>       convert a raw 32-byte key file to its 64-hex equivalent
  --key-b64 <base64>      convert a base64-encoded 32-byte key to its 64-hex equivalent
  --key-text <text>       derive a 32-byte key from text (SHA-256) and print its hex

Encryption:

  --key <64-hex> [plaintext]
                          encrypt <plaintext> (or stdin) with that key and print
                          the "enc:v1:<nonce>:<ciphertext>:<tag>" value to place
                          in the config file. Use the SAME key for every secret.
  --credential-file <path> [plaintext]
                          encrypt using the 64-hex key in a credential file.

Store the 64-hex key as the systemd credential
LoadCredential=ufsrv_secret_key:<file> (the file holds exactly the 64 hex chars).

The server decrypts any such value at runtime with the single application-wide
key read from $CREDENTIALS_DIRECTORY/ufsrv_secret_key
(src/ufsrv_core/ufsrv_config/ufsrv_config_secret.c).

Usage:
  python3 encrypt_plaintext.py --generate-key
  python3 encrypt_plaintext.py --key-file /path/to/key.bin
  python3 encrypt_plaintext.py --key-b64 <base64>
  python3 encrypt_plaintext.py --key-text 'passphrase'
  python3 encrypt_plaintext.py --key <64-hex> 'db-password'
  python3 encrypt_plaintext.py --credential-file /opt/ufsrv/etc/secrets/ufsrv_secret_key < secret.txt

Requires: pip install cryptography
"""

import argparse
import base64
import hashlib
import secrets
import sys

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

KEY_BYTES = 32
NONCE_BYTES = 12
TAG_BYTES = 16
PREFIX = "enc:v1:"


def encrypt(plaintext: bytes, key: bytes):
    nonce = secrets.token_bytes(NONCE_BYTES)
    ct_plus_tag = AESGCM(key).encrypt(nonce, plaintext, None)
    ciphertext = ct_plus_tag[:-TAG_BYTES]
    tag = ct_plus_tag[-TAG_BYTES:]
    return nonce, ciphertext, tag


def print_key_hex(key: bytes):
    print("# store as LoadCredential=ufsrv_secret_key:<file> (hex only)")
    print(f"KEY={key.hex()}")


def main():
    ap = argparse.ArgumentParser(description="Encrypt a plaintext secret for a ufsrv config file")
    ap.add_argument("--generate-key", action="store_true",
                    help="generate and print a new 32-byte key (hex) and exit")
    ap.add_argument("--key-file", default=None,
                    help="convert a raw 32-byte key file to its 64-hex equivalent and exit")
    ap.add_argument("--key-b64", default=None,
                    help="convert a base64-encoded 32-byte key to its 64-hex equivalent and exit")
    ap.add_argument("--key-text", default=None,
                    help="derive a 32-byte key from text (SHA-256) and print its 64-hex equivalent")
    ap.add_argument("--key", default=None,
                    help="32-byte key as 64 hex chars (required to encrypt)")
    ap.add_argument("--credential-file", default=None,
                    help="read the 64-hex encryption key from an existing credential file")
    ap.add_argument("plaintext", nargs="?", default=None,
                    help="plaintext secret (default: read from stdin)")
    args = ap.parse_args()

    modes = [
        args.generate_key,
        args.key_file is not None,
        args.key_b64 is not None,
        args.key_text is not None,
        args.key is not None,
        args.credential_file is not None,
    ]
    if sum(modes) > 1:
        sys.exit("error: --generate-key, --key-file, --key-b64, --key-text, --key and --credential-file are mutually exclusive")

    if args.generate_key:
        print_key_hex(secrets.token_bytes(KEY_BYTES))
        return

    if args.key_file is not None:
        with open(args.key_file, "rb") as f:
            key = f.read()
        if len(key) != KEY_BYTES:
            sys.exit(f"error: {args.key_file} is {len(key)} bytes, expected {KEY_BYTES}")
        print_key_hex(key)
        return

    if args.key_b64 is not None:
        try:
            key = base64.b64decode(args.key_b64, validate=True)
        except Exception as e:
            sys.exit(f"error: --key-b64 is not valid base64: {e}")
        if len(key) != KEY_BYTES:
            sys.exit(f"error: --key-b64 decodes to {len(key)} bytes, expected {KEY_BYTES}")
        print_key_hex(key)
        return

    if args.key_text is not None:
        key = hashlib.sha256(args.key_text.encode("utf-8")).digest()
        print_key_hex(key)
        return

    encoded_key = args.key
    if args.credential_file is not None:
        try:
            with open(args.credential_file, "rb") as f:
                # Accept a single hex line with an optional trailing newline.
                raw_key = f.read(2 * KEY_BYTES + 3)
            if raw_key.endswith(b"\r\n"):
                raw_key = raw_key[:-2]
            elif raw_key.endswith(b"\n"):
                raw_key = raw_key[:-1]
            encoded_key = raw_key.decode("ascii")
        except (OSError, UnicodeError):
            sys.exit("error: could not read the hex credential file")
        if len(encoded_key) != 2 * KEY_BYTES:
            sys.exit("error: credential file must contain exactly 64 hex characters")
    if encoded_key is None:
        sys.exit("error: --key or --credential-file is required to encrypt")

    try:
        key = bytes.fromhex(encoded_key)
    except ValueError:
        sys.exit("error: --key must be 64 hex characters (32 bytes)")
    if len(key) != KEY_BYTES:
        sys.exit(f"error: --key decodes to {len(key)} bytes, expected {KEY_BYTES}")

    if args.plaintext is not None:
        plaintext = args.plaintext.encode()
    else:
        plaintext = sys.stdin.buffer.read().rstrip(b"\n")

    nonce, ciphertext, tag = encrypt(plaintext, key)

    print("# config file — encrypted value")
    print(f"VALUE={PREFIX}{nonce.hex()}:{ciphertext.hex()}:{tag.hex()}")


if __name__ == "__main__":
    main()
