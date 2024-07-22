#include "..\include\NavKit\InputHandler.h"

InputHandler::InputHandler(NavKit* navKit) : navKit(navKit) {
	mouseButtonMask = 0;
	mousePos[0] = 0, mousePos[1] = 0;
	origMousePos[0] = 0, origMousePos[1] = 0; // Used to compute mouse movement totals across frames.}
	mouseScroll = 0;
}

void InputHandler::handleInput() {
	// Handle input events.
	navKit->navp->doNavpHitTest = false;
	SDL_Event event;

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_KEYDOWN:
			// Handle any key presses here.
			if (event.key.keysym.sym == SDLK_ESCAPE)
			{
				navKit->done = true;
			}
			else if (event.key.keysym.sym == SDLK_TAB)
			{
				navKit->showMenu = !navKit->showMenu;
			}
			break;

		case SDL_MOUSEWHEEL:
			if (event.wheel.y < 0)
			{
				// wheel down
				if (navKit->mouseOverMenu)
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
				if (navKit->mouseOverMenu)
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
				if (!navKit->mouseOverMenu)
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
				if (!navKit->mouseOverMenu)
				{
					navKit->navp->doNavpHitTest = true;
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

	if (!navKit->mouseOverMenu) {
		if (navKit->navp->doNavpHitTest && navKit->navp->navpLoaded && navKit->navp->showNavp) {
			int newSelectedNavpArea = hitTest(&navKit->ctx, navKit->navp->navMesh, mousePos[0], mousePos[1], navKit->renderer->width, navKit->renderer->height);
			if (newSelectedNavpArea == navKit->navp->selectedNavpArea) {
				navKit->navp->selectedNavpArea = -1;
			}
			else {
				navKit->navp->selectedNavpArea = newSelectedNavpArea;
			}
			navKit->navp->doNavpHitTest = false;
		}
	}

}

int InputHandler::hitTest(BuildContext* ctx, NavPower::NavMesh* navMesh, int mx, int my, int width, int height) {
	glBindFramebuffer(GL_FRAMEBUFFER, navKit->renderer->framebuffer);
	glClearColor(1.0, 1.0, 1.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		ctx->log(RC_LOG_ERROR, "FB error, status: 0x%x", status);

		printf("FB error, status: 0x%x\n", status);
		return -1;
	}
	navKit->navp->renderNavMeshForHitTest();
	GLubyte pixel[4];

	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glReadPixels(mx, my, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	int selectedArea = int(pixel[1]) * 255 + int(pixel[2]);
	if (selectedArea == 65280) {
		ctx->log(RC_LOG_PROGRESS, "Deselected area.");
		return -1;
	}
	ctx->log(RC_LOG_PROGRESS, "Selected area: %d", selectedArea);
	return selectedArea;
}