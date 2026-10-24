#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"

class WebReaderActivity final : public Activity {
 public:
  explicit WebReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             std::string initialUrl = "");
  ~WebReaderActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  enum class State {
    Bookmarks,  // Trang chủ trình duyệt hiện đại (Speed Dial)
    Viewing,    // Đang duyệt trang web tương tác
    Error       // Báo lỗi kết nối / tải trang
  };

  enum class SearchEngine {
    Google,
    DuckDuckGo,
    Bing
  };

  enum class LoadPhase {
    Idle,
    StartBar,
    FetchHtml,
    ParseHtml,
    FetchImages,
    Finalize
  };

  enum class ItemType {
    Text,
    Heading,
    HeadingLink,
    SnippetLink,
    Link,
    Image,
    InputBox,
    Divider
  };

  struct Bookmark {
    std::string name;
    std::string url;
    std::string subtitle;
    std::string tag;
    int logoIndex = -1;          // 0..5 cho 6 trang mặc định, -1 cho trang tự lưu
    std::string customLogoPath;  // Đường dẫn file logo 36x36 tự động tải về (/web_logo_<domain>.jpg)
  };

  struct CookieEntry {
    std::string domain;
    std::string cookiePair;
  };

  struct RawBlock {
    ItemType type = ItemType::Text;
    std::string text;
    std::string url;
    std::string pendingImageUrl;
    std::string imageCachePath;
    int imgW = 0;
    int imgH = 0;
  };

  struct LayoutItem {
    ItemType type = ItemType::Text;
    std::string text;
    std::string url;
    std::string imageCachePath;
    int imageW = 0;
    int imageH = 0;
    int pixelHeight = 26;
    bool isFirstLineOfLink = false;
  };

  struct PageSlice {
    std::vector<LayoutItem> items;
  };

  struct HitRegion {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    std::string url;
    bool isInputBox = false;
  };

  enum class WebFontFamily : uint8_t {
    SystemUi = 0,
    NotoSans = 1,
    ReaderCustom = 2
  };

  enum class WebErrorKind : uint8_t {
    None = 0,
    NotFound404 = 1,
    Forbidden403 = 2,
    ServerError5xx = 3,
    ConnectionTimeout = 4,
    OutOfMemory = 5,
    NoWifi = 6,
    EmptyContent = 7,
    Generic = 8
  };

  State state = State::Bookmarks;
  SearchEngine searchEngine = SearchEngine::Google;
  LoadPhase loadPhase = LoadPhase::Idle;
  WebErrorKind errorKind = WebErrorKind::None;
  int errorHttpCode = 0;

  std::string currentUrl;
  std::string pendingHtml;
  std::string pageTitle;
  std::string errorMessage;

  std::vector<Bookmark> bookmarks;
  std::vector<CookieEntry> cookieJar;
  std::vector<RawBlock> pendingBlocks;
  std::vector<size_t> pendingImageIndices;
  size_t nextImageToFetch = 0;

  std::vector<PageSlice> pages;
  std::vector<HitRegion> activeHitRegions;
  std::vector<std::string> backStack;
  std::vector<std::string> forwardStack;

  int currentPage = 0;
  bool jsEnabled = true;
  bool imagesEnabled = false;  // Mặc định TẮT tải ảnh: chỉ tải chữ để tối ưu tốc độ, tiết kiệm RAM & pin
  bool mobileMode = true;
  bool menuOpen = false;
  bool displayMenuOpen = false;
  bool exitConfirmOpen = false;
  bool bookmarkActionOpen = false;
  int selectedBookmarkIdx = -1;
  bool isLoading = false;
  bool cacheJustCleared = false;
  int loadingProgress = 0;
  int cachedImageCount = 0;
  size_t totalCacheBytes = 0;

  struct OfflineArticle {
    std::string title;
    std::string url;
    std::string date;
    std::string filePath;
  };
  std::vector<OfflineArticle> offlineArticles;
  std::string toastMessage;
  uint32_t toastDismissTime = 0;

  // Tùy chỉnh hiển thị đọc sách (Display / Reader Settings)
  WebFontFamily webFontFamily = WebFontFamily::NotoSans;
  uint8_t webFontSizeIdx = 1;      // 0=12pt (Nhỏ), 1=14pt (Vừa), 2=16pt (Lớn), 3=18pt (Rất lớn)
  uint8_t webLineSpacingIdx = 1;   // 0=Hẹp, 1=Chuẩn, 2=Rộng, 3=Rất rộng
  uint8_t webMarginIdx = 1;        // 0=Hẹp (12px), 1=Chuẩn (20px), 2=Rộng (32px)
  bool webFocusReading = false;    // Focus Reading (in đậm đầu từ ngữ)
  bool requestFullRefresh = false; // Yêu cầu refresh mực E-ink sạch

  int getActiveContentFontId() const;
  int getActiveHeadingFontId() const;
  int getActiveMarginPx() const;
  int getActiveLineHeight(bool isHeading) const;

  void initBookmarks();
  void loadSavedBookmarks();
  void saveBookmarksToStorage() const;
  bool isCurrentPageBookmarked() const;
  void toggleBookmarkCurrentPage();
  void removeUnusedLogoFile(const Bookmark& removedBm);
  bool downloadWebsiteLogo(const std::string& siteUrl, std::string& outLogoPath);

  void loadOfflineArticles();
  void saveOfflineArticlesIndex() const;
  bool saveCurrentArticleOffline();
  void openOfflineArticle(size_t index);
  void deleteOfflineArticle(size_t index);
  void showToast(const std::string& msg);

  void ensureWifiConnected();
  const char* getSearchEngineName() const;
  std::string buildSearchUrl(const std::string& query) const;
  void cycleSearchEngine();

  std::string getCookiesForUrl(const std::string& url) const;
  void storeCookieFromHeader(const std::string& url, const std::string& setCookieHeader);
  void clearCookiesForDomain(const std::string& domain);
  void clearAllBrowsingData();
  std::string formatCacheBadge() const;

  void updateProgressImmediate(int progress);
  void startLoadUrl(const std::string& rawUrl);
  void stepLoadPipeline();
  void navigateTo(const std::string& url, bool pushHistory = true);
  void goBack();
  void goForward();
  void reloadPage();
  void parseHtmlToBlocks(const std::string& html);
  void layoutBlocksToPages();
  bool downloadUltraLightImage(const std::string& imgUrl, std::string& outCachePath, int& outW, int& outH);
  void cleanupCachedImages();

  void promptEnterUrl(const std::string& prefill = "");
  void promptFormInput(const std::string& actionUrl);
  void promptWifiConnection(const std::string& nextUrl = "");
  void nextPage();
  void prevPage();

  void renderBrowserToolbar();
  void renderBookmarks();
  void renderViewing();
  void renderError();
  void renderHamburgerMenu();
  void renderDisplaySettingsMenu();
  void renderExitConfirmDialog();
  void renderBookmarkActionDialog();

  bool handleToolbarTouch(int x, int y);
  bool handleMenuTouch(int x, int y);
  bool handleDisplayMenuTouch(int x, int y);
  bool handleExitConfirmTouch(int x, int y);
  bool handleBookmarkActionTouch(int x, int y);
};
