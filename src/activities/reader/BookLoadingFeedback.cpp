#include "BookLoadingFeedback.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"

bool BookLoadingFeedback::showIfDue(GfxRenderer& renderer) {
  if (!renderer.hasFrameBuffer() || !isDue(millis())) return false;
  finish();
  GUI.drawPopup(renderer, tr(STR_LOADING));
  return true;
}
