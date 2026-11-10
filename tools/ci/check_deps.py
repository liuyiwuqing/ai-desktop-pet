#!/usr/bin/env python3
"""Check that firmware components form an acyclic dependency graph."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPONENT_ROOTS = (ROOT / "firmware" / "components", ROOT / "extensions")


def parse_deps(cmake_file: Path, known: set[str]) -> set[str]:
    text = cmake_file.read_text(encoding="utf-8").replace("\n", " ")
    deps: set[str] = set()
    for match in re.finditer(r"\b(?:PRIV_REQUIRES|REQUIRES)\b([^)]*)", text):
        for token in match.group(1).split():
            token = token.strip('"')
            if token in known:
                deps.add(token)
    return deps


def find_cycle(graph: dict[str, set[str]]) -> list[str] | None:
    white, gray, black = 0, 1, 2
    color = {node: white for node in graph}
    stack: list[str] = []

    def visit(node: str) -> list[str] | None:
        color[node] = gray
        stack.append(node)
        for dep in sorted(graph[node]):
            if color[dep] == gray:
                return stack[stack.index(dep):] + [dep]
            if color[dep] == white:
                cycle = visit(dep)
                if cycle is not None:
                    return cycle
        stack.pop()
        color[node] = black
        return None

    for node in sorted(graph):
        if color[node] == white:
            cycle = visit(node)
            if cycle is not None:
                return cycle
    return None


def main() -> int:
    roots = [root for root in COMPONENT_ROOTS if root.is_dir()]
    if not roots:
        print(f"missing component directories: {COMPONENT_ROOTS}", file=sys.stderr)
        return 1

    dirs: dict[str, Path] = {}
    for root in roots:
        for path in sorted(root.iterdir()):
            if not path.is_dir() or not (path / "CMakeLists.txt").is_file():
                continue
            if path.name in dirs:
                print(
                    f"FAIL duplicate component name '{path.name}': "
                    f"{dirs[path.name]} and {path}",
                    file=sys.stderr,
                )
                return 1
            dirs[path.name] = path

    known = set(dirs)
    graph: dict[str, set[str]] = {}
    for name in sorted(known):
        graph[name] = parse_deps(dirs[name] / "CMakeLists.txt", known)

    for name, deps in graph.items():
        print(f"{name}: {', '.join(sorted(deps)) or '-'}")

    cycle = find_cycle(graph)
    if cycle is not None:
        print(f"FAIL dependency cycle: {' -> '.join(cycle)}", file=sys.stderr)
        return 1

    print(f"OK: {len(graph)} components, no dependency cycles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
