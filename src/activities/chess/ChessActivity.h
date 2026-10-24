#pragma once

#include "activities/Activity.h"
#include "activities/chess/ChessEngine.h"

class ChessActivity final : public Activity {
 public:
  enum class Difficulty {
    Easy,     // Depth 2
    Medium,   // Depth 3
    Hard,     // Depth 4
    TwoPlayer // Pass & play
  };

  ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~ChessActivity() override = default;

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  chess::ChessEngine engine;
  int selectedSquare = -1;
  chess::Move currentLegalMoves[32];
  int currentLegalMoveCount = 0;

  bool playerIsWhite = true;
  Difficulty difficulty = Difficulty::Medium;
  bool isAiThinking = false;
  chess::GameStatus status = chess::GameStatus::Ongoing;

  // Board layout constants
  static constexpr int BOARD_X = 32;
  static constexpr int BOARD_Y = 66;
  static constexpr int CELL_SIZE = 52;
  static constexpr int BOARD_SIZE = CELL_SIZE * 8;  // 416

  void drawHeader();
  void drawBoard();
  void drawPieces();
  void drawHighlights();
  void drawControls();
  void drawPieceBitmap(int x, int y, char piece, bool onDarkSquare);

  void handleTouch(int x, int y);
  void triggerAiMove();
  int toBoardIndex(int displayCol, int displayRow) const;
  void fromBoardIndex(int index, int& displayCol, int& displayRow) const;
};
