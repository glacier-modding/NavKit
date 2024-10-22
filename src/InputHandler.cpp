#include "..\include\NavKit\InputHandler.h"

InputHandler::InputHandler(NavKit* navKit) : navKit(navKit) {
	mouseButtonMask = 0;
	mousePos[0] = 0, mousePos[1] = 0;
	origMousePos[0] = 0, origMousePos[1] = 0;
	mouseScroll = 0;
}

void InputHandler::handleInput() {
	// Handle input events.
	navKit->navp->doNavpHitTest = false;
	SDL_Event event;
	mouseScroll = 0;
	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_WINDOWEVENT:
			if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
				resized = true;
			}
			break;

		case SDL_KEYDOWN:
			// Handle any key presses here.
			if (event.key.keysym.sym == SDLK_TAB)
			{
				navKit->gui->showMenu = !navKit->gui->showMenu;
			}
			break;

		case SDL_MOUSEWHEEL:
			if (event.wheel.y < 0)
			{
				// wheel down
				if (navKit->gui->mouseOverMenu)
				{
					mouseScroll++;
				}
				else
				{
					navKit->scrollZoom += 1.0f;
				}
			}
			else
			{
				if (navKit->gui->mouseOverMenu)
				{
					mouseScroll--;
				}
				else
				{
					navKit->scrollZoom -= 1.0f;
				}
			}
			break;
		case SDL_MOUSEBUTTONDOWN:
			if (event.button.button == SDL_BUTTON_RIGHT)
			{
				if (!navKit->gui->mouseOverMenu)
				{
					// Rotate view
					navKit->rotate = true;
					navKit->movedDuringRotate = false;
					origMousePos[0] = mousePos[0];
					origMousePos[1] = mousePos[1];
					navKit->renderer->origCameraEulers[0] = navKit->renderer->cameraEulers[0];
					navKit->renderer->origCameraEulers[1] = navKit->renderer->cameraEulers[1];
				}
			}
			break;

		case SDL_MOUSEBUTTONUP:
			// Handle mouse clicks here.
			if (event.button.button == SDL_BUTTON_RIGHT)
			{
				navKit->rotate = false;
			}
			else if (event.button.button == SDL_BUTTON_LEFT)
			{
				if (!navKit->gui->mouseOverMenu)
				{
					navKit->navp->doNavpHitTest = true;
					navKit->airg->doAirgHitTest = true;
				}
			}

			break;

		case SDL_MOUSEMOTION:
			mousePos[0] = event.motion.x;
			mousePos[1] = navKit->renderer->height - 1 - event.motion.y;

			if (navKit->rotate)
			{
				int dx = mousePos[0] - origMousePos[0];
				int dy = mousePos[1] - origMousePos[1];
				navKit->renderer->cameraEulers[0] = navKit->renderer->origCameraEulers[0] - dy * 0.25f;
				navKit->renderer->cameraEulers[1] = navKit->renderer->origCameraEulers[1] + dx * 0.25f;
				if (dx * dx + dy * dy > 3 * 3)
				{
					navKit->movedDuringRotate = true;
				}
			}
			break;

		case SDL_QUIT:
			navKit->done = true;
			break;

		default:
			break;
		}
	}
	mouseButtonMask = 0;
	if (SDL_GetMouseState(0, 0) & SDL_BUTTON_LMASK)
		mouseButtonMask |= IMGUI_MBUT_LEFT;
	if (SDL_GetMouseState(0, 0) & SDL_BUTTON_RMASK)
		mouseButtonMask |= IMGUI_MBUT_RIGHT;

	navKit->keybSpeed = 22.0f;
	if (SDL_GetModState() & KMOD_SHIFT)
	{
		navKit->keybSpeed *= 4.0f;
	}
	if (SDL_GetModState() & KMOD_CTRL)
	{
		navKit->keybSpeed /= 4.0f;
	}
}

void InputHandler::hitTest() {
	if (!navKit->gui->mouseOverMenu) {
		if ((navKit->navp->doNavpHitTest && navKit->navp->navpLoaded && navKit->navp->showNavp) ||
			(navKit->airg->doAirgHitTest && navKit->airg->airgLoaded && navKit->airg->showAirg)) {
			HitTestResult hitTestResult = navKit->renderer->hitTestRender(mousePos[0], mousePos[1]);
			if (hitTestResult.type == HitTestType::NAVMESH_AREA) {
				if (hitTestResult.selectedIndex == navKit->navp->selectedNavpAreaIndex) {
					navKit->navp->setSelectedNavpAreaIndex(-1);
				}
				else {
					navKit->navp->setSelectedNavpAreaIndex(hitTestResult.selectedIndex);
				}
			}
			else if (hitTestResult.type == HitTestType::AIRG_WAYPOINT) {
				if (hitTestResult.selectedIndex == navKit->airg->selectedWaypointIndex) {
					navKit->airg->setSelectedAirgWaypointIndex(-1);
				}
				else {
					navKit->airg->setSelectedAirgWaypointIndex(hitTestResult.selectedIndex);
				}
			}
			else {
				navKit->navp->setSelectedNavpAreaIndex(-1);
				navKit->airg->setSelectedAirgWaypointIndex(-1);
			}
			navKit->navp->doNavpHitTest = false;
			navKit->airg->doAirgHitTest = false;
		}
	}
}
