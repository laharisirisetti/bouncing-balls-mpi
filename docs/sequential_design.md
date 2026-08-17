# Bouncing Balls — Sequential Approach

## Key idea

A collision's outcome depends only on **how many** balls land on a cell, not on
which balls they are. So each step we simply count the occupants of every cell,
then update each ball's direction from that count. This avoids tracking pairs of
balls and keeps every step linear in the number of balls.

## Algorithm

For each of the `T` time steps:

1. **Move** every ball one cell in its direction (the grid wraps around at the
   edges), recording how many balls now occupy each cell.
2. **Resolve** each ball's new direction from its cell's occupant count:
   - 1 or 3 balls → unchanged
   - 2 balls → turn 90° right
   - 4 balls → reverse

After `T` steps, output every ball's final position and direction in input order.

## Complexity

- Time: `O(K · T)` — optimal, since each of the `T` steps must move all `K` balls.
- Space: `O(K)` — at most `K` cells are occupied at any moment.

## Why this approach

The count-only rule is what makes it simple and fast: there's no need to detect
or pair up colliding balls. Alternatives (e.g. sorting balls by position each
step) add a `log K` factor for no benefit.
