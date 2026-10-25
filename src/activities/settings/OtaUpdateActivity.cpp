#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/OtaUpdater.h"

namespace {
void drawSimpleButton(GfxRenderer& renderer, const Rect& r, const char* label, bool bold = false) {
  renderer.drawRect(r.x, r.y, r.width, r.height);
  const auto fontId = UI_10_FONT_ID;
  const int textW = renderer.getTextWidth(fontId, label, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  const int textH = renderer.getLineHeight(fontId);
  const int textX = r.x + (r.width - textW) / 2;
  const int textY = r.y + (r.height - textH) / 2;
  renderer.drawText(fontId, textX, textY, label, true, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}
}  // namespace

void OtaUpdateActivity::prepareChangelogLines() {
  changelogLines.clear();
  scrollLine = 0;

  std::string raw = updater.getReleaseNotes();
  if (raw.empty()) {
    raw = OtaUpdater::getDefaultCurrentChangelog();
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int maxLineWidth = pageWidth - (metrics.contentSidePadding * 2) - 24;

  size_t start = 0;
  while (start < raw.size()) {
    size_t end = raw.find('\n', start);
    std::string line = (end == std::string::npos) ? raw.substr(start) : raw.substr(start, end - start);
    if (end == std::string::npos) {
      start = raw.size();
    } else {
      start = end + 1;
    }

    if (line.empty()) {
      changelogLines.push_back("");
      continue;
    }

    std::string currentLine;
    size_t wordStart = 0;
    while (wordStart < line.size()) {
      size_t wordEnd = line.find(' ', wordStart);
      std::string word =
          (wordEnd == std::string::npos) ? line.substr(wordStart) : line.substr(wordStart, wordEnd - wordStart);
      if (wordEnd == std::string::npos) {
        wordStart = line.size();
      } else {
        wordStart = wordEnd + 1;
      }

      std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
      if (renderer.getTextWidth(UI_10_FONT_ID, testLine.c_str()) > maxLineWidth && !currentLine.empty()) {
        changelogLines.push_back(currentLine);
        currentLine = word;
      } else {
        currentLine = testLine;
      }
    }
    if (!currentLine.empty()) {
      changelogLines.push_back(currentLine);
    }
  }
}

void OtaUpdateActivity::startCheckForUpdate() {
  {
    RenderLock lock(*this);
    state = CHECKING_FOR_UPDATE;
  }
  requestUpdateAndWait();

  const auto res = updater.checkForUpdate();
  if (res == OtaUpdater::HTTP_ERROR || res == OtaUpdater::JSON_PARSE_ERROR || res == OtaUpdater::OOM_ERROR) {
    LOG_DBG("OTA", "Update check failed: %d", res);
    {
      RenderLock lock(*this);
      failedDetail = (res == OtaUpdater::HTTP_ERROR) ? "Lỗi kết nối mạng đến máy chủ" : "Lỗi phân tích dữ liệu cập nhật";
      state = FAILED;
    }
    requestUpdate();
    return;
  }

  prepareChangelogLines();

  {
    RenderLock lock(*this);
    state = VIEW_UPDATE;
  }
  requestUpdate();
}

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_ERR("OTA", "WiFi connection failed, exiting");
    finish();
    return;
  }

  LOG_DBG("OTA", "WiFi connected, starting check for update");
  startCheckForUpdate();
}

void OtaUpdateActivity::onEnter() {
  Activity::onEnter();

  LOG_DBG("OTA", "Turning on WiFi...");
  WiFi.mode(WIFI_STA);

  LOG_DBG("OTA", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void OtaUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  float updaterProgress = 0;
  if (state == UPDATE_IN_PROGRESS) {
    LOG_DBG("OTA", "Update progress: %d / %d", updater.getProcessedSize(), updater.getTotalSize());
    updaterProgress = static_cast<float>(updater.getProcessedSize()) / static_cast<float>(updater.getTotalSize());
    if (static_cast<int>(updaterProgress * 50) == lastUpdaterPercentage / 2) {
      return;
    }
    lastUpdaterPercentage = static_cast<int>(updaterProgress * 100);
  }

  if (state == CHECKING_FOR_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_UPDATE));
  } else if (state == VIEW_UPDATE) {
    int y = metrics.topPadding + metrics.headerHeight + 6;

    char verBuf[96];
    snprintf(verBuf, sizeof(verBuf), "Phiên bản hiện tại: %s", CROSSPOINT_VERSION);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, verBuf);
    y += height + 4;

    const char* chanName =
        (SETTINGS.otaChannel == CrossPointSettings::OTA_CHANNEL_DEV) ? "Phát triển (Dev)" : "Ổn định (Release)";
    char chanBuf[96];
    snprintf(chanBuf, sizeof(chanBuf), "Kênh cập nhật: %s", chanName);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, chanBuf);
    y += height + 4;

    if (updater.isUpdateNewer()) {
      char newVerBuf[96];
      snprintf(newVerBuf, sizeof(newVerBuf), "Có bản cập nhật mới: %s", updater.getLatestVersion().c_str());
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, newVerBuf, true, EpdFontFamily::BOLD);
    } else {
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, "Bạn đang ở phiên bản mới nhất.", true,
                        EpdFontFamily::BOLD);
    }
    y += height + 6;

    renderer.drawLine(metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding, y);
    y += 8;

    if (updater.isUpdateNewer()) {
      std::string title = "Nhật ký thay đổi (" + updater.getLatestVersion() + "):";
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, title.c_str(), true, EpdFontFamily::BOLD);
    } else {
      std::string title = "Nhật ký thay đổi (" + std::string(CROSSPOINT_VERSION) + "):";
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, title.c_str(), true, EpdFontFamily::BOLD);
    }
    y += height + 6;

    const int bottomButtonsHeight = updater.isUpdateNewer() ? 92 : 84;
    const int bottomAreaY = pageHeight - metrics.buttonHintsHeight - bottomButtonsHeight;
    const int boxHeight = std::max(60, bottomAreaY - y - 8);

    changelogBoxRect = Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, boxHeight};
    renderer.drawRect(changelogBoxRect.x, changelogBoxRect.y, changelogBoxRect.width, changelogBoxRect.height);

    const int lineSpacing = height + 2;
    const int maxVisibleLines = std::max(1, (boxHeight - 12) / lineSpacing);
    if (scrollLine > static_cast<int>(changelogLines.size()) - maxVisibleLines) {
      scrollLine = std::max(0, static_cast<int>(changelogLines.size()) - maxVisibleLines);
    }
    const int startLine = scrollLine;
    const int endLine = std::min(startLine + maxVisibleLines, static_cast<int>(changelogLines.size()));

    int textY = changelogBoxRect.y + 6;
    for (int i = startLine; i < endLine; i++) {
      renderer.drawText(UI_10_FONT_ID, changelogBoxRect.x + 8, textY, changelogLines[i].c_str());
      textY += lineSpacing;
    }

    if (changelogLines.size() > static_cast<size_t>(maxVisibleLines)) {
      char indBuf[32];
      snprintf(indBuf, sizeof(indBuf), "[%d/%d]", endLine, static_cast<int>(changelogLines.size()));
      const int indW = renderer.getTextWidth(UI_10_FONT_ID, indBuf);
      renderer.drawText(UI_10_FONT_ID, changelogBoxRect.x + changelogBoxRect.width - indW - 6,
                        changelogBoxRect.y + changelogBoxRect.height - height - 4, indBuf);
    }

    const int btnGap = 8;
    const int contentW = pageWidth - metrics.contentSidePadding * 2;
    if (updater.isUpdateNewer()) {
      updateButtonRect = Rect{metrics.contentSidePadding, bottomAreaY, contentW, 40};
      drawSimpleButton(renderer, updateButtonRect, "CẬP NHẬT NGAY", true);

      const int halfW = (contentW - btnGap) / 2;
      changeChannelButtonRect = Rect{metrics.contentSidePadding, bottomAreaY + 46, halfW, 36};
      drawSimpleButton(renderer, changeChannelButtonRect, "Đổi kênh");

      retryButtonRect = Rect{metrics.contentSidePadding + halfW + btnGap, bottomAreaY + 46, halfW, 36};
      drawSimpleButton(renderer, retryButtonRect, "Kiểm tra lại");
      backButtonRect = Rect{0, 0, 0, 0};
    } else {
      updateButtonRect = Rect{0, 0, 0, 0};
      const int halfW = (contentW - btnGap) / 2;
      changeChannelButtonRect = Rect{metrics.contentSidePadding, bottomAreaY, halfW, 38};
      drawSimpleButton(renderer, changeChannelButtonRect, "Đổi kênh");

      retryButtonRect = Rect{metrics.contentSidePadding + halfW + btnGap, bottomAreaY, halfW, 38};
      drawSimpleButton(renderer, retryButtonRect, "Kiểm tra lại");

      backButtonRect = Rect{metrics.contentSidePadding, bottomAreaY + 44, contentW, 34};
      drawSimpleButton(renderer, backButtonRect, "Quay lại");
    }

    const auto labels = mappedInput.mapLabels("Quay lại", updater.isUpdateNewer() ? "Cập nhật" : "Đổi kênh",
                                              "Cuộn lên", "Cuộn xuống");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

    if (confirmPopup.processRender(renderer, mappedInput)) return;
  } else if (state == UPDATE_IN_PROGRESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING));

    int y = top + height + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(updaterProgress * 100), 100);

    y += metrics.progressBarHeight + metrics.verticalSpacing;
    y += height + metrics.verticalSpacing;
    renderer.drawCenteredText(
        UI_10_FONT_ID, y,
        (std::to_string(updater.getProcessedSize()) + " / " + std::to_string(updater.getTotalSize())).c_str());
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
    if (failedDetail != nullptr) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + height + metrics.verticalSpacing, failedDetail);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FINISHED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
    const int hintY = top + height + metrics.verticalSpacing;
    const Rect hintBounds{metrics.contentSidePadding, hintY, pageWidth - metrics.contentSidePadding * 2,
                          pageHeight - hintY};
    UITheme::drawCenteredWrappedText(renderer, hintBounds, UI_10_FONT_ID, tr(STR_POWER_ON_HINT), 3, true,
                                     EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
  }

  renderer.displayBuffer();
}

void OtaUpdateActivity::runUpdateInstall() {
  LOG_DBG("OTA", "New update available, starting download...");
  {
    RenderLock lock(*this);
    state = UPDATE_IN_PROGRESS;
  }
  requestUpdateAndWait();
  const auto res = updater.installUpdate(
      [](void* ctx) {
        static_cast<OtaUpdateActivity*>(ctx)->requestUpdate(true);
      },
      this);

  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update failed: %d", res);
    {
      RenderLock lock(*this);
      failedDetail = res == OtaUpdater::WRONG_DEVICE_ERROR ? tr(STR_FIRMWARE_WRONG_DEVICE) : nullptr;
      state = FAILED;
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = FINISHED;
  }
  requestUpdateAndWait();
  delay(3000);
  {
    RenderLock lock(*this);
    state = SHUTTING_DOWN;
  }
}

void OtaUpdateActivity::loop() {
  if (state == VIEW_UPDATE) {
    if (confirmPopup.isActive()) {
      confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); });
      return;
    }

    int x = 0;
    int y = 0;
    const bool tapped = mappedInput.wasScreenTapped(x, y);

    if (tapped) {
      const auto& metrics = UITheme::getInstance().getMetrics();
      // Back button on header
      if (y < metrics.topPadding + metrics.headerHeight && x < 80) {
        finish();
        return;
      }

      // Update button
      if (updater.isUpdateNewer() && updateButtonRect.width > 0 &&
          x >= updateButtonRect.x && x <= updateButtonRect.x + updateButtonRect.width &&
          y >= updateButtonRect.y && y <= updateButtonRect.y + updateButtonRect.height) {
        const char* options[] = {tr(STR_CANCEL), tr(STR_UPDATE)};
        confirmPopup.showMessage("Xác nhận cập nhật",
                                 ("Tải và cài đặt phiên bản " + updater.getLatestVersion() + " ngay bây giờ?").c_str(),
                                 options, 2, 1, [this](const int idx) {
                                   if (idx == 1) {
                                     runUpdateInstall();
                                   }
                                 });
        requestUpdate();
        return;
      }

      // Change channel button
      if (changeChannelButtonRect.width > 0 &&
          x >= changeChannelButtonRect.x && x <= changeChannelButtonRect.x + changeChannelButtonRect.width &&
          y >= changeChannelButtonRect.y && y <= changeChannelButtonRect.y + changeChannelButtonRect.height) {
        SETTINGS.otaChannel = (SETTINGS.otaChannel == CrossPointSettings::OTA_CHANNEL_DEV)
                                  ? CrossPointSettings::OTA_CHANNEL_RELEASE
                                  : CrossPointSettings::OTA_CHANNEL_DEV;
        SETTINGS.saveToFile();
        startCheckForUpdate();
        return;
      }

      // Retry button
      if (retryButtonRect.width > 0 &&
          x >= retryButtonRect.x && x <= retryButtonRect.x + retryButtonRect.width &&
          y >= retryButtonRect.y && y <= retryButtonRect.y + retryButtonRect.height) {
        startCheckForUpdate();
        return;
      }

      // Back button in bottom area
      if (backButtonRect.width > 0 &&
          x >= backButtonRect.x && x <= backButtonRect.x + backButtonRect.width &&
          y >= backButtonRect.y && y <= backButtonRect.y + backButtonRect.height) {
        finish();
        return;
      }

      // Scroll changelog box
      if (changelogBoxRect.width > 0 &&
          x >= changelogBoxRect.x && x <= changelogBoxRect.x + changelogBoxRect.width &&
          y >= changelogBoxRect.y && y <= changelogBoxRect.y + changelogBoxRect.height) {
        if (y < changelogBoxRect.y + changelogBoxRect.height / 2) {
          scrollLine = std::max(0, scrollLine - 3);
        } else {
          scrollLine += 3;
        }
        requestUpdate();
        return;
      }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (updater.isUpdateNewer()) {
        const char* options[] = {tr(STR_CANCEL), tr(STR_UPDATE)};
        confirmPopup.showMessage("Xác nhận cập nhật",
                                 ("Tải và cài đặt phiên bản " + updater.getLatestVersion() + " ngay bây giờ?").c_str(),
                                 options, 2, 1, [this](const int idx) {
                                   if (idx == 1) {
                                     runUpdateInstall();
                                   }
                                 });
        requestUpdate();
      } else {
        SETTINGS.otaChannel = (SETTINGS.otaChannel == CrossPointSettings::OTA_CHANNEL_DEV)
                                  ? CrossPointSettings::OTA_CHANNEL_RELEASE
                                  : CrossPointSettings::OTA_CHANNEL_DEV;
        SETTINGS.saveToFile();
        startCheckForUpdate();
      }
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::NavPrevious) ||
        mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (scrollLine > 0) {
        scrollLine = std::max(0, scrollLine - 3);
        requestUpdate();
      }
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::NavNext) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      scrollLine += 3;
      requestUpdate();
      return;
    }
    return;
  }

  if (state == FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}
