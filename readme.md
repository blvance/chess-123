Fork or clone your this chess project into a new GitHub repository.

Add support for FEN stringsLinks to an external site. to your game setup so that instead of the current way you are setting up your game board you are setting it up with a call similar to the following call.

FENtoBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR");

Your routine should be able to take just the board position portion of a FEN string, or the entire FEN string like so:

FENtoBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

(you can ignore the end for now)

This will allow you to quickly check that your castling, promotion and en passant code is working.

## Recent Updates

- Fixed type declaration issues between `Chess.h` and `Bitboard.h`:
  - Removed duplicate `ChessPiece` enum from `Chess.h`.
  - `Chess` now uses the shared `ChessPiece`/`BitMove` types from `Bitboard.h`.
- Implemented and validated `Chess::generateAllMoves()` (with `generateLegalMoves()` kept as a compatibility alias).
  - Added safety guard for missing current player.
  - Corrected move index encoding to `rank * 8 + file` (`y * 8 + x`).
- Hooked move generation into game flow:
  - Runs after `Chess::setUpBoard()`.
  - Runs after each move in `Chess::bitMovedFromTo(...)`.
- Added stability fixes to reduce `SIGABRT`/out-of-range crashes:
  - Made `Game::getCurrentPlayer()` and `Game::getPlayerAt()` bounds-safe.
  - Added null checks before dereferencing current player in `Application.cpp`.
- Verified project builds successfully after these changes (`demo` target).

## Chess Move Implementation (Current)

### Pawn moves
- Forward movement is directional by player:
  - White uses `+1` in Y.
  - Black uses `-1` in Y.
- A pawn can move forward one square if the destination is empty.
- A pawn can move forward two squares only from starting rank:
  - White from rank `y = 1`
  - Black from rank `y = 6`
- The two-square move also requires the intermediate square to be empty.
- Pawn captures are diagonal only:
  - Must move one file left/right and one rank forward.
  - Destination must contain an opponent piece.

### Knight moves
- Knights move in an L-shape:
  - `(dx, dy) = (2, 1)` or `(1, 2)`.
- No path-blocking logic is required for knights.

### King moves
- King can move one square in any direction:
  - Horizontal, vertical, or diagonal.
- Zero-distance moves are rejected.

### Rook, bishop, and queen moves
- Integrated `MagicBitboards.h` sliding attack generation:
  - Rook attacks use `ratt(square, occupancy)`.
  - Bishop attacks use `batt(square, occupancy)`.
  - Queen attacks use rook+bishop union.
- Sliding pieces stop at blockers automatically and include capture squares.
- Friendly-occupied destination squares are filtered out.

### Shared legality checks
- Moves to the same square are rejected.
- Moves onto a square occupied by a friendly piece are rejected.
- Turn-based movement is enforced:
  - White and black alternate via `endTurn()`.
  - Only the active player's pieces can be moved.
- Captures are supported for all pieces:
  - Destination enemy piece is removed from the board when a legal capture is made.
  - Pawn captures remain diagonal-only.

### Not implemented yet
- Castling.
- En passant.
- Pawn promotion.
- Full check/checkmate legality filtering.

## Chess AI Update

- Added a negamax AI with alpha-beta pruning in `Chess`.
- Search depth: `3` plies (configurable via `kChessAIDepth` in `classes/Chess.cpp`).
- AI side: currently set to Black (`kChessAIPlayer = 1`), so you play White by default.
- Move generation in search uses the same piece rules as gameplay, including slider attacks via `MagicBitboards.h`.
- Evaluation uses material values plus a small center-control positional bonus.

### Challenges

- Avoiding UI-object mutation during search:
  - Search now uses a lightweight `BoardState` array instead of mutating `Bit`/`ChessSquare` objects.
- Keeping search and gameplay move rules consistent:
  - Added shared board-state move generation for the AI (`generateAllMovesForBoard`).
- Making captures safe when applying AI moves to the live board:
  - AI applies the chosen move through holder logic and then calls the existing turn-end path.

### Current Play Strength

- The AI responds quickly at depth 3 and makes legal captures and tactical material trades.
- It is not check-aware (no check/checkmate legality filtering yet), so strength is limited to pseudo-legal tactical play.
