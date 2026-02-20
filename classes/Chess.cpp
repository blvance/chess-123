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
    return true;
}

void Chess::stopGame()
{
    _grid->forEachSquare([](ChessSquare* square, int x, int y) {
        square->destroyBit();
    });
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
