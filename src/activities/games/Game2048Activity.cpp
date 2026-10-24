#include "Game2048Activity.h"

#include <ArduinoJson.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "HalDisplay.h"
#include "activities/ActivityManager.h"
#include "components/TouchHeaderBackButton.h"
#include "fontIds.h"

extern ActivityManager activityManager;

namespace {
constexpr const char* SAVE_FILE_PATH = "/.crosspoint/game2048.json";
}  // namespace

Game2048Activity::Game2048Activity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Game2048", renderer, mappedInput) {}

void Game2048Activity::onEnter() {
  Activity::onEnter();
  renderer.clearScreen();
  loadBestScore();
  initNewGame();
  requestUpdate(true);
}

void Game2048Activity::loadBestScore() {
  bestScore = 0;
  if (!Storage.exists(SAVE_FILE_PATH)) return;

  HalFile file;
  if (!Storage.openFileForRead("2048", SAVE_FILE_PATH, file)) return;

  JsonDocument doc;
  const auto err = deserializeJson(doc, file);
  if (err == DeserializationError::Ok) {
    bestScore = doc["best_score"] | 0;
  }
}

void Game2048Activity::saveBestScore() {
  HalFile file;
  if (!Storage.openFileForWrite("2048", SAVE_FILE_PATH, file)) return;

  JsonDocument doc;
  doc["best_score"] = bestScore;
  serializeJson(doc, file);
}

void Game2048Activity::initNewGame() {
  for (int r = 0; r < GRID_SIZE; ++r) {
    for (int c = 0; c < GRID_SIZE; ++c) {
      board[r][c] = 0;
      undoBoard[r][c] = 0;
    }
  }
  score = 0;
  undoScore = 0;
  canUndo = false;
  gameOver = false;
  gameWon = false;
  continuePlaying = false;

  spawnRandomTile();
  spawnRandomTile();
}

void Game2048Activity::spawnRandomTile() {
  std::vector<std::pair<int, int>> emptyCells;
  for (int r = 0; r < GRID_SIZE; ++r) {
    for (int c = 0; c < GRID_SIZE; ++c) {
      if (board[r][c] == 0) {
        emptyCells.emplace_back(r, c);
      }
    }
  }
  if (emptyCells.empty()) return;

  const int idx = rand() % emptyCells.size();
  const auto [r, c] = emptyCells[idx];
  // 90% cơ hội sinh số 2, 10% cơ hội sinh số 4
  board[r][c] = (rand() % 10 == 0) ? 4 : 2;
}

bool Game2048Activity::slideAndMerge(int row[GRID_SIZE]) {
  bool changed = false;
  int temp[GRID_SIZE] = {0};
  int writeIdx = 0;

  // 1. Dồn các số khác 0 về phía trước
  for (int i = 0; i < GRID_SIZE; ++i) {
    if (row[i] != 0) {
      temp[writeIdx++] = row[i];
    }
  }

  // 2. Gộp 2 số giống nhau kề nhau
  for (int i = 0; i < writeIdx - 1; ++i) {
    if (temp[i] != 0 && temp[i] == temp[i + 1]) {
      temp[i] *= 2;
      score += temp[i];
      if (temp[i] == 2048 && !continuePlaying) {
        gameWon = true;
      }
      for (int j = i + 1; j < writeIdx - 1; ++j) {
        temp[j] = temp[j + 1];
      }
      temp[writeIdx - 1] = 0;
      writeIdx--;
    }
  }

  // 3. So sánh với mảng ban đầu
  for (int i = 0; i < GRID_SIZE; ++i) {
    if (row[i] != temp[i]) {
      changed = true;
      row[i] = temp[i];
    }
  }

  return changed;
}

bool Game2048Activity::canMove() const {
  for (int r = 0; r < GRID_SIZE; ++r) {
    for (int c = 0; c < GRID_SIZE; ++c) {
      if (board[r][c] == 0) return true;
      if (c + 1 < GRID_SIZE && board[r][c] == board[r][c + 1]) return true;
      if (r + 1 < GRID_SIZE && board[r][c] == board[r + 1][c]) return true;
    }
  }
  return false;
}

bool Game2048Activity::move(const MappedInputManager::SwipeDir dir) {
  if (gameOver) return false;

  int savedBoard[GRID_SIZE][GRID_SIZE];
  for (int r = 0; r < GRID_SIZE; ++r) {
    for (int c = 0; c < GRID_SIZE; ++c) {
      savedBoard[r][c] = board[r][c];
    }
  }
  const int savedScore = score;
  bool changed = false;

  if (dir == MappedInputManager::SwipeDir::Left) {
    for (int r = 0; r < GRID_SIZE; ++r) {
      if (slideAndMerge(board[r])) changed = true;
    }
  } else if (dir == MappedInputManager::SwipeDir::Right) {
    for (int r = 0; r < GRID_SIZE; ++r) {
      int line[GRID_SIZE];
      for (int c = 0; c < GRID_SIZE; ++c) line[c] = board[r][GRID_SIZE - 1 - c];
      if (slideAndMerge(line)) changed = true;
      for (int c = 0; c < GRID_SIZE; ++c) board[r][GRID_SIZE - 1 - c] = line[c];
    }
  } else if (dir == MappedInputManager::SwipeDir::Up) {
    for (int c = 0; c < GRID_SIZE; ++c) {
      int line[GRID_SIZE];
      for (int r = 0; r < GRID_SIZE; ++r) line[r] = board[r][c];
      if (slideAndMerge(line)) changed = true;
      for (int r = 0; r < GRID_SIZE; ++r) board[r][c] = line[r];
    }
  } else if (dir == MappedInputManager::SwipeDir::Down) {
    for (int c = 0; c < GRID_SIZE; ++c) {
      int line[GRID_SIZE];
      for (int r = 0; r < GRID_SIZE; ++r) line[r] = board[GRID_SIZE - 1 - r][c];
      if (slideAndMerge(line)) changed = true;
      for (int r = 0; r < GRID_SIZE; ++r) board[GRID_SIZE - 1 - r][c] = line[r];
    }
  }

  if (changed) {
    for (int r = 0; r < GRID_SIZE; ++r) {
      for (int c = 0; c < GRID_SIZE; ++c) {
        undoBoard[r][c] = savedBoard[r][c];
      }
    }
    undoScore = savedScore;
    canUndo = true;

    spawnRandomTile();

    if (score > bestScore) {
      bestScore = score;
      saveBestScore();
    }

    if (!canMove()) {
      gameOver = true;
    }
    requestUpdate(true);
    return true;
  }

  return false;
}

void Game2048Activity::undo() {
  if (!canUndo) return;
  for (int r = 0; r < GRID_SIZE; ++r) {
    for (int c = 0; c < GRID_SIZE; ++c) {
      board[r][c] = undoBoard[r][c];
    }
  }
  score = undoScore;
  canUndo = false;
  gameOver = false;
  requestUpdate(true);
}

void Game2048Activity::renderHeader() {
  const int screenW = renderer.getScreenWidth();
  TouchHeaderBackButton::drawSquircleBackButton(renderer, 20, 14, 40, 10);

  // Tiêu đề "2048"
  renderer.drawText(NOTOSANS_16_FONT_ID, 74, 20, "2048", true, EpdFontFamily::BOLD);

  // Khối hiển thị Điểm (SCORE)
  const int scoreW = 92;
  const int scoreH = 44;
  const int scoreX = screenW - 20 - scoreW * 2 - 10;
  const int bestX = screenW - 20 - scoreW;
  const int badgeY = 12;

  // Thẻ SCORE
  renderer.fillRoundedRect(scoreX, badgeY, scoreW, scoreH, 8, Color::White);
  renderer.drawRoundedRect(scoreX, badgeY, scoreW, scoreH, 1, 8, true);
  renderer.drawText(SMALL_FONT_ID, scoreX + 8, badgeY + 6, "SCORE", true, EpdFontFamily::BOLD);
  char scoreBuf[16];
  snprintf(scoreBuf, sizeof(scoreBuf), "%d", score);
  renderer.drawText(UI_10_FONT_ID, scoreX + 8, badgeY + 22, scoreBuf, true, EpdFontFamily::BOLD);

  // Thẻ BEST
  renderer.fillRoundedRect(bestX, badgeY, scoreW, scoreH, 8, Color::Black);
  renderer.drawText(SMALL_FONT_ID, bestX + 8, badgeY + 6, "BEST", false, EpdFontFamily::BOLD);
  char bestBuf[16];
  snprintf(bestBuf, sizeof(bestBuf), "%d", bestScore);
  renderer.drawText(UI_10_FONT_ID, bestX + 8, badgeY + 22, bestBuf, false, EpdFontFamily::BOLD);
}

void Game2048Activity::renderTile(const int val, const int x, const int y, const int size) {
  if (val == 0) {
    renderer.drawRoundedRect(x, y, size, size, 1, 10, true);
    return;
  }

  // Các phong cách nền theo giá trị số (phù hợp tối đa với E-ink)
  if (val == 2) {
    renderer.fillRoundedRect(x, y, size, size, 10, Color::White);
    renderer.drawRoundedRect(x, y, size, size, 1, 10, true);
  } else if (val == 4) {
    renderer.fillRoundedRect(x, y, size, size, 10, Color::White);
    renderer.drawRoundedRect(x, y, size, size, 2, 10, true);
  } else if (val == 8 || val == 16) {
    renderer.fillRoundedRect(x, y, size, size, 10, Color::LightGray);
    renderer.drawRoundedRect(x, y, size, size, 1, 10, true);
  } else if (val == 32 || val == 64) {
    renderer.fillRoundedRect(x, y, size, size, 10, Color::DarkGray);
    renderer.drawRoundedRect(x, y, size, size, 1, 10, true);
  } else {
    // 128, 256, 512, 1024, 2048... (Nền đen đặc)
    renderer.fillRoundedRect(x, y, size, size, 10, Color::Black);
    if (val >= 2048) {
      renderer.drawRoundedRect(x + 3, y + 3, size - 6, size - 6, 1, 8, false);
    }
  }

  // Vẽ chữ số
  char numStr[16];
  snprintf(numStr, sizeof(numStr), "%d", val);
  const bool isInverted = (val >= 64);
  const int fontId = (val < 100) ? NOTOSANS_16_FONT_ID : (val < 1000 ? UI_12_FONT_ID : UI_10_FONT_ID);
  const int tw = renderer.getTextWidth(fontId, numStr, EpdFontFamily::BOLD);
  const int th = renderer.getLineHeight(fontId);
  const int tx = x + (size - tw) / 2;
  const int ty = y + (size - th) / 2 + (fontId == NOTOSANS_16_FONT_ID ? 4 : 0);

  renderer.drawText(fontId, tx, ty, numStr, !isInverted, EpdFontFamily::BOLD);
}

void Game2048Activity::renderBoard() {
  const int screenW = renderer.getScreenWidth();
  const int boardSize = 400;
  const int boardX = (screenW - boardSize) / 2;
  const int boardY = 72;

  // Khung ngoài bàn cờ
  renderer.fillRoundedRect(boardX, boardY, boardSize, boardSize, 14, Color::White);
  renderer.drawRoundedRect(boardX, boardY, boardSize, boardSize, 2, 14, true);

  const int gap = 8;
  const int pad = 10;
  const int tileSize = (boardSize - pad * 2 - gap * (GRID_SIZE - 1)) / GRID_SIZE;

  for (int r = 0; r < GRID_SIZE; ++r) {
    for (int c = 0; c < GRID_SIZE; ++c) {
      const int tx = boardX + pad + c * (tileSize + gap);
      const int ty = boardY + pad + r * (tileSize + gap);
      renderTile(board[r][c], tx, ty, tileSize);
    }
  }

  // Thông báo Thắng cuộc hoặc Thua cuộc (Game Over Overlay)
  if (gameOver || (gameWon && !continuePlaying)) {
    const int popW = 280;
    const int popH = 110;
    const int popX = (screenW - popW) / 2;
    const int popY = boardY + (boardSize - popH) / 2;

    renderer.fillRoundedRect(popX, popY, popW, popH, 12, Color::Black);
    renderer.drawRoundedRect(popX, popY, popW, popH, 2, 12, false);

    const char* msg = gameOver ? "GAME OVER!" : "BẠN ĐÃ ĐẠT 2048!";
    const int mw = renderer.getTextWidth(UI_12_FONT_ID, msg, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, popX + (popW - mw) / 2, popY + 22, msg, false, EpdFontFamily::BOLD);

    const char* sub = gameOver ? "Bấm [Chơi mới] để thử lại" : "Chạm màn hình để tiếp tục";
    const int sw = renderer.getTextWidth(SMALL_FONT_ID, sub, EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, popX + (popW - sw) / 2, popY + 62, sub, false, EpdFontFamily::REGULAR);
  }
}

void Game2048Activity::renderControls() {
  const int screenW = renderer.getScreenWidth();

  // Hàng nút [ Chơi mới ] và [ Hoàn tác ]
  const int btnY = 492;
  const int newBtnX = 40;
  const int newBtnW = 180;
  const int btnH = 46;

  renderer.fillRoundedRect(newBtnX, btnY, newBtnW, btnH, 12, Color::White);
  renderer.drawRoundedRect(newBtnX, btnY, newBtnW, btnH, 2, 12, true);
  const char* newTxt = "Chơi ván mới";
  const int ntw = renderer.getTextWidth(UI_10_FONT_ID, newTxt, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, newBtnX + (newBtnW - ntw) / 2, btnY + 14, newTxt, true, EpdFontFamily::BOLD);

  const int undoBtnX = screenW - 40 - 180;
  const int undoBtnW = 180;
  if (canUndo) {
    renderer.fillRoundedRect(undoBtnX, btnY, undoBtnW, btnH, 12, Color::White);
    renderer.drawRoundedRect(undoBtnX, btnY, undoBtnW, btnH, 2, 12, true);
  } else {
    renderer.drawRoundedRect(undoBtnX, btnY, undoBtnW, btnH, 1, 12, true);
  }
  const char* undoTxt = "Hoàn tác (Undo)";
  const int utw = renderer.getTextWidth(UI_10_FONT_ID, undoTxt, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, undoBtnX + (undoBtnW - utw) / 2, btnY + 14, undoTxt, canUndo, EpdFontFamily::BOLD);

  // Phím điều hướng ảo hình chữ thập (D-Pad Squircle) cho người dùng thích chạm
  const int dpadCenterY = 620;
  const int dpadCenterX = screenW / 2;
  const int dBtnSize = 54;
  const int dGap = 6;

  // Nút Lên [ ▲ ]
  const int upX = dpadCenterX - dBtnSize / 2;
  const int upY = dpadCenterY - dBtnSize - dGap;
  renderer.fillRoundedRect(upX, upY, dBtnSize, dBtnSize, 10, Color::White);
  renderer.drawRoundedRect(upX, upY, dBtnSize, dBtnSize, 2, 10, true);
  renderer.drawLine(upX + dBtnSize / 2, upY + 18, upX + dBtnSize / 2 - 8, upY + 30, 2, true);
  renderer.drawLine(upX + dBtnSize / 2, upY + 18, upX + dBtnSize / 2 + 8, upY + 30, 2, true);

  // Nút Xuống [ ▼ ]
  const int downX = dpadCenterX - dBtnSize / 2;
  const int downY = dpadCenterY + dGap;
  renderer.fillRoundedRect(downX, downY, dBtnSize, dBtnSize, 10, Color::White);
  renderer.drawRoundedRect(downX, downY, dBtnSize, dBtnSize, 2, 10, true);
  renderer.drawLine(downX + dBtnSize / 2, downY + 36, downX + dBtnSize / 2 - 8, downY + 24, 2, true);
  renderer.drawLine(downX + dBtnSize / 2, downY + 36, downX + dBtnSize / 2 + 8, downY + 24, 2, true);

  // Nút Trái [ ◄ ]
  const int leftX = dpadCenterX - dBtnSize - dBtnSize / 2 - dGap;
  const int leftY = dpadCenterY - dBtnSize / 2;
  renderer.fillRoundedRect(leftX, leftY, dBtnSize, dBtnSize, 10, Color::White);
  renderer.drawRoundedRect(leftX, leftY, dBtnSize, dBtnSize, 2, 10, true);
  renderer.drawLine(leftX + 18, leftY + dBtnSize / 2, leftX + 30, leftY + dBtnSize / 2 - 8, 2, true);
  renderer.drawLine(leftX + 18, leftY + dBtnSize / 2, leftX + 30, leftY + dBtnSize / 2 + 8, 2, true);

  // Nút Phải [ ► ]
  const int rightX = dpadCenterX + dBtnSize / 2 + dGap;
  const int rightY = dpadCenterY - dBtnSize / 2;
  renderer.fillRoundedRect(rightX, rightY, dBtnSize, dBtnSize, 10, Color::White);
  renderer.drawRoundedRect(rightX, rightY, dBtnSize, dBtnSize, 2, 10, true);
  renderer.drawLine(rightX + 36, rightY + dBtnSize / 2, rightX + 24, rightY + dBtnSize / 2 - 8, 2, true);
  renderer.drawLine(rightX + 36, rightY + dBtnSize / 2, rightX + 24, rightY + dBtnSize / 2 + 8, 2, true);

  // Ghi chú hướng dẫn ở cuối
  const char* hint = "Vuốt màn hình bàn cờ hoặc chạm phím mũi tên để trượt ô";
  const int hw = renderer.getTextWidth(SMALL_FONT_ID, hint, EpdFontFamily::REGULAR);
  renderer.drawText(SMALL_FONT_ID, (screenW - hw) / 2, 720, hint, true, EpdFontFamily::REGULAR);
}

void Game2048Activity::handleTouch(const int x, const int y) {
  const int screenW = renderer.getScreenWidth();

  // 1. Nút Back (top-left squircle)
  if (x >= 14 && x <= 66 && y >= 8 && y <= 60) {
    activityManager.goToPlugins(false);
    return;
  }

  // 2. Chạm tiếp tục sau khi đạt 2048
  if (gameWon && !continuePlaying) {
    continuePlaying = true;
    requestUpdate(true);
    return;
  }

  // 3. Nút [ Chơi ván mới ]
  if (x >= 40 && x <= 220 && y >= 492 && y <= 538) {
    initNewGame();
    requestUpdate(true);
    return;
  }

  // 4. Nút [ Hoàn tác ]
  if (x >= screenW - 220 && x <= screenW - 40 && y >= 492 && y <= 538) {
    undo();
    return;
  }

  // 5. Cụm phím D-Pad
  const int dpadCenterY = 620;
  const int dpadCenterX = screenW / 2;
  const int dBtnSize = 54;
  const int dGap = 6;

  // Lên
  const int upX = dpadCenterX - dBtnSize / 2;
  const int upY = dpadCenterY - dBtnSize - dGap;
  if (x >= upX && x <= upX + dBtnSize && y >= upY && y <= upY + dBtnSize) {
    move(MappedInputManager::SwipeDir::Up);
    return;
  }

  // Xuống
  const int downX = dpadCenterX - dBtnSize / 2;
  const int downY = dpadCenterY + dGap;
  if (x >= downX && x <= downX + dBtnSize && y >= downY && y <= downY + dBtnSize) {
    move(MappedInputManager::SwipeDir::Down);
    return;
  }

  // Trái
  const int leftX = dpadCenterX - dBtnSize - dBtnSize / 2 - dGap;
  const int leftY = dpadCenterY - dBtnSize / 2;
  if (x >= leftX && x <= leftX + dBtnSize && y >= leftY && y <= leftY + dBtnSize) {
    move(MappedInputManager::SwipeDir::Left);
    return;
  }

  // Phải
  const int rightX = dpadCenterX + dBtnSize / 2 + dGap;
  const int rightY = dpadCenterY - dBtnSize / 2;
  if (x >= rightX && x <= rightX + dBtnSize && y >= rightY && y <= rightY + dBtnSize) {
    move(MappedInputManager::SwipeDir::Right);
    return;
  }
}

void Game2048Activity::loop() {
  // Nhận diện thao tác vuốt (Swipe) trực tiếp trên màn hình
  const auto swipe = mappedInput.wasSwipe();
  if (swipe != MappedInputManager::SwipeDir::None) {
    move(swipe);
    return;
  }

  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    handleTouch(tx, ty);
    return;
  }

  if (mappedInput.wasBackGesture() || mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToPlugins(false);
    return;
  }

  // Hỗ trợ phím cứng vật lý (nếu có)
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    move(MappedInputManager::SwipeDir::Up);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    move(MappedInputManager::SwipeDir::Down);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    move(MappedInputManager::SwipeDir::Left);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    move(MappedInputManager::SwipeDir::Right);
    return;
  }
}

void Game2048Activity::render(RenderLock&&) {
  renderer.clearScreen();
  renderHeader();
  renderBoard();
  renderControls();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
