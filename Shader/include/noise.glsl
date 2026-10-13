#ifndef NOISE_GLSL
#define NOISE_GLSL
//Hash based noise for the shaders (value noise, fractal noise and cells)
//The hash functions are from "Hash without Sine" by David Hoskins (MIT license, https://www.shadertoy.com/view/4djSRW)

float hash12(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float hash13(vec3 p3)
{
	p3 = fract(p3 * 0.1031);
	p3 += dot(p3, p3.zyx + 31.32);
	return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.xx + p3.yz) * p3.zy);
}

float hash11(float p)
{
	p = fract(p * 0.1031);
	p *= p + 33.33;
	p *= p + p;
	return fract(p);
}

/**
* Value noise from 0 to 1, smooth (quintic)
*/
float valueNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	float a = hash12(i), b = hash12(i + vec2(1.0, 0.0)), c = hash12(i + vec2(0.0, 1.0)), d = hash12(i + vec2(1.0, 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float valueNoise(vec3 p)
{
	vec3 i = floor(p);
	vec3 f = fract(p);
	vec3 u = f * f * (3.0 - 2.0 * f);
	float n000 = hash13(i), n100 = hash13(i + vec3(1, 0, 0)), n010 = hash13(i + vec3(0, 1, 0)), n110 = hash13(i + vec3(1, 1, 0));
	float n001 = hash13(i + vec3(0, 0, 1)), n101 = hash13(i + vec3(1, 0, 1)), n011 = hash13(i + vec3(0, 1, 1)), n111 = hash13(i + vec3(1, 1, 1));
	return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y), mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

float valueNoise(float x)
{
	float i = floor(x);
	float f = fract(x);
	return mix(hash11(i), hash11(i + 1.0), f * f * (3.0 - 2.0 * f));
}

/**
* Fractal value noise from 0 to 1
*/
float fbm(vec2 p, int octaves)
{
	float sum = 0.0, amplitude = 0.5, total = 0.0;
	const mat2 rotation = mat2(0.8, 0.6, -0.6, 0.8);
	for (int i = 0; i < octaves; ++i)
	{
		sum += amplitude * valueNoise(p);
		total += amplitude;
		p = rotation * p * 2.03 + vec2(13.7, 7.1);
		amplitude *= 0.5;
	}
	return sum / total;
}

/**
* Value noise that repeats every period cells (for tileable textures), from 0 to 1
*/
float tiledNoise(vec2 p, float period)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	vec2 i00 = mod(i, period), i11 = mod(i + 1.0, period);
	float a = hash12(i00), b = hash12(vec2(i11.x, i00.y)), c = hash12(vec2(i00.x, i11.y)), d = hash12(i11);
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

/**
* Like tiledNoise, but with a different period along each axis (e.g. stretched along one axis)
*/
float tiledNoise(vec2 p, vec2 period)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	vec2 i00 = mod(i, period), i11 = mod(i + 1.0, period);
	float a = hash12(i00), b = hash12(vec2(i11.x, i00.y)), c = hash12(vec2(i00.x, i11.y)), d = hash12(i11);
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

/**
* Fractal tileable noise: uv from 0 to 1 repeats, the first octave has period cells
*/
float tiledFbm(vec2 uv, float period, int octaves)
{
	float sum = 0.0, amplitude = 0.5, total = 0.0;
	for (int i = 0; i < octaves; ++i)
	{
		sum += amplitude * tiledNoise(uv * period, period);
		total += amplitude;
		period *= 2.0;
		amplitude *= 0.5;
	}
	return sum / total;
}

/**
* Tileable cells (Worley): x distance to the closest cell point, y to the second closest, z random value of the closest cell
*/
vec3 tiledCells(vec2 uv, vec2 period)
{
	vec2 p = uv * period;
	vec2 i = floor(p);
	vec2 f = fract(p);
	float closest = 8.0, second = 8.0, id = 0.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			vec2 cell = i + vec2(x, y);
			vec2 wrapped = mod(cell, period);
			vec2 point = vec2(x, y) + hash22(wrapped) - f;
			float d = dot(point, point);
			if (d < closest)
			{
				second = closest;
				closest = d;
				id = hash12(wrapped + 71.3);
			}
			else if (d < second)
			{
				second = d;
			}
		}
	}
	return vec3(sqrt(closest), sqrt(second), id);
}

vec3 tiledCells(vec2 uv, float period)
{
	return tiledCells(uv, vec2(period));
}

#endif
