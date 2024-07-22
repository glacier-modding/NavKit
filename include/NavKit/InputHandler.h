#pragma once

#include "NavKit.h"

class NavKit;

class InputHandler {
public:
	InputHandler(NavKit* navKit);
	void handleInput();

	int hitTest(BuildContext* ctx, NavPower::NavMesh* navMesh, int mx, int my, int width, int height);
	int mousePos[2];
	int origMousePos[2];
	int mouseScroll;
	unsigned char mouseButtonMask;

private:
	NavKit* navKit;
};