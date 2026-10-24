#include "GalleryActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "activities/ActivityManager.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/icons/listIcons.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr const char* WALLPAPERS_DIR = "/wallpapers";
}  // namespace

GalleryActivity::GalleryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Gallery", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

void GalleryActivity::onEnter() {
  UiListActivity::onEnter();
  scanImages();
}

void GalleryActivity::onExit() {
  UiListActivity::onExit();
  imageFiles.clear();
}

void GalleryActivity::scanImages() {
  imageFiles.clear();

  // Đảm bảo thư mục /wallpapers luôn tồn tại
  if (!Storage.exists(WALLPAPERS_DIR)) {
    Storage.mkdir(WALLPAPERS_DIR);
  }

  // Quét các thư mục lưu ảnh phổ biến
  static constexpr const char* SEARCH_DIRS[] = {"/wallpapers", "/photos", "/images", "/pictures", "/"};
  const auto nameBuf = makeUniqueNoThrow<char[]>(512);
  if (!nameBuf) {
    LOG_ERR("GALLERY", "OOM: filename buffer");
    return;
  }

  std::vector<std::string> foundPaths;

  for (const char* dirPath : SEARCH_DIRS) {
    auto dir = Storage.open(dirPath);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }
    dir.rewindDirectory();
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory()) {
        file.close();
        continue;
      }
      file.getName(nameBuf.get(), 512);
      if (nameBuf[0] == '.') {
        file.close();
        continue;
      }
      std::string fname(nameBuf.get());
      if (FsHelpers::hasImageExtension(fname)) {
        std::string fullPath = dirPath;
        if (fullPath.back() != '/') fullPath += "/";
        fullPath += fname;
        // Tránh trùng lặp đường dẫn
        if (std::find(foundPaths.begin(), foundPaths.end(), fullPath) == foundPaths.end()) {
          foundPaths.push_back(fullPath);
        }
      }
      file.close();
    }
    dir.close();
  }

  FsHelpers::sortFileList(foundPaths);

  imageFiles.reserve(foundPaths.size());
  for (const auto& path : foundPaths) {
    ImageEntry entry;
    entry.fullPath = path;
    const size_t slash = path.rfind('/');
    entry.filename = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    const size_t dot = entry.filename.rfind('.');
    entry.displayName = (dot != std::string::npos) ? entry.filename.substr(0, dot) : entry.filename;

    std::string ext = (dot != std::string::npos) ? entry.filename.substr(dot + 1) : "";
    std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
    entry.extText = ext;

    auto f = Storage.open(path.c_str());
    if (f) {
      const size_t sz = f.size();
      f.close();
      char szBuf[32];
      if (sz < 1024) {
        snprintf(szBuf, sizeof(szBuf), "%zu B", sz);
      } else if (sz < 1024 * 1024) {
        snprintf(szBuf, sizeof(szBuf), "%zu KB", sz / 1024);
      } else {
        snprintf(szBuf, sizeof(szBuf), "%.1f MB", static_cast<float>(sz) / (1024.0f * 1024.0f));
      }
      entry.sizeText = szBuf;
    }

    entry.isCurrentWallpaper = (path == "/sleep.bmp");
    imageFiles.push_back(std::move(entry));
  }
}

void GalleryActivity::onBackButton() {
  activityManager.goToPlugins(/*showOpds=*/false);
}

const char* GalleryActivity::headerTitle() const {
  return tr(STR_GALLERY);
}

void GalleryActivity::drawChrome() {
  TouchHeaderBackButton::drawCompact(renderer, tr(STR_GALLERY));
}

bool GalleryActivity::handleCustomInput() {
  if (optionPopup.isActive()) {
    return false;
  }

  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer)) {
    onBackButton();
    return true;
  }

  return false;
}

void GalleryActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(imageFiles.size())) return;
  app.clearTapFlash();
  auto viewer = makeUniqueNoThrow<BmpViewerActivity>(renderer, mappedInput, imageFiles[index].fullPath,
                                                     /*returnToCaller=*/true);
  if (!viewer) {
    LOG_ERR("GALLERY", "OOM: BmpViewerActivity");
    return;
  }
  startActivityForResult(std::move(viewer), [this](const ActivityResult&) {
    requestUpdate();
  });
}

void GalleryActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= static_cast<int>(imageFiles.size())) return;
  app.clearTapFlash();
  showImageActions(index);
}

void GalleryActivity::showImageActions(const int index) {
  if (index < 0 || index >= static_cast<int>(imageFiles.size()) || optionPopup.isActive()) return;

  static constexpr StrId OPTIONS[] = {StrId::STR_OPEN, StrId::STR_SET_SLEEP_COVER, StrId::STR_DELETE};
  optionPopup.show(StrId::STR_WALLPAPERS, OPTIONS, 3, 0, [this, index](const int selected) {
    if (selected == 0) {
      activateIndex(index);
    } else if (selected == 1) {
      setAsSleepCover(index);
    } else if (selected == 2) {
      deleteImage(index);
    }
  });
  requestUpdate();
}

void GalleryActivity::setAsSleepCover(const int index) {
  if (index < 0 || index >= static_cast<int>(imageFiles.size())) return;
  const std::string fullPath = imageFiles[index].fullPath;
  const std::string filename = imageFiles[index].filename;

  const std::string heading = tr(STR_SET_SLEEP_COVER) + std::string("? ");
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, heading, filename);
  if (!confirmation) return;

  startActivityForResult(std::move(confirmation), [this, fullPath](const ActivityResult& res) {
    if (!res.isCancelled) {
      GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      const bool success = BmpViewerActivity::setSleepCoverFromFile(renderer, fullPath);
      if (success) {
        GUI.drawPopup(renderer, tr(STR_DONE));
      } else {
        GUI.drawPopup(renderer, tr(STR_FAILED_LOWER));
      }
      delay(1000);
      requestUpdate(true);
    }
  });
}

void GalleryActivity::deleteImage(const int index) {
  if (index < 0 || index >= static_cast<int>(imageFiles.size())) return;
  const std::string fullPath = imageFiles[index].fullPath;
  const std::string filename = imageFiles[index].filename;

  const std::string heading = tr(STR_DELETE) + std::string("? ");
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, heading, filename);
  if (!confirmation) return;

  startActivityForResult(std::move(confirmation), [this, fullPath](const ActivityResult& res) {
    if (!res.isCancelled) {
      Storage.remove(fullPath.c_str());
      scanImages();
      if (nav.selected >= listCount()) {
        nav.selected = listCount() > 0 ? listCount() - 1 : 0;
      }
      nav.follow(listCount());
      requestUpdate(true);
    }
  });
}

void GalleryActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();

  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing),
      metrics.contentSidePadding,
      16,
      metrics.contentSidePadding});

  if (imageFiles.empty()) {
    screen.centeredText(tr(STR_NO_IMAGES_FOUND), screen.theme().bodyText);
    screen.spacer(8);
    fui::TextStyle sub = screen.theme().smallText;
    sub.align = fui::TextAlign::Center;
    screen.centeredText(tr(STR_WALLPAPER_FOLDER), sub);
    return;
  }

  fui::ListProps props;
  props.count = static_cast<uint16_t>(imageFiles.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.rowProviderCtx = this;
  props.rowProvider = [](void* ctx, uint16_t index, fui::ListItem& item) {
    auto* self = static_cast<GalleryActivity*>(ctx);
    if (index >= self->imageFiles.size()) return;
    const auto& entry = self->imageFiles[index];
    static char s_subBuf[96];
    if (entry.isCurrentWallpaper) {
      snprintf(s_subBuf, sizeof(s_subBuf), "%s • %s • %s", tr(STR_CURRENT_WALLPAPER), entry.extText.c_str(),
               entry.sizeText.c_str());
    } else {
      snprintf(s_subBuf, sizeof(s_subBuf), "%s • %s", entry.extText.c_str(), entry.sizeText.c_str());
    }

    item.label = entry.displayName.c_str();
    item.subtitle = s_subBuf;
    item.value = ">";
    item.icon = fui::bitmapFromIcon(icon_image_24);
    item.actionValue = static_cast<int16_t>(index);
  };

  syncListViewport(screen, props);
  screen.list(props);
}

void GalleryActivity::drawFooter() {
  // Không vẽ thanh Dock hệ thống khi đang ở trong ứng dụng
}

