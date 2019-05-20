
#include "window.h"
#include "shaderprintf.h"
#include "gl_helpers.h"
#include "math_helpers.h"
#include "gl_timing.h"
#include "math.hpp"
#include "text_renderer.h"
#include <dwrite_3.h>
#include <d2d1_3.h>
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "winmm.lib")

#include <vector>
#include <map>
#include <array>

const int screenw = 1600, screenh = 900;

#include <random>

#include <locale>
#include <codecvt>
extern WPARAM down[];
extern std::wstring text;
extern int downptr, hitptr;

bool keyDown(UINT vk_code);
bool keyHit(UINT vk_code);

int main() {

	OpenGL context(screenw, screenh, "", false);

	LARGE_INTEGER start, current, frequency;
	QueryPerformanceFrequency(&frequency);

	Font font(L"Consolas");

	bool loop = true;
	unsigned frame = 0;

	QueryPerformanceCounter(&start);
	float prevTime = .0f;
	while (loop) {

		MSG msg;

		hitptr = 0;

		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			switch (msg.message) {
			case WM_QUIT:
				loop = false;
				break;
			}
		}

		QueryPerformanceCounter(&current);
		float t = float(double(current.QuadPart - start.QuadPart) / double(frequency.QuadPart));

		float off = 5.f;

		TimeStamp start;

		for (int i = 0; i < downptr; ++i) {
			LPARAM scan = MapVirtualKeyEx(down[i], MAPVK_VK_TO_VSC_EX, GetKeyboardLayout(0));
			switch (down[i]) {
			case VK_SEPARATOR:
				scan = MapVirtualKeyEx(VK_RETURN, MAPVK_VK_TO_VSC_EX, GetKeyboardLayout(0));
			case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
			case VK_RCONTROL: case VK_RMENU:
			case VK_LWIN: case VK_RWIN: case VK_APPS:
			case VK_PRIOR: case VK_NEXT:
			case VK_END: case VK_HOME:
			case VK_INSERT: case VK_DELETE:
			case VK_DIVIDE:
			case VK_NUMLOCK:
				scan |= KF_EXTENDED;
				break;
			default: break;
			}
			if (down[i] == VK_PAUSE) scan = 69; // MapVirtualKeyEx returns wrong code for pause (???)
			WCHAR name[256];
			GetKeyNameTextW(scan << 16, name, 256);
			std::wstring n(name);
			font.drawText(n, 10.f, off + 120.f, 15.f);
			off += 25.f;
		}


		off = 5.f;
		for (int i = 0; i < joyGetNumDevs(); ++i) {
			JOYCAPSW caps;
			joyGetDevCapsW(i, &caps, sizeof(caps));

			if (std::wstring(caps.szPname).length() == 0) continue;

			JOYINFOEX kek;
			kek.dwSize = sizeof(kek);
			kek.dwFlags = JOY_RETURNALL;
			joyGetPosEx(i, &kek);
			std::wstring mask;
			for (int i = caps.wNumButtons-1; i >= 0; --i)
				mask = mask + std::to_wstring((kek.dwButtons>>i)&1);

			float x = float(kek.dwXpos - caps.wXmin) / float(caps.wXmax - caps.wXmin);
			float y = float(kek.dwYpos - caps.wYmin) / float(caps.wYmax - caps.wYmin);
			float z = float(kek.dwZpos - caps.wZmin) / float(caps.wZmax - caps.wZmin);
			float u = float(kek.dwUpos - caps.wUmin) / float(caps.wUmax - caps.wUmin);
			float v = float(kek.dwVpos - caps.wVmin) / float(caps.wVmax - caps.wVmin);
			float r = float(kek.dwRpos - caps.wRmin) / float(caps.wRmax - caps.wRmin);

			font.drawText(caps.szPname + std::wstring(L", buttons: ") + mask +
				L", x: " + std::to_wstring(x) +
				L", y: " + std::to_wstring(y) +
				L", z: " + std::to_wstring(z) +
				L", u: " + std::to_wstring(u) +
				L", v: " + std::to_wstring(v) +
				L", r: " + std::to_wstring(r), 405.f, off, 15.f);
			off += 25.f;
		}


		font.drawText(L"rendered in " + std::to_wstring(prevTime) + L"ms, " + std::to_wstring(downptr) + L" keys down\n" + text, 5.f, 5.f, 15.f, screenw-5);

		TimeStamp end;

		prevTime = elapsedTime(start, end);

		swapBuffers();
		glClearColor(.1f, .1f, .1f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
	return 0;
}
