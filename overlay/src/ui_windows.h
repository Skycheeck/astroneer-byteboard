#pragma once
// The two overlay windows: the research catalog and the list of an open printer.
// Threads: render thread only, between ImGui::NewFrame and ImGui::Render.
#include "bridge.h"
#include "ui_state.h"

namespace rd {

void DrawCatalogWindow(UiState& ui, const Live& live);
void DrawPrinterWindow(UiState& ui, const Live& live);

}  // namespace rd
