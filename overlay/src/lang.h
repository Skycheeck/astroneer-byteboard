#pragma once
// The interface language, following the game's language (state.json "language"): Russian, English,
// or a language with a translation file <mod>/lang/<locale>.tsv (German, French, Polish, ...). Any
// other game language gets English. Until the mod reports one, the Windows display language decides;
// the BYTEBOARD_LANG environment variable (a locale such as "de") overrides both, for previews.
//
// Texts in the code are Russian/English pairs, T("Цена", "Cost"). For another language the English
// text is looked up in that language's translation file; a missing line falls back to English.
// Game data (names, descriptions, recipes, mission titles) comes from the extracted data instead
// (catalog.h).
// Threads: LoadTranslations runs once on the overlay's MainThread before the hooks are installed;
// everything else runs on the render thread. SetFrameLanguage runs once per frame before any UI
// text is made.
#include <string>
#include <string_view>
#include <vector>

namespace rd {

enum class Lang { Ru, En, Other };

// Reads every <mod>/lang/*.tsv. A line whose %s/%d placeholders differ from its English text is
// skipped and logged.
void LoadTranslations();

// dataLocales: the locales the extracted data has texts for (besides ru and en).
void SetFrameLanguage(std::string_view gameLanguage, const std::vector<std::string>& dataLocales);
Lang UiLang();
// The locale whose game texts to show: "ru", "en", or one of dataLocales ("es-ES"); "en" when the
// data lacks the game's language.
const std::string& DataLocale();

// Interface text for the frame's language.
const char* T(const char* ru, const char* en);
// A label of the extraction's taxonomy (groups, subgroups, printers): Russian, English, or the
// translation of the English label; English when en is empty falls back to Russian.
const std::string& TLabel(const std::string& ru, const std::string& en);
// Every translation of an English label, for search.
std::string AllTranslations(const std::string& en);

// format with its single "%s" replaced by arg (no printf: translations cannot break it).
std::string Format1(const char* format, const std::string& arg);

// "1 500 Б", "1,500 B", "1.500 B": the number in the frame language's style, with the byte unit.
std::string Bytes(long long value);
// The number alone.
std::string Number(long long value);

}  // namespace rd
