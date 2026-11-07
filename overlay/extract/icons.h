#pragma once
// The catalog's icons and the atlas the overlay reads.
#include <string>
#include <unordered_map>
#include <vector>

#include "catalog.h"
#include "log.h"
#include "pak.h"

namespace rd::extract {

// Icon name -> kIconSize x kIconSize RGBA8 pixels, for every icon that could be decoded.
using IconPixels = std::unordered_map<std::string, Bytes>;

// Decodes the icons of all catalog items. An icon that cannot be decoded is reported and left out (its
// items show a letter instead); throws ToolError when two textures share a name or none at all decodes.
IconPixels BuildIcons(const Pak& pak, const std::vector<CatalogItem>& catalog, const Logger& log);

// overlay/icons.rgba as docs/PROTOCOL.md fixes it: u32 width and u32 height (little-endian), then RGBA8
// pixels row by row; kIconSize x kIconSize cells, 16 per row, in the order of iconNames.
Bytes BuildAtlas(const std::vector<std::string>& iconNames, const IconPixels& icons, int& width, int& height);

}  // namespace rd::extract
