#pragma once
#include <wx/menu.h>

class Menu {
public:
    static wxMenuBar* createMenuBar();

    static int handleMenuClicked(int menuId);

    static void setMenuItemEnabled(int menuId, bool isEnabled);

    static void handleCheckboxMenuItem(int menuId, bool& stateVariable, const char* itemName);

    static void handleCellColorDataRadioMenuItem(int selectedMenuId);

    static void updateMenuState();

    static void setMenuItemChecked(int menuId, bool isChecked, const char* itemName);
};
