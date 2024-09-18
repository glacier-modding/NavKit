#pragma once

#include "NavKit.h"

class NavKit;

class InputHandler {
public:
	InputHandler(NavKit* navKit);
	void handleInput();
	void hitTest();

	int mousePos[2];
	int origMousePos[2];
	int mouseScroll;
	bool resized;
	unsigned char mouseButtonMask;

private:
	NavKit* navKit;
};