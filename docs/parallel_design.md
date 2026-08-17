# Parallel Design — Bouncing Balls

## Goal

The sequential simulation is `O(K·T)`: every one of the `T` steps touches all `K`
balls. That's already optimal for a single core, so the only way to go faster is
to spread the per-step work across processes. This doc records the two
decompositions I considered, why one wins, and where its limits are.

The whole problem hinges on one fact about the rules: **a collision is decided
entirely by how many balls land on a single cell.** It never depends on balls
elsewhere on the grid. So the real question for parallelizing isn't "how do I
split the balls" — it's "how do I make sure that whoever resolves a cell can see
every ball that landed on it."

## Approach 1 — split the balls

The obvious first idea: hand each process an equal slice of the balls. Everyone
moves their own balls each step, then we figure out collisions.

The problem shows up immediately at the collision step. A ball owned by process A
can land on the same cell as a ball owned by process B, and neither knows about
the other. To resolve directions, every process needs the global count at every
cell its balls touched — which in practice means all-to-all sharing of positions
every single step so each process can rebuild the full picture.

That kills the idea. The balls are split, but the collision resolution isn't: each
process still ends up reconstructing global information over all `K` balls, so the
per-step cost stays `O(K)` — no better than sequential — and now we've _added_ a
round of global communication on top every step. Load balance is perfect and it's
trivial to implement, but it doesn't actually make anything faster. Not worth
pursuing.

## Approach 2 — split the grid

The second idea flips the axis of the split: instead of dividing the balls, divide
the **grid**. Each process owns a contiguous band of the grid (a range of rows, or
columns) and is responsible for whatever balls are currently inside it.

This directly solves Approach 1's problem. Because a collision only involves balls
sharing a cell, and every cell belongs to exactly one process, **the process that
owns a cell already sees every ball that could collide there.** Collision
resolution becomes a purely local operation — no global knowledge required.

The only communication left is balls crossing a band boundary. Since a ball moves
one cell per step, it can only ever cross into an immediately neighbouring band, so
the sharing is nearest-neighbour: each step a process just hands its outgoing balls
to (at most) its two neighbours and receives their incoming ones. The grid wraps
around, so the bands form a ring — the first and last processes are neighbours too.
Once the hand-off is done, everyone resolves their own cells and the step is
finished.

Balls carry their original index with them as they migrate, so however they get
shuffled around, the final output can always be reassembled back into input order.

Compared to Approach 1 this is better on every axis that matters: the per-step work
actually drops to roughly `K/P` per process, the communication is a small
nearest-neighbour exchange instead of an all-to-all, and each process only holds
its own share of the balls. The cost is that it's more involved to get right —
migrations, the wrap-around ring, and reassembling output order all need care.

## Decision

**Going with Approach 2, splitting along whichever dimension is larger** — rows if
the grid is taller, columns if it's wider. A larger dimension gives more slices to
hand out and keeps each band thinner, which spreads the work better.

For how to size the bands, there's a choice between giving every process an equal
number of rows/columns, or sizing bands by ball density so each process starts with
a similar number of balls. Density-aware splitting is the more sophisticated option
and it clearly helps when balls are unevenly clustered — but it only balances the
_initial_ layout (balls drift as the simulation runs), it can't beat the density of
a single over-packed line anyway, and it adds real complexity to something we want
to get correct first.

So the call is **equal-sized bands** for now. The benchmark inputs will be
uniformly spread, where an equal split is already well balanced, and it keeps the
first working version simple. Density-weighting is a localized change we can revisit
later if a skewed workload actually shows an imbalance worth fixing — the rest of
the design doesn't depend on it.

## What we get, and where it breaks down

Roughly, the per-step work goes from `O(K)` to `O(K/P)` when balls are reasonably
spread, with only a light nearest-neighbour exchange between steps, and memory per
process drops to its own share. Running on a single process should reproduce the
sequential result exactly, which doubles as a correctness check.

The honest limitations:

- **Load balance follows the balls, not the split.** If most balls pile into a few
  bands, those processes become the bottleneck and speedup suffers. Equal-sized
  bands assume a reasonably even spread.
- **A single dimension only slices so far.** The design assumes there are at least
  as many rows/columns as processes so that a one-step move can never jump past a
  neighbour. Extremely thin grids would need a different split.
- **The problem is bounded work.** Total work is fixed, so beyond some process
  count the communication and the unavoidable serial parts (reading input,
  gathering and ordering the final output) stop the speedup — the classic Amdahl
  ceiling. On a single machine that ceiling also lands around the physical core
  count, past which oversubscription hurts rather than helps.

None of these are blockers for what we're building; they're the things to point at
when the scaling curve inevitably flattens, and explaining _why_ it flattens is
part of the point of the exercise.

## Approaches not taken

Worth naming for completeness. A **two-dimensional block split** (dividing both rows
and columns) reduces the boundary-to-area ratio and would scale better on very large,
evenly-populated grids, at the cost of more neighbours and more bookkeeping —
over-engineering for this problem. A **hash-based ownership** scheme, where cells are
assigned to processes by hash rather than by contiguous band, is the one thing that
would rescue the pathological "all balls on one line" case, because it scatters
clusters across processes — but it trades away the cheap nearest-neighbour
communication for all-to-all. Both are reasonable answers to _different_ problems
than the one we have.
