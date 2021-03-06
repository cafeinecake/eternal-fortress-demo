
#include <cmath>

void lookAt(float* cameraToWorld, float camx, float camy, float camz, float atx, float aty, float atz, float upx, float upy, float upz) {
	atx -= camx; aty -= camy; atz -= camz;
	float atrl = 1.f / sqrt(atx * atx + aty * aty + atz * atz); atx *= atrl; aty *= atrl; atz *= atrl;
	float uprl = 1.f / sqrt(upx * upx + upy * upy + upz * upz); upx *= uprl; upy *= uprl; upz *= uprl;
	float rightx = aty * upz - atz * upy, righty = atz * upx - atx * upz, rightz = atx * upy - aty * upx;
	float rrl = 1.f / sqrt(rightx*rightx + righty * righty + rightz * rightz); rightx *= rrl; righty *= rrl; rightz *= rrl;
	upx = righty * atz - rightz * aty; upy = rightz * atx - rightx * atz; upz = rightx * aty - righty * atx;
	cameraToWorld[0] = rightx; cameraToWorld[1] = righty; cameraToWorld[2] = rightz; cameraToWorld[3] = .0f;
	cameraToWorld[4] = upx; cameraToWorld[5] = upy; cameraToWorld[6] = upz; cameraToWorld[7] = .0f;
	cameraToWorld[8] = -atx; cameraToWorld[9] = -aty; cameraToWorld[10] = -atz; cameraToWorld[11] = .0f;
	cameraToWorld[12] = camx; cameraToWorld[13] = camy; cameraToWorld[14] = camz; cameraToWorld[15] = 1.f;
}

void setupProjection(float* cameraToClip, float fov, float w_over_h, float nearPlane, float farPlane) {
	for (int i = 0; i < 16; ++i) cameraToClip[i] = .0f;
	const float right = nearPlane * w_over_h * tan(fov / 2.0f), bottom = nearPlane * tan(fov / 2.0f);
	cameraToClip[0] = nearPlane / right;
	cameraToClip[5] = nearPlane / bottom;
	cameraToClip[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
	cameraToClip[11] = -1.0f;
	cameraToClip[14] = -2.0f * farPlane * nearPlane / (farPlane - nearPlane);
}

void setupOrtho(float* cameraToClip, float w_over_h, float size, float nearPlane, float farPlane) {
	for (int i = 0; i < 16; ++i) cameraToClip[i] = .0f;
	cameraToClip[0] = 1.f / size / w_over_h;
	cameraToClip[5] = 1.f / size;
	cameraToClip[10] = -2.f / (farPlane - nearPlane);
	cameraToClip[14] = -1.f - 2.f*nearPlane / (farPlane - nearPlane);
	cameraToClip[15] = 1.f;
}
