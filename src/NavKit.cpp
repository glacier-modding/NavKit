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

	while (!done)
	{
		inputHandler->handleInput();

		renderer->renderFrame();
		// Render GUI
		glFrontFace(GL_CCW);
		glDisable(GL_DEPTH_TEST);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluOrtho2D(0, renderer->width, 0, renderer->height);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		mouseOverMenu = false;

		imguiBeginFrame(inputHandler->mousePos[0], inputHandler->mousePos[1], inputHandler->mouseButtonMask, inputHandler->mouseScroll);
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
	inputHandler = new InputHandler(this);

	sample = new Sample_SoloMesh();
	sample->setContext(&ctx);
	showMenu = true;
	showLog = true;
	logScroll = 0;
	gameConnection = 0;
	geom = 0;
	lastLogCount = -1;

	scrollZoom = 0;
	rotate = false;
	movedDuringRotate = false;
	lastLogCount = -1;

	done = false;
}

NavKit::~NavKit() {
	delete sceneExtract;
	delete navp;
	delete obj;
	delete airg;
	delete renderer;
	delete inputHandler;
}
