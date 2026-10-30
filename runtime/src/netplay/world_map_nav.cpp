#include "netplay/world_map_nav.h"

// Basara: there is no NSMBW world map (the module read its dWmMap/dWmPlayer objects at fixed
// NSMBW addresses). Taps never hit a point and no walk is ever started.
namespace WorldMapNav {

int PointAtTap(float, float) { return -1; }
bool PlayerOn(int) { return false; }
void WalkTo(int) {}

uint32_t Buttons(bool& active) {
    active = false;
    return 0;
}

void Cancel() {}
void Dump() {}

}  // namespace WorldMapNav
