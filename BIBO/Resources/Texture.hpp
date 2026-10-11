#pragma once

#include <memory>
#include <string>
#include <GL/glew.h>

using namespace std;

namespace texSettings
{
	extern bool bilinear; // 0 = NEAREST (neighbor), 1 = (BI)LINEAR
	extern unsigned int mipMappingQuality; //0 = Off, 1 = Nearest Neighbor, 2 = Linear
}

namespace bibo
{

	class Texture
	{
		GLuint handle;
		int textureUnit;
		void upload(int width, int height, const unsigned char* rgbaPixels);
	public:
		/**
		* Loads an image file (PNG, JPG, TGA, ...). If the file cannot be loaded, a magenta placeholder is used.
		*/
		Texture(const string& path, int unit, bool repeat);
		Texture(const string& path, int unit);
		/**
		* Creates a texture from RGBA pixels (4 bytes per pixel)
		*/
		Texture(int width, int height, const unsigned char* rgbaPixels, int unit, bool repeat);
		~Texture();
		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		/**
		* Black and white checkerboard (e.g. a chequered flag)
		*/
		static unique_ptr<Texture> createCheckerboard(int size, int squares, int unit);
		/**
		* Random grey pixels with dark scan lines, looks like the static of an analog TV
		*/
		static unique_ptr<Texture> createNoise(int size, unsigned int seed, int unit);

		void bind(int unit, bool bilinear, unsigned int mipMappingQuality);
		void bind(int unit);
		void bind();
		void unbind();
		GLuint getHandle(){ return handle; }
		//Repeated by default: models like the bear have texture coordinates outside of [0, 1]
		bool repeat = true;
	};
}

