#!/usr/bin/env python3
"""Generate, verify, parse and update BRK $sha256$ password hashes.

The target implementation in lib/crypto/pw.c hashes the concatenation of
the salt and the password:

    hex(SHA256(salt + password))

and stores it as:

    $sha256$<salt>$<hex>
"""

import argparse
import getpass
import hashlib
import hmac
import os
import secrets
import stat
import sys
import tempfile

PREFIX = "$sha256$"
HEX_LEN = 64
DEFAULT_SHADOW = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "etc", "shadow")
)


def parse_hash(stored):
    if not stored.startswith(PREFIX):
        raise ValueError("not a $sha256$ hash")
    rest = stored[len(PREFIX) :]
    salt, sep, digest = rest.partition("$")
    if not sep or not salt or len(digest) != HEX_LEN:
        raise ValueError("malformed $sha256$ hash")
    if any(c not in "0123456789abcdef" for c in digest):
        raise ValueError("malformed hash digest")
    if any(c in "$:\n\r" for c in salt):
        raise ValueError("invalid salt")
    return salt, digest


def make_hash(password, salt=None):
    if salt is None:
        salt = secrets.token_hex(4)
    digest = hashlib.sha256((salt + password).encode()).hexdigest()
    return f"{PREFIX}{salt}${digest}"


def verify_password(password, stored):
    try:
        salt, digest = parse_hash(stored)
    except ValueError:
        return False
    computed = hashlib.sha256((salt + password).encode()).hexdigest()
    return hmac.compare_digest(computed, digest)


def shadow_entries(path):
    with open(path, "r", encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            stripped = line.rstrip("\r\n")
            if not stripped or stripped.startswith("#"):
                continue
            name, sep, rest = stripped.partition(":")
            if not sep:
                continue
            yield lineno, name, rest


def update_shadow(path, user, new_hash):
    with open(path, "r", encoding="utf-8", newline="") as f:
        lines = f.readlines()

    found = False
    for i, line in enumerate(lines):
        stripped = line.rstrip("\r\n")
        if not stripped or stripped.startswith("#"):
            continue
        name, sep, rest = stripped.partition(":")
        if not sep or name != user:
            continue
        extra = ""
        if ":" in rest:
            extra = rest[rest.index(":") :]
        lines[i] = f"{user}:{new_hash}{extra}\n"
        found = True
        break

    if not found:
        raise ValueError(f"user not found in {path}: {user}")

    directory = os.path.dirname(path) or "."
    mode = stat.S_IMODE(os.stat(path).st_mode)
    fd, tmp = tempfile.mkstemp(prefix=".shadow.", dir=directory)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as f:
            f.writelines(lines)
            f.flush()
            os.fsync(f.fileno())
        os.chmod(tmp, mode)
        os.replace(tmp, path)
    except Exception:
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass
        raise


def cmd_hash(args):
    try:
        print(make_hash(args.password, args.salt))
    except ValueError as exc:
        print(f"pwutil: {exc}", file=sys.stderr)
        return 1
    return 0


def cmd_verify(args):
    if verify_password(args.password, args.hash):
        print("OK")
        return 0
    print("FAIL", file=sys.stderr)
    return 1


def cmd_parse(args):
    try:
        salt, digest = parse_hash(args.hash)
    except ValueError as exc:
        print(f"pwutil: {exc}", file=sys.stderr)
        return 1
    print(f"format: {PREFIX}")
    print(f"salt:   {salt}")
    print(f"sha256: {digest}")
    return 0


def cmd_list(args):
    try:
        entries = list(shadow_entries(args.shadow))
    except OSError as exc:
        print(f"pwutil: {exc}", file=sys.stderr)
        return 1
    for _, name, rest in entries:
        stored = rest.partition(":")[0]
        try:
            salt, digest = parse_hash(stored)
        except ValueError:
            print(f"{name}: (not a $sha256$ hash)")
            continue
        print(f"{name}: salt={salt} sha256={digest}")
    return 0


def cmd_set(args):
    if not args.password:
        print("pwutil: empty password is not allowed", file=sys.stderr)
        return 1
    try:
        new_hash = make_hash(args.password, args.salt)
        update_shadow(args.shadow, args.user, new_hash)
    except (OSError, ValueError) as exc:
        print(f"pwutil: {exc}", file=sys.stderr)
        return 1
    print(f"password updated for {args.user}")
    return 0


def cmd_passwd(args):
    password = getpass.getpass("New password: ")
    confirm = getpass.getpass("Retype new password: ")
    if password != confirm:
        print("pwutil: passwords do not match", file=sys.stderr)
        return 1
    if not password:
        print("pwutil: empty password is not allowed", file=sys.stderr)
        return 1
    try:
        new_hash = make_hash(password)
        update_shadow(args.shadow, args.user, new_hash)
    except (OSError, ValueError) as exc:
        print(f"pwutil: {exc}", file=sys.stderr)
        return 1
    print(f"password updated for {args.user}")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Generate, verify and update BRK $sha256$ password hashes."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("hash", help="generate a password hash")
    p.add_argument("password")
    p.add_argument("--salt")
    p.set_defaults(func=cmd_hash)

    p = sub.add_parser("verify", help="verify a password against a hash")
    p.add_argument("password")
    p.add_argument("hash")
    p.set_defaults(func=cmd_verify)

    p = sub.add_parser("parse", help="parse a password hash")
    p.add_argument("hash")
    p.set_defaults(func=cmd_parse)

    p = sub.add_parser("list", help="list users and their salts")
    p.add_argument("--shadow", default=DEFAULT_SHADOW)
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("set", help="update a user's password")
    p.add_argument("user")
    p.add_argument("password")
    p.add_argument("--salt")
    p.add_argument("--shadow", default=DEFAULT_SHADOW)
    p.set_defaults(func=cmd_set)

    p = sub.add_parser("passwd", help="prompt for and update a user's password")
    p.add_argument("user")
    p.add_argument("--shadow", default=DEFAULT_SHADOW)
    p.set_defaults(func=cmd_passwd)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
