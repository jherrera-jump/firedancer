#!/usr/bin/env python3
"""Validate firedancer link manifests for version/namespace consistency.

Loads all manifest.toml files and checks for:
  - Namespace collisions (duplicate [[produces]] link names)
  - Consumer satisfaction (every [[consumes]] has a matching [[produces]])
  - Version compatibility (semver major must match, consumer minor <= producer minor)
  - MTU consistency (non-zero consumer mtu must match producer mtu)

Usage: python3 validate_manifests.py manifest1.toml [manifest2.toml ...]
"""

import re
import sys
import os


def parse_manifest(path):
    """Parse a manifest.toml into a dict with 'name', 'produces', 'consumes' lists.

    Minimal TOML parser that handles [plugin], [[produces]], and [[consumes]]
    sections with string and integer key-value pairs.  This avoids depending
    on tomllib (Python 3.11+) or any third-party package."""

    result = {"name": None, "produces": [], "consumes": [], "path": path}

    with open(path) as f:
        text = f.read()

    # Extract plugin name from [plugin] section
    m = re.search(
        r"^\[plugin\]\s*\n(?:\s*#[^\n]*\n)*\s*name\s*=\s*\"([^\"]+)\"",
        text,
        re.MULTILINE,
    )
    if m:
        result["name"] = m.group(1)

    # Split on [[ headers to find produces/consumes blocks.
    # re.split with a capturing group keeps the delimiter text in the list.
    blocks = re.split(r"\[\[(produces|consumes)\]\]", text)

    # blocks layout: [preamble, type1, content1, type2, content2, ...]
    i = 1
    while i < len(blocks) - 1:
        block_type = blocks[i]
        content = blocks[i + 1]
        entry = {}
        for line in content.strip().split("\n"):
            line = line.split("#")[0].strip()  # strip comments
            if not line:
                continue
            # String value
            sm = re.match(r"(\w+)\s*=\s*\"([^\"]*)\"", line)
            if sm:
                entry[sm.group(1)] = sm.group(2)
                continue
            # Integer value
            im = re.match(r"(\w+)\s*=\s*(\d+)", line)
            if im:
                entry[im.group(1)] = int(im.group(2))
                continue
        if "link" in entry:
            result[block_type].append(entry)
        i += 2

    return result


def parse_version(v):
    """Parse 'major.minor.patch' string into a (major, minor, patch) tuple.

    Returns None if the string is malformed."""
    parts = v.split(".")
    if len(parts) < 2:
        return None
    try:
        major = int(parts[0])
        minor = int(parts[1])
        patch = int(parts[2]) if len(parts) > 2 else 0
        return (major, minor, patch)
    except ValueError:
        return None


def validate(manifests):
    """Run all validation checks across the parsed manifests.

    Returns a list of error strings (empty means success)."""
    errors = []
    producers = {}  # link_name -> (manifest_path, entry)

    # ---- Namespace collisions ----
    for m in manifests:
        for p in m["produces"]:
            link = p["link"]
            if link in producers:
                errors.append(
                    f'ERROR: link "{link}" produced by both '
                    f'"{producers[link][0]}" and "{m["path"]}"'
                )
            else:
                producers[link] = (m["path"], p)

    # ---- Consumer satisfaction, version compatibility, MTU consistency ----
    for m in manifests:
        source = m["name"] or m["path"]
        for c in m["consumes"]:
            link = c["link"]

            # Consumer satisfaction — missing producer is fatal for
            # core manifests but only a warning for plugins (a plugin
            # may consume links that only exist in one binary variant).
            if link not in producers:
                if m["name"] is not None:
                    print(
                        f'WARNING: plugin "{source}" consumes "{link}" '
                        f"but no manifest produces it",
                        file=sys.stderr,
                    )
                else:
                    errors.append(
                        f'ERROR: "{source}" consumes "{link}" '
                        f"but no manifest produces it"
                    )
                continue

            _, prod = producers[link]

            # Version compatibility
            if "version" in c and "version" in prod:
                cv = parse_version(c["version"])
                pv = parse_version(prod["version"])
                if cv and pv:
                    if cv[0] != pv[0]:
                        errors.append(
                            f'ERROR: plugin "{source}" consumes "{link}" '
                            f'version "{c["version"]}" but producer declares '
                            f'version "{prod["version"]}" (major mismatch)'
                        )
                    elif cv[1] > pv[1]:
                        errors.append(
                            f'ERROR: plugin "{source}" consumes "{link}" '
                            f'version "{c["version"]}" but producer declares '
                            f'version "{prod["version"]}" '
                            f"(consumer minor > producer minor)"
                        )

            # MTU consistency
            c_mtu = c.get("mtu", 0)
            p_mtu = prod.get("mtu", 0)
            if c_mtu and p_mtu and c_mtu != p_mtu:
                errors.append(
                    f'ERROR: plugin "{source}" consumes "{link}" '
                    f"with mtu {c_mtu} but producer declares mtu {p_mtu}"
                )

    return errors


def main():
    if len(sys.argv) < 2:
        print("Usage: validate_manifests.py manifest1.toml [manifest2.toml ...]")
        sys.exit(0)

    manifests = [parse_manifest(p) for p in sys.argv[1:]]
    errors = validate(manifests)

    if errors:
        for e in errors:
            print(e, file=sys.stderr)
        sys.exit(1)

    print(f"Validated {len(manifests)} manifest(s): OK")


if __name__ == "__main__":
    main()