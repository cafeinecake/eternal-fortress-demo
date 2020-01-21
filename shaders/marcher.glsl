#version 460
// the local thread block size; the program will be ran in sets of 16 by 16 threads.
layout(local_size_x = 16, local_size_y = 16) in;

// simple pseudo-RNG based on the jenkins hash mix function
uvec4 rndseed;
void jenkins_mix()
{
	rndseed.x -= rndseed.y; rndseed.x -= rndseed.z; rndseed.x ^= rndseed.z >> 13;
	rndseed.y -= rndseed.z; rndseed.y -= rndseed.x; rndseed.y ^= rndseed.x << 8;
	rndseed.z -= rndseed.x; rndseed.z -= rndseed.y; rndseed.z ^= rndseed.y >> 13;
	rndseed.x -= rndseed.y; rndseed.x -= rndseed.z; rndseed.x ^= rndseed.z >> 12;
	rndseed.y -= rndseed.z; rndseed.y -= rndseed.x; rndseed.y ^= rndseed.x << 16;
	rndseed.z -= rndseed.x; rndseed.z -= rndseed.y; rndseed.z ^= rndseed.y >> 5;
	rndseed.x -= rndseed.y; rndseed.x -= rndseed.z; rndseed.x ^= rndseed.z >> 3;
	rndseed.y -= rndseed.z; rndseed.y -= rndseed.x; rndseed.y ^= rndseed.x << 10;
	rndseed.z -= rndseed.x; rndseed.z -= rndseed.y; rndseed.z ^= rndseed.y >> 15;
}
void srand(uint A, uint B, uint C) { rndseed = uvec4(A, B, C, 0); jenkins_mix(); jenkins_mix(); }
float rand()
{
	if (0 == rndseed.w++ % 3) jenkins_mix();
	return float((rndseed.xyz = rndseed.yzx).x) / pow(2., 32.);
}

const int MATERIAL_SKY = 0;
const int MATERIAL_OTHER = 1;

// uniform variables are global from the glsl perspective; you set them in the CPU side and every thread gets the same value
uniform int source;
uniform int frame;
uniform float secs;

layout(r32f) uniform image2D zbuffer;
layout(rgba16f) uniform image2DArray samplebuffer;

struct CameraParams {
    vec3 pos;
	float padding;
	vec3 dir;
	float zoom;
	vec3 up;
	float padding2;
	vec3 right;
	float padding3;
};

layout(std140) uniform cameraArray {
	CameraParams cameras[2];
};

vec2 getThreadUV(uvec3 id) {
	return vec2(id.xy) / 1024.0;
}

// mandelbox distance function by Rrola (Buddhi's distance estimation)
// http://www.fractalforums.com/index.php?topic=2785.msg21412#msg21412

const float SCALE = 2.7;
const float MR2 = 0.01;
vec4 mbox_scalevec = vec4(SCALE, SCALE, SCALE, abs(SCALE)) / MR2;
float mbox_C1 = abs(SCALE-1.0);

float mandelbox(vec3 position, int iters=7) {
    float mbox_C2 = pow(abs(SCALE), float(1-iters));
	// distance estimate
	vec4 p = vec4(position.xyz, 1.0), p0 = vec4(position.xyz, 1.0);  // p.w is knighty's DEfactor
	for (int i=0; i<iters; i++) {
		p.xyz = clamp(p.xyz, -1.0, 1.0) * 2.0 - p.xyz;  // box fold: min3, max3, mad3
		float r2 = dot(p.xyz, p.xyz);  // dp3
		p.xyzw *= clamp(max(MR2/r2, MR2), 0.0, 1.0);  // sphere fold: div1, max1.sat, mul4
		p.xyzw = p*mbox_scalevec + p0;  // mad4
	}
	float d = (length(p.xyz) - mbox_C1) / p.w - mbox_C2;
    if (iters == 5) d += 1.0e-2; // hacky inflation fix
    if (iters == 6) d += 2e-3;
    return d;
}

float scene(vec3 p, out int material, float pixelConeSize=1.) {
	material = MATERIAL_OTHER;
	return mandelbox(p, 7);
}

vec3 evalnormal(vec3 p) {
	vec2 e=vec2(1e-5, 0.f);
	int m;
	return normalize(vec3(
				scene(p + e.xyy,m) - scene(p - e.xyy,m),
				scene(p + e.yxy,m) - scene(p - e.yxy,m),
				scene(p + e.yyx,m) - scene(p - e.yyx,m)
				));
}

void getCameraProjection(CameraParams cam, vec2 uv, out vec3 outPos, out vec3 outDir) {
	uv /= cam.zoom;
	// vec3 right = cross(cam.dir, vec3(0., 1., 0.));
	// vec3 up = cross(cam.dir, right);
	outPos = cam.pos + cam.dir + (uv.x - 0.5) * cam.right + (uv.y - 0.5) * cam.up;
	outDir = normalize(outPos - cam.pos);
}

// absolute error (measured with euclidian length) seems to be less than 0.003
float packJitter(vec2 jitter)
{
    vec2 a = jitter + vec2(0.5);
    a *= 255;
    uvec2 b = uvec2(a);
    uint c = (b.x << 8) | b.y;
    return uintBitsToFloat(c);
}

vec2 unpackJitter(float d)
{
    uint c = floatBitsToUint(d);
    uvec2 b = uvec2((c >> 8) & 0xff, c & 0xff);
    vec2 a = vec2(b) / 255;
    return a - vec2(0.5);
}

float march(inout vec3 p, vec3 rd, out int material, int num_iters, float maxDist=1e9) {
    vec3 ro = p;
    int i;
    float t = 0.;

    for (i = 0; i < num_iters; i++) {
        int mat;

        p = ro + t * rd;
        float d = scene(p, mat);
        if (d < t * 1e-5) {
            material = mat;
            break;
        }

        if (t >= maxDist) {
            break;
        }

        t += d;
    }

    // See "Enhanced Sphere Tracing" section 3.4. and
    // section 3.1.1 in "Efficient Antialiased Rendering of 3-D Linear Fractals"
    for (int i = 0; i < 3; i++) {
        //float e = t*((2.0 * sin((uFOV/360.0)*2.0*3.1416*0.5)) / iResolution.x);
        const float uFOV = 90.; // TODO: use real fov
        float e = t*(2.0 * sin(uFOV*0.00873) / imageSize(samplebuffer).x);
        t += scene(ro + t*rd, material) - e;
    }


    return t;

}

void main() {
    uint seed = uint(gl_GlobalInvocationID.x) + uint(gl_GlobalInvocationID.y);
    srand(seed, seed, seed);
    seed = uint(rand()*9999 + frame);
    ivec2 res = imageSize(zbuffer).xy;
    int samplesPerPixel = imageSize(samplebuffer).z;

    float minDepth = 1e20;
    vec3 accum = vec3(0.);

    for (int sample_id=0; sample_id < samplesPerPixel; sample_id++)
    {
        vec2 jitter = vec2(rand(), rand()) - vec2(0.5, 0.5);

        vec2 uv = getThreadUV(gl_GlobalInvocationID);
        jitter /= imageSize(zbuffer).xy;
        uv += jitter;

        CameraParams cam = cameras[1];
        vec3 p, dir;
        getCameraProjection(cam, uv, p, dir);

        int hitmat = MATERIAL_SKY;
        float travel = march(p, dir, hitmat, 300);

        vec3 color;
        vec3 skyColor = vec3(0., 0.2*abs(dir.y), 0.5 - dir.y);
        //if (hitmat == MATERIAL_SKY && travel < 100.) { hitmat = MATERIAL_OTHER; }

        float depth = length(p - cam.pos);
        switch (hitmat) {
            case MATERIAL_SKY:
                color = skyColor;
                break;
            case MATERIAL_OTHER:

                vec3 normal = evalnormal(p);
                vec3 to_camera = normalize(cam.pos - p);
                vec3 to_light = normalize(vec3(-0.5, -1.0, 0.7));

                int shadowMat = MATERIAL_SKY;
                vec3 shadowRayPos = p+to_camera*(0.001 + 0.0009 * rand());
                const float maxShadowDist = 20.;
                float lightDist = march(shadowRayPos, to_light, shadowMat, 60, maxShadowDist);
                float sun = min(lightDist, maxShadowDist) / maxShadowDist;

                float shine = max(0., dot(normal, to_light));
                vec3 base = vec3(1.); //vec3(0.5) + .5*sin(vec3(p) * 50.);
                color = base * sun * vec3(shine);
                color = clamp(color, vec3(0.), vec3(10.));
                float fog = pow(min(1., depth / 10.), 4.0);
                color = mix(color, vec3(0.5, 0., 0.), fog);
                break;
        }

        accum += color;
        minDepth = min(minDepth, depth);

        imageStore(samplebuffer, ivec3(gl_GlobalInvocationID.xy, sample_id), vec4(color, packJitter(jitter)));
    }

    imageStore(zbuffer, ivec2(gl_GlobalInvocationID.xy), vec4(minDepth));
}

