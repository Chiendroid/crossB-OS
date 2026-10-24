#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/Dictionary.h"
#include "util/DictionaryRegistry.h"

class DictionaryAppActivity final : public Activity {
 public:
  struct BuiltinWord {
    const char* word;
    const char* ipa;
    const char* def;
  };

  enum class LineStyle {
    Heading,     // Từ loại (e.g. • Danh từ, • Động từ)
    Definition,  // Định nghĩa chính
    Example,     // Ví dụ minh họa
    Normal       // Văn bản thông thường / chú thích
  };

  struct FormattedLine {
    std::string text;
    LineStyle style = LineStyle::Normal;
  };

  DictionaryAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void openSearchInput();
  void performLookup(const std::string& word);
  void switchDictionary();
  void updateActiveDictionaryInfo();
  void formatDefinitionLines();

  std::string currentQuery;
  std::string displayedWord;
  std::string phonetic;
  std::string rawDefinition;
  std::vector<FormattedLine> wrappedLines;

  int currentPage = 0;
  int totalPages = 1;
  static constexpr int LINES_PER_PAGE = 18;

  std::string activeDictTitle;
  std::string activeDictBasePath;
  bool dictionaryFound = false;

  static const BuiltinWord* findBuiltinWord(const std::string& query);
};
