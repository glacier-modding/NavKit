#pragma once
#include <SDL_events.h>
#include <SDL_syswm.h>

class Menu {
public:
    static int handleMenuClicked(const SDL_SysWMmsg* wmMsg);

    static void setMenuItemEnabled(UINT menuId, bool isEnabled);

    static void handleCheckboxMenuItem(UINT menuId, bool& stateVariable, const char* itemName);

    static void handleCellColorDataRadioMenuItem(int selectedMenuId);

    static void updateMenuState();

    static void setMenuItemChecked(UINT menuId, bool isChecked, const char* itemName);
};
