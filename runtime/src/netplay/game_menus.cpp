#include "netplay/game_menus.h"

// Basara: the NSMBW object tree, scene word and menu StateIDs this module read (0x80377A10,
// 0x80428730, 0x809953C0.., all inside Basara's .text) do not exist here. Every query answers
// "no menu on screen", so the overlay, suspend and virtual-remote code never touch guest memory
// through it. Map Basara's own menus here if a caller ever needs them.
namespace GameMenus {

SelectPlayers SelectPlayersState() { return SelectPlayers::Hidden; }
bool SelectPlayersSettled() { return false; }
int SelectPlayersCursor() { return 0; }

bool FileSelectWaiting() { return false; }
int FileSelectCursor() { return 0; }
int FileSelectRawCursor() { return -1; }

bool HasProfile(uint16_t) { return false; }
std::vector<uint16_t> Profiles() { return {}; }

// Not 0: that is NSMBW's strap screen, where the overlay hides the touch controller for taps.
uint16_t CurrentScene() { return 0xFFFF; }
bool PauseMenuOpen() { return false; }
int PauseMenuKind() { return 0; }
uint32_t CourseSelectMenuObject() { return 0; }
int CourseMenuCursor() { return -1; }
uint32_t PauseWindowObject() { return 0; }
bool PauseWindowShowing() { return false; }
uint32_t SelectCursorObject() { return 0; }

bool PlayerInSpinnableAir(int, float* verticalSpeed) {
    if (verticalSpeed) {
        *verticalSpeed = 0.0f;
    }
    return false;
}

std::string DumpStates() { return {}; }

} // namespace GameMenus
