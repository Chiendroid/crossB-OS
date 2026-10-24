#pragma once

#include "MappedInputManager.h"
#include "activities/Activity.h"

class Game2048Activity final : public Activity {
 private:
  static constexpr int GRID_SIZE = 4;
  int board[GRID_SIZE][GRID_SIZE]{};
  int undoBoard[GRID_SIZE][GRID_SIZE]{};
  int score = 0;
  int undoScore = 0;
  int bestScore = 0;
  bool canUndo = false;
  bool gameOver = false;
  bool gameWon = false;
  bool continuePlaying = false;

  void initNewGame();
  void spawnRandomTile();
  bool canMove() const;
  bool move(MappedInputManager::SwipeDir dir);
  bool slideAndMerge(int row[GRID_SIZE]);
  void undo();
  void loadBestScore();
  void saveBestScore();

  void renderHeader();
  void renderBoard();
  void renderTile(int val, int x, int y, int size);
  void renderControls();
  void handleTouch(int x, int y);

 public:
  Game2048Activity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~Game2048Activity() override = default;

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
