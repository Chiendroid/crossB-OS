#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"

namespace ReaderUtils {
inline bool shouldShowFontPreviewLoading(const char* familyName) {
  (void)familyName;
  return false;
}
}  // namespace ReaderUtils
