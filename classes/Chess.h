#pragma once

#include "Game.h"
#include "Grid.h"
#include "Bitboard.h"
#include <vector>

constexpr int pieceSize = 80;

class Chess : public Game
{
public:
    Chess();
    ~Chess();

    void setUpBoard() override;

    bool canBitMoveFrom(Bit &bit, BitHolder &src) override;
    bool canBitMoveFromTo(Bit &bit, BitHolder &src, BitHolder &dst) override;
    bool actionForEmptyHolder(BitHolder &holder) override;
    void bitMovedFromTo(Bit &bit, BitHolder &src, BitHolder &dst) override;

    void stopGame() override;

    Player *checkForWinner() override;
    bool checkForDraw() override;

    std::string initialStateString() override;
    std::string stateString() override;
    void setStateString(const std::string &s) override;

    Grid* getGrid() override { return _grid; }
    
    // Move generation for debugging/analysis
    std::vector<BitMove> generateLegalMoves();

private:
    Bit* PieceForPlayer(const int playerNumber, ChessPiece piece);
    Player* ownerAt(int x, int y) const;
    void FENtoBoard(const std::string& fen);
    void syncTurnFromFEN(const std::string& fen);
    char pieceNotation(int x, int y) const;
    
    // Movement validation helpers
    bool isValidMove(int fromX, int fromY, int toX, int toY, ChessPiece piece, int playerNumber);
    bool canPawnMove(int fromX, int fromY, int toX, int toY, int playerNumber);
    bool canPawnCapture(int fromX, int fromY, int toX, int toY, int playerNumber);
    bool canKnightMove(int fromX, int fromY, int toX, int toY);
    bool canKingMove(int fromX, int fromY, int toX, int toY);
    bool isPathClear(int fromX, int fromY, int toX, int toY);
    bool isOccupiedByOpponent(int x, int y, int playerNumber);
    bool isOccupiedByFriend(int x, int y, int playerNumber);
    
    Grid* _grid;
    std::string boardToFEN() const;
};
