#include "lang.h"

#include <windows.h>

#include <map>
#include <system_error>
#include <unordered_map>

#include "paths.h"
#include "util.h"

namespace rd {

namespace {

using Table = std::unordered_map<std::string, std::string>;  // English text -> translation

std::map<std::string, Table> g_tables;  // locale (file name, "de", "pt-BR") -> table; read-only after loading

// The frame's language (render thread).
Lang g_lang = Lang::Ru;
const Table* g_table = nullptr;  // for Lang::Other
std::string g_dataLocale = "ru";
std::string g_base = "ru";  // the language part of the locale, for number style
std::string g_lastInput = "\x01";  // what the frame state was computed from

std::string BaseOf(std::string_view locale) {
    return AsciiLower(std::string(locale.substr(0, locale.find_first_of("-_"))));
}

bool SameLocale(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char x = a[i] == '_' ? '-' : a[i];
        const char y = b[i] == '_' ? '-' : b[i];
        if (AsciiLower(std::string(1, x)) != AsciiLower(std::string(1, y))) return false;
    }
    return true;
}

// The best of candidates for locale: the same locale, else the plain language ("es" for
// "es-ES"), else any locale of the same language; nullptr when none.
const std::string* Pick(std::string_view locale, const std::vector<const std::string*>& candidates) {
    for (const std::string* c : candidates) {
        if (SameLocale(*c, locale)) return c;
    }
    const std::string base = BaseOf(locale);
    for (const std::string* c : candidates) {
        if (AsciiLower(*c) == base) return c;
    }
    for (const std::string* c : candidates) {
        if (BaseOf(*c) == base) return c;
    }
    return nullptr;
}

// BYTEBOARD_LANG, read once; empty when not set.
const std::string& Override() {
    static const std::string value = [] {
        char buf[32] = {};
        const DWORD n = GetEnvironmentVariableA("BYTEBOARD_LANG", buf, sizeof buf);
        return n > 0 && n < sizeof buf ? std::string(buf, n) : std::string();
    }();
    return value;
}

// The Windows display language as a locale ("ru-RU", "de-DE").
std::string SystemLocale() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0)) return "en";
    return Narrow(name);
}

// The %s/%d placeholders of a text, in order ("%%" is not one).
std::string Placeholders(std::string_view text) {
    std::string out;
    for (size_t i = 0; i + 1 < text.size(); ++i) {
        if (text[i] != '%') continue;
        if (text[i + 1] == '%') {
            ++i;
            continue;
        }
        out += text[i + 1];
    }
    return out;
}

}  // namespace

void LoadTranslations() {
    const fs::path folder = Paths().mod / "lang";
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != L".tsv") continue;
        const std::string locale = Narrow(it->path().stem().wstring());
        std::string text;
        if (!ReadSharedFile(it->path(), text)) {
            Log("translation " + locale + ": cannot be read");
            continue;
        }
        Table& table = g_tables[locale];
        int skipped = 0;
        for (std::string_view line : Split(text, '\n')) {
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (line.empty() || line.front() == '#') continue;
            const size_t tab = line.find('\t');
            if (tab == std::string_view::npos) continue;
            const std::string en(line.substr(0, tab));
            const std::string translated(Trim(line.substr(tab + 1)));
            if (translated.empty()) continue;
            if (Placeholders(en) != Placeholders(translated)) {
                ++skipped;
                continue;
            }
            table[en] = translated;
        }
        Log("translation " + locale + ": " + std::to_string(table.size()) + " texts" +
            (skipped ? ", skipped " + std::to_string(skipped) + " whose %s/%d placeholders differ from the English text" : ""));
    }
}

void SetFrameLanguage(std::string_view gameLanguage, const std::vector<std::string>& dataLocales) {
    std::string locale = !Override().empty() ? Override() : !gameLanguage.empty() ? std::string(gameLanguage) : SystemLocale();
    const std::string input = locale + "|" + std::to_string(dataLocales.size());
    if (input == g_lastInput) return;
    g_lastInput = input;

    g_base = BaseOf(locale);
    g_table = nullptr;
    SetUpperLocale(g_base == "ru" || g_base == "en" ? std::string() : locale);
    if (g_base == "ru") {
        g_lang = Lang::Ru;
        g_dataLocale = "ru";
        return;
    }
    std::vector<const std::string*> tables;
    for (const auto& [name, table] : g_tables) tables.push_back(&name);
    const std::string* table = g_base == "en" ? nullptr : Pick(locale, tables);
    std::vector<const std::string*> data;
    for (const std::string& name : dataLocales) data.push_back(&name);
    const std::string* dataLocale = g_base == "en" ? nullptr : Pick(locale, data);
    if (table) {
        g_lang = Lang::Other;
        g_table = &g_tables.at(*table);
    } else {
        g_lang = Lang::En;
        g_base = "en";  // English text gets English numbers
    }
    g_dataLocale = table && dataLocale ? *dataLocale : "en";
}

Lang UiLang() {
    return g_lang;
}

const std::string& DataLocale() {
    return g_dataLocale;
}

const char* T(const char* ru, const char* en) {
    if (g_lang == Lang::Ru) return ru;
    if (g_table) {
        const auto it = g_table->find(en);
        if (it != g_table->end()) return it->second.c_str();
    }
    return en;
}

const std::string& TLabel(const std::string& ru, const std::string& en) {
    if (g_lang == Lang::Ru || en.empty()) return ru;
    if (g_table) {
        const auto it = g_table->find(en);
        if (it != g_table->end()) return it->second;
    }
    return en;
}

std::string AllTranslations(const std::string& en) {
    std::string out;
    for (const auto& [locale, table] : g_tables) {
        const auto it = table.find(en);
        if (it != table.end()) out += " " + it->second;
    }
    return out;
}

std::string Format1(const char* format, const std::string& arg) {
    std::string out(format);
    const size_t at = out.find("%s");
    if (at != std::string::npos) out.replace(at, 2, arg);
    return out;
}

std::string Number(long long value) {
    if (g_base == "ru") return FormatInt(value);
    // A no-break space where the language groups with spaces (Bahnschrift has no narrow one).
    const char* separator = g_base == "fr" || g_base == "pl" ? "\xC2\xA0"
                            : g_base == "en"                  ? ","
                                                              : ".";
    const std::string digits = std::to_string(value < 0 ? -value : value);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += separator;
        out += digits[i];
    }
    return (value < 0 ? "-" : "") + out;
}

std::string Bytes(long long value) {
    return Number(value) + (g_base == "ru" ? " Б" : " B");
}

}  // namespace rd
