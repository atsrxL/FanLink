#!/usr/bin/env python3
"""Deterministic accounting model for the 16-node poll loop."""

from __future__ import annotations

import argparse
from dataclasses import dataclass


@dataclass
class Node:
    polls: int = 0
    replies: int = 0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--slot-us", type=int, default=10_000)
    parser.add_argument(
        "--loss", type=float, default=0.0, help="deterministic illustrative loss ratio, 0..1"
    )
    args = parser.parse_args()
    if args.seconds <= 0 or args.slot_us <= 0 or not 0.0 <= args.loss <= 1.0:
        raise SystemExit("invalid arguments")

    slots = int(args.seconds * 1_000_000 / args.slot_us)
    nodes = [Node() for _ in range(16)]
    loss_accumulators = [0.0] * 16
    for slot in range(slots):
        node_index = slot % 16
        node = nodes[node_index]
        node.polls += 1
        loss_accumulators[node_index] += args.loss
        lost = loss_accumulators[node_index] >= 1.0
        if lost:
            loss_accumulators[node_index] -= 1.0
        if not lost:
            node.replies += 1

    print(f"slots={slots} slot_rate_hz={1_000_000 / args.slot_us:.3f}")
    print(f"full_sweep_ms={16 * args.slot_us / 1000:.3f}")
    for index, node in enumerate(nodes, 1):
        print(f"node={index:02d} polls={node.polls} replies={node.replies}")


if __name__ == "__main__":
    main()
