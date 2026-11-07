#include "icons.h"

#include <cstring>

#include "texture.h"

namespace rd::extract {

namespace {

constexpr int kIconsPerRow = 16;
constexpr size_t kAtlasHeaderSize = 8;
constexpr size_t kBytesPerPixel = 4;

}  // namespace

IconPixels BuildIcons(const Pak& pak, const std::vector<CatalogItem>& catalog, const Logger& log) {
    // Icon name -> texture object path, in first-use order. The overlay keys icons by name, so two
    // different textures with the same name would be ambiguous.
    std::vector<std::pair<std::string, std::string>> iconPaths;
    std::unordered_map<std::string, size_t> indexByName;
    for (const CatalogItem& item : catalog) {
        if (!item.iconPath) continue;
        const auto [known, added] = indexByName.try_emplace(*item.icon, iconPaths.size());
        if (added) {
            iconPaths.push_back({*item.icon, *item.iconPath});
        } else if (iconPaths[known->second].second != *item.iconPath) {
            throw ToolError("two icon textures are named " + *item.icon + ": " + iconPaths[known->second].second +
                            " and " + *item.iconPath);
        }
    }

    // A FormatError leaves one icon out; any other error (a corrupt pak) stops the step.
    IconPixels icons;
    std::vector<std::string> failures;
    for (const auto& [name, objectPath] : iconPaths) {
        try {
            icons.emplace(name, ReadIcon(pak, objectPath));
        } catch (const FormatError& error) {
            failures.push_back(name + ": " + error.what());
        }
    }
    if (!iconPaths.empty() && icons.empty()) {
        std::string message = "none of the " + std::to_string(iconPaths.size()) + " icons could be decoded:";
        for (size_t k = 0; k < failures.size() && k < 20; ++k) message += "\n  " + failures[k];
        throw ToolError(message);
    }

    log(std::to_string(icons.size()) + " of " + std::to_string(iconPaths.size()) + " icons");
    if (!failures.empty()) {
        log("icons that could not be decoded:");
        for (const std::string& failure : failures) log("  " + failure);
    }
    return icons;
}

Bytes BuildAtlas(const std::vector<std::string>& iconNames, const IconPixels& icons, int& width, int& height) {
    const size_t count = iconNames.size();
    width = kIconsPerRow * kIconSize;
    height = (int)((count + kIconsPerRow - 1) / kIconsPerRow) * kIconSize;
    Bytes atlas(kAtlasHeaderSize + (size_t)width * height * kBytesPerPixel);
    const uint32_t header[2] = {(uint32_t)width, (uint32_t)height};
    std::memcpy(atlas.data(), header, sizeof header);  // little-endian on x64
    const size_t side = kIconSize;
    const size_t iconRowSize = side * kBytesPerPixel;
    for (size_t index = 0; index < count; ++index) {
        const Bytes& pixels = icons.at(iconNames[index]);
        const size_t left = (index % kIconsPerRow) * side;
        const size_t top = (index / kIconsPerRow) * side;
        for (size_t y = 0; y < side; ++y) {
            const size_t target = kAtlasHeaderSize + ((top + y) * width + left) * kBytesPerPixel;
            std::memcpy(atlas.data() + target, pixels.data() + y * iconRowSize, iconRowSize);
        }
    }
    return atlas;
}

}  // namespace rd::extract
