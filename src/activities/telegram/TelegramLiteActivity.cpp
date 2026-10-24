#include "TelegramLiteActivity.h"

#include <ArduinoJson.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "activities/ActivityManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/icons/telegramIcons.h"
#include "components/icons/touchHeaderIcons.h"
#include "fontIds.h"

namespace {

static void drawFreeInkIcon(const GfxRenderer& renderer, const freeink::Icon& icon, int x, int y) {
  const int bytesPerRow = (icon.w + 7) / 8;
  for (int r = 0; r < icon.h; ++r) {
    for (int c = 0; c < icon.w; ++c) {
      uint8_t byteVal = icon.bits[r * bytesPerRow + (c / 8)];
      bool isBlack = ((byteVal & (0x80 >> (c % 8))) == 0);
      if (isBlack) {
        renderer.drawPixel(x + c, y + r, true);
      }
    }
  }
}

static const char* kConfigPath = "/.crosspoint/telegram.json";

// Gọi HTTP GET đơn giản
static std::string telegramHttpGet(const std::string& url) {
#if DEVICE_SIMULATOR
  std::string cmd = "curl -s -m 5 \"" + url + "\"";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return "";
  char buffer[512];
  std::string result;
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result += buffer;
  }
  pclose(pipe);
  return result;
#else
  // Cho thiết bị ESP32 phần cứng khi có Wi-Fi
  return "";
#endif
}

// Gọi HTTP POST gửi tin nhắn
static bool telegramHttpPost(const std::string& url, const std::string& jsonBody) {
#if DEVICE_SIMULATOR
  // Thoát ký tự nháy kép cho command
  std::string cmd = "curl -s -m 5 -X POST -H \"Content-Type: application/json\" -d '" + jsonBody + "' \"" + url + "\"";
  int ret = system(cmd.c_str());
  return ret == 0;
#else
  return false;
#endif
}

}  // namespace

TelegramLiteActivity::TelegramLiteActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("TelegramLite", renderer, mappedInput) {}

void TelegramLiteActivity::onEnter() {
  Activity::onEnter();
  renderer.clearScreen();

  loadConfig();

  if (botToken.empty()) {
    isSetupMode = true;
  } else {
    isSetupMode = false;
    checkBotInfo();
    fetchUpdates();
  }

  requestUpdate();
}

void TelegramLiteActivity::onExit() {
  Activity::onExit();
}

void TelegramLiteActivity::loadConfig() {
  HalFile file;
  if (Storage.openFileForRead("TELEGRAM", kConfigPath, file)) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (!err) {
      std::string t = doc["bot_token"] | "";
      if (!t.empty()) botToken = t;
      chatId = doc["chat_id"] | "";
      botUsername = doc["bot_username"] | "crossbasictestbot";
      botFirstName = doc["bot_first_name"] | "Test xteink";
    }
  }
}

void TelegramLiteActivity::saveConfig() {
  HalFile file;
  if (Storage.openFileForWrite("TELEGRAM", kConfigPath, file)) {
    JsonDocument doc;
    doc["bot_token"] = botToken;
    doc["chat_id"] = chatId;
    doc["bot_username"] = botUsername;
    doc["bot_first_name"] = botFirstName;
    serializeJson(doc, file);
    file.close();
  }
}

void TelegramLiteActivity::checkBotInfo() {
  if (botToken.empty()) return;

  std::string url = "https://api.telegram.org/bot" + botToken + "/getMe";
  std::string resp = telegramHttpGet(url);
  if (!resp.empty()) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp);
    if (!err && doc["ok"] == true) {
      botFirstName = doc["result"]["first_name"] | "Test xteink";
      botUsername = doc["result"]["username"] | "crossbasictestbot";
      connectionStatus = "Trực tuyến";
      saveConfig();
    }
  }
}

void TelegramLiteActivity::fetchUpdates() {
  if (botToken.empty()) return;

  std::string url = "https://api.telegram.org/bot" + botToken + "/getUpdates?limit=20";
  if (lastUpdateId > 0) {
    url += "&offset=" + std::to_string(lastUpdateId + 1);
  }

  std::string resp = telegramHttpGet(url);
  if (resp.empty()) {
    connectionStatus = "Đã kết nối";
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err || doc["ok"] != true) {
    return;
  }

  JsonArray updates = doc["result"].as<JsonArray>();
  for (JsonObject u : updates) {
    int64_t uid = u["update_id"];
    if (uid > lastUpdateId) {
      lastUpdateId = uid;
    }

    JsonObject msg = u["message"];
    if (!msg.isNull()) {
      std::string text = msg["text"] | "";
      if (!text.empty()) {
        std::string sender = msg["from"]["first_name"] | "Người dùng";
        int64_t fromId = msg["from"]["id"];
        chatId = std::to_string(fromId);

        // Format giờ gửi từ timestamp
        time_t rawTime = msg["date"] | 0;
        char timeBuf[16] = "10:30";
        if (rawTime > 0) {
          struct tm* ti = localtime(&rawTime);
          if (ti) {
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", ti->tm_hour, ti->tm_min);
          }
        }

        TelegramMessage newMsg;
        newMsg.messageId = msg["message_id"] | (int64_t)(messages.size() + 1);
        newMsg.sender = sender;
        newMsg.text = text;
        newMsg.timeStr = timeBuf;
        newMsg.isOutgoing = false;

        messages.push_back(newMsg);
      }
    }
  }

  connectionStatus = "Trực tuyến · Đã đồng bộ";
  saveConfig();
  requestUpdate();
}

void TelegramLiteActivity::sendMessage(const std::string& text) {
  if (text.empty() || botToken.empty()) return;

  // Lấy thời gian hiện tại
  uint16_t y = 2026;
  uint8_t m = 10, d = 23, h = 10, min = 55;
  if (halClock.isAvailable()) {
    halClock.getDateTime(y, m, d, h, min);
  }
  char timeBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", h, min);

  // Thêm ngay tin nhắn vào danh sách cục bộ
  TelegramMessage newMsg;
  newMsg.messageId = messages.size() + 1;
  newMsg.sender = "Bạn";
  newMsg.text = text;
  newMsg.timeStr = timeBuf;
  newMsg.isOutgoing = true;
  messages.push_back(newMsg);

  // Gửi API thật nếu đã có chatId
  if (!chatId.empty()) {
    std::string url = "https://api.telegram.org/bot" + botToken + "/sendMessage";
    std::string jsonBody = "{\"chat_id\":\"" + chatId + "\",\"text\":\"" + text + "\"}";
    telegramHttpPost(url, jsonBody);
  }

  requestUpdate();
}

void TelegramLiteActivity::openTokenInput() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Nhập Telegram Bot Token", botToken),
      [this](const ActivityResult& result) {
        if (const auto* kr = std::get_if<KeyboardResult>(&result.data)) {
          if (!result.isCancelled && !kr->text.empty()) {
            botToken = kr->text;
            saveConfig();
            isSetupMode = false;
            checkBotInfo();
            fetchUpdates();
          }
        }
      });
}

void TelegramLiteActivity::openChatIdInput() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Nhập Telegram Chat ID", chatId),
      [this](const ActivityResult& result) {
        if (const auto* kr = std::get_if<KeyboardResult>(&result.data)) {
          if (!result.isCancelled && !kr->text.empty()) {
            chatId = kr->text;
            saveConfig();
            requestUpdate();
          }
        }
      });
}

void TelegramLiteActivity::openComposeInput() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Soạn tin nhắn Telegram", ""),
      [this](const ActivityResult& result) {
        if (const auto* kr = std::get_if<KeyboardResult>(&result.data)) {
          if (!result.isCancelled && !kr->text.empty()) {
            sendMessage(kr->text);
          }
        }
      });
}

void TelegramLiteActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (isSetupMode && !botToken.empty()) {
      isSetupMode = false;
      requestUpdate();
      return;
    }
    activityManager.goToPlugins(false);
    return;
  }

  // Vuốt dọc cuộn tin nhắn
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    scrollOffset = std::min(scrollOffset + 120, (int)messages.size() * 100);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    scrollOffset = std::max(0, scrollOffset - 120);
    requestUpdate();
    return;
  }

  int touchX = 0, touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    const int screenWidth = renderer.getScreenWidth();
    const int screenHeight = renderer.getScreenHeight();

    // 1. Nút Back góc trái trên
    if (touchX < 55 && touchY < 52) {
      if (isSetupMode && !botToken.empty()) {
        isSetupMode = false;
        requestUpdate();
        return;
      }
      activityManager.goToPlugins(false);
      return;
    }

    if (isSetupMode) {
      // Nút [ Cập Nhật Bot Token ]
      if (touchX >= 24 && touchX <= screenWidth - 24 && touchY >= 470 && touchY <= 520) {
        openTokenInput();
        return;
      }
      // Nút [ Vào Hộp Tin Nhắn ]
      if (touchX >= 24 && touchX <= screenWidth - 24 && touchY >= 535 && touchY <= 585) {
        isSetupMode = false;
        fetchUpdates();
        requestUpdate();
        return;
      }
    } else {
      // Header controls: Nút Làm mới (X: 300..380), Nút Cài đặt (X: 390..470)
      if (touchY >= 8 && touchY <= 44) {
        if (touchX >= screenWidth - 170 && touchX <= screenWidth - 95) {
          fetchUpdates();
          return;
        }
        if (touchX >= screenWidth - 90 && touchX <= screenWidth - 10) {
          isSetupMode = true;
          requestUpdate();
          return;
        }
      }

      // Nút [ 💬 Soạn tin nhắn gửi đi... ] ở đáy màn hình
      if (touchY >= screenHeight - 65 && touchY <= screenHeight - 12) {
        openComposeInput();
        return;
      }
    }
  }
}

void TelegramLiteActivity::renderSetupScreen() {
  const int screenWidth = renderer.getScreenWidth();

  // Header Clean
  renderer.drawLine(0, 52, screenWidth, 52, true);
  TouchHeaderBackButton::drawSquircleBackButton(renderer, 10, 8, 36, 10);
  renderer.drawText(LEXENDDECA_14_FONT_ID, 56, 15, "Cài Đặt Telegram Bot", true, EpdFontFamily::BOLD);

  // Icon Telegram to rõ ở giữa
  drawFreeInkIcon(renderer, icon_telegram_32, (screenWidth - 32) / 2, 80);

  // Tiêu đề & Giới thiệu
  const char* titleStr = "TELEGRAM LITE";
  int tTw = renderer.getTextWidth(LEXENDDECA_16_FONT_ID, titleStr, EpdFontFamily::BOLD);
  renderer.drawText(LEXENDDECA_16_FONT_ID, (screenWidth - tTw) / 2, 125, titleStr, true, EpdFontFamily::BOLD);

  char subStr[64];
  snprintf(subStr, sizeof(subStr), "Bot: @%s (%s)", botUsername.c_str(), botFirstName.c_str());
  int sTw = renderer.getTextWidth(UI_10_FONT_ID, subStr, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, (screenWidth - sTw) / 2, 155, subStr, true, EpdFontFamily::BOLD);

  // Khung thông tin cấu hình
  const int cardX = 20;
  const int cardY = 190;
  const int cardW = screenWidth - (cardX * 2);
  const int cardH = 260;

  renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 1, 14, true);

  renderer.drawText(LEXENDDECA_12_FONT_ID, cardX + 16, cardY + 16, "Thông Tin Kết Nối Hiện Tại:", true, EpdFontFamily::BOLD);
  renderer.drawLine(cardX + 16, cardY + 42, cardX + cardW - 16, cardY + 42, true);

  char tokMasked[64];
  if (botToken.length() > 16) {
    snprintf(tokMasked, sizeof(tokMasked), "Token: %s...%s", botToken.substr(0, 8).c_str(),
             botToken.substr(botToken.length() - 6).c_str());
  } else {
    snprintf(tokMasked, sizeof(tokMasked), "Token: %s", botToken.empty() ? "(Chưa có)" : botToken.c_str());
  }
  renderer.drawText(UI_10_FONT_ID, cardX + 16, cardY + 56, tokMasked, true, EpdFontFamily::REGULAR);

  char chatBuf[64];
  snprintf(chatBuf, sizeof(chatBuf), "Chat ID: %s", chatId.empty() ? "(Tự động nhận khi bạn gửi tin)" : chatId.c_str());
  renderer.drawText(UI_10_FONT_ID, cardX + 16, cardY + 84, chatBuf, true, EpdFontFamily::REGULAR);

  renderer.drawLine(cardX + 16, cardY + 114, cardX + cardW - 16, cardY + 114, true);

  renderer.drawText(LEXENDDECA_12_FONT_ID, cardX + 16, cardY + 126, "Cách Nhận Tin Nhắn:", true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, cardX + 16, cardY + 152, "1. Mở Telegram trên điện thoại.", true, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, cardX + 16, cardY + 176, "2. Tìm @crossbasictestbot.", true, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, cardX + 16, cardY + 200, "3. Nhấn Start hoặc gửi một tin nhắn bất kỳ.", true, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, cardX + 16, cardY + 224, "4. Quay lại đây và bấm [ Làm mới ]!", true, EpdFontFamily::BOLD);

  // Nút 1: [ Cập Nhật Token Mới ]
  const int btn1Y = 470;
  const int btnW = cardW;
  renderer.drawRoundedRect(cardX, btn1Y, btnW, 46, 1, 10, true);
  const char* b1Str = "Đổi Bot Token Khác";
  int b1Tw = renderer.getTextWidth(UI_10_FONT_ID, b1Str, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, cardX + (btnW - b1Tw) / 2, btn1Y + 14, b1Str, true, EpdFontFamily::REGULAR);

  // Nút 2: [ Vào Hộp Tin Nhắn ]
  const int btn2Y = 530;
  renderer.fillRoundedRect(cardX, btn2Y, btnW, 46, 10, Color::Black);
  const char* b2Str = "Vào Hộp Tin Nhắn Ngay";
  int b2Tw = renderer.getTextWidth(UI_10_FONT_ID, b2Str, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, cardX + (btnW - b2Tw) / 2, btn2Y + 14, b2Str, false, EpdFontFamily::BOLD);
}

void TelegramLiteActivity::renderChatScreen() {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  // 1. Header Clean
  renderer.drawLine(0, 52, screenWidth, 52, true);
  TouchHeaderBackButton::drawSquircleBackButton(renderer, 10, 8, 36, 10);

  // Tiêu đề Chat & Trạng thái
  std::string title = "@" + botUsername;
  renderer.drawText(LEXENDDECA_12_FONT_ID, 54, 10, title.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, 54, 30, connectionStatus.c_str(), true, EpdFontFamily::REGULAR);

  // Nút [ Làm mới ]
  const int refBtnW = 76;
  const int refBtnX = screenWidth - refBtnW - 86;
  renderer.drawRoundedRect(refBtnX, 10, refBtnW, 32, 1, 8, true);
  int rtw = renderer.getTextWidth(UI_10_FONT_ID, "Làm mới", EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, refBtnX + (refBtnW - rtw) / 2, 16, "Làm mới", true, EpdFontFamily::REGULAR);

  // Nút [ Cài đặt ]
  const int setBtnW = 74;
  const int setBtnX = screenWidth - setBtnW - 8;
  renderer.drawRoundedRect(setBtnX, 10, setBtnW, 32, 1, 8, true);
  int stw = renderer.getTextWidth(UI_10_FONT_ID, "Cài đặt", EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, setBtnX + (setBtnW - stw) / 2, 16, "Cài đặt", true, EpdFontFamily::REGULAR);

  // 2. Vùng Tin Nhắn
  const int maxContentY = screenHeight - 75;

  if (messages.empty()) {
    // Chưa có tin nhắn -> Hiển thị màn hình chờ kết nối
    const int boxX = 24;
    const int boxY = 120;
    const int boxW = screenWidth - 48;
    const int boxH = 280;

    renderer.drawRoundedRect(boxX, boxY, boxW, boxH, 1, 14, true);

    drawFreeInkIcon(renderer, icon_telegram_32, (screenWidth - 32) / 2, boxY + 20);

    const char* emptyTitle = "Hộp Thư Telegram Trống";
    int etTw = renderer.getTextWidth(LEXENDDECA_14_FONT_ID, emptyTitle, EpdFontFamily::BOLD);
    renderer.drawText(LEXENDDECA_14_FONT_ID, (screenWidth - etTw) / 2, boxY + 65, emptyTitle, true,
                      EpdFontFamily::BOLD);

    renderer.drawText(UI_10_FONT_ID, boxX + 16, boxY + 110, "1. Mở Telegram trên điện thoại.", true,
                      EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, boxX + 16, boxY + 138, "2. Tìm kiếm bot: @crossbasictestbot", true,
                      EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, boxX + 16, boxY + 166, "3. Bấm Start hoặc nhắn bất kỳ từ nào (ví dụ: Hello).",
                      true, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, boxX + 16, boxY + 194, "4. Quay lại đây và bấm nút [ Làm mới ] ở trên!", true,
                      EpdFontFamily::BOLD);

    renderer.drawLine(boxX + 16, boxY + 226, boxX + boxW - 16, boxY + 226, true);
    renderer.drawText(SMALL_FONT_ID, boxX + 16, boxY + 240, "Bạn cũng có thể soạn tin nhắn gửi đi ở ô dưới đáy.",
                      true, EpdFontFamily::REGULAR);
  } else {
    // Có tin nhắn -> Render bong bóng chat
    int curY = 66 - scrollOffset;

    for (const auto& msg : messages) {
      const int bubbleW = 360;
      const int padding = 12;

      std::vector<std::string> lines =
          renderer.wrappedText(UI_10_FONT_ID, msg.text.c_str(), bubbleW - padding * 2, 20);
      const int textBlockH = lines.size() * 22;
      const int bubbleH = textBlockH + 34;

      if (curY + bubbleH > 56 && curY < maxContentY) {
        if (msg.isOutgoing) {
          // Tin gửi đi (Bên phải)
          const int bx = screenWidth - bubbleW - 14;
          renderer.drawRoundedRect(bx, curY, bubbleW, bubbleH, 2, 12, true);

          char metaBuf[64];
          snprintf(metaBuf, sizeof(metaBuf), "%s · %s", msg.sender.c_str(), msg.timeStr.c_str());
          renderer.drawText(SMALL_FONT_ID, bx + padding, curY + 6, metaBuf, true, EpdFontFamily::BOLD);

          for (size_t li = 0; li < lines.size(); ++li) {
            renderer.drawText(UI_10_FONT_ID, bx + padding, curY + 26 + li * 22, lines[li].c_str(), true,
                              EpdFontFamily::REGULAR);
          }
        } else {
          // Tin nhận về (Bên trái)
          const int bx = 14;
          renderer.drawRoundedRect(bx, curY, bubbleW, bubbleH, 1, 12, true);

          char metaBuf[64];
          snprintf(metaBuf, sizeof(metaBuf), "%s · %s", msg.sender.c_str(), msg.timeStr.c_str());
          renderer.drawText(SMALL_FONT_ID, bx + padding, curY + 6, metaBuf, true, EpdFontFamily::BOLD);

          for (size_t li = 0; li < lines.size(); ++li) {
            renderer.drawText(UI_10_FONT_ID, bx + padding, curY + 26 + li * 22, lines[li].c_str(), true,
                              EpdFontFamily::REGULAR);
          }
        }
      }

      curY += bubbleH + 12;
    }
  }

  // 3. Thanh Soạn Tin Nhắn Ở Đáy
  const int compY = screenHeight - 60;
  const int compW = screenWidth - 28;
  renderer.fillRoundedRect(14, compY, compW, 44, 10, Color::White);
  renderer.drawRoundedRect(14, compY, compW, 44, 1, 10, true);

  const char* compHint = "Soạn tin nhắn gửi đi...";
  renderer.drawText(UI_10_FONT_ID, 28, compY + 13, compHint, true, EpdFontFamily::REGULAR);

  // Nút Gửi bên phải
  const int sendBtnW = 60;
  const int sendBtnX = 14 + compW - sendBtnW - 4;
  renderer.drawRoundedRect(sendBtnX, compY + 4, sendBtnW, 36, 1, 8, true);
  int sntw = renderer.getTextWidth(UI_10_FONT_ID, "Gửi", EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, sendBtnX + (sendBtnW - sntw) / 2, compY + 12, "Gửi", true, EpdFontFamily::BOLD);
}

void TelegramLiteActivity::render(RenderLock&& lock) {
  renderer.clearScreen();

  if (isSetupMode) {
    renderSetupScreen();
  } else {
    renderChatScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
