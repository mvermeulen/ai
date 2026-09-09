"""
Generic bitmask/backtracking solver for N x N sudoku variants.

Designed around the puzzle JSON shape used by puzzlemadness.co.uk's
"16x16 Giant Sudoku" (and its other grid-based sudoku variants), but it
only depends on three things, so it works for any "each region contains
every digit exactly once" puzzle:

  - size:          number of symbols / cells per row (e.g. 16)
  - starting_grid: flat list of length size*size, 0 == empty cell
  - regions:       list of cell-index lists; every region must contain
                    each digit 1..size exactly once in a solution

Rows, columns and the jigsaw/box layout are all just "regions" here, so
irregular (jigsaw) sudokus and extra regions (hyper-sudoku style bonus
boxes) work the same way as classic boxes.

Algorithm: bitmask candidate sets per cell + recursive backtracking with
the minimum-remaining-values (MRV) heuristic and constraint propagation
(naked singles are placed immediately, cascading). This is plenty fast
for human-graded puzzles (easy..tough) at 16x16; a medium puzzle with
100+ givens solves in a small fraction of a second.
"""

from __future__ import annotations

from dataclasses import dataclass, field


NUMBERS_MAPPING = ["0"] + [str(d) for d in range(1, 10)] + list("ABCDEFG")


def label(value: int) -> str:
    """Render a cell value the way puzzlemadness.co.uk does: 1-9 then A-G."""
    return NUMBERS_MAPPING[value]


def rows_regions(size: int) -> list[list[int]]:
    return [[r * size + c for c in range(size)] for r in range(size)]


def cols_regions(size: int) -> list[list[int]]:
    return [[r * size + c for r in range(size)] for c in range(size)]


def boxes_from_layout(layout: list[int]) -> list[list[int]]:
    """`layout[i]` gives the region id (0..size-1) that cell i belongs to."""
    boxes: dict[int, list[int]] = {}
    for cell, region_id in enumerate(layout):
        boxes.setdefault(region_id, []).append(cell)
    return [boxes[k] for k in sorted(boxes)]


def standard_regions(size: int, layout: list[int] | None,
                      extra_regions: list[list[int]] | None = None) -> list[list[int]]:
    """Build the full region list: rows + columns + boxes (+ any extras)."""
    regions = rows_regions(size) + cols_regions(size)
    if layout is not None:
        regions += boxes_from_layout(layout)
    if extra_regions:
        regions += extra_regions
    return regions


class Unsolvable(Exception):
    pass


@dataclass
class SolveResult:
    grid: list[int]
    guesses: int = 0  # number of backtracking choice points explored


def solve(size: int, starting_grid: list[int], regions: list[list[int]],
          limit: int = 2) -> list[SolveResult]:
    """Solve a sudoku-family puzzle.

    Returns up to `limit` solutions (default stops after finding 2, which
    is enough to prove/disprove uniqueness without wasting time enumerating
    every solution of a badly under-constrained grid).
    """
    n = size
    full_mask = (1 << n) - 1
    cell_count = n * n

    if len(starting_grid) != cell_count:
        raise ValueError(f"starting_grid must have {cell_count} cells, got {len(starting_grid)}")

    def bit(d: int) -> int:
        return 1 << (d - 1)

    # peers[cell] = set of all other cells sharing a region with it
    peers: list[set[int]] = [set() for _ in range(cell_count)]
    for region in regions:
        for a in region:
            peers[a].update(c for c in region if c != a)

    grid = [0] * cell_count
    candidates = [full_mask] * cell_count
    guesses = 0

    def place(cell: int, val: int) -> bool:
        """Assign val to cell and cascade any resulting naked singles.
        Returns False as soon as a contradiction (empty candidate set or
        a peer already holding the same value) is found."""
        queue = [(cell, val)]
        while queue:
            c, v = queue.pop()
            if grid[c] == v:
                continue
            if grid[c] != 0:
                return False  # cell already holds a different value
            m = bit(v)
            if not (candidates[c] & m):
                return False
            grid[c] = v
            candidates[c] = m
            for p in peers[c]:
                if grid[p] == v:
                    return False
                if candidates[p] & m:
                    candidates[p] &= ~m
                    if candidates[p] == 0:
                        return False
                    if grid[p] == 0 and candidates[p].bit_length() == 1 and \
                            bin(candidates[p]).count("1") == 1:
                        queue.append((p, candidates[p].bit_length()))
        return True

    # Seed candidates from the givens.
    for cell, val in enumerate(starting_grid):
        if val:
            if not place(cell, val):
                raise Unsolvable(f"given at cell {cell} conflicts with another given")

    results: list[SolveResult] = []

    def backtrack() -> bool:
        nonlocal guesses
        best_cell = -1
        best_count = n + 1
        for c in range(cell_count):
            if grid[c] == 0:
                cnt = bin(candidates[c]).count("1")
                if cnt == 0:
                    return False
                if cnt < best_count:
                    best_count = cnt
                    best_cell = c
                    if cnt == 1:
                        break

        if best_cell == -1:
            results.append(SolveResult(list(grid), guesses))
            return len(results) >= limit  # True => stop searching

        c = best_cell
        for d in range(1, n + 1):
            if candidates[c] & bit(d):
                guesses += 1
                saved_grid = list(grid)
                saved_candidates = list(candidates)
                if place(c, d) and backtrack():
                    return True
                grid[:] = saved_grid
                candidates[:] = saved_candidates
        return False

    backtrack()
    return results


def format_grid(size: int, grid: list[int]) -> str:
    box = int(round(size ** 0.5))
    lines = []
    for r in range(size):
        row_cells = []
        for c in range(size):
            row_cells.append(label(grid[r * size + c]))
            if box and (c + 1) % box == 0 and c + 1 != size:
                row_cells.append("|")
        line = " ".join(row_cells)
        lines.append(line)
        if box and (r + 1) % box == 0 and r + 1 != size:
            lines.append("-" * len(line))
    return "\n".join(lines)
