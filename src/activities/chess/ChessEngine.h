#pragma once

#include <cstdint>
#include <vector>

namespace chess {

// Piece representation:
// Empty = ' '
// White: 'P', 'N', 'B', 'R', 'Q', 'K'
// Black: 'p', 'n', 'b', 'r', 'q', 'k'

struct Move {
  int8_t from = -1;
  int8_t to = -1;
  char pieceMoved = ' ';
  char pieceCaptured = ' ';
  char promotion = ' ';

  bool isValid() const { return from >= 0 && to >= 0 && from < 64 && to < 64; }
  bool operator==(const Move& o) const { return from == o.from && to == o.to && promotion == o.promotion; }
};

enum class GameStatus {
  Ongoing,
  Check,
  CheckmateWhiteWins,
  CheckmateBlackWins,
  Stalemate
};

class ChessEngine {
 public:
  ChessEngine();

  void reset();
  bool isWhiteTurn() const { return whiteTurn; }
  void setWhiteTurn(bool white) { whiteTurn = white; }

  char getPiece(int index) const {
    if (index < 0 || index >= 64) return ' ';
    return board[index];
  }

  char getPiece(int col, int row) const {
    if (col < 0 || col > 7 || row < 0 || row > 7) return ' ';
    return board[row * 8 + col];
  }

  // Generate legal moves for a specific square
  int getLegalMovesFrom(int fromIndex, Move* outMoves, int maxMoves) const;

  // Generate all legal moves for current player
  int getAllLegalMoves(Move* outMoves, int maxMoves) const;

  // Perform move (returns true if move was legal and applied)
  bool makeMove(const Move& move);

  // Undo last move
  bool undoMove();

  // Can undo?
  bool canUndo() const { return !history.empty(); }

  // Game state checks
  bool isCheck(bool white) const;
  GameStatus getGameStatus();

  // AI Move search (negamax with alpha-beta pruning)
  Move findBestMove(int depth);

  // History count
  size_t getMoveCount() const { return history.size(); }
  const std::vector<Move>& getHistory() const { return history; }
  const Move* getLastMove() const { return history.empty() ? nullptr : &history.back(); }

  // Captured pieces
  const std::vector<char>& getCapturedByWhite() const { return capturedByWhite; }
  const std::vector<char>& getCapturedByBlack() const { return capturedByBlack; }

 private:
  char board[64];
  bool whiteTurn = true;
  std::vector<Move> history;
  std::vector<char> capturedByWhite;
  std::vector<char> capturedByBlack;

  // Pseudo-legal move generator
  int generatePseudoMoves(int fromIndex, Move* outMoves, int maxMoves) const;
  int generateAllPseudoMoves(bool white, Move* outMoves, int maxMoves) const;

  // Square attack check
  bool isSquareAttacked(int sq, bool byWhite) const;

  // Search helpers
  int evaluateBoard() const;
  int alphaBeta(int depth, int alpha, int beta, bool maximizing);
};

}  // namespace chess
