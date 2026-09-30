#pragma once
#include "../util/Platform.h"
#include <wx/menu.h>

class Menu {
public:
    static wxMenuBar* createMenuBar();

    static int handleMenuClicked(int menuId);

    static void setMenuItemEnabled(UINT menuId, bool isEnabled);

    static void handleCheckboxMenuItem(UINT menuId, bool& stateVariable, const char* itemName);

    static void handleCellColorDataRadioMenuItem(int selectedMenuId);

    static void updateMenuState();

    static void setMenuItemChecked(UINT menuId, bool isChecked, const char* itemName);
};
