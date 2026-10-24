#include "ChessActivity.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/chess/ChessPieceBitmaps.h"
#include "components/TouchHeaderBackButton.h"
#include "fontIds.h"

ChessActivity::ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("CHESS", renderer, mappedInput) {}

void ChessActivity::onEnter() {
  Activity::onEnter();
  status = engine.getGameStatus();
  requestUpdate(true);
}

int ChessActivity::toBoardIndex(int displayCol, int displayRow) const {
  if (playerIsWhite) {
    return displayRow * 8 + displayCol;
  } else {
    // Flipped board for Black perspective
    return (7 - displayRow) * 8 + (7 - displayCol);
  }
}

void ChessActivity::fromBoardIndex(int index, int& displayCol, int& displayRow) const {
  const int r = index / 8;
  const int c = index % 8;
  if (playerIsWhite) {
    displayCol = c;
    displayRow = r;
  } else {
    displayCol = 7 - c;
    displayRow = 7 - r;
  }
}

void ChessActivity::drawPieceBitmap(int x, int y, char piece, bool onDarkSquare) {
  const uint8_t* whiteBits = nullptr;
  const uint8_t* blackBits = nullptr;

  switch (piece) {
    case 'K': whiteBits = CHESS_WK_WHITE; blackBits = CHESS_WK_BLACK; break;
    case 'Q': whiteBits = CHESS_WQ_WHITE; blackBits = CHESS_WQ_BLACK; break;
    case 'R': whiteBits = CHESS_WR_WHITE; blackBits = CHESS_WR_BLACK; break;
    case 'B': whiteBits = CHESS_WB_WHITE; blackBits = CHESS_WB_BLACK; break;
    case 'N': whiteBits = CHESS_WN_WHITE; blackBits = CHESS_WN_BLACK; break;
    case 'P': whiteBits = CHESS_WP_WHITE; blackBits = CHESS_WP_BLACK; break;
    case 'k': whiteBits = CHESS_BK_WHITE; blackBits = CHESS_BK_BLACK; break;
    case 'q': whiteBits = CHESS_BQ_WHITE; blackBits = CHESS_BQ_BLACK; break;
    case 'r': whiteBits = CHESS_BR_WHITE; blackBits = CHESS_BR_BLACK; break;
    case 'b': whiteBits = CHESS_BB_WHITE; blackBits = CHESS_BB_BLACK; break;
    case 'n': whiteBits = CHESS_BN_WHITE; blackBits = CHESS_BN_BLACK; break;
    case 'p': whiteBits = CHESS_BP_WHITE; blackBits = CHESS_BP_BLACK; break;
    default: return;
  }

  for (int r = 0; r < 32; ++r) {
    for (int b = 0; b < 4; ++b) {
      const uint8_t wByte = whiteBits[r * 4 + b];
      const uint8_t bByte = blackBits[r * 4 + b];
      for (int bit = 0; bit < 8; ++bit) {
        const int px = x + b * 8 + bit;
        const int py = y + r;
        if ((wByte >> (7 - bit)) & 1) {
          renderer.drawPixel(px, py, false);  // white
        } else if ((bByte >> (7 - bit)) & 1) {
          renderer.drawPixel(px, py, true);   // black
        }
      }
    }
  }
}

void ChessActivity::drawHeader() {
  const int screenW = renderer.getScreenWidth();

  // Nút quay lại chuẩn Squircle OS (đồng nhất toàn hệ thống)
  const int btnX = 14;
  const int btnY = 12;
  const int btnSize = 40;
  TouchHeaderBackButton::drawSquircleBackButton(renderer, btnX, btnY, btnSize, 10);

  // Tiêu đề chính giữa trang nhã, không mở ngoặc
  const char* title = "Chess";
  const int tw = renderer.getTextWidth(NOTOSANS_16_FONT_ID, title, EpdFontFamily::BOLD);
  renderer.drawText(NOTOSANS_16_FONT_ID, (screenW - tw) / 2, 21, title, true, EpdFontFamily::BOLD);

  // Huy hiệu góc phải: Lượt đi
  const char* turnBadge = engine.isWhiteTurn() ? "White's turn" : "Black's turn";
  const int bw = renderer.getTextWidth(UI_10_FONT_ID, turnBadge, EpdFontFamily::BOLD);
  const int badgeW = bw + 18;
  const int badgeH = 32;
  const int badgeX = screenW - badgeW - 14;
  const int badgeY = 16;
  if (engine.isWhiteTurn()) {
    renderer.fillRoundedRect(badgeX, badgeY, badgeW, badgeH, 10, Color::White);
    renderer.drawRoundedRect(badgeX, badgeY, badgeW, badgeH, 1, 10, true);
    renderer.drawText(UI_10_FONT_ID, badgeX + 9, badgeY + 8, turnBadge, true, EpdFontFamily::BOLD);
  } else {
    renderer.fillRoundedRect(badgeX, badgeY, badgeW, badgeH, 10, Color::Black);
    renderer.drawText(UI_10_FONT_ID, badgeX + 9, badgeY + 8, turnBadge, false, EpdFontFamily::BOLD);
  }
}

void ChessActivity::drawBoard() {
  // Khung viền bàn cờ
  renderer.drawRect(BOARD_X - 1, BOARD_Y - 1, BOARD_SIZE + 2, BOARD_SIZE + 2, true);

  // Ký hiệu cột (a-h) và dòng (1-8)
  for (int i = 0; i < 8; ++i) {
    char fileChar = playerIsWhite ? ('a' + i) : ('h' - i);
    char rankChar = playerIsWhite ? ('8' - i) : ('1' + i);
    char buf[2] = {fileChar, '\0'};
    char rbuf[2] = {rankChar, '\0'};

    // File dưới đáy bàn cờ
    const int fx = BOARD_X + i * CELL_SIZE + (CELL_SIZE - 6) / 2;
    renderer.drawText(SMALL_FONT_ID, fx, BOARD_Y + BOARD_SIZE + 4, buf, true, EpdFontFamily::REGULAR);

    // Rank bên trái bàn cờ
    const int ry = BOARD_Y + i * CELL_SIZE + (CELL_SIZE - 10) / 2;
    renderer.drawText(SMALL_FONT_ID, BOARD_X - 16, ry, rbuf, true, EpdFontFamily::REGULAR);
  }

  // Vẽ từng ô cờ 8x8
  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
      const int x = BOARD_X + c * CELL_SIZE;
      const int y = BOARD_Y + r * CELL_SIZE;
      const bool isDark = (r + c) % 2 == 1;

      // Viền ô cờ
      renderer.drawRect(x, y, CELL_SIZE, CELL_SIZE, true);

      // Ô tối: Vẽ hoa văn caro / chấm bi nhẹ để tạo độ tương phản cao trên e-paper
      if (isDark) {
        for (int py = y + 1; py < y + CELL_SIZE - 1; py += 3) {
          for (int px = x + 1; px < x + CELL_SIZE - 1; px += 3) {
            renderer.drawPixel(px + ((py / 3) % 2), py, true);
          }
        }
      }
    }
  }
}

void ChessActivity::drawHighlights() {
  // 1. Nước đi vừa thực hiện (Last move highlight)
  const chess::Move* last = engine.getLastMove();
  if (last != nullptr) {
    int fc, fr, tc, tr;
    fromBoardIndex(last->from, fc, fr);
    fromBoardIndex(last->to, tc, tr);

    const int fx = BOARD_X + fc * CELL_SIZE;
    const int fy = BOARD_Y + fr * CELL_SIZE;
    const int tx = BOARD_X + tc * CELL_SIZE;
    const int ty = BOARD_Y + tr * CELL_SIZE;

    // Vẽ góc vuông đánh dấu ô xuất phát
    renderer.drawLine(fx + 2, fy + 2, fx + 10, fy + 2, true);
    renderer.drawLine(fx + 2, fy + 2, fx + 2, fy + 10, true);
    renderer.drawLine(fx + CELL_SIZE - 3, fy + 2, fx + CELL_SIZE - 11, fy + 2, true);
    renderer.drawLine(fx + CELL_SIZE - 3, fy + 2, fx + CELL_SIZE - 3, fy + 10, true);

    // Vẽ góc vuông đánh dấu ô đích
    renderer.drawLine(tx + 2, ty + 2, tx + 10, ty + 2, true);
    renderer.drawLine(tx + 2, ty + 2, tx + 2, ty + 10, true);
    renderer.drawLine(tx + CELL_SIZE - 3, ty + 2, tx + CELL_SIZE - 11, ty + 2, true);
    renderer.drawLine(tx + CELL_SIZE - 3, ty + 2, tx + CELL_SIZE - 3, ty + 10, true);
    renderer.drawLine(tx + 2, ty + CELL_SIZE - 3, tx + 10, ty + CELL_SIZE - 3, true);
    renderer.drawLine(tx + 2, ty + CELL_SIZE - 3, tx + 2, ty + CELL_SIZE - 11, true);
    renderer.drawLine(tx + CELL_SIZE - 3, ty + CELL_SIZE - 3, tx + CELL_SIZE - 11, ty + CELL_SIZE - 3, true);
    renderer.drawLine(tx + CELL_SIZE - 3, ty + CELL_SIZE - 3, tx + CELL_SIZE - 3, ty + CELL_SIZE - 11, true);
  }

  // 2. Ô đang được chọn (Selected square)
  if (selectedSquare >= 0) {
    int selC, selR;
    fromBoardIndex(selectedSquare, selC, selR);
    const int sx = BOARD_X + selC * CELL_SIZE;
    const int sy = BOARD_Y + selR * CELL_SIZE;

    // Viền đậm 3 lớp nổi bật
    renderer.drawRect(sx + 1, sy + 1, CELL_SIZE - 2, CELL_SIZE - 2, true);
    renderer.drawRect(sx + 2, sy + 2, CELL_SIZE - 4, CELL_SIZE - 4, true);
    renderer.drawRect(sx + 3, sy + 3, CELL_SIZE - 6, CELL_SIZE - 6, true);
  }

  // 3. Các ô đích có thể đi (Legal move target dots & brackets)
  for (int i = 0; i < currentLegalMoveCount; ++i) {
    const chess::Move& m = currentLegalMoves[i];
    int tc, tr;
    fromBoardIndex(m.to, tc, tr);
    const int tx = BOARD_X + tc * CELL_SIZE;
    const int ty = BOARD_Y + tr * CELL_SIZE;

    if (m.pieceCaptured == ' ') {
      // Ô trống: vẽ chấm tròn đen đường kính 10px ở tâm
      constexpr int dotSize = 10;
      renderer.fillRoundedRect(tx + (CELL_SIZE - dotSize) / 2, ty + (CELL_SIZE - dotSize) / 2, dotSize, dotSize,
                              dotSize / 2, Color::Black);
    } else {
      // Ô có quân đối phương: vẽ khung bo góc đậm bao quanh quân ăn
      renderer.drawRoundedRect(tx + 2, ty + 2, CELL_SIZE - 4, CELL_SIZE - 4, 2, 8, true);
    }
  }
}

void ChessActivity::drawPieces() {
  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
      const int boardIdx = toBoardIndex(c, r);
      const char piece = engine.getPiece(boardIdx);
      if (piece != ' ') {
        const int px = BOARD_X + c * CELL_SIZE + (CELL_SIZE - 32) / 2;
        const int py = BOARD_Y + r * CELL_SIZE + (CELL_SIZE - 32) / 2;
        const bool isDark = (r + c) % 2 == 1;
        drawPieceBitmap(px, py, piece, isDark);
      }
    }
  }
}

void ChessActivity::drawControls() {
  const int screenW = renderer.getScreenWidth();

  // 1. Khung trạng thái trận đấu (Status Bar)
  const int statusY = 512;
  const int statusH = 38;
  const int statusX = 24;
  const int statusW = screenW - 48;
  renderer.fillRoundedRect(statusX, statusY, statusW, statusH, 10, Color::White);
  renderer.drawRoundedRect(statusX, statusY, statusW, statusH, 1, 10, true);

  char statusText[64];
  if (status == chess::GameStatus::CheckmateWhiteWins) {
    snprintf(statusText, sizeof(statusText), "Checkmate - White wins!");
  } else if (status == chess::GameStatus::CheckmateBlackWins) {
    snprintf(statusText, sizeof(statusText), "Checkmate - Black wins!");
  } else if (status == chess::GameStatus::Stalemate) {
    snprintf(statusText, sizeof(statusText), "Draw - Stalemate");
  } else if (status == chess::GameStatus::Check) {
    snprintf(statusText, sizeof(statusText), "Check! %s king under attack", engine.isWhiteTurn() ? "White" : "Black");
  } else if (isAiThinking) {
    snprintf(statusText, sizeof(statusText), "AI is calculating move...");
  } else {
    if (difficulty == Difficulty::TwoPlayer) {
      snprintf(statusText, sizeof(statusText), "%s to move", engine.isWhiteTurn() ? "White" : "Black");
    } else {
      const bool isPlayerTurn = (engine.isWhiteTurn() == playerIsWhite);
      snprintf(statusText, sizeof(statusText), isPlayerTurn ? "Your turn" : "AI turn");
    }
  }
  const int stw = renderer.getTextWidth(UI_10_FONT_ID, statusText, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, (screenW - stw) / 2, statusY + 10, statusText, true, EpdFontFamily::BOLD);

  // 2. Hàng nút chức năng 1: [ New Game ] [ Undo ] [ Side: White/Black ]
  const int r1Y = 560;
  const int btnH = 42;
  const int btnW = 136;
  const int b1X = 24;
  const int b2X = 172;
  const int b3X = 320;

  renderer.fillRoundedRect(b1X, r1Y, btnW, btnH, 10, Color::White);
  renderer.drawRoundedRect(b1X, r1Y, btnW, btnH, 1, 10, true);
  const char* t1 = "New Game";
  const int tw1 = renderer.getTextWidth(UI_10_FONT_ID, t1, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, b1X + (btnW - tw1) / 2, r1Y + 11, t1, true, EpdFontFamily::REGULAR);

  renderer.fillRoundedRect(b2X, r1Y, btnW, btnH, 10, Color::White);
  renderer.drawRoundedRect(b2X, r1Y, btnW, btnH, 1, 10, true);
  const char* t2 = "Undo";
  const int tw2 = renderer.getTextWidth(UI_10_FONT_ID, t2, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, b2X + (btnW - tw2) / 2, r1Y + 11, t2, true, EpdFontFamily::REGULAR);

  renderer.fillRoundedRect(b3X, r1Y, btnW, btnH, 10, Color::White);
  renderer.drawRoundedRect(b3X, r1Y, btnW, btnH, 1, 10, true);
  const char* t3 = playerIsWhite ? "Side: White" : "Side: Black";
  const int tw3 = renderer.getTextWidth(UI_10_FONT_ID, t3, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, b3X + (btnW - tw3) / 2, r1Y + 11, t3, true, EpdFontFamily::REGULAR);

  // 3. Hàng nút 2: Chế độ chơi & Độ khó (Không dùng ngoặc, dùng chuẩn tiếng Anh thanh lịch)
  const int r2Y = 612;
  const int r2W = statusW;
  renderer.fillRoundedRect(statusX, r2Y, r2W, btnH, 10, Color::White);
  renderer.drawRoundedRect(statusX, r2Y, r2W, btnH, 1, 10, true);

  const char* diffText = "Mode: AI Medium";
  switch (difficulty) {
    case Difficulty::Easy: diffText = "Mode: AI Easy"; break;
    case Difficulty::Medium: diffText = "Mode: AI Medium"; break;
    case Difficulty::Hard: diffText = "Mode: AI Hard"; break;
    case Difficulty::TwoPlayer: diffText = "Mode: Pass & Play"; break;
  }
  const int dtw = renderer.getTextWidth(UI_10_FONT_ID, diffText, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, (screenW - dtw) / 2, r2Y + 11, diffText, true, EpdFontFamily::REGULAR);

  // 4. Quân đã ăn (Captured pieces)
  const int capY = 666;
  const auto& capW = engine.getCapturedByWhite();
  const auto& capB = engine.getCapturedByBlack();

  char capWBuf[64] = "White captured: ";
  for (char p : capW) {
    const size_t len = strlen(capWBuf);
    if (len + 2 < sizeof(capWBuf)) {
      capWBuf[len] = static_cast<char>(std::toupper(p));
      capWBuf[len + 1] = ' ';
      capWBuf[len + 2] = '\0';
    }
  }
  if (capW.empty()) strcat(capWBuf, "-");

  char capBBuf[64] = "Black captured: ";
  for (char p : capB) {
    const size_t len = strlen(capBBuf);
    if (len + 2 < sizeof(capBBuf)) {
      capBBuf[len] = static_cast<char>(std::toupper(p));
      capBBuf[len + 1] = ' ';
      capBBuf[len + 2] = '\0';
    }
  }
  if (capB.empty()) strcat(capBBuf, "-");

  renderer.drawText(SMALL_FONT_ID, statusX + 4, capY, capWBuf, true, EpdFontFamily::REGULAR);
  renderer.drawText(SMALL_FONT_ID, statusX + 4, capY + 18, capBBuf, true, EpdFontFamily::REGULAR);

  // 5. Thanh phân cách và gợi ý chạm tinh gọn ở đáy màn hình
  renderer.drawLine(statusX, 712, statusX + statusW, 712, true);
  const char* hint = "Tap piece to select · Tap circle to move";
  const int htw = renderer.getTextWidth(SMALL_FONT_ID, hint, EpdFontFamily::REGULAR);
  renderer.drawText(SMALL_FONT_ID, (screenW - htw) / 2, 726, hint, true, EpdFontFamily::REGULAR);
}

void ChessActivity::triggerAiMove() {
  if (difficulty == Difficulty::TwoPlayer) return;
  const bool isPlayerTurn = (engine.isWhiteTurn() == playerIsWhite);
  if (isPlayerTurn) return;

  int depth = 3;
  if (difficulty == Difficulty::Easy) depth = 2;
  if (difficulty == Difficulty::Hard) depth = 4;

  const chess::Move best = engine.findBestMove(depth);
  if (best.isValid()) {
    engine.makeMove(best);
  }
  isAiThinking = false;
  status = engine.getGameStatus();
  requestUpdate(true);
}

void ChessActivity::handleTouch(const int x, const int y) {
  const int screenW = renderer.getScreenWidth();

  // 1. Nút quay lại (Top-left)
  if (x >= 14 && x <= 64 && y >= 12 && y <= 62) {
    activityManager.goToPlugins(false);
    return;
  }

  // 2. Chạm vào bàn cờ (x: 32 đến 448, y: 74 đến 490)
  if (x >= BOARD_X && x < BOARD_X + BOARD_SIZE && y >= BOARD_Y && y < BOARD_Y + BOARD_SIZE) {
    const int c = (x - BOARD_X) / CELL_SIZE;
    const int r = (y - BOARD_Y) / CELL_SIZE;
    const int sq = toBoardIndex(c, r);

    // Nếu đã chọn một quân cờ trước đó:
    if (selectedSquare >= 0) {
      // Kiểm tra xem ô chạm có phải nước đi hợp lệ không
      bool isLegal = false;
      chess::Move chosenMove;
      for (int i = 0; i < currentLegalMoveCount; ++i) {
        if (currentLegalMoves[i].to == sq) {
          isLegal = true;
          chosenMove = currentLegalMoves[i];
          break;
        }
      }

      if (isLegal) {
        engine.makeMove(chosenMove);
        selectedSquare = -1;
        currentLegalMoveCount = 0;
        status = engine.getGameStatus();
        requestUpdate(true);

        // Kích hoạt lượt AI nếu đang đấu với máy
        if (difficulty != Difficulty::TwoPlayer && status == chess::GameStatus::Ongoing) {
          isAiThinking = true;
          triggerAiMove();
        }
        return;
      }

      // Nếu chạm vào quân khác của phe mình -> Đổi chọn quân đó
      const char targetPiece = engine.getPiece(sq);
      const bool isWhiteTurn = engine.isWhiteTurn();
      const bool isOwnPiece = targetPiece != ' ' && ((isWhiteTurn && targetPiece >= 'A' && targetPiece <= 'Z') ||
                                                    (!isWhiteTurn && targetPiece >= 'a' && targetPiece <= 'z'));
      if (isOwnPiece) {
        selectedSquare = sq;
        currentLegalMoveCount = engine.getLegalMovesFrom(sq, currentLegalMoves, 32);
        requestUpdate(true);
        return;
      }

      // Chạm ra ngoài hoặc ô không hợp lệ -> Bỏ chọn
      selectedSquare = -1;
      currentLegalMoveCount = 0;
      requestUpdate(true);
      return;
    }

    // Chưa chọn quân: Chọn quân tại ô chạm nếu đúng lượt
    const char piece = engine.getPiece(sq);
    const bool isWhiteTurn = engine.isWhiteTurn();
    const bool isOwnPiece = piece != ' ' && ((isWhiteTurn && piece >= 'A' && piece <= 'Z') ||
                                            (!isWhiteTurn && piece >= 'a' && piece <= 'z'));

    // Khi đấu với máy, người chơi chỉ được chọn quân của phe mình
    if (difficulty != Difficulty::TwoPlayer) {
      if (isWhiteTurn != playerIsWhite) {
        return;  // Đang là lượt của máy
      }
    }

    if (isOwnPiece) {
      selectedSquare = sq;
      currentLegalMoveCount = engine.getLegalMovesFrom(sq, currentLegalMoves, 32);
      requestUpdate(true);
      return;
    }
  }

  // 3. Nút [ New Game ]
  if (x >= 24 && x <= 160 && y >= 558 && y <= 606) {
    engine.reset();
    selectedSquare = -1;
    currentLegalMoveCount = 0;
    isAiThinking = false;
    status = chess::GameStatus::Ongoing;
    if (!playerIsWhite && difficulty != Difficulty::TwoPlayer) {
      triggerAiMove();
    }
    requestUpdate(true);
    return;
  }

  // 4. Nút [ Undo ]
  if (x >= 172 && x <= 308 && y >= 558 && y <= 606) {
    if (difficulty != Difficulty::TwoPlayer) {
      // Đi lại 2 nước (nước của máy và nước của mình)
      engine.undoMove();
      engine.undoMove();
    } else {
      engine.undoMove();
    }
    selectedSquare = -1;
    currentLegalMoveCount = 0;
    isAiThinking = false;
    status = engine.getGameStatus();
    requestUpdate(true);
    return;
  }

  // 5. Nút [ Side: White / Black ]
  if (x >= 320 && x <= 456 && y >= 558 && y <= 606) {
    playerIsWhite = !playerIsWhite;
    selectedSquare = -1;
    currentLegalMoveCount = 0;
    if (!playerIsWhite && engine.isWhiteTurn() && difficulty != Difficulty::TwoPlayer) {
      triggerAiMove();
    }
    requestUpdate(true);
    return;
  }

  // 6. Nút [ Mode / Difficulty ]
  if (x >= 24 && x <= screenW - 24 && y >= 610 && y <= 658) {
    switch (difficulty) {
      case Difficulty::Easy: difficulty = Difficulty::Medium; break;
      case Difficulty::Medium: difficulty = Difficulty::Hard; break;
      case Difficulty::Hard: difficulty = Difficulty::TwoPlayer; break;
      case Difficulty::TwoPlayer: difficulty = Difficulty::Easy; break;
    }
    requestUpdate(true);
    return;
  }
}

void ChessActivity::loop() {
  int touchX = 0, touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    handleTouch(touchX, touchY);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToPlugins(false);
    return;
  }
}

void ChessActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawHeader();
  drawBoard();
  drawHighlights();
  drawPieces();
  drawControls();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
