#include "ReleaseJsonParser.h"

#include <cstdlib>
#include <cstring>

namespace {

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

void appendUtf8(char* buf, size_t& len, size_t maxLen, uint16_t cp) {
  if (cp < 0x80) {
    if (len + 1 < maxLen) {
      buf[len++] = static_cast<char>(cp);
    }
  } else if (cp < 0x800) {
    if (len + 2 < maxLen) {
      buf[len++] = static_cast<char>(0xC0 | (cp >> 6));
      buf[len++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
  } else {
    if (len + 3 < maxLen) {
      buf[len++] = static_cast<char>(0xE0 | (cp >> 12));
      buf[len++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      buf[len++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
}

}  // namespace

ReleaseJsonParser::ReleaseJsonParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}) {
  safeCopy(firmwareAssetName, sizeof(firmwareAssetName), "firmware.bin", sizeof("firmware.bin") - 1);
  reset();
}

void ReleaseJsonParser::setFirmwareAssetName(const char* name) {
  safeCopy(firmwareAssetName, sizeof(firmwareAssetName), name, strlen(name));
}

void ReleaseJsonParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  assetDepth = 0;
  inRootArray = false;
  tagName[0] = '\0';
  firmwareUrl[0] = '\0';
  releaseBody[0] = '\0';
  firmwareSize = 0;
  tagFound = false;
  firmwareFound = false;
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSize = 0;

  bodyState = BodyState::SEEK_KEY;
  keyMatchIndex = 0;
  bodyLen = 0;
  unicodeHex = 0;
  unicodeDigits = 0;
}

void ReleaseJsonParser::feed(const char* data, size_t len) {
  if (bodyState != BodyState::DONE) {
    const char KEY_PATTERN[] = "\"body\"";
    for (size_t i = 0; i < len; i++) {
      char c = data[i];
      switch (bodyState) {
        case BodyState::SEEK_KEY:
          if (c == KEY_PATTERN[keyMatchIndex]) {
            keyMatchIndex++;
            if (keyMatchIndex == sizeof(KEY_PATTERN) - 1) {
              bodyState = BodyState::SEEK_COLON;
              keyMatchIndex = 0;
            }
          } else {
            keyMatchIndex = (c == KEY_PATTERN[0]) ? 1 : 0;
          }
          break;

        case BodyState::SEEK_COLON:
          if (c == ':') {
            bodyState = BodyState::SEEK_QUOTE;
          } else if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            bodyState = BodyState::SEEK_KEY;
          }
          break;

        case BodyState::SEEK_QUOTE:
          if (c == '"') {
            bodyState = BodyState::IN_STRING;
            bodyLen = 0;
            releaseBody[0] = '\0';
          } else if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            bodyState = BodyState::DONE;
          }
          break;

        case BodyState::IN_STRING:
          if (c == '\\') {
            bodyState = BodyState::ESCAPE;
          } else if (c == '"') {
            releaseBody[bodyLen] = '\0';
            bodyState = BodyState::DONE;
          } else {
            if (bodyLen + 1 < sizeof(releaseBody)) {
              releaseBody[bodyLen++] = c;
              releaseBody[bodyLen] = '\0';
            }
          }
          break;

        case BodyState::ESCAPE:
          switch (c) {
            case '"':
            case '\\':
            case '/':
              if (bodyLen + 1 < sizeof(releaseBody)) {
                releaseBody[bodyLen++] = c;
                releaseBody[bodyLen] = '\0';
              }
              bodyState = BodyState::IN_STRING;
              break;
            case 'b':
              if (bodyLen + 1 < sizeof(releaseBody)) {
                releaseBody[bodyLen++] = '\b';
                releaseBody[bodyLen] = '\0';
              }
              bodyState = BodyState::IN_STRING;
              break;
            case 'f':
              if (bodyLen + 1 < sizeof(releaseBody)) {
                releaseBody[bodyLen++] = '\f';
                releaseBody[bodyLen] = '\0';
              }
              bodyState = BodyState::IN_STRING;
              break;
            case 'n':
              if (bodyLen + 1 < sizeof(releaseBody)) {
                releaseBody[bodyLen++] = '\n';
                releaseBody[bodyLen] = '\0';
              }
              bodyState = BodyState::IN_STRING;
              break;
            case 'r':
              if (bodyLen + 1 < sizeof(releaseBody)) {
                releaseBody[bodyLen++] = '\r';
                releaseBody[bodyLen] = '\0';
              }
              bodyState = BodyState::IN_STRING;
              break;
            case 't':
              if (bodyLen + 1 < sizeof(releaseBody)) {
                releaseBody[bodyLen++] = '\t';
                releaseBody[bodyLen] = '\0';
              }
              bodyState = BodyState::IN_STRING;
              break;
            case 'u':
              bodyState = BodyState::UNICODE_ESCAPE;
              unicodeHex = 0;
              unicodeDigits = 0;
              break;
            default:
              if (bodyLen + 1 < sizeof(releaseBody)) {
                releaseBody[bodyLen++] = c;
                releaseBody[bodyLen] = '\0';
              }
              bodyState = BodyState::IN_STRING;
              break;
          }
          break;

        case BodyState::UNICODE_ESCAPE: {
          uint8_t val = 0;
          if (c >= '0' && c <= '9') val = c - '0';
          else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
          else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
          unicodeHex = (unicodeHex << 4) | val;
          unicodeDigits++;
          if (unicodeDigits == 4) {
            appendUtf8(releaseBody, bodyLen, sizeof(releaseBody), unicodeHex);
            releaseBody[bodyLen] = '\0';
            bodyState = BodyState::IN_STRING;
          }
          break;
        }

        case BodyState::DONE:
          break;
      }
    }
  }

  parser.feed(data, len);
}

bool ReleaseJsonParser::foundTag() const { return tagFound; }
bool ReleaseJsonParser::foundFirmware() const { return firmwareFound; }
const char* ReleaseJsonParser::getTagName() const { return tagName; }
const char* ReleaseJsonParser::getFirmwareUrl() const { return firmwareUrl; }
size_t ReleaseJsonParser::getFirmwareSize() const { return firmwareSize; }
const char* ReleaseJsonParser::getReleaseBody() const { return releaseBody; }

void ReleaseJsonParser::commitAsset() {
  if (strcmp(currentAssetName, firmwareAssetName) == 0) {
    memcpy(firmwareUrl, currentAssetUrl, sizeof(firmwareUrl));
    firmwareSize = currentAssetSize;
    firmwareFound = true;
  }
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSize = 0;
}

// -- SAX callbacks (static trampolines) -------------------------------------

void ReleaseJsonParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL: {
      const uint8_t targetDepth = self->inRootArray ? 2 : 1;
      if (self->depth == targetDepth) {
        if (len == 8 && memcmp(key, "tag_name", 8) == 0)
          self->lastKey = LastKey::TAG_NAME;
        else if (len == 4 && memcmp(key, "body", 4) == 0)
          self->lastKey = LastKey::BODY;
        else if (len == 6 && memcmp(key, "assets", 6) == 0)
          self->lastKey = LastKey::ASSETS;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    }
    case Position::IN_ASSET_OBJECT:
      if (self->assetDepth == 1) {
        if (len == 4 && memcmp(key, "name", 4) == 0)
          self->lastKey = LastKey::ASSET_NAME;
        else if (len == 20 && memcmp(key, "browser_download_url", 20) == 0)
          self->lastKey = LastKey::ASSET_URL;
        else if (len == 4 && memcmp(key, "size", 4) == 0)
          self->lastKey = LastKey::ASSET_SIZE;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->lastKey) {
    case LastKey::TAG_NAME: {
      const uint8_t targetDepth = self->inRootArray ? 2 : 1;
      if (self->position == Position::TOP_LEVEL && self->depth == targetDepth) {
        safeCopy(self->tagName, sizeof(self->tagName), value, len);
        self->tagFound = true;
      }
      break;
    }
    case LastKey::BODY: {
      const uint8_t targetDepth = self->inRootArray ? 2 : 1;
      if (self->position == Position::TOP_LEVEL && self->depth == targetDepth) {
        if (self->releaseBody[0] == '\0') {
          safeCopy(self->releaseBody, sizeof(self->releaseBody), value, len);
        }
      }
      break;
    }
    case LastKey::ASSET_NAME:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)
        safeCopy(self->currentAssetName, sizeof(self->currentAssetName), value, len);
      break;
    case LastKey::ASSET_URL:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)
        safeCopy(self->currentAssetUrl, sizeof(self->currentAssetUrl), value, len);
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnNumber(void* ctx, const char* value, size_t /*len*/) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  if (self->lastKey == LastKey::ASSET_SIZE && self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
    self->currentAssetSize = static_cast<size_t>(strtoul(value, nullptr, 10));
  }
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<ReleaseJsonParser*>(ctx)->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnNull(void* ctx) { static_cast<ReleaseJsonParser*>(ctx)->lastKey = LastKey::NONE; }

void ReleaseJsonParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::IN_ASSET_OBJECT;
      self->assetDepth = 1;
      self->currentAssetName[0] = '\0';
      self->currentAssetUrl[0] = '\0';
      self->currentAssetSize = 0;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
  }
}

void ReleaseJsonParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      if (self->assetDepth == 0) {
        self->commitAsset();
        self->position = Position::IN_ASSETS_ARRAY;
      }
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth == 0) {
        self->inRootArray = true;
        self->depth++;
      } else if (self->lastKey == LastKey::ASSETS && self->depth == (self->inRootArray ? 2 : 1)) {
        self->position = Position::IN_ASSETS_ARRAY;
      } else {
        self->depth++;
      }
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      if (self->depth == 0) self->inRootArray = false;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      self->lastKey = LastKey::NONE;
      break;
  }
}
