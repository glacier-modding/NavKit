#pragma once

#include "NavKit.h"

class NavKit;

class Gui {
public:
    Gui(NavKit *navKit);

    void drawGui();

    bool mouseOverMenu;
    bool showMenu;
    bool showLog;
    int logScroll;
    int collapsedLogScroll;
    int lastLogCount;

private:
    NavKit *navKit;
};
