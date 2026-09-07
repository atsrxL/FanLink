#!/usr/bin/env python3
"""Derive a reproducible 32-bit FanLink network ID from a phrase (FNV-1a)."""

from __future__ import annotations

import argparse


def fnv1a32(data: bytes) -> int:
    value = 0x811C9DC5
    for byte in data:
        value ^= byte
        value = (value * 0x01000193) & 0xFFFFFFFF
    return value


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("phrase", help="private project phrase; not encryption")
    args = parser.parse_args()
    value = fnv1a32(args.phrase.encode("utf-8"))
    print(f"0x{value:08X}UL")


if __name__ == "__main__":
    main()
