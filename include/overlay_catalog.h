#pragma once
#include "text_compiler.h"
#include "settings.h"
struct OverlayDefinition { OverlayMode mode; const char *id; const char *name; TextMapper map; };
const OverlayDefinition *overlayCatalog(std::size_t &count);
const OverlayDefinition &activeOverlay();
