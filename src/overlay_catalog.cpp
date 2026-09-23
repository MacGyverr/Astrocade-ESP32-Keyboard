#include "overlay_catalog.h"
#include "overlay_basic.h"
namespace {
    const OverlayDefinition overlays[] = {{OverlayMode::Basic, "basic", "Bally BASIC", overlayBasicMapText}};
}
const OverlayDefinition *overlayCatalog(std::size_t &count)
{
    count = sizeof(overlays) / sizeof(overlays[0]);
    return overlays;
}
const OverlayDefinition &activeOverlay()
{
    for (const auto &overlay : overlays) if (overlay.mode == settingsGetOverlayMode()) return overlay;
    return overlays[0];
}
