#pragma once

#include <string>
#ifndef GLEW_INCLUDED
#define GLEW_INCLUDED
#include <GL\glew.h>
#include <gl\wglew.h>
#include <GL\GL.h>
#endif

using namespace std;

namespace bibo {

	/**
	Reads mainShader file
	*/
	class Shader
	{
	public:
		/**
		@param vertexShader Path to vertex mainShader file *.vert
		@param fragmentShader Path to fragment mainShader file *.frag
		**/
		Shader(const string& vertexShader,
			const string& fragmentShader);
		~Shader();

		/**
		* useShader() formerly
		* sometimes only "Use()"
		*/
		void enable() const;

		GLint getUniformLocation(const char* strUniform);
		GLint getAttribLocation(const char* strAttrib);
		GLuint programHandle;

	private:
		void loadShader(const string& shader, 
						GLenum shaderType,
						GLuint& handle);
		void link();

		GLuint vertexHandle;
		GLuint fragmentHandle;

	};
}