# Round 6: the door maze

Round 6 is RoomId 5, one 16-page level with 56 door entries. Decoded with `decode_doors.py` (see its header
for the table format) and checked against the user's lettered map of the level.

- 55 doors warp inside the level: 27 two-way pairs (54 doors) and one one-way door. The 56th, door #54, is
  the boss door to the arena (RoomId 13, Chameleon Man, boss slot 1).
- Every warp arrives on a door tile: you always appear standing on the partner door.
- The round's exit is not in the door table (it is the tile-type-7 door, which opens once `BossesLeft` is 0).
- Door numbers are the order in the table (row, then page, then x, as the game stores them). Letters are the
  labels on the user's map; V is used on it for two different pairs, so the second is V'.

| Door Label | Doors | Where (row, page, tile) |
|---|-----------|----------------------|
| A | #2 <-> #9 | (10,0,2) <-> (5,3,0) |
| B | #3 <-> #6 | (10,0,5) <-> (10,1,2) |
| C | #0 <-> #4 | (5,0,2) <-> (10,0,15) |
| D | #1 <-> #7 | (5,0,5) <-> (10,2,1) |
| E | #5 <-> #10 | (5,1,13) <-> (10,3,0) |
| F | #8 <-> #15 | (10,2,13) <-> (5,5,13) |
| G | #11 <-> #14 | (10,3,8) <-> (5,5,4) |
| H | #12 <-> #13 | (5,4,11) <-> (10,4,13) |
| I | #16 <-> #27 | (5,6,11) <-> (10,8,5) |
| J | #17 <-> #18 | (10,6,15) <-> (3,7,2) |
| K | #20 <-> #25 | (7,7,2) <-> (5,8,14) |
| L | #22 <-> #31 | (10,7,2) <-> (5,9,5) |
| M | #19 <-> #24 | (3,7,15) <-> (5,8,2) |
| N | #21 <-> #26 | (7,7,15) <-> (10,8,2) |
| O | #23 <-> #28 | (10,7,15) <-> (10,8,8) |
| P | #29 <-> #53 | (10,8,14) <-> (3,15,0) |
| Q | #32 <-> #33 | (5,9,12) <-> (10,9,5) |
| R | #35 <-> #39 | (5,10,0) <-> (10,11,0) |
| S | #36 <-> #40 | (5,10,13) <-> (10,11,13) |
| T | #30 <-> #41 | (3,9,8) <-> (10,12,15) |
| U | #34 <-> #45 | (10,9,8) <-> (10,13,2) |
| V | #37 <-> #49 | (10,10,0) <-> (5,14,13) |
| W | #38 <-> #42 | (10,10,13) <-> (5,13,2) |
| X | #43 <-> #50 | (5,13,11) <-> (10,14,1) |
| Y | #44 <-> #46 | (5,13,14) <-> (10,13,14) |
| Z | #48 <-> #51 | (5,14,5) <-> (10,14,5) |
| a | #47 <-> #55 | (5,14,1) <-> (10,15,0) |
| one-way | #52 -> #3 | (10,14,13) -> (10,0,5) |

The round starts at row 10, x tile 8 on page 0 (not a door).

## US vs the Japanese prototype

The maze is the same. All 56 door entries match field for field: positions, destinations, which doors are
boss doors, and where each warp arrives. Both versions read the tables with the same code
(`TryEnterDoor`); the only change is that the prototype adds 8 to the arrival Y where the US adds 4, which
doesn't change the tile row. The differences are in where you stand:

- Boss door #54: you arrive in the arena on tile 6 (US) or tile 0 (prototype).
- Round 6's start position: x $88 (tile 8) in the US, $A8 (tile 10) in the prototype. The prototype's
  start Y values are all 4 higher, to go with the larger offset above.
- The arena's exit doors (room 13, four of them) are the same in both: they send you back to where you came from.
