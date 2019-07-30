
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

const int screenw = 1600, screenh = 1200;

#include <random>

#include <locale>
#include <codecvt>

extern WPARAM down[];
extern std::wstring text;
extern int downptr, hitptr;

bool keyDown(UINT vk_code);
bool keyHit(UINT vk_code);

// MPM
int main() {

	OpenGL context(screenw, screenh, "fluid", false);

	LARGE_INTEGER start, current, frequency;
	QueryPerformanceFrequency(&frequency);

	Font font(L"Consolas");

	bool loop = true;
	unsigned frame = 0;

	Program gridSimulate = createProgram("shaders/gridSimulate.glsl");
	Program particleSimulate = createProgram("shaders/particleSimulate.glsl");
	Program p2g = createProgram("shaders/p2gVert.glsl", "", "", "shaders/p2gGeom.glsl", "shaders/p2gFrag.glsl");

	Program drawPIC = createProgram("shaders/picVert.glsl", "", "", "shaders/picGeom.glsl", "shaders/picFrag.glsl");
	Program draw = createProgram("shaders/gridBlitVert.glsl", "", "", "", "shaders/gridBlitFrag.glsl");

	QueryPerformanceCounter(&start);
	float prevTime = .0f;

	int N = 256 * 1024;

	Buffer particlePos, particleVel;
	Buffer particleAff;
	glNamedBufferStorage(particlePos, sizeof(float) * 4 * N, nullptr, 0);
	glNamedBufferStorage(particleVel, sizeof(float) * 4 * N, nullptr, 0);
	glNamedBufferStorage(particleAff, sizeof(float) * 4 * 3 * N, nullptr, 0);

	int shadeRes = 256;
	Texture<GL_TEXTURE_3D> shadeGrid;
	glTextureStorage3D(shadeGrid, 1, GL_RGBA32F, shadeRes, shadeRes, shadeRes);

	int res = 64;
	Texture<GL_TEXTURE_3D> velocity[3], divergence, pressure[2], density;
	Texture<GL_TEXTURE_3D> fluidVolume, boundaryVelocity;
	for(int i = 0; i<3; ++i)
		glTextureStorage3D(velocity[i], 1, GL_RGBA32F, res, res, res);
	for (int i = 0; i < 2; ++i)
		glTextureStorage3D(pressure[i], 1, GL_R32F, res, res, res);

	glTextureStorage3D(divergence, 1, GL_R32F, res, res, res);
	glTextureStorage3D(density, 1, GL_RGBA32F, res, res, res);
	glTextureStorage3D(fluidVolume, 1, GL_RGBA32F, res, res, res);
	glTextureStorage3D(boundaryVelocity, 1, GL_RGBA32F, res, res, res);

	//Texture<GL_TEXTURE_2D> meme = loadImage(L"assets/välkky.png");

	vec4 camPosition(.5f, .5f, 2.f, 1.f);
	vec2 viewAngle(.0f, .0f);
	bool mouseDrag = false;
	ivec2 mouseOrig;
	ivec2 mouse;
	mat4 previousCam;
	mat4 proj = projection(screenw / float(screenh), 80.f, .2f, 15.0f);

	int ping = 0;
	int pressurePing = 0;

	glUseProgram(particleSimulate);
	glUniform1i("mode", 0);
	bindBuffer("posBuffer", particlePos);
	bindBuffer("velBuffer", particleVel);
	glDispatchCompute(N / 256, 1, 1);
	glUseProgram(gridSimulate);
	glUniform1i("mode", 0);
	bindImage("velocity", 0, velocity[ping], GL_WRITE_ONLY, GL_RGBA32F);
	bindImage("densityImage", 0, density, GL_WRITE_ONLY, GL_RGBA32F);
	bindImage("pressure", 0, pressure[0], GL_WRITE_ONLY, GL_R32F);
	glDispatchCompute(res/8, res/8, res/8);

	Buffer printBuffer;
	glNamedBufferStorage(printBuffer, sizeof(int) * 1024 * 1024, nullptr, GL_DYNAMIC_STORAGE_BIT);

	float phase = 1.f;

	const float dx = 1 / float(res);
	const float dt = .005f;
	float t = .0f;

	Framebuffer gridFbo, shadeFbo;

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
			case WM_LBUTTONDOWN:
				mouseDrag = true;
				mouseOrig = ivec2(msg.lParam & 0xFFFF, msg.lParam >> 16);
				ShowCursor(false);
				break;
			case WM_MOUSEMOVE:
				if (mouseDrag) {
					viewAngle += vec2(ivec2(msg.lParam & 0xFFFF, msg.lParam >> 16) - mouseOrig)*.005f;
					setMouse(POINT{ mouseOrig.x, mouseOrig.y });
				}
				break;
			case WM_LBUTTONUP:
				ShowCursor(true);
				mouseDrag = false;
				break;
			}
		}

		while (viewAngle.x > pi) viewAngle.x -= 2.f*pi;
		while (viewAngle.x < -pi) viewAngle.x += 2.f*pi;
		viewAngle.y = clamp(viewAngle.y, -.5*pi, .5*pi);


		mat4 cam = yRotate(viewAngle.x) * xRotate(-viewAngle.y);
		vec3 cameraVelocity(float(keyDown('D') - keyDown('A')), float(keyDown('E') - keyDown('Q')), float(keyDown('S') - keyDown('W')));
		cam.col(3) = camPosition += cam * vec4(cameraVelocity*.02f, .0f);
		auto icam = invert(cam);
		auto iproj = invert(proj);

		QueryPerformanceCounter(&current);
		//float t = float(double(current.QuadPart - start.QuadPart) / double(frequency.QuadPart));

		float off = 5.f;

		TimeStamp start;

		glUseProgram(gridSimulate);
		glUniform1f("t", t);
		glUniform1f("dt", dt);
		glUniform1f("dx", dx);
		glUniform1i("mode", 1);
		glUniform1i("size", res);
		bindImage("velocity", 0, velocity[ping], GL_WRITE_ONLY, GL_RGBA32F);
		bindImage("densityImage", 0, density, GL_WRITE_ONLY, GL_RGBA32F);
		glDispatchCompute(res / 8, res / 8, res / 8);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		glUseProgram(p2g);
		glBindFramebuffer(GL_FRAMEBUFFER, gridFbo);
		glDisable(GL_DEPTH_TEST);
		bindOutputTexture("density", density, 0);
		bindOutputTexture("velocity", velocity[ping], 0);

		glUniform1i("res", res);

		bindBuffer("posBuffer", particlePos);
		bindBuffer("velBuffer", particleVel);
		bindBuffer("affBuffer", particleAff);

		glViewport(0, 0, res, res);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		glDrawArrays(GL_POINTS, 0, N);
		//bindPrintBuffer(p2g, printBuffer);
		//printf("%s\n", getPrintBufferString(printBuffer).c_str());
		glDisable(GL_BLEND);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, screenw, screenh);

		glUseProgram(gridSimulate);
		glUniform1i("mode", 2);
		bindImage("shade", 0, shadeGrid, GL_WRITE_ONLY, GL_RGBA32F);
		bindTexture("oldDensity", density);
		bindImage("velocity", 0, velocity[1 - ping], GL_WRITE_ONLY, GL_RGBA32F);
		bindImage("fluidVolume", 0, fluidVolume, GL_WRITE_ONLY, GL_RGBA32F);
		bindImage("boundaryVelocity", 0, boundaryVelocity, GL_WRITE_ONLY, GL_RGBA32F);
		bindTexture("oldVelocity", velocity[ping]);
		glDispatchCompute(res / 8, res / 8, res / 8);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		ping = 1 - ping;

		glUniform1i("mode", 6);
		bindTexture("oldDensity", density);
		bindImage("velocity", 0, velocity[1 - ping], GL_WRITE_ONLY, GL_RGBA32F);
		bindTexture("oldVelocity", velocity[ping]);
		glDispatchCompute(res / 8, res / 8, res / 8);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		ping = 1 - ping;
		
		glUniform1i("mode", 3);
		bindTexture("oldDensity", density);
		bindImage("pressure", 0, pressure[pressurePing], GL_WRITE_ONLY, GL_R32F);
		bindImage("divergenceImage", 0, divergence, GL_WRITE_ONLY, GL_R32F);
		bindImage("fluidVolume", 0, fluidVolume, GL_READ_ONLY, GL_RGBA32F);
		bindImage("boundaryVelocity", 0, boundaryVelocity, GL_READ_ONLY, GL_RGBA32F);
		bindTexture("oldVelocity", velocity[ping]);
		glDispatchCompute(res / 8, res / 8, res / 8);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		for (int i = 0; i < 64; ++i) {
			glUniform1i("mode", 4);
			bindImage("pressure", 0, pressure[1-pressurePing], GL_WRITE_ONLY, GL_R32F);
			bindTexture("oldDensity", density);
			bindTexture("oldPressure", pressure[pressurePing]);
			bindTexture("divergence", divergence);
			bindTexture("fluidRelVol", fluidVolume);
			glDispatchCompute(res / 8, res / 8, res / 8);
			glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
			pressurePing = 1 - pressurePing;
		}

		glUniform1i("mode", 5);
		bindTexture("oldDensity", density);
		bindTexture("oldPressure", pressure[pressurePing]);
		bindImage("velocity", 0, velocity[1 - ping], GL_WRITE_ONLY, GL_RGBA32F);
		bindImage("velDifference", 0, velocity[2], GL_WRITE_ONLY, GL_RGBA32F);
		bindTexture("oldVelocity", velocity[ping]);
		bindImage("boundaryVelocity", 0, boundaryVelocity, GL_READ_ONLY, GL_RGBA32F);
		bindTexture("fluidVol", fluidVolume);
		bindTexture("fluidRelVol", fluidVolume);
		glUniform1f("phase", phase);
		//phase = 1.f-phase;
		glDispatchCompute(res / 8, res / 8, res / 8);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		ping = 1 - ping;

		glUseProgram(particleSimulate);
		glUniform1f("dx", dx);
		glUniform1i("mode", 1);
		glUniform1i("size", res);
		glUniform1f("t", t);
		glUniform1f("dt", dt);
		bindTexture("fluidVol", fluidVolume);
		bindBuffer("posBuffer", particlePos);
		bindBuffer("velBuffer", particleVel);
		bindBuffer("affBuffer", particleAff);
		bindTexture("oldVelocity", velocity[ping]);
		bindTexture("velDiff", velocity[2]);
		bindTexture("oldDensity", density);
		glDispatchCompute(N / 256, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		
		t += dt;
		
		glUseProgram(p2g);
		glBindFramebuffer(GL_FRAMEBUFFER, shadeFbo);
		glDisable(GL_DEPTH_TEST);
		bindOutputTexture("density", shadeGrid, 0);

		glUniform1i("res", shadeRes);

		bindBuffer("posBuffer", particlePos);
		bindBuffer("velBuffer", particleVel);
		bindBuffer("affBuffer", particleAff);

		glViewport(0, 0, shadeRes, shadeRes);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		glDrawArrays(GL_POINTS, 0, N);
		//bindPrintBuffer(p2g, printBuffer);
		//printf("%s\n", getPrintBufferString(printBuffer).c_str());
		glDisable(GL_BLEND);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, screenw, screenh);


		glUseProgram(gridSimulate);
		glUniform1i("mode", 7);
		bindImage("shade", 0, shadeGrid, GL_READ_WRITE, GL_RGBA32F);
		glDispatchCompute(128,1,1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		TimeStamp end;

		glUseProgram(draw);
		glUniform1f("t", t);
		//bindTexture("oldPressure", pressure[pressurePing]);
		//bindTexture("density", density);
		//bindTexture("divergence", divergence);
		bindTexture("shade", shadeGrid);
		//bindTexture("fluidVolume", fluidVolume);
		//bindTexture("boundaryVelocity", boundaryVelocity);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		
		glUseProgram(drawPIC);
		bindBuffer("posBuffer", particlePos);
		bindBuffer("velBuffer", particleVel);
		bindTexture("shade", shadeGrid);
		glUniformMatrix4fv("worldToCam", 1, false, icam.data);
		glUniformMatrix4fv("camToClip", 1, false, proj.data);
		//glEnable(GL_BLEND);
		//glBlendFunc(GL_ONE, GL_ONE);
		glEnable(GL_DEPTH_TEST);
		glDrawArrays(GL_POINTS, 0, N);
		//glDisable(GL_BLEND);

		font.drawText(std::to_wstring(prevTime) + L"ms", 5.f, 5.f, 15.f, screenw - 5);

		swapBuffers();
		prevTime = elapsedTime(start, end);
		
		glClearColor(.0f, .0f, .0f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
	return 0;
}
