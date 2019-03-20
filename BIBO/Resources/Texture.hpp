#pragma once

#include <string>
#ifndef GLEW_DEFINED
#define GLEW_DEFINED
#include <GL\glew.h>
#endif

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
	public:
		Texture(const wstring& path, int unit, bool repeat);
		Texture(const wstring& path, int unit);
		~Texture();

		void bind(int unit, bool bilinear, unsigned int mipMappingQuality);
		void bind(int unit);
		void bind();
		void unbind();
		GLuint getHandle(){ return handle; }
		bool repeat = false;
	};
}

