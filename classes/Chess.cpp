#include "Chess.h"
#include <limits>
#include <cmath>

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
    
    // Re-apply turn from the FEN string after startGame() resets currentTurnNo
    syncTurnFromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    // Run move generation for the initial position.
    generateLegalMoves();
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
    
    // Validate the move based on piece type
    return isValidMove(fromX, fromY, toX, toY, piece, playerNumber);
}

bool Chess::isValidMove(int fromX, int fromY, int toX, int toY, ChessPiece piece, int playerNumber)
{
    switch (piece) {
        case Pawn:
            // Check for normal move or capture
            return canPawnMove(fromX, fromY, toX, toY, playerNumber) ||
                   canPawnCapture(fromX, fromY, toX, toY, playerNumber);
        case Knight:
            return canKnightMove(fromX, fromY, toX, toY);
        case King:
            return canKingMove(fromX, fromY, toX, toY);
        case Rook:
        case Bishop:
        case Queen:
            // Not implemented yet, return false for now
            return false;
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
    generateLegalMoves();
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
    std::vector<BitMove> moves;
    Player* currentPlayer = getCurrentPlayer();
    if (!currentPlayer) {
        return moves;
    }
    int playerNumber = currentPlayer->playerNumber();
    int currentColor = playerNumber * 128;
    
    // Iterate through all squares on the board
    _grid->forEachSquare([&](ChessSquare* square, int x, int y) {
        Bit* bit = square->bit();
        
        // Skip empty squares or opponent's pieces
        if (!bit || (bit->gameTag() & 128) != currentColor) {
            return;
        }
        
        ChessPiece piece = static_cast<ChessPiece>(bit->gameTag() & 0x7F);
        
        // Try all possible destination squares
        for (int toX = 0; toX < 8; toX++) {
            for (int toY = 0; toY < 8; toY++) {
                // Skip the source square
                if (toX == x && toY == y) continue;
                
                // Check if this is a legal move
                if (isValidMove(x, y, toX, toY, piece, playerNumber)) {
                    // Don't add move if destination is occupied by friendly piece
                    if (!isOccupiedByFriend(toX, toY, playerNumber)) {
                        moves.push_back(BitMove(y * 8 + x, toY * 8 + toX, piece));
                    }
                }
            }
        }
    });
    printf("Generated %zu legal moves for player %d\n", moves.size(), playerNumber);
    // At this line, 'moves' contains all legal moves for the current position
    return moves;
}
