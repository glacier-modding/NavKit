#pragma once

class NavKit;

class Gui {
public:
    explicit Gui(NavKit *navKit);

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
