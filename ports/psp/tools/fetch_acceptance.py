#!/usr/bin/env python3
"""Fetch the checksum-pinned archived obby for compatibility work, not a fixture.

Source: https://github.com/OpenBLOX-Community/rbxl-maps/blob/master/2008%20-%20Obstakle%20course!!!.rbxl
The archive's date/name is provenance, not an independently verified authorship.
This map is not implicitly included in the release archive.
"""
import argparse
import hashlib
from pathlib import Path
from urllib.parse import quote
from urllib.request import urlopen

NAME = "2008 - Obstakle course!!!.rbxl"
SHA256 = "cf0ae1af8093aadca156bc75c5c859988cf9502982816ada4d9f65bc9032acd3"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, nargs="?", default=Path("places/Obstakle-course-2008.rbxl"))
    args = parser.parse_args()
    url = "https://media.githubusercontent.com/media/OpenBLOX-Community/rbxl-maps/master/" + quote(NAME)
    with urlopen(url, timeout=60) as response:
        data = response.read(64 * 1024 * 1024)
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError("Archived map checksum changed; do not silently substitute another revision")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"{args.output}: {len(data)} bytes; SHA256 {SHA256}")


if __name__ == "__main__":
    main()
