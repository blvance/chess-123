#include "Chess.h"
#include "MagicBitboards.h"
#include <limits>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <chrono>

namespace {
constexpr int kChessAIDepth = 5;
constexpr int kChessAIPlayer = 1; // 0=white, 1=black
constexpr int kNegInf = -1000000000;
constexpr int kPosInf = 1000000000;
}

Chess::Chess()
{
    _grid = new Grid(8, 8);
}

Chess::~Chess()
{
    delete _grid;
}

char Chess::pieceNotation(int x, int y) const
{
    const char *wpieces = { "0PNBRQK" };
    const char *bpieces = { "0pnbrqk" };
    Bit *bit = _grid->getSquare(x, y)->bit();
    char notation = '0';
    if (bit) {
        notation = bit->gameTag() < 128 ? wpieces[bit->gameTag()] : bpieces[bit->gameTag()-128];
    }
    return notation;
}

Bit* Chess::PieceForPlayer(const int playerNumber, ChessPiece piece)
{
    const char* pieces[] = { "pawn.png", "knight.png", "bishop.png", "rook.png", "queen.png", "king.png" };

    Bit* bit = new Bit();
    // should possibly be cached from player class?
    const char* pieceName = pieces[piece - 1];
    std::string spritePath = std::string("") + (playerNumber == 0 ? "w_" : "b_") + pieceName;
    bit->LoadTextureFromFile(spritePath.c_str());
    bit->setOwner(getPlayerAt(playerNumber));
    bit->setSize(pieceSize, pieceSize);

    return bit;
}

void Chess::setUpBoard()
{
    setNumberOfPlayers(2);
    _gameOptions.rowX = 8;
    _gameOptions.rowY = 8;

    _grid->initializeChessSquares(pieceSize, "boardsquare.png");
    FENtoBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    startGame();

    if (gameHasAI()) {
        setAIPlayer(kChessAIPlayer);
    }
    
    // Re-apply turn from the FEN string after startGame() resets currentTurnNo
    syncTurnFromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    // Run move generation for the initial position.
    generateAllMoves();
}

Chess::BoardState Chess::boardStateFromGrid() const
{
    BoardState board{};
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            ChessSquare* sq = _grid->getSquare(x, y);
            Bit* bit = sq ? sq->bit() : nullptr;
            board[y * 8 + x] = bit ? bit->gameTag() : 0;
        }
    }
    return board;
}
void Chess::FENtoBoard(const std::string& fen)
{
    // 1. Split the FEN string to get only the piece placement (the first part)
    // Example: "rnb... w KQkq" -> "rnb..."
    std::string boardPart = fen.substr(0, fen.find(' '));

    // 2. Clear the board completely
    _grid->forEachSquare([](ChessSquare* square, int x, int y) {
        square->destroyBit(); 
    });

    int file = 0;
    int rank = 7; // Start at Rank 8 (index 7)

    for (char c : boardPart) {
        if (c == '/') {
            rank--;
            file = 0;
        } else if (std::isdigit(c)) {
            file += (c - '0');
        } else {
            // Determine Piece Type and Player
            int playerNumber = std::isupper(c) ? 0 : 1;
            ChessPiece piece = NoPiece;
            char type = std::tolower(c);
            
            if (type == 'p') piece = Pawn;
            else if (type == 'n') piece = Knight;
            else if (type == 'b') piece = Bishop;
            else if (type == 'r') piece = Rook;
            else if (type == 'q') piece = Queen;
            else if (type == 'k') piece = King;

            if (piece != NoPiece) {
                ChessSquare* square = _grid->getSquare(file, rank);
                Bit* bit = PieceForPlayer(playerNumber, piece);
                
                // Tagging logic: White 1-6, Black 129-134
                bit->setGameTag(playerNumber == 0 ? (int)piece : (int)piece + 128);
                bit->setPosition(square->getPosition());
                square->setBit(bit);
            }
            file++;
        }
    }
}

void Chess::syncTurnFromFEN(const std::string& fen)
{
    // Extract the active player from FEN and sync the turn
    size_t firstSpace = fen.find(' ');
    if (firstSpace != std::string::npos && firstSpace + 1 < fen.length()) {
        char turn = fen[firstSpace + 1];
        int activePlayer = (turn == 'w') ? 0 : 1;
        // Sync turn with the FEN's active player
        // currentTurnNo & 1 determines whose turn it is (0 for even, 1 for odd)
        while ((getCurrentPlayer()->playerNumber()) != activePlayer) {
            endTurn();
        }
    }
}

std::string Chess::boardToFEN() const
{
    std::string fen;

    for (int rank = 7; rank >= 0; rank--)  // 8 → 1
    {
        int emptyCount = 0;

        for (int file = 0; file < 8; file++)
        {
            char piece = pieceNotation(file, rank);

            if (piece == '0')
            {
                emptyCount++;
            }
            else
            {
                if (emptyCount > 0)
                {
                    fen += std::to_string(emptyCount);
                    emptyCount = 0;
                }

                fen += piece;
            }
        }

        if (emptyCount > 0)
            fen += std::to_string(emptyCount);

        if (rank > 0)
            fen += '/';
    }

    return fen;
}

bool Chess::actionForEmptyHolder(BitHolder &holder)
{
    return false;
}

bool Chess::canBitMoveFrom(Bit &bit, BitHolder &src)
{
    // need to implement friendly/unfriendly in bit so for now this hack
    int currentPlayer = getCurrentPlayer()->playerNumber() * 128;
    int pieceColor = bit.gameTag() & 128;
    if (pieceColor == currentPlayer) return true;
    return false;
}

bool Chess::canBitMoveFromTo(Bit &bit, BitHolder &src, BitHolder &dst)
{
    ChessSquare* srcSquare = dynamic_cast<ChessSquare*>(&src);
    ChessSquare* dstSquare = dynamic_cast<ChessSquare*>(&dst);
    
    if (!srcSquare || !dstSquare) return false;
    
    int fromX = srcSquare->getColumn();
    int fromY = srcSquare->getRow();
    int toX = dstSquare->getColumn();
    int toY = dstSquare->getRow();
    
    // Can't move to the same square
    if (fromX == toX && fromY == toY) return false;
    
    // Can't move to a square occupied by your own piece
    if (isOccupiedByFriend(toX, toY, getCurrentPlayer()->playerNumber())) return false;
    
    // Get the piece type from the bit's game tag
    ChessPiece piece = static_cast<ChessPiece>(bit.gameTag() & 0x7F);
    int playerNumber = getCurrentPlayer()->playerNumber();
    
    // Validate the move shape first.
    if (!isValidMove(fromX, fromY, toX, toY, piece, playerNumber)) {
        return false;
    }

    // Paranoid-king legality: disallow moves that leave own king in check.
    BoardState board = boardStateFromGrid();
    BitMove move(fromY * 8 + fromX, toY * 8 + toX, piece);
    return isMoveLegalOnBoard(board, move, playerNumber);
}

bool Chess::isValidMove(int fromX, int fromY, int toX, int toY, ChessPiece piece, int playerNumber)
{
    if (!_grid->isValid(fromX, fromY) || !_grid->isValid(toX, toY)) {
        return false;
    }

    const int fromSq = fromY * 8 + fromX;
    const int toSq = toY * 8 + toX;

    switch (piece) {
        case Pawn:
            // Check for normal move or capture
            return canPawnMove(fromX, fromY, toX, toY, playerNumber) ||
                   canPawnCapture(fromX, fromY, toX, toY, playerNumber);
        case Knight:
            return canKnightMove(fromX, fromY, toX, toY);
        case King:
            return canKingMove(fromX, fromY, toX, toY);
        case Rook: {
            uint64_t attacks = ratt(fromSq, boardOccupancy());
            return (attacks & (1ULL << toSq)) != 0ULL;
        }
        case Bishop: {
            uint64_t attacks = batt(fromSq, boardOccupancy());
            return (attacks & (1ULL << toSq)) != 0ULL;
        }
        case Queen: {
            uint64_t attacks = ratt(fromSq, boardOccupancy()) | batt(fromSq, boardOccupancy());
            return (attacks & (1ULL << toSq)) != 0ULL;
        }
        default:
            return false;
    }
}

bool Chess::canPawnMove(int fromX, int fromY, int toX, int toY, int playerNumber)
{
    // Pawns can only move forward (no capture)
    int direction = (playerNumber == 0) ? 1 : -1;  // White moves up (+1), Black moves down (-1)
    
    // Can't move horizontally
    if (fromX != toX) return false;
    
    // Can't move backward
    if ((toY - fromY) * direction <= 0) return false;
    
    // Destination must be empty
    if (_grid->getSquare(toX, toY)->bit() != nullptr) return false;
    
    // One square forward
    if (toY == fromY + direction) {
        return true;
    }
    
    // Two squares forward from starting position
    bool isStartingRank = (playerNumber == 0 && fromY == 1) || (playerNumber == 1 && fromY == 6);
    if (isStartingRank && toY == fromY + 2 * direction) {
        // Check if the square in between is empty
        int middleY = fromY + direction;
        if (_grid->getSquare(fromX, middleY)->bit() == nullptr) {
            return true;
        }
    }
    
    return false;
}

bool Chess::canPawnCapture(int fromX, int fromY, int toX, int toY, int playerNumber)
{
    // Pawns capture diagonally
    int direction = (playerNumber == 0) ? 1 : -1;  // White moves up (+1), Black moves down (-1)
    
    // Must move diagonally forward
    if (abs(fromX - toX) != 1) return false;
    if (toY != fromY + direction) return false;
    
    // Destination must have an opponent's piece
    return isOccupiedByOpponent(toX, toY, playerNumber);
}

bool Chess::canKnightMove(int fromX, int fromY, int toX, int toY)
{
    int dx = abs(toX - fromX);
    int dy = abs(toY - fromY);
    
    // Knight moves in an L-shape: 2 squares in one direction, 1 in the other
    return (dx == 2 && dy == 1) || (dx == 1 && dy == 2);
}

bool Chess::canKingMove(int fromX, int fromY, int toX, int toY)
{
    int dx = abs(toX - fromX);
    int dy = abs(toY - fromY);
    
    // King can move one square in any direction
    return (dx <= 1 && dy <= 1) && (dx > 0 || dy > 0);
}

bool Chess::isPathClear(int fromX, int fromY, int toX, int toY)
{
    // Helper function for future sliding pieces (Rook, Bishop, Queen)
    int dx = (toX > fromX) ? 1 : (toX < fromX) ? -1 : 0;
    int dy = (toY > fromY) ? 1 : (toY < fromY) ? -1 : 0;
    
    int x = fromX + dx;
    int y = fromY + dy;
    
    while (x != toX || y != toY) {
        if (_grid->getSquare(x, y)->bit() != nullptr) {
            return false;
        }
        x += dx;
        y += dy;
    }
    
    return true;
}

bool Chess::isOccupiedByOpponent(int x, int y, int playerNumber)
{
    if (!_grid->isValid(x, y)) return false;
    
    ChessSquare* square = _grid->getSquare(x, y);
    if (!square || !square->bit()) return false;
    
    Bit* bit = square->bit();
    int pieceColor = bit->gameTag() & 128;
    int currentColor = playerNumber * 128;
    
    return pieceColor != currentColor;  // Different color means opponent
}

bool Chess::isOccupiedByFriend(int x, int y, int playerNumber)
{
    if (!_grid->isValid(x, y)) return false;
    
    ChessSquare* square = _grid->getSquare(x, y);
    if (!square || !square->bit()) return false;
    
    Bit* bit = square->bit();
    int pieceColor = bit->gameTag() & 128;
    int currentColor = playerNumber * 128;
    
    return pieceColor == currentColor;  // Same color means friendly
}

uint64_t Chess::boardOccupancy() const
{
    uint64_t occ = 0ULL;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            ChessSquare* square = _grid->getSquare(x, y);
            if (square && square->bit()) {
                occ |= (1ULL << (y * 8 + x));
            }
        }
    }
    return occ;
}

uint64_t Chess::boardOccupancy(const BoardState& board) const
{
    uint64_t occ = 0ULL;
    for (int sq = 0; sq < 64; sq++) {
        if (board[sq] != 0) {
            occ |= (1ULL << sq);
        }
    }
    return occ;
}

std::vector<BitMove> Chess::generateAllMovesForBoard(const BoardState& board, int playerNumber) const
{
    std::vector<BitMove> moves;
    const int currentColor = playerNumber * 128;
    const uint64_t occ = boardOccupancy(board);
    uint64_t friendlyOcc = 0ULL;

    for (int sq = 0; sq < 64; sq++) {
        if (board[sq] != 0 && (board[sq] & 128) == currentColor) {
            friendlyOcc |= (1ULL << sq);
        }
    }

    for (int fromSq = 0; fromSq < 64; fromSq++) {
        const int tag = board[fromSq];
        if (tag == 0 || (tag & 128) != currentColor) {
            continue;
        }

        ChessPiece piece = static_cast<ChessPiece>(tag & 0x7F);
        int fromX = fromSq % 8;
        int fromY = fromSq / 8;
        uint64_t targets = 0ULL;

        switch (piece) {
            case Pawn: {
                const int direction = (playerNumber == 0) ? 1 : -1;
                const int startRank = (playerNumber == 0) ? 1 : 6;
                int oneStepY = fromY + direction;

                if (oneStepY >= 0 && oneStepY < 8) {
                    int oneStepSq = oneStepY * 8 + fromX;
                    if (board[oneStepSq] == 0) {
                        targets |= (1ULL << oneStepSq);

                        int twoStepY = fromY + 2 * direction;
                        if (fromY == startRank && twoStepY >= 0 && twoStepY < 8) {
                            int twoStepSq = twoStepY * 8 + fromX;
                            if (board[twoStepSq] == 0) {
                                targets |= (1ULL << twoStepSq);
                            }
                        }
                    }
                }

                int captureY = fromY + direction;
                if (captureY >= 0 && captureY < 8) {
                    int leftX = fromX - 1;
                    int rightX = fromX + 1;
                    if (leftX >= 0) {
                        int leftSq = captureY * 8 + leftX;
                        if (board[leftSq] != 0 && (board[leftSq] & 128) != currentColor) {
                            targets |= (1ULL << leftSq);
                        }
                    }
                    if (rightX < 8) {
                        int rightSq = captureY * 8 + rightX;
                        if (board[rightSq] != 0 && (board[rightSq] & 128) != currentColor) {
                            targets |= (1ULL << rightSq);
                        }
                    }
                }
                break;
            }
            case Knight: {
                static const int offsets[8][2] = {
                    {1, 2}, {2, 1}, {2, -1}, {1, -2},
                    {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
                };
                for (const auto &o : offsets) {
                    int toX = fromX + o[0];
                    int toY = fromY + o[1];
                    if (toX >= 0 && toX < 8 && toY >= 0 && toY < 8) {
                        targets |= (1ULL << (toY * 8 + toX));
                    }
                }
                break;
            }
            case Bishop:
                targets = batt(fromSq, occ);
                break;
            case Rook:
                targets = ratt(fromSq, occ);
                break;
            case Queen:
                targets = batt(fromSq, occ) | ratt(fromSq, occ);
                break;
            case King: {
                static const int offsets[8][2] = {
                    {1, 1}, {1, 0}, {1, -1}, {0, -1},
                    {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}
                };
                for (const auto &o : offsets) {
                    int toX = fromX + o[0];
                    int toY = fromY + o[1];
                    if (toX >= 0 && toX < 8 && toY >= 0 && toY < 8) {
                        targets |= (1ULL << (toY * 8 + toX));
                    }
                }
                break;
            }
            default:
                break;
        }

        targets &= ~friendlyOcc;
        while (targets) {
            int toSq = getFirstBit(targets);
            targets &= (targets - 1);
            BitMove candidate(fromSq, toSq, piece);

            BoardState mutableBoard = board;
            if (isMoveLegalOnBoard(mutableBoard, candidate, playerNumber)) {
                moves.push_back(candidate);
            }
        }
    }

    return moves;
}

int Chess::findKingSquare(const BoardState& board, int playerNumber) const
{
    const int kingTag = (playerNumber == 0) ? King : (King + 128);
    for (int sq = 0; sq < 64; sq++) {
        if (board[sq] == kingTag) {
            return sq;
        }
    }
    return -1;
}

bool Chess::isSquareAttacked(const BoardState& board, int square, int attackerPlayerNumber) const
{
    if (square < 0 || square >= 64) {
        return false;
    }

    const int attackerColor = attackerPlayerNumber * 128;
    const uint64_t occ = boardOccupancy(board);
    const int sqX = square % 8;
    const int sqY = square / 8;

    // Pawn attacks into target square.
    if (attackerPlayerNumber == 0) { // white pawns move north (+y), attack from south diagonals
        int fromY = sqY - 1;
        if (fromY >= 0) {
            int fromX1 = sqX - 1;
            int fromX2 = sqX + 1;
            if (fromX1 >= 0) {
                int fromSq = fromY * 8 + fromX1;
                if (board[fromSq] == Pawn) return true;
            }
            if (fromX2 < 8) {
                int fromSq = fromY * 8 + fromX2;
                if (board[fromSq] == Pawn) return true;
            }
        }
    } else { // black pawns move south (-y), attack from north diagonals
        int fromY = sqY + 1;
        if (fromY < 8) {
            int fromX1 = sqX - 1;
            int fromX2 = sqX + 1;
            if (fromX1 >= 0) {
                int fromSq = fromY * 8 + fromX1;
                if (board[fromSq] == (Pawn + 128)) return true;
            }
            if (fromX2 < 8) {
                int fromSq = fromY * 8 + fromX2;
                if (board[fromSq] == (Pawn + 128)) return true;
            }
        }
    }

    // Knight attacks.
    uint64_t knightAttackers = KnightAttacks[square];
    while (knightAttackers) {
        int fromSq = getFirstBit(knightAttackers);
        knightAttackers &= (knightAttackers - 1);
        int tag = board[fromSq];
        if (tag != 0 && (tag & 128) == attackerColor && (tag & 0x7F) == Knight) {
            return true;
        }
    }

    // King attacks (adjacent).
    uint64_t kingAttackers = KingAttacks[square];
    while (kingAttackers) {
        int fromSq = getFirstBit(kingAttackers);
        kingAttackers &= (kingAttackers - 1);
        int tag = board[fromSq];
        if (tag != 0 && (tag & 128) == attackerColor && (tag & 0x7F) == King) {
            return true;
        }
    }

    // Slider attacks on rook and bishop rays.
    uint64_t rookRays = ratt(square, occ);
    while (rookRays) {
        int fromSq = getFirstBit(rookRays);
        rookRays &= (rookRays - 1);
        int tag = board[fromSq];
        if (tag != 0 && (tag & 128) == attackerColor) {
            ChessPiece p = static_cast<ChessPiece>(tag & 0x7F);
            if (p == Rook || p == Queen) {
                return true;
            }
        }
    }

    uint64_t bishopRays = batt(square, occ);
    while (bishopRays) {
        int fromSq = getFirstBit(bishopRays);
        bishopRays &= (bishopRays - 1);
        int tag = board[fromSq];
        if (tag != 0 && (tag & 128) == attackerColor) {
            ChessPiece p = static_cast<ChessPiece>(tag & 0x7F);
            if (p == Bishop || p == Queen) {
                return true;
            }
        }
    }

    return false;
}

bool Chess::isKingInCheck(const BoardState& board, int playerNumber) const
{
    int kingSq = findKingSquare(board, playerNumber);
    if (kingSq < 0) {
        return true;
    }
    return isSquareAttacked(board, kingSq, 1 - playerNumber);
}

bool Chess::isMoveLegalOnBoard(BoardState& board, const BitMove& move, int playerNumber) const
{
    int captured = 0;
    applyMove(board, move, captured);
    bool legal = !isKingInCheck(board, playerNumber);
    undoMove(board, move, captured);
    return legal;
}

int Chess::evaluateBoard(const BoardState& board, int aiPlayerNumber) const
{
    static const int pieceValues[] = {
        0,      // NoPiece
        100,    // Pawn
        320,    // Knight
        330,    // Bishop
        500,    // Rook
        900,    // Queen
        20000   // King
    };

    int score = 0;
    const int aiColor = aiPlayerNumber * 128;

    for (int sq = 0; sq < 64; sq++) {
        int tag = board[sq];
        if (tag == 0) {
            continue;
        }

        ChessPiece piece = static_cast<ChessPiece>(tag & 0x7F);
        int colorSign = ((tag & 128) == aiColor) ? 1 : -1;
        int value = pieceValues[piece];

        int x = sq % 8;
        int y = sq / 8;
        int centerX = 3 - std::abs(x - 3);
        int centerY = 3 - std::abs(y - 3);
        int centerBonus = std::max(0, centerX + centerY);

        int positional = 0;
        switch (piece) {
            case Pawn: positional = centerBonus * 2; break;
            case Knight: positional = centerBonus * 8; break;
            case Bishop: positional = centerBonus * 5; break;
            case Rook: positional = centerBonus * 2; break;
            case Queen: positional = centerBonus * 3; break;
            case King: positional = centerBonus; break;
            default: break;
        }

        score += colorSign * (value + positional);
    }

    return score;
}

void Chess::applyMove(BoardState& board, const BitMove& move, int& capturedPiece) const
{
    capturedPiece = board[move.to];
    board[move.to] = board[move.from];
    board[move.from] = 0;
}

void Chess::undoMove(BoardState& board, const BitMove& move, int capturedPiece) const
{
    board[move.from] = board[move.to];
    board[move.to] = capturedPiece;
}

int Chess::negamax(BoardState& board, int depth, int alpha, int beta, int playerNumber, int aiPlayerNumber) const
{
    _searchNodeCounter++;

    if (depth == 0) {
        int perspective = (playerNumber == aiPlayerNumber) ? 1 : -1;
        return perspective * evaluateBoard(board, aiPlayerNumber);
    }

    std::vector<BitMove> moves = generateAllMovesForBoard(board, playerNumber);
    if (moves.empty()) {
        int perspective = (playerNumber == aiPlayerNumber) ? 1 : -1;
        return perspective * evaluateBoard(board, aiPlayerNumber);
    }

    int best = kNegInf;
    for (const BitMove& move : moves) {
        int captured = 0;
        applyMove(board, move, captured);
        int score = -negamax(board, depth - 1, -beta, -alpha, 1 - playerNumber, aiPlayerNumber);
        undoMove(board, move, captured);

        best = std::max(best, score);
        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            break;
        }
    }

    return best;
}

bool Chess::applyBestMoveToGrid(const BitMove& move)
{
    int fromX = move.from % 8;
    int fromY = move.from / 8;
    int toX = move.to % 8;
    int toY = move.to / 8;

    ChessSquare* src = _grid->getSquare(fromX, fromY);
    ChessSquare* dst = _grid->getSquare(toX, toY);
    if (!src || !dst || !src->bit()) {
        return false;
    }

    Bit* moving = src->bit();
    if (!canBitMoveFromTo(*moving, *src, *dst)) {
        return false;
    }

    dst->setBit(moving);
    src->draggedBitTo(moving, dst);
    moving->setPosition(dst->getPosition());
    bitMovedFromTo(*moving, *src, *dst);
    return true;
}

void Chess::updateAI()
{
    Player* current = getCurrentPlayer();
    if (!current || !current->isAIPlayer()) {
        return;
    }

    using Clock = std::chrono::high_resolution_clock;
    auto searchStart = Clock::now();
    _searchNodeCounter = 0;
    _lastSearchDepth = kChessAIDepth;

    int aiPlayer = current->playerNumber();
    BoardState board = boardStateFromGrid();
    std::vector<BitMove> moves = generateAllMovesForBoard(board, aiPlayer);
    if (moves.empty()) {
        auto searchEnd = Clock::now();
        _lastSearchNodeCount = _searchNodeCounter;
        _lastSearchTimeMs = std::chrono::duration<double, std::milli>(searchEnd - searchStart).count();
        endTurn();
        return;
    }

    int bestScore = kNegInf;
    BitMove bestMove = moves.front();

    for (const BitMove& move : moves) {
        _searchNodeCounter++; // root move node
        int captured = 0;
        applyMove(board, move, captured);
        int score = -negamax(board, kChessAIDepth - 1, kNegInf, kPosInf, 1 - aiPlayer, aiPlayer);
        undoMove(board, move, captured);

        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
        }
    }

    auto searchEnd = Clock::now();
    _lastSearchNodeCount = _searchNodeCounter;
    _lastSearchTimeMs = std::chrono::duration<double, std::milli>(searchEnd - searchStart).count();
    applyBestMoveToGrid(bestMove);
}


void Chess::stopGame()
{
    _grid->forEachSquare([](ChessSquare* square, int x, int y) {
        square->destroyBit();
    });
}

void Chess::bitMovedFromTo(Bit &bit, BitHolder &src, BitHolder &dst)
{
    // Handle piece capture - if destination has a piece, it's already been handled by pieceTaken
    // Just need to end the turn
    endTurn();

    // Run move generation for the side to move after turn switch.
    generateAllMoves();
}

Player* Chess::ownerAt(int x, int y) const
{
    if (x < 0 || x >= 8 || y < 0 || y >= 8) {
        return nullptr;
    }

    auto square = _grid->getSquare(x, y);
    if (!square || !square->bit()) {
        return nullptr;
    }
    return square->bit()->getOwner();
}

Player* Chess::checkForWinner()
{
    return nullptr;
}

bool Chess::checkForDraw()
{
    return false;
}

std::string Chess::initialStateString()
{
    return stateString();
}

std::string Chess::stateString()
{
    std::string s;
    s.reserve(64);
    _grid->forEachSquare([&](ChessSquare* square, int x, int y) {
        s += pieceNotation(x, y);
    });
    return s;
}

void Chess::setStateString(const std::string &s)
{
    _grid->forEachSquare([&](ChessSquare* square, int x, int y) {
        int index = y * 8 + x;
        char playerNumber = s[index] - '0';
        if (playerNumber) {
            square->setBit(PieceForPlayer(playerNumber - 1, Pawn));
        } else {
            square->setBit(nullptr);
        }
    });
}

std::vector<BitMove> Chess::generateLegalMoves()
{
    return generateAllMoves();
}

std::vector<BitMove> Chess::generateAllMoves()
{
    std::vector<BitMove> moves;
    Player* currentPlayer = getCurrentPlayer();
    if (!currentPlayer) {
        return moves;
    }
    int playerNumber = currentPlayer->playerNumber();
    int currentColor = playerNumber * 128;
    
    const uint64_t occ = boardOccupancy();
    uint64_t friendlyOcc = 0ULL;

    _grid->forEachSquare([&](ChessSquare* square, int x, int y) {
        Bit* bit = square->bit();
        if (bit && (bit->gameTag() & 128) == currentColor) {
            friendlyOcc |= (1ULL << (y * 8 + x));
        }
    });

    _grid->forEachSquare([&](ChessSquare* square, int fromX, int fromY) {
        Bit* bit = square->bit();
        if (!bit || (bit->gameTag() & 128) != currentColor) {
            return;
        }

        const ChessPiece piece = static_cast<ChessPiece>(bit->gameTag() & 0x7F);
        const int fromSq = fromY * 8 + fromX;
        uint64_t targets = 0ULL;

        switch (piece) {
            case Pawn: {
                const int direction = (playerNumber == 0) ? 1 : -1;
                const int startRank = (playerNumber == 0) ? 1 : 6;

                int oneStepY = fromY + direction;
                if (_grid->isValid(fromX, oneStepY) && _grid->getSquare(fromX, oneStepY)->bit() == nullptr) {
                    targets |= (1ULL << (oneStepY * 8 + fromX));
                    int twoStepY = fromY + 2 * direction;
                    if (fromY == startRank && _grid->isValid(fromX, twoStepY) &&
                        _grid->getSquare(fromX, twoStepY)->bit() == nullptr) {
                        targets |= (1ULL << (twoStepY * 8 + fromX));
                    }
                }

                int captureY = fromY + direction;
                int leftX = fromX - 1;
                int rightX = fromX + 1;
                if (_grid->isValid(leftX, captureY) && isOccupiedByOpponent(leftX, captureY, playerNumber)) {
                    targets |= (1ULL << (captureY * 8 + leftX));
                }
                if (_grid->isValid(rightX, captureY) && isOccupiedByOpponent(rightX, captureY, playerNumber)) {
                    targets |= (1ULL << (captureY * 8 + rightX));
                }
                break;
            }
            case Knight: {
                static const int offsets[8][2] = {
                    {1, 2}, {2, 1}, {2, -1}, {1, -2},
                    {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
                };
                for (const auto &o : offsets) {
                    int toX = fromX + o[0];
                    int toY = fromY + o[1];
                    if (_grid->isValid(toX, toY)) {
                        targets |= (1ULL << (toY * 8 + toX));
                    }
                }
                break;
            }
            case Bishop:
                targets = batt(fromSq, occ);
                break;
            case Rook:
                targets = ratt(fromSq, occ);
                break;
            case Queen:
                targets = batt(fromSq, occ) | ratt(fromSq, occ);
                break;
            case King: {
                static const int offsets[8][2] = {
                    {1, 1}, {1, 0}, {1, -1}, {0, -1},
                    {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}
                };
                for (const auto &o : offsets) {
                    int toX = fromX + o[0];
                    int toY = fromY + o[1];
                    if (_grid->isValid(toX, toY)) {
                        targets |= (1ULL << (toY * 8 + toX));
                    }
                }
                break;
            }
            default:
                break;
        }

        targets &= ~friendlyOcc;

        while (targets) {
            int toSq = getFirstBit(targets);
            targets &= (targets - 1);
            moves.push_back(BitMove(fromSq, toSq, piece));
        }
    });
    return moves;
}
