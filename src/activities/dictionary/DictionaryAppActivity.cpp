#include "DictionaryAppActivity.h"

#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <sstream>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/ActivityManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/icons/listIcons.h"
#include "components/icons/search24.h"
#include "components/icons/touchHeaderIcons.h"
#include "fontIds.h"
#include "util/IpaUtils.h"

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

// Tập từ vựng mẫu Anh - Việt tích hợp sẵn để tra cứu ngay cả khi chưa cắm thẻ SD
static const DictionaryAppActivity::BuiltinWord s_builtinWords[] = {
    {"hello", "/həˈloʊ/", "• thán từ: Xin chào, chào bạn.\n• danh từ: Lời chào, tiếng chào hỏi.\n= to say hello to someone+gửi lời chào đến ai đó."},
    {"book", "/bʊk/", "• danh từ: Sách, cuốn sách, tác phẩm văn học.\n• động từ: Đặt chỗ trước, ghi tên vào sổ.\n= to book a table+đặt trước một bàn ăn."},
    {"read", "/riːd/", "• động từ: Đọc, đọc hiểu, nghiên cứu.\n  Đọc sách, hiểu được ý nghĩa ẩn giấu.\n= to read a book+đọc một cuốn sách."},
    {"learn", "/lɜːrn/", "• động từ: Học, học hỏi, tiếp thu tri thức.\n  Biết được tin, rút ra bài học kinh nghiệm."},
    {"knowledge", "/ˈnɑː.lɪdʒ/", "• danh từ: Tri thức, kiến thức, sự hiểu biết sâu rộng.\n  Sự nhận thức, sự quen thuộc."},
    {"study", "/ˈstʌd.i/", "• danh từ: Sự học tập, phòng học, công trình nghiên cứu.\n• động từ: Học tập, nghiên cứu, xem xét kỹ."},
    {"world", "/wɜːrld/", "• danh từ: Thế giới, địa cầu, nhân loại, giới, cõi đời."},
    {"life", "/laɪf/", "• danh từ: Cuộc sống, sinh mạng, đời người, sự sống."},
    {"time", "/taɪm/", "• danh từ: Thời gian, thì giờ, dịp, cơ hội, thời khắc."},
    {"friend", "/frend/", "• danh từ: Người bạn, bạn bè, người đồng hành, bạn hữu."},
    {"family", "/ˈfæm.əl.i/", "• danh từ: Gia đình, dòng họ, tổ ấm, thân tộc."},
    {"work", "/wɜːrk/", "• danh từ: Công việc, việc làm, tác phẩm.\n• động từ: Làm việc, hoạt động, vận hành."},
    {"peace", "/piːs/", "• danh từ: Hòa bình, sự yên bình, thanh thản, an tĩnh."},
    {"happy", "/ˈhæp.i/", "• tính từ: Hạnh phúc, vui vẻ, sung sướng, may mắn."},
    {"light", "/laɪt/", "• danh từ: Ánh sáng, nguồn sáng, ngọn đèn.\n• tính từ: Nhẹ, sáng sủa, thanh thoát."},
    {"dark", "/dɑːrk/", "• tính từ: Tối tăm, u ám, thâm trầm.\n• danh từ: Bóng tối, bóng đêm."},
    {"dream", "/driːm/", "• danh từ: Giấc mơ, ước mơ, chiêm bao, khát vọng.\n• động từ: Mơ mộng, ước mong."},
    {"hope", "/hoʊp/", "• danh từ: Hy vọng, niềm tin tưởng.\n• động từ: Hy vọng, mong chờ điều tốt đẹp."},
    {"future", "/ˈfjuː.tʃɚ/", "• danh từ: Tương lai, tiền đồ, mai sau.\n• tính từ: Thuộc về tương lai."},
    {"thank", "/θæŋk/", "• động từ: Cảm ơn, biết ơn, tri ân.\n• danh từ: Lời cảm ơn."},
    {"good", "/ɡʊd/", "• tính từ: Tốt, hay, giỏi, tử tế, thuận lợi.\n• danh từ: Điều thiện, lợi ích."},
    {"great", "/ɡreɪt/", "• tính từ: Tuyệt vời, vĩ đại, to lớn, xuất sắc."},
    {"computer", "/kəmˈpjuː.t̬ɚ/", "• danh từ: Máy vi tính, máy tính điện tử."},
    {"screen", "/skriːn/", "• danh từ: Màn hình, bức bình phong, tấm chắn.\n• động từ: Che chở, chiếu bóng."},
    {"word", "/wɜːrd/", "• danh từ: Từ ngữ, lời nói, lời hứa, tin tức."},
    {"think", "/θɪŋk/", "• động từ: Suy nghĩ, ngẫm nghĩ, cân nhắc, tưởng tượng."},
    {"system", "/ˈsɪs.təm/", "• danh từ: Hệ thống, chế độ, cơ cấu, hệ điều hành."},
    {"start", "/stɑːrt/", "• động từ: Bắt đầu, khởi động, khởi hành.\n• danh từ: Sự mở đầu, điểm xuất phát."},
    {"help", "/help/", "• động từ: Giúp đỡ, hỗ trợ, cứu giúp.\n• danh từ: Sự trợ giúp, người giúp việc."},
    {"dictionary", "/ˈdɪk.ʃən.er.i/", "• danh từ: Từ điển, sách tra cứu từ vựng."},
};

constexpr size_t BUILTIN_WORD_COUNT = sizeof(s_builtinWords) / sizeof(s_builtinWords[0]);

std::string trimAndLower(const std::string& input) {
  std::string s = input;
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
  s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

static std::string stripHtmlAndCleanEntities(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  bool inTag = false;
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '<') {
      if (i + 3 < input.size() && (input[i + 1] == 'b' || input[i + 1] == 'B') &&
          (input[i + 2] == 'r' || input[i + 2] == 'R')) {
        out += '\n';
      } else if (i + 2 < input.size() && (input[i + 1] == 'p' || input[i + 1] == 'P')) {
        out += "\n\n";
      } else if (i + 3 < input.size() && (input[i + 1] == 'l' || input[i + 1] == 'L') &&
                 (input[i + 2] == 'i' || input[i + 2] == 'I')) {
        out += "\n• ";
      }
      inTag = true;
      continue;
    }
    if (input[i] == '>') {
      inTag = false;
      continue;
    }
    if (inTag) continue;

    if (input[i] == '&') {
      if (input.compare(i, 4, "&lt;") == 0) {
        out += '<';
        i += 3;
        continue;
      }
      if (input.compare(i, 4, "&gt;") == 0) {
        out += '>';
        i += 3;
        continue;
      }
      if (input.compare(i, 5, "&amp;") == 0) {
        out += '&';
        i += 4;
        continue;
      }
      if (input.compare(i, 6, "&quot;") == 0) {
        out += '"';
        i += 5;
        continue;
      }
      if (input.compare(i, 6, "&apos;") == 0 || input.compare(i, 5, "&#39;") == 0) {
        out += '\'';
        i += (input[i + 2] == 'a' ? 5 : 4);
        continue;
      }
      if (input.compare(i, 6, "&nbsp;") == 0) {
        out += ' ';
        i += 5;
        continue;
      }
    }

    out += input[i];
  }
  return out;
}

}  // namespace

const DictionaryAppActivity::BuiltinWord* DictionaryAppActivity::findBuiltinWord(const std::string& query) {
  const std::string lower = trimAndLower(query);
  for (size_t i = 0; i < BUILTIN_WORD_COUNT; ++i) {
    if (lower == s_builtinWords[i].word) {
      return &s_builtinWords[i];
    }
  }
  return nullptr;
}

DictionaryAppActivity::DictionaryAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("DictionaryApp", renderer, mappedInput), currentQuery("knowledge") {}

void DictionaryAppActivity::onEnter() {
  Activity::onEnter();
  renderer.clearScreen();

  DictionaryRegistry::discover(availableDicts);
  updateActiveDictionaryInfo();
  performLookup(currentQuery);
}

void DictionaryAppActivity::onExit() {
  Activity::onExit();
}

void DictionaryAppActivity::updateActiveDictionaryInfo() {
  if (strlen(SETTINGS.dictionaryName) == 0 && !availableDicts.empty()) {
    int bestIdx = 0;
    for (size_t i = 0; i < availableDicts.size(); ++i) {
      std::string lowerName = trimAndLower(availableDicts[i].name);
      if (lowerName.find("anh") != std::string::npos || lowerName.find("vi") != std::string::npos ||
          lowerName.find("ev") != std::string::npos) {
        bestIdx = static_cast<int>(i);
        break;
      }
    }
    strncpy(SETTINGS.dictionaryName, availableDicts[bestIdx].name.c_str(), sizeof(SETTINGS.dictionaryName) - 1);
    SETTINGS.saveToFile();
  }

  dictionaryFound = false;
  if (strlen(SETTINGS.dictionaryName) > 0) {
    if (dict.open(SETTINGS.dictionaryName)) {
      dictionaryFound = true;
      activeDictTitle = SETTINGS.dictionaryName;
    }
  }

  if (!dictionaryFound) {
    activeDictTitle = "Anh - Việt (Cơ bản)";
  }
}

void DictionaryAppActivity::openSearchInput() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Tra từ điển Anh - Việt", currentQuery),
      [this](const ActivityResult& result) {
        if (const auto* kr = std::get_if<KeyboardResult>(&result.data)) {
          if (!result.isCancelled && !kr->text.empty()) {
            performLookup(kr->text);
          }
        }
      });
}

void DictionaryAppActivity::switchDictionary() {
  if (availableDicts.size() > 1) {
    int idx = -1;
    for (size_t i = 0; i < availableDicts.size(); ++i) {
      if (availableDicts[i].name == SETTINGS.dictionaryName) {
        idx = static_cast<int>(i);
        break;
      }
    }
    int nextIdx = (idx + 1) % availableDicts.size();
    strncpy(SETTINGS.dictionaryName, availableDicts[nextIdx].name.c_str(), sizeof(SETTINGS.dictionaryName) - 1);
    SETTINGS.saveToFile();
    updateActiveDictionaryInfo();
    performLookup(currentQuery);
  } else {
    DictionaryRegistry::discover(availableDicts);
    updateActiveDictionaryInfo();
    performLookup(currentQuery);
  }
}

void DictionaryAppActivity::performLookup(const std::string& word) {
  currentQuery = word;
  const std::string cleanWord = trimAndLower(word);
  displayedWord = cleanWord;
  phonetic.clear();
  rawDefinition.clear();
  wrappedLines.clear();
  currentPage = 0;

  if (cleanWord.empty()) {
    rawDefinition = "Nhập một từ tiếng Anh vào thanh tìm kiếm ở trên để bắt đầu tra cứu.";
    formatDefinitionLines();
    requestUpdate();
    return;
  }

  // 1. Thử tra cứu bằng engine StarDict trên thẻ nhớ
  if (dictionaryFound) {
    std::string dictDef;
    std::string matchedWord;
    Dictionary::LookupResult res;
    if (dict.lookup(cleanWord.c_str(), dictDef, matchedWord, &res)) {
      displayedWord = matchedWord;
      rawDefinition = stripHtmlAndCleanEntities(dictDef);

      // Trích xuất phiên âm /.../ nếu có ở đầu định nghĩa StarDict
      size_t slashStart = rawDefinition.find('/');
      if (slashStart != std::string::npos && slashStart < 80) {
        size_t slashEnd = rawDefinition.find('/', slashStart + 1);
        if (slashEnd != std::string::npos && slashEnd - slashStart < 40) {
          phonetic = rawDefinition.substr(slashStart, slashEnd - slashStart + 1);
          size_t lineEnd = rawDefinition.find('\n', slashEnd);
          if (lineEnd != std::string::npos && lineEnd < slashEnd + 4) {
            rawDefinition.erase(0, lineEnd + 1);
          }
        }
      }

      phonetic = sanitizeIpaForDisplay(phonetic);
      rawDefinition = sanitizeIpaForDisplay(rawDefinition);
    }
  }

  // 2. Nếu không tìm thấy hoặc chưa có file StarDict, dùng từ vựng built-in
  if (rawDefinition.empty()) {
    const BuiltinWord* bw = findBuiltinWord(cleanWord);
    if (bw) {
      displayedWord = bw->word;
      phonetic = sanitizeIpaForDisplay(bw->ipa);
      rawDefinition = sanitizeIpaForDisplay(bw->def);
    } else {
      std::ostringstream ss;
      ss << "Không tìm thấy từ \"" << cleanWord << "\" trong từ điển hiện tại.\n\n";
      if (!dictionaryFound) {
        ss << "HƯỚNG DẪN CÀI ĐẶT TỪ ĐIỂN ĐẦY ĐỦ:\n"
           << "1. Tải bộ từ điển định dạng StarDict (.dict, .idx, .ifo)\n"
           << "   (ví dụ: Anh-Việt, Oxford, Cambridge, Longman).\n"
           << "2. Chép thư mục từ điển vào thẻ nhớ SD theo đường dẫn:\n"
           << "   /dictionaries/Anh-Viet/\n"
           << "3. Khởi động lại hoặc bấm nút [Đổi] ở góc phải để kích hoạt.\n"
           << "Hỗ trợ tra cứu offline hoàn toàn với hàng trăm ngàn từ vựng.";
      } else {
        ss << "Gợi ý: Kiểm tra lại chính tả hoặc bấm [Đổi] ở góc trên để chuyển sang bộ từ điển khác.";
      }
      rawDefinition = ss.str();
    }
  }

  formatDefinitionLines();
  requestUpdate();
}

void DictionaryAppActivity::formatDefinitionLines() {
  wrappedLines.clear();
  const int maxWidth = renderer.getScreenWidth() - 72;

  std::istringstream stream(rawDefinition);
  std::string paragraph;
  while (std::getline(stream, paragraph)) {
    while (!paragraph.empty() && (paragraph.back() == '\r' || paragraph.back() == ' ')) {
      paragraph.pop_back();
    }
    if (paragraph.empty()) {
      wrappedLines.push_back({"", LineStyle::Normal});
      continue;
    }

    LineStyle style = LineStyle::Normal;
    int textIndent = 0;

    // Phân loại ngữ cảnh theo quy ước StarDict và từ vựng tích hợp
    if (paragraph[0] == '*' || paragraph.find("• ") == 0 ||
        paragraph.find("danh từ") == 0 || paragraph.find("động từ") == 0 ||
        paragraph.find("tính từ") == 0 || paragraph.find("thán từ") == 0 ||
        paragraph.find("phó từ") == 0 || paragraph.find("giới từ") == 0) {
      style = LineStyle::Heading;
      size_t start = 0;
      while (start < paragraph.size()) {
        if (paragraph[start] == '*' || paragraph[start] == ' ') {
          start++;
        } else if (paragraph.compare(start, 3, "•") == 0) {
          start += 3;
        } else {
          break;
        }
      }
      paragraph = "• " + paragraph.substr(start);
    } else if (paragraph[0] == '=') {
      style = LineStyle::Example;
      size_t start = 1;
      while (start < paragraph.size() && paragraph[start] == ' ') start++;
      std::string ex = paragraph.substr(start);
      size_t plusPos = ex.find('+');
      if (plusPos != std::string::npos) {
        ex.replace(plusPos, 1, ": ");
      }
      paragraph = "  › " + ex;
      textIndent = 12;
    } else if (paragraph[0] == '-' || paragraph.find("+ ") == 0) {
      style = LineStyle::Definition;
      size_t start = 1;
      while (start < paragraph.size() &&
             (paragraph[start] == '-' || paragraph[start] == '+' || paragraph[start] == ' ')) {
        start++;
      }
      paragraph = "  - " + paragraph.substr(start);
      textIndent = 8;
    }

    const int availableWidth = maxWidth - textIndent;
    std::istringstream wordStream(paragraph);
    std::string currentLine;
    std::string w;
    while (wordStream >> w) {
      std::string testLine = currentLine.empty() ? w : currentLine + " " + w;
      int width = renderer.getTextWidth(UI_10_FONT_ID, testLine.c_str(),
                                        style == LineStyle::Heading ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      if (width > availableWidth && !currentLine.empty()) {
        wrappedLines.push_back({currentLine, style});
        currentLine = (textIndent > 0 ? "    " : "") + w;
      } else {
        currentLine = testLine;
      }
    }
    if (!currentLine.empty()) {
      wrappedLines.push_back({currentLine, style});
    }
  }

  if (wrappedLines.empty()) {
    wrappedLines.push_back({"Không có định nghĩa.", LineStyle::Normal});
  }

  totalPages = (static_cast<int>(wrappedLines.size()) + LINES_PER_PAGE - 1) / LINES_PER_PAGE;
  if (totalPages < 1) totalPages = 1;
}

void DictionaryAppActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToPlugins(false);
    return;
  }

  int touchX = 0, touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    const int screenWidth = renderer.getScreenWidth();
    const int screenHeight = renderer.getScreenHeight();

    // 1. Nút Back góc trái trên
    if (touchX < 80 && touchY < 54) {
      activityManager.goToPlugins(false);
      return;
    }

    // 2. Nút Đổi từ điển góc phải trên
    if (touchX > screenWidth - 90 && touchY < 54) {
      switchDictionary();
      return;
    }

    // 3. Khung Tìm Kiếm (Search Box)
    if (touchX >= 20 && touchX <= screenWidth - 20 && touchY >= 60 && touchY <= 114) {
      openSearchInput();
      return;
    }

    // 4. Nút Chuyển trang định nghĩa (nếu có nhiều trang)
    if (totalPages > 1 && touchY >= screenHeight - 65 && touchY <= screenHeight - 10) {
      if (touchX < 200 && currentPage > 0) {
        currentPage--;
        requestUpdate();
        return;
      } else if (touchX > 280 && currentPage < totalPages - 1) {
        currentPage++;
        requestUpdate();
        return;
      }
    }
  }
}

void DictionaryAppActivity::render(RenderLock&& lock) {
  renderer.clearScreen();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int marginX = 20;

  // 1. Header Clean CrossInk (Nền trắng tinh, nút back squircle chuẩn OS, đường kẻ mỏng 1px)
  const int headerH = 52;
  renderer.drawLine(0, headerH, screenWidth, headerH, true);

  // Nút Back chuẩn hóa Squircle đồng nhất toàn hệ thống (Image 2)
  TouchHeaderBackButton::drawSquircleBackButton(renderer, 12, 8, 36, 10);

  // Tiêu đề Header
  renderer.drawText(UI_12_FONT_ID, 58, 16, "Từ điển", true, EpdFontFamily::BOLD);

  // Nút [Đổi] góc phải trên
  const int switchBtnW = 68;
  const int switchBtnH = 30;
  const int switchBtnX = screenWidth - switchBtnW - 16;
  const int switchBtnY = 11;
  renderer.drawRoundedRect(switchBtnX, switchBtnY, switchBtnW, switchBtnH, 1, 6, true);
  const int switchTw = renderer.getTextWidth(UI_10_FONT_ID, "Đổi", EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, switchBtnX + (switchBtnW - switchTw) / 2, switchBtnY + 5, "Đổi", true,
                    EpdFontFamily::BOLD);

  // 2. Khung Tìm Kiếm (Search Box) dáng bo tròn mềm mại, viền 1px sạch sẽ
  const int searchBarY = 66;
  const int searchBarW = screenWidth - (marginX * 2);
  const int searchBarH = 42;

  renderer.drawRoundedRect(marginX, searchBarY, searchBarW, searchBarH, 1, 10, true);
  drawFreeInkIcon(renderer, Search24Icon, marginX + 12, searchBarY + 9);

  std::string searchDisplay = currentQuery.empty() ? "Chạm để tra từ tiếng Anh..." : currentQuery;
  renderer.drawText(UI_10_FONT_ID, marginX + 44, searchBarY + 11, searchDisplay.c_str(), true,
                    currentQuery.empty() ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD);

  // 3. Card Kết Quả Nghĩa Từ (Clean Editorial Card, viền mỏng 1px, bo góc 12px)
  const int cardY = 120;
  const int cardW = screenWidth - (marginX * 2);
  const int cardH = screenHeight - cardY - 20;

  renderer.drawRoundedRect(marginX, cardY, cardW, cardH, 1, 12, true);

  // Header từ vựng bên trong thẻ
  std::string titleDisplay = displayedWord.empty() ? "Từ tra cứu" : displayedWord;
  renderer.drawText(UI_12_FONT_ID, marginX + 20, cardY + 16, titleDisplay.c_str(), true, EpdFontFamily::BOLD);
  int wordTw = renderer.getTextWidth(UI_12_FONT_ID, titleDisplay.c_str(), EpdFontFamily::BOLD);

  // Badge phiên âm phát âm rõ ràng, chuẩn IPA trong chip bo tròn nhẹ
  if (!phonetic.empty()) {
    int pTw = renderer.getTextWidth(UI_10_FONT_ID, phonetic.c_str(), EpdFontFamily::REGULAR);
    int badgeX = marginX + 28 + wordTw;
    int badgeY = cardY + 13;
    int badgeW = pTw + 16;
    if (badgeX + badgeW < marginX + cardW - 130) {
      renderer.drawRoundedRect(badgeX, badgeY, badgeW, 26, 1, 6, true);
      renderer.drawText(UI_10_FONT_ID, badgeX + 8, badgeY + 4, phonetic.c_str(), true, EpdFontFamily::REGULAR);
    } else {
      renderer.drawText(UI_10_FONT_ID, badgeX, cardY + 16, phonetic.c_str(), true, EpdFontFamily::REGULAR);
    }
  }

  // Tag tên từ điển đang hoạt động ở góc phải
  std::string dictBadge = activeDictTitle;
  int badgeTw = renderer.getTextWidth(SMALL_FONT_ID, dictBadge.c_str(), EpdFontFamily::REGULAR);
  renderer.drawText(SMALL_FONT_ID, marginX + cardW - badgeTw - 20, cardY + 20, dictBadge.c_str(), true,
                    EpdFontFamily::REGULAR);

  // Đường kẻ ngang mỏng 1px ngăn cách header thẻ
  renderer.drawLine(marginX + 16, cardY + 50, marginX + cardW - 16, cardY + 50, true);

  // 4. Danh sách dòng nghĩa từ sử dụng font UI_10_FONT_ID sắc nét, thoáng đãng
  const int startLine = currentPage * LINES_PER_PAGE;
  const int endLine = std::min(startLine + LINES_PER_PAGE, static_cast<int>(wrappedLines.size()));
  int lineY = cardY + 64;
  for (int i = startLine; i < endLine; ++i) {
    const auto& fLine = wrappedLines[i];
    if (fLine.text.empty()) {
      lineY += 10;
      continue;
    }

    if (fLine.style == LineStyle::Heading) {
      if (i > startLine) lineY += 8;
      renderer.drawText(UI_10_FONT_ID, marginX + 20, lineY, fLine.text.c_str(), true, EpdFontFamily::BOLD);
    } else if (fLine.style == LineStyle::Example) {
      renderer.drawText(UI_10_FONT_ID, marginX + 24, lineY, fLine.text.c_str(), true, EpdFontFamily::REGULAR);
    } else {
      renderer.drawText(UI_10_FONT_ID, marginX + 20, lineY, fLine.text.c_str(), true, EpdFontFamily::REGULAR);
    }
    lineY += 26;
  }

  // 5. Thanh chuyển trang nếu nhiều hơn 1 trang
  if (totalPages > 1) {
    const int pageNavY = cardY + cardH - 32;
    renderer.drawLine(marginX + 16, pageNavY - 8, marginX + cardW - 16, pageNavY - 8, true);

    if (currentPage > 0) {
      renderer.drawText(UI_10_FONT_ID, marginX + 24, pageNavY, "< Trước", true, EpdFontFamily::BOLD);
    }
    char pageBuf[32];
    snprintf(pageBuf, sizeof(pageBuf), "Trang %d / %d", currentPage + 1, totalPages);
    renderer.drawCenteredText(UI_10_FONT_ID, pageNavY, pageBuf, true, EpdFontFamily::REGULAR);
    if (currentPage < totalPages - 1) {
      int nextTw = renderer.getTextWidth(UI_10_FONT_ID, "Sau >", EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, marginX + cardW - nextTw - 24, pageNavY, "Sau >", true, EpdFontFamily::BOLD);
    }
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
