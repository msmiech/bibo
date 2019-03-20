#include "Texture.hpp"

#include "../Util/Util.hpp"

using namespace std;
using namespace bibo;
using namespace util;

bool texSettings::bilinear = true;
unsigned int texSettings::mipMappingQuality = 2;

Texture::Texture(const wstring& path, int unit, bool repeat) : Texture(path, unit)
{
	this->repeat = repeat;
}

Texture::Texture(const wstring& path, int unit) : textureUnit(unit)
{
	glGenTextures(1, &handle);

	glBindTexture(GL_TEXTURE_2D, handle);


	unsigned int width, height, format, type;
	auto data = reinterpret_cast<GLuint*>(LoadTexture(path, width, height, format, type));

	glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
	if (format != GL_RGBA)
	{
		ilConvertImage(IL_RGBA, IL_UNSIGNED_BYTE);
	}
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glBindTexture(GL_TEXTURE_2D, 0);
}

Texture::~Texture() {
	glDeleteTextures(1, &handle);
}

void Texture::bind(int unit, bool bilinear, unsigned int mipMappingQuality)
{
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, handle);
	if (mipMappingQuality == 0)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, bilinear ? GL_LINEAR : GL_NEAREST);
	}
	else
	{
		if (mipMappingQuality == 1)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, bilinear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST);

		}
		else if (mipMappingQuality == 2)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, bilinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR);
		}
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, bilinear ? GL_LINEAR : GL_NEAREST);

	if (repeat)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	}
}

void Texture::bind(int unit)
{
	bind(unit, texSettings::bilinear, texSettings::mipMappingQuality);
}

void Texture::bind() {
	bind(textureUnit);
}

void Texture::unbind()
{
	glBindTexture(GL_TEXTURE_2D, 0);
}