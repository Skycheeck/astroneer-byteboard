#pragma once
// Icon pixels from the game's textures (tools/lib/texture.mjs and tools/lib/dxt.mjs, used by
// tools/steps/build-icons.mjs).
#include <string>

#include "pak.h"

namespace rd::extract {

constexpr int kIconSize = 64;

// The top mip of the cooked Texture2D at objectPath (e.g.
// "/Game/UI/Textures/Icons/Items/ui_icon_comp_tapper.ui_icon_comp_tapper") as kIconSize x kIconSize
// RGBA8 pixels, box-filtered when the texture has another size. Throws FormatError when the texture
// cannot be read or decoded.
Bytes ReadIcon(const Pak& pak, const std::string& objectPath);

}  // namespace rd::extract
