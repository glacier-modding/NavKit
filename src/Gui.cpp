#include "..\include\NavKit\Gui.h"

Gui::Gui(NavKit* navKit) : navKit(navKit) {
	showMenu = true;
	showLog = true;
	logScroll = 0;
	lastLogCount = -1;
	mouseOverMenu = false;
}

void Gui::drawGui() {
	glFrontFace(GL_CCW);
	glDisable(GL_DEPTH_TEST);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(0, navKit->renderer->width, 0, navKit->renderer->height);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	mouseOverMenu = false;
	imguiBeginFrame(navKit->inputHandler->mousePos[0], navKit->inputHandler->mousePos[1], navKit->inputHandler->mouseButtonMask, navKit->inputHandler->mouseScroll);
	if (showMenu)
	{
		const char msg[] = "W/S/A/D/Q/E: Move  LMB: Select / Deselect RMB: Rotate  Tab: Show / Hide UI  Ctrl: Slow camera movement  Shift: Fast camera movement";
		imguiDrawText(280, navKit->renderer->height - 20, IMGUI_ALIGN_LEFT, msg, imguiRGBA(255, 255, 255, 128));
		char cameraPosMessage[128];
		snprintf(cameraPosMessage, sizeof cameraPosMessage, "Camera position: %f, %f, %f", navKit->renderer->cameraPos[0], navKit->renderer->cameraPos[1], navKit->renderer->cameraPos[2]);
		imguiDrawText(280, navKit->renderer->height - 40, IMGUI_ALIGN_LEFT, cameraPosMessage, imguiRGBA(255, 255, 255, 128));
		char cameraAngleMessage[128];
		snprintf(cameraAngleMessage, sizeof cameraAngleMessage, "Camera angles: %f, %f", navKit->renderer->cameraEulers[0], navKit->renderer->cameraEulers[1]);
		imguiDrawText(280, navKit->renderer->height - 60, IMGUI_ALIGN_LEFT, cameraAngleMessage, imguiRGBA(255, 255, 255, 128));

		navKit->sceneExtract->drawMenu();
		navKit->navp->drawMenu();
		navKit->airg->drawMenu();
		navKit->obj->drawMenu();

		int consoleHeight = showLog ? 220 : 60;
		int consoleWidth = showLog ? navKit->renderer->width - 310 - 250 : 100;
		if (imguiBeginScrollArea("Log", 250 + 20, 20, consoleWidth, consoleHeight, &logScroll))
			mouseOverMenu = true;
		if (showLog) {
			for (int i = 0; i < navKit->ctx.getLogCount(); ++i)
				imguiLabel(std::string{ navKit->ctx.getLogText(i) }.c_str());
			if (lastLogCount != navKit->ctx.getLogCount()) {
				logScroll = std::max(0, navKit->ctx.getLogCount() * 20 - 160);
				lastLogCount = navKit->ctx.getLogCount();
			}
		}
		if (imguiCheck("Show Log", showLog)) {
			if (showLog) {
				showLog = false;
				collapsedLogScroll = logScroll;
				logScroll = 0;
			}
			else {
				showLog = true;
				logScroll = collapsedLogScroll;
			}
		}

		imguiEndScrollArea();
	}

	imguiEndFrame();
	imguiRenderGLDraw();
}