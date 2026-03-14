#pragma once

#include "Game.h"
#include "Grid.h"
#include "Bitboard.h"
#include <vector>
#include <array>
#include <cstdint>

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
    void updateAI() override;
    bool gameHasAI() override { return true; }

    Grid* getGrid() override { return _grid; }
    
    // Assignment-required move generation entry point.
    std::vector<BitMove> generateAllMoves();

    // Backward-compatible alias used by existing code.
    std::vector<BitMove> generateLegalMoves();
    uint64_t getLastSearchNodeCount() const { return _lastSearchNodeCount; }
    double getLastSearchTimeMs() const { return _lastSearchTimeMs; }
    int getLastSearchDepth() const { return _lastSearchDepth; }

private:
    using BoardState = std::array<int, 64>;

    Bit* PieceForPlayer(const int playerNumber, ChessPiece piece);
    Player* ownerAt(int x, int y) const;
    void FENtoBoard(const std::string& fen);
    void syncTurnFromFEN(const std::string& fen);
    char pieceNotation(int x, int y) const;
    BoardState boardStateFromGrid() const;
    std::vector<BitMove> generateAllMovesForBoard(const BoardState& board, int playerNumber) const;
    uint64_t boardOccupancy(const BoardState& board) const;
    int evaluateBoard(const BoardState& board, int aiPlayerNumber) const;
    int negamax(BoardState& board, int depth, int alpha, int beta, int playerNumber, int aiPlayerNumber) const;
    void applyMove(BoardState& board, const BitMove& move, int& capturedPiece) const;
    void undoMove(BoardState& board, const BitMove& move, int capturedPiece) const;
    bool applyBestMoveToGrid(const BitMove& move);
    int findKingSquare(const BoardState& board, int playerNumber) const;
    bool isSquareAttacked(const BoardState& board, int square, int attackerPlayerNumber) const;
    bool isKingInCheck(const BoardState& board, int playerNumber) const;
    bool isMoveLegalOnBoard(BoardState& board, const BitMove& move, int playerNumber) const;
    
    // Movement validation helpers
    bool isValidMove(int fromX, int fromY, int toX, int toY, ChessPiece piece, int playerNumber);
    bool canPawnMove(int fromX, int fromY, int toX, int toY, int playerNumber);
    bool canPawnCapture(int fromX, int fromY, int toX, int toY, int playerNumber);
    bool canKnightMove(int fromX, int fromY, int toX, int toY);
    bool canKingMove(int fromX, int fromY, int toX, int toY);
    bool isPathClear(int fromX, int fromY, int toX, int toY);
    bool isOccupiedByOpponent(int x, int y, int playerNumber);
    bool isOccupiedByFriend(int x, int y, int playerNumber);
    uint64_t boardOccupancy() const;
    
    Grid* _grid;
    std::string boardToFEN() const;
    mutable uint64_t _searchNodeCounter = 0;
    uint64_t _lastSearchNodeCount = 0;
    double _lastSearchTimeMs = 0.0;
    int _lastSearchDepth = 0;
};
