/**
 * Copyright (c) 2024 Daniel Bierek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/
#include "..\include\NavKit\NavKit.h"

using std::string;
using std::vector;

int main(int argc, char** argv) {
	NavKit navKitProgram;
	return navKitProgram.runProgram(argc, argv);
}

int NavKit::runProgram(int argc, char** argv) {
	if (!renderer->initWindowAndRenderer()) {
		return -1;
	}

	// TODO: Add mutex for tbuffer to keep processing messages in the background.
	// std::thread handleMessagesThread([=] { HandleMessages(); });
	// handleMessagesThread.detach();

	bool done = false;
	while (!done)
	{
		// Handle input events.
		int mouseScroll = 0;
		bool processHitTest = false;
		bool processHitTestShift = false;
		navp->doNavpHitTest = false;
		SDL_Event event;

		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_KEYDOWN:
				// Handle any key presses here.
				if (event.key.keysym.sym == SDLK_ESCAPE)
				{
					done = true;
				}
				else if (event.key.keysym.sym == SDLK_TAB)
				{
					showMenu = !showMenu;
				}
				break;

			case SDL_MOUSEWHEEL:
				if (event.wheel.y < 0)
				{
					// wheel down
					if (mouseOverMenu)
					{
						mouseScroll++;
					}
					else
					{
						scrollZoom += 1.0f;
					}
				}
				else
				{
					if (mouseOverMenu)
					{
						mouseScroll--;
					}
					else
					{
						scrollZoom -= 1.0f;
					}
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (event.button.button == SDL_BUTTON_RIGHT)
				{
					if (!mouseOverMenu)
					{
						// Rotate view
						rotate = true;
						movedDuringRotate = false;
						origMousePos[0] = mousePos[0];
						origMousePos[1] = mousePos[1];
						renderer->origCameraEulers[0] = renderer->cameraEulers[0];
						renderer->origCameraEulers[1] = renderer->cameraEulers[1];
					}
				}
				break;

			case SDL_MOUSEBUTTONUP:
				// Handle mouse clicks here.
				if (event.button.button == SDL_BUTTON_RIGHT)
				{
					rotate = false;
					if (!mouseOverMenu)
					{
						if (!movedDuringRotate)
						{
							processHitTest = true;
							processHitTestShift = true;
						}
					}
				}
				else if (event.button.button == SDL_BUTTON_LEFT)
				{
					if (!mouseOverMenu)
					{
						processHitTest = true;
						navp->doNavpHitTest = true;
						processHitTestShift = (SDL_GetModState() & KMOD_SHIFT) ? true : false;
					}
				}

				break;

			case SDL_MOUSEMOTION:
				mousePos[0] = event.motion.x;
				mousePos[1] = renderer->height - 1 - event.motion.y;

				if (rotate)
				{
					int dx = mousePos[0] - origMousePos[0];
					int dy = mousePos[1] - origMousePos[1];
					renderer->cameraEulers[0] = renderer->origCameraEulers[0] - dy * 0.25f;
					renderer->cameraEulers[1] = renderer->origCameraEulers[1] + dx * 0.25f;
					if (dx * dx + dy * dy > 3 * 3)
					{
						movedDuringRotate = true;
					}
				}
				break;

			case SDL_QUIT:
				done = true;
				break;

			default:
				break;
			}
		}

		unsigned char mouseButtonMask = 0;
		if (SDL_GetMouseState(0, 0) & SDL_BUTTON_LMASK)
			mouseButtonMask |= IMGUI_MBUT_LEFT;
		if (SDL_GetMouseState(0, 0) & SDL_BUTTON_RMASK)
			mouseButtonMask |= IMGUI_MBUT_RIGHT;

		renderer->renderFrame();
		keybSpeed = 22.0f;
		if (SDL_GetModState() & KMOD_SHIFT)
		{
			keybSpeed *= 4.0f;
		}
		if (SDL_GetModState() & KMOD_CTRL)
		{
			keybSpeed /= 4.0f;
		}


		if (!mouseOverMenu) {
			if (navp->doNavpHitTest && navp->navpLoaded && navp->showNavp) {
				int newSelectedNavpArea = hitTest(&ctx, navp->navMesh, mousePos[0], mousePos[1], renderer->width, renderer->height);
				if (newSelectedNavpArea == navp->selectedNavpArea) {
					navp->selectedNavpArea = -1;
				}
				else {
					navp->selectedNavpArea = newSelectedNavpArea;
				}
				navp->doNavpHitTest = false;
			}
		}

		// Render GUI
		glFrontFace(GL_CCW);
		glDisable(GL_DEPTH_TEST);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluOrtho2D(0, renderer->width, 0, renderer->height);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		mouseOverMenu = false;

		imguiBeginFrame(mousePos[0], mousePos[1], mouseButtonMask, mouseScroll);
		if (showMenu)
		{
			const char msg[] = "W/S/A/D: Move  RMB: Rotate";
			imguiDrawText(280, renderer->height - 20, IMGUI_ALIGN_LEFT, msg, imguiRGBA(255, 255, 255, 128));
			char cameraPosMessage[128];
			snprintf(cameraPosMessage, sizeof cameraPosMessage, "Camera position: %f, %f, %f", renderer->cameraPos[0], renderer->cameraPos[1], renderer->cameraPos[2]);
			imguiDrawText(280, renderer->height - 40, IMGUI_ALIGN_LEFT, cameraPosMessage, imguiRGBA(255, 255, 255, 128));
			char cameraAngleMessage[128];
			snprintf(cameraAngleMessage, sizeof cameraAngleMessage, "Camera angles: %f, %f", renderer->cameraEulers[0], renderer->cameraEulers[1]);
			imguiDrawText(280, renderer->height - 60, IMGUI_ALIGN_LEFT, cameraAngleMessage, imguiRGBA(255, 255, 255, 128));

			sceneExtract->drawMenu();
			navp->drawMenu();
			airg->drawMenu();
			obj->drawMenu();

			int consoleHeight = showLog ? 200 : 60;
			if (imguiBeginScrollArea("Log", 250 + 20, 10, renderer->width - 300 - 250, consoleHeight, &logScroll))
				mouseOverMenu = true;
			if (imguiCheck("Show Log", showLog))
				showLog = !showLog;
			if (showLog) {
				for (int i = 0; i < ctx.getLogCount(); ++i)
					imguiLabel(ctx.getLogText(i));
				if (lastLogCount != ctx.getLogCount()) {
					logScroll = std::max(0, ctx.getLogCount() * 20 - 160);
					lastLogCount = ctx.getLogCount();
				}
			}
			imguiEndScrollArea();
		}

		imguiEndFrame();
		imguiRenderGLDraw();

		sceneExtract->finalizeExtract();
		navp->finalizeLoad();
		obj->finalizeLoad();
		airg->finalizeLoad();

		glEnable(GL_DEPTH_TEST);
		SDL_GL_SwapWindow(renderer->window);
	}

	imguiRenderGLDestroy();

	SDL_Quit();

	NFD_Quit();
	return 0;
}

NavKit::NavKit() {
	sceneExtract = new SceneExtract(this);
	navp = new Navp(this);
	obj = new Obj(this);
	airg = new Airg(this);
	renderer = new Renderer(this);

	sample = new Sample_SoloMesh();
	sample->setContext(&ctx);
	showMenu = true;
	showLog = true;
	logScroll = 0;
	gameConnection = 0;
	geom = 0;
	lastLogCount = -1;
	mousePos[0] = 0, mousePos[1] = 0;
	origMousePos[0] = 0, origMousePos[1] = 0; // Used to compute mouse movement totals across frames.

	scrollZoom = 0;
	rotate = false;
	movedDuringRotate = false;
	lastLogCount = -1;
}

NavKit::~NavKit() {
	delete sceneExtract;
	delete navp;
	delete obj;
	delete airg;
	delete renderer;
}

int NavKit::hitTest(BuildContext* ctx, NavPower::NavMesh* navMesh, int mx, int my, int width, int height) {
	glBindFramebuffer(GL_FRAMEBUFFER, renderer->framebuffer);
	glClearColor(1.0, 1.0, 1.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		ctx->log(RC_LOG_ERROR, "FB error, status: 0x%x", status);

		printf("FB error, status: 0x%x\n", status);
		return -1;
	}
	navp->renderNavMeshForHitTest();
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