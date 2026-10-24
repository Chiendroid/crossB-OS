#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"

struct TelegramMessage {
  int64_t messageId = 0;
  std::string sender;      // Tên người gửi (e.g. "Chien", "Telegram Bot")
  std::string text;        // Nội dung tin nhắn
  std::string timeStr;     // Thời gian gửi (e.g. "10:30", "Hôm nay 09:15")
  bool isOutgoing = false; // true = Tin của mình gửi đi, false = Tin nhận về
};

class TelegramLiteActivity final : public Activity {
 public:
  TelegramLiteActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void openTokenInput();
  void openChatIdInput();
  void openComposeInput();

  void loadConfig();
  void saveConfig();
  void checkBotInfo();
  void fetchUpdates();
  void sendMessage(const std::string& text);

  void renderSetupScreen();
  void renderChatScreen();

  bool isSetupMode = true;
  std::string botToken;
  std::string chatId;
  std::string botFirstName;
  std::string botUsername;
  std::string connectionStatus = "Chưa kết nối";

  std::vector<TelegramMessage> messages;
  int scrollOffset = 0;
  int64_t lastUpdateId = 0;
};
