#include "ChessEngine.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace chess {

static const char INITIAL_BOARD[64] = {
    'r', 'n', 'b', 'q', 'k', 'b', 'n', 'r',
    'p', 'p', 'p', 'p', 'p', 'p', 'p', 'p',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    'P', 'P', 'P', 'P', 'P', 'P', 'P', 'P',
    'R', 'N', 'B', 'Q', 'K', 'B', 'N', 'R'
};

// Piece square tables (White perspective, flip row for Black)
static const int PST_PAWN[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};

static const int PST_KNIGHT[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};

static const int PST_BISHOP[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};

static const int PST_ROOK[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
      5, 10, 10, 10, 10, 10, 10,  5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
      0,  0,  0,  5,  5,  0,  0,  0
};

static const int PST_QUEEN[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};

static const int PST_KING_MID[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20
};

static inline bool isWhitePiece(char p) { return p >= 'A' && p <= 'Z'; }
static inline bool isBlackPiece(char p) { return p >= 'a' && p <= 'z'; }

ChessEngine::ChessEngine() {
  reset();
}

void ChessEngine::reset() {
  for (int i = 0; i < 64; ++i) {
    board[i] = INITIAL_BOARD[i];
  }
  whiteTurn = true;
  history.clear();
  capturedByWhite.clear();
  capturedByBlack.clear();
}

int ChessEngine::generatePseudoMoves(int fromIndex, Move* outMoves, int maxMoves) const {
  if (fromIndex < 0 || fromIndex >= 64 || !outMoves || maxMoves <= 0) return 0;
  const char p = board[fromIndex];
  if (p == ' ') return 0;

  const bool isWhite = isWhitePiece(p);
  const int fromR = fromIndex / 8;
  const int fromC = fromIndex % 8;
  int count = 0;

  auto addMove = [&](int toR, int toC, char promo = ' ') {
    if (count >= maxMoves) return;
    if (toR < 0 || toR > 7 || toC < 0 || toC > 7) return;
    const int toIndex = toR * 8 + toC;
    const char target = board[toIndex];
    if (target != ' ') {
      if (isWhite && isWhitePiece(target)) return;
      if (!isWhite && isBlackPiece(target)) return;
    }
    Move m;
    m.from = static_cast<int8_t>(fromIndex);
    m.to = static_cast<int8_t>(toIndex);
    m.pieceMoved = p;
    m.pieceCaptured = target;
    m.promotion = promo;
    outMoves[count++] = m;
  };

  const char lowerP = static_cast<char>(std::tolower(p));

  // --- PAWN ---
  if (lowerP == 'p') {
    const int fwd = isWhite ? -1 : 1;
    const int startRow = isWhite ? 6 : 1;
    const int promoRow = isWhite ? 0 : 7;

    // 1-step forward
    const int nextR = fromR + fwd;
    if (nextR >= 0 && nextR <= 7 && board[nextR * 8 + fromC] == ' ') {
      if (nextR == promoRow) {
        addMove(nextR, fromC, isWhite ? 'Q' : 'q');
      } else {
        addMove(nextR, fromC);
      }
      // 2-step forward from starting row
      if (fromR == startRow) {
        const int step2R = fromR + (fwd * 2);
        if (board[step2R * 8 + fromC] == ' ') {
          addMove(step2R, fromC);
        }
      }
    }

    // Diagonal captures
    for (int dc : {-1, 1}) {
      const int capC = fromC + dc;
      if (capC >= 0 && capC <= 7) {
        const char target = board[nextR * 8 + capC];
        if (target != ' ' && ((isWhite && isBlackPiece(target)) || (!isWhite && isWhitePiece(target)))) {
          if (nextR == promoRow) {
            addMove(nextR, capC, isWhite ? 'Q' : 'q');
          } else {
            addMove(nextR, capC);
          }
        }
      }
    }
    return count;
  }

  // --- KNIGHT ---
  if (lowerP == 'n') {
    static const int dR[] = {-2, -2, -1, -1,  1,  1,  2,  2};
    static const int dC[] = {-1,  1, -2,  2, -2,  2, -1,  1};
    for (int i = 0; i < 8; ++i) {
      addMove(fromR + dR[i], fromC + dC[i]);
    }
    return count;
  }

  // --- KING ---
  if (lowerP == 'k') {
    for (int dr = -1; dr <= 1; ++dr) {
      for (int dc = -1; dc <= 1; ++dc) {
        if (dr == 0 && dc == 0) continue;
        addMove(fromR + dr, fromC + dc);
      }
    }
    return count;
  }

  // --- BISHOP / ROOK / QUEEN (Ray pieces) ---
  bool isBishop = (lowerP == 'b' || lowerP == 'q');
  bool isRook = (lowerP == 'r' || lowerP == 'q');

  static const int rayR[] = {-1,  1,  0,  0, -1, -1,  1,  1};
  static const int rayC[] = { 0,  0, -1,  1, -1,  1, -1,  1};

  for (int d = 0; d < 8; ++d) {
    if (d < 4 && !isRook) continue;
    if (d >= 4 && !isBishop) continue;

    int r = fromR + rayR[d];
    int c = fromC + rayC[d];
    while (r >= 0 && r <= 7 && c >= 0 && c <= 7) {
      const int toIdx = r * 8 + c;
      const char target = board[toIdx];
      if (target == ' ') {
        addMove(r, c);
      } else {
        if ((isWhite && isBlackPiece(target)) || (!isWhite && isWhitePiece(target))) {
          addMove(r, c);
        }
        break;  // blocked
      }
      r += rayR[d];
      c += rayC[d];
    }
  }

  return count;
}

int ChessEngine::generateAllPseudoMoves(bool white, Move* outMoves, int maxMoves) const {
  int total = 0;
  for (int i = 0; i < 64 && total < maxMoves; ++i) {
    const char p = board[i];
    if (p == ' ') continue;
    if (white ? isWhitePiece(p) : isBlackPiece(p)) {
      total += generatePseudoMoves(i, outMoves + total, maxMoves - total);
    }
  }
  return total;
}

bool ChessEngine::isSquareAttacked(int sq, bool byWhite) const {
  if (sq < 0 || sq >= 64) return false;
  const int sqR = sq / 8;
  const int sqC = sq % 8;

  // 1. Attacked by Pawn
  const int pawnFwd = byWhite ? 1 : -1;  // reverse check
  const int pR = sqR + pawnFwd;
  if (pR >= 0 && pR <= 7) {
    for (int pC : {sqC - 1, sqC + 1}) {
      if (pC >= 0 && pC <= 7) {
        const char p = board[pR * 8 + pC];
        if (p == (byWhite ? 'P' : 'p')) return true;
      }
    }
  }

  // 2. Attacked by Knight
  static const int ndR[] = {-2, -2, -1, -1,  1,  1,  2,  2};
  static const int ndC[] = {-1,  1, -2,  2, -2,  2, -1,  1};
  for (int i = 0; i < 8; ++i) {
    const int r = sqR + ndR[i];
    const int c = sqC + ndC[i];
    if (r >= 0 && r <= 7 && c >= 0 && c <= 7) {
      const char p = board[r * 8 + c];
      if (p == (byWhite ? 'N' : 'n')) return true;
    }
  }

  // 3. Attacked by King
  for (int dr = -1; dr <= 1; ++dr) {
    for (int dc = -1; dc <= 1; ++dc) {
      if (dr == 0 && dc == 0) continue;
      const int r = sqR + dr;
      const int c = sqC + dc;
      if (r >= 0 && r <= 7 && c >= 0 && c <= 7) {
        const char p = board[r * 8 + c];
        if (p == (byWhite ? 'K' : 'k')) return true;
      }
    }
  }

  // 4. Attacked by Straight line (Rook, Queen)
  static const int rookR[] = {-1, 1, 0, 0};
  static const int rookC[] = { 0, 0,-1, 1};
  for (int d = 0; d < 4; ++d) {
    int r = sqR + rookR[d];
    int c = sqC + rookC[d];
    while (r >= 0 && r <= 7 && c >= 0 && c <= 7) {
      const char p = board[r * 8 + c];
      if (p != ' ') {
        if (p == (byWhite ? 'R' : 'r') || p == (byWhite ? 'Q' : 'q')) return true;
        break;
      }
      r += rookR[d];
      c += rookC[d];
    }
  }

  // 5. Attacked by Diagonal (Bishop, Queen)
  static const int bishR[] = {-1, -1, 1, 1};
  static const int bishC[] = {-1,  1,-1, 1};
  for (int d = 0; d < 4; ++d) {
    int r = sqR + bishR[d];
    int c = sqC + bishC[d];
    while (r >= 0 && r <= 7 && c >= 0 && c <= 7) {
      const char p = board[r * 8 + c];
      if (p != ' ') {
        if (p == (byWhite ? 'B' : 'b') || p == (byWhite ? 'Q' : 'q')) return true;
        break;
      }
      r += bishR[d];
      c += bishC[d];
    }
  }

  return false;
}

bool ChessEngine::isCheck(bool white) const {
  // Find King
  const char kingChar = white ? 'K' : 'k';
  int kingSq = -1;
  for (int i = 0; i < 64; ++i) {
    if (board[i] == kingChar) {
      kingSq = i;
      break;
    }
  }
  if (kingSq < 0) return true;  // King captured
  return isSquareAttacked(kingSq, !white);
}

int ChessEngine::getLegalMovesFrom(int fromIndex, Move* outMoves, int maxMoves) const {
  if (fromIndex < 0 || fromIndex >= 64 || !outMoves || maxMoves <= 0) return 0;
  const char p = board[fromIndex];
  if (p == ' ' || (whiteTurn ? !isWhitePiece(p) : !isBlackPiece(p))) return 0;

  Move pseudo[32];
  const int pCount = generatePseudoMoves(fromIndex, pseudo, 32);

  int legalCount = 0;
  ChessEngine* mutableThis = const_cast<ChessEngine*>(this);

  for (int i = 0; i < pCount; ++i) {
    const Move& m = pseudo[i];
    // Try move
    mutableThis->board[m.to] = m.promotion != ' ' ? m.promotion : m.pieceMoved;
    mutableThis->board[m.from] = ' ';

    if (!mutableThis->isCheck(whiteTurn)) {
      if (legalCount < maxMoves) {
        outMoves[legalCount++] = m;
      }
    }

    // Undo move
    mutableThis->board[m.from] = m.pieceMoved;
    mutableThis->board[m.to] = m.pieceCaptured;
  }

  return legalCount;
}

int ChessEngine::getAllLegalMoves(Move* outMoves, int maxMoves) const {
  if (!outMoves || maxMoves <= 0) return 0;
  int total = 0;
  for (int i = 0; i < 64 && total < maxMoves; ++i) {
    const char p = board[i];
    if (p == ' ') continue;
    if (whiteTurn ? isWhitePiece(p) : isBlackPiece(p)) {
      total += getLegalMovesFrom(i, outMoves + total, maxMoves - total);
    }
  }
  return total;
}

bool ChessEngine::makeMove(const Move& move) {
  if (!move.isValid()) return false;
  Move legalMoves[32];
  const int count = getLegalMovesFrom(move.from, legalMoves, 32);
  bool found = false;
  Move verifiedMove;
  for (int i = 0; i < count; ++i) {
    if (legalMoves[i].to == move.to) {
      found = true;
      verifiedMove = legalMoves[i];
      break;
    }
  }
  if (!found) return false;

  board[verifiedMove.to] = verifiedMove.promotion != ' ' ? verifiedMove.promotion : verifiedMove.pieceMoved;
  board[verifiedMove.from] = ' ';

  if (verifiedMove.pieceCaptured != ' ') {
    if (whiteTurn) {
      capturedByWhite.push_back(verifiedMove.pieceCaptured);
    } else {
      capturedByBlack.push_back(verifiedMove.pieceCaptured);
    }
  }

  history.push_back(verifiedMove);
  whiteTurn = !whiteTurn;
  return true;
}

bool ChessEngine::undoMove() {
  if (history.empty()) return false;
  const Move last = history.back();
  history.pop_back();

  board[last.from] = last.pieceMoved;
  board[last.to] = last.pieceCaptured;

  if (last.pieceCaptured != ' ') {
    if (!whiteTurn && !capturedByWhite.empty()) {
      capturedByWhite.pop_back();
    } else if (whiteTurn && !capturedByBlack.empty()) {
      capturedByBlack.pop_back();
    }
  }

  whiteTurn = !whiteTurn;
  return true;
}

GameStatus ChessEngine::getGameStatus() {
  Move legalMoves[1];
  const int count = getAllLegalMoves(legalMoves, 1);
  const bool check = isCheck(whiteTurn);

  if (count == 0) {
    if (check) {
      return whiteTurn ? GameStatus::CheckmateBlackWins : GameStatus::CheckmateWhiteWins;
    }
    return GameStatus::Stalemate;
  }
  if (check) {
    return GameStatus::Check;
  }
  return GameStatus::Ongoing;
}

int ChessEngine::evaluateBoard() const {
  int score = 0;
  for (int i = 0; i < 64; ++i) {
    const char p = board[i];
    if (p == ' ') continue;

    const bool isW = isWhitePiece(p);
    const int r = i / 8;
    const int c = i % 8;
    const int pstIdx = isW ? i : ((7 - r) * 8 + c);

    int pieceVal = 0;
    int pstVal = 0;

    switch (std::tolower(p)) {
      case 'p':
        pieceVal = 100;
        pstVal = PST_PAWN[pstIdx];
        break;
      case 'n':
        pieceVal = 320;
        pstVal = PST_KNIGHT[pstIdx];
        break;
      case 'b':
        pieceVal = 330;
        pstVal = PST_BISHOP[pstIdx];
        break;
      case 'r':
        pieceVal = 500;
        pstVal = PST_ROOK[pstIdx];
        break;
      case 'q':
        pieceVal = 900;
        pstVal = PST_QUEEN[pstIdx];
        break;
      case 'k':
        pieceVal = 20000;
        pstVal = PST_KING_MID[pstIdx];
        break;
      default:
        break;
    }

    if (isW) {
      score += (pieceVal + pstVal);
    } else {
      score -= (pieceVal + pstVal);
    }
  }
  return score;
}

int ChessEngine::alphaBeta(int depth, int alpha, int beta, bool maximizing) {
  if (depth == 0) {
    return evaluateBoard();
  }

  Move moves[64];
  const int count = getAllLegalMoves(moves, 64);

  if (count == 0) {
    if (isCheck(whiteTurn)) {
      return maximizing ? -99999 + (4 - depth) : 99999 - (4 - depth);
    }
    return 0;  // stalemate
  }

  if (maximizing) {
    int maxEval = -1000000;
    for (int i = 0; i < count; ++i) {
      const Move& m = moves[i];
      board[m.to] = m.promotion != ' ' ? m.promotion : m.pieceMoved;
      board[m.from] = ' ';
      whiteTurn = !whiteTurn;

      const int eval = alphaBeta(depth - 1, alpha, beta, false);

      // undo
      board[m.from] = m.pieceMoved;
      board[m.to] = m.pieceCaptured;
      whiteTurn = !whiteTurn;

      maxEval = std::max(maxEval, eval);
      alpha = std::max(alpha, eval);
      if (beta <= alpha) break;
    }
    return maxEval;
  } else {
    int minEval = 1000000;
    for (int i = 0; i < count; ++i) {
      const Move& m = moves[i];
      board[m.to] = m.promotion != ' ' ? m.promotion : m.pieceMoved;
      board[m.from] = ' ';
      whiteTurn = !whiteTurn;

      const int eval = alphaBeta(depth - 1, alpha, beta, true);

      // undo
      board[m.from] = m.pieceMoved;
      board[m.to] = m.pieceCaptured;
      whiteTurn = !whiteTurn;

      minEval = std::min(minEval, eval);
      beta = std::min(beta, eval);
      if (beta <= alpha) break;
    }
    return minEval;
  }
}

Move ChessEngine::findBestMove(int depth) {
  Move moves[64];
  const int count = getAllLegalMoves(moves, 64);
  if (count == 0) return Move{};

  Move bestMove = moves[0];
  const bool maximizing = whiteTurn;

  if (maximizing) {
    int maxEval = -1000000;
    int alpha = -1000000;
    int beta = 1000000;
    for (int i = 0; i < count; ++i) {
      const Move& m = moves[i];
      board[m.to] = m.promotion != ' ' ? m.promotion : m.pieceMoved;
      board[m.from] = ' ';
      whiteTurn = !whiteTurn;

      const int eval = alphaBeta(depth - 1, alpha, beta, false);

      board[m.from] = m.pieceMoved;
      board[m.to] = m.pieceCaptured;
      whiteTurn = !whiteTurn;

      if (eval > maxEval) {
        maxEval = eval;
        bestMove = m;
      }
      alpha = std::max(alpha, eval);
    }
  } else {
    int minEval = 1000000;
    int alpha = -1000000;
    int beta = 1000000;
    for (int i = 0; i < count; ++i) {
      const Move& m = moves[i];
      board[m.to] = m.promotion != ' ' ? m.promotion : m.pieceMoved;
      board[m.from] = ' ';
      whiteTurn = !whiteTurn;

      const int eval = alphaBeta(depth - 1, alpha, beta, true);

      board[m.from] = m.pieceMoved;
      board[m.to] = m.pieceCaptured;
      whiteTurn = !whiteTurn;

      if (eval < minEval) {
        minEval = eval;
        bestMove = m;
      }
      beta = std::min(beta, eval);
    }
  }

  return bestMove;
}

}  // namespace chess
