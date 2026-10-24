#pragma once
#include <Utf8.h>

#include <cstdint>
#include <string>
#include <vector>

/// Returns true if the Unicode codepoint falls within an IPA phonetic range.
/// Ranges covered:
///   U+0250–U+02AF  IPA Extensions
///   U+02B0–U+02FF  Modifier Letters (IPA subset)
///   U+1D00–U+1D7F  Phonetic Extensions
///   U+1D80–U+1DBF  Phonetic Extensions Supplement
/// Additional IPA characters outside those blocks:
///   U+00E6, U+00F0, U+00F8, U+0127, U+014B, U+0153, U+03B2, U+03B8, U+03C7
/// Combining marks used in IPA are attached to the previous run by splitIpaRuns().
static inline bool isIpaCodepoint(uint32_t cp) {
  return cp == 0x00E6 || cp == 0x00F0 || cp == 0x00F8 || cp == 0x0127 || cp == 0x014B || cp == 0x0153 || cp == 0x03B2 ||
         cp == 0x03B8 || cp == 0x03C7 || (cp >= 0x0250 && cp <= 0x02FF) || (cp >= 0x1D00 && cp <= 0x1DBF);
}

struct IpaTextSpan {
  std::string text;
  bool isIpa;
};

/// Map an IPA or special phonetic codepoint to a displayable glyph sequence
/// using standard Latin and Vietnamese characters that exist in all built-in fonts.
static inline void appendIpaDisplayFallback(uint32_t cp, std::string& out) {
  switch (cp) {
    case 0x00A0: out += ' '; break; // nbsp
    case 0x00E6: out += "ae"; break; // ae
    case 0x00F0: out += "th"; break; // eth
    case 0x00F8: out += 'o'; break; // o slash
    case 0x0127: out += 'h'; break; // h stroke
    case 0x014B: out += "ng"; break; // eng
    case 0x0153: out += "oe"; break; // oe
    case 0x0250: out += 'a'; break; // turned a
    case 0x0251: // script a (ɑ)
    case 0x0252: out += 'a'; break; // turned alpha (ɒ)
    case 0x0254: out += 'o'; break; // open o (ɔ)
    case 0x0259: out += "ơ"; break; // schwa (ə)
    case 0x025A: // rhotacized schwa (ɚ)
    case 0x025D: out += "ơr"; break; // reversed epsilon hook (ɝ)
    case 0x025B: out += 'e'; break; // open e (ɛ)
    case 0x025C: out += "ơ"; break; // reversed open e (ɜ)
    case 0x0261: out += 'g'; break; // script g (ɡ)
    case 0x026A: out += 'i'; break; // small capital i (ɪ)
    case 0x026B: out += 'l'; break; // velarized l (ɫ)
    case 0x0272: out += "ny"; break; // n hook (ɲ)
    case 0x0273: out += 'n'; break; // retroflex n (ɳ)
    case 0x0279: // turned r (ɹ)
    case 0x027B: // turned r hook (ɻ)
    case 0x027D: // r tail (ɽ)
    case 0x027E: out += 'r'; break; // fishhook r (ɾ)
    case 0x0280: out += 'R'; break; // small capital r (ʀ)
    case 0x0283: out += "sh"; break; // esh (ʃ)
    case 0x028A: out += 'u'; break; // upsilon (ʊ)
    case 0x028C: out += "ă"; break; // turned v (ʌ)
    case 0x0292: out += "zh"; break; // ezh (ʒ)
    case 0x0294: out += '\''; break; // glottal stop (ʔ)
    case 0x02A4: out += 'j'; break; // dezh (ʤ)
    case 0x02A7: out += "ch"; break; // tesh (ʧ)
    case 0x02B0: out += 'h'; break; // modifier h (ʰ)
    case 0x02B2: out += 'y'; break; // modifier j (ʲ)
    case 0x02B7: out += 'w'; break; // modifier w (ʷ)
    case 0x02BC: out += '\''; break; // modifier apostrophe (ʼ)
    case 0x02C8: out += '\''; break; // primary stress (ˈ)
    case 0x02CC: out += ','; break; // secondary stress (ˌ)
    case 0x02D0: // length mark (ː)
    case 0x02D1: out += ':'; break; // half-length mark (ˑ)
    case 0x03B2: out += 'b'; break; // beta (β)
    case 0x03B8: out += "th"; break; // theta (θ)
    case 0x03C7: out += "kh"; break; // chi (χ)
    case 0x2013: // en dash
    case 0x2014: // em dash
    case 0x2212: out += '-'; break; // minus
    case 0x2018: // left single quote
    case 0x2019: out += '\''; break; // right single quote
    case 0x201C: // left double quote
    case 0x201D: out += '"'; break; // right double quote
    case 0x2026: out += "..."; break; // ellipsis
    default:
      if (!isIpaCodepoint(cp) && !utf8IsCombiningMark(cp) && cp != REPLACEMENT_GLYPH) {
        utf8AppendCodepoint(cp, out);
      }
      break;
  }
}

/// Convert all IPA and unsupported phonetic characters in a string into
/// clear, readable, standard characters that render crisply on built-in fonts.
static inline std::string sanitizeIpaForDisplay(const char* text) {
  std::string out;
  if (!text) return out;
  out.reserve(std::char_traits<char>::length(text));
  const auto* p = reinterpret_cast<const uint8_t*>(text);
  uint32_t cp;
  while ((cp = utf8NextCodepoint(&p))) {
    appendIpaDisplayFallback(cp, out);
  }

  // Refine common consonant blends in English phonetics:
  // "tsh" (/tʃ/) -> "ch" (as in chair, future)
  // "dzh" (/dʒ/) -> "j"  (as in joy, knowledge)
  size_t pos = 0;
  while ((pos = out.find("tsh", pos)) != std::string::npos) {
    out.replace(pos, 3, "ch");
    pos += 2;
  }
  pos = 0;
  while ((pos = out.find("dzh", pos)) != std::string::npos) {
    out.replace(pos, 3, "j");
    pos += 1;
  }

  return out;
}

static inline std::string sanitizeIpaForDisplay(const std::string& text) {
  return sanitizeIpaForDisplay(text.c_str());
}

/// Split a UTF-8 string into runs of IPA vs non-IPA codepoints.
/// Results are appended into `out`; caller must clear `out` before each call.
static inline void splitIpaRuns(const char* text, std::vector<IpaTextSpan>& out) {
  if (!text || !text[0]) return;
  std::string current;
  bool currentIsIpa = false;
  bool first = true;
  const auto* p = reinterpret_cast<const uint8_t*>(text);
  uint32_t cp;
  while ((cp = utf8NextCodepoint(&p))) {
    const bool combining = utf8IsCombiningMark(cp);
    const bool ipa = combining ? currentIsIpa : isIpaCodepoint(cp);
    if (!first && !combining && ipa != currentIsIpa) {
      out.push_back({std::move(current), currentIsIpa});
      current.clear();
    }
    currentIsIpa = ipa;
    first = false;
    utf8AppendCodepoint(cp, current);
  }
  if (!current.empty()) out.push_back({std::move(current), currentIsIpa});
}
