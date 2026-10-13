#pragma once

#include <string>
#include <GL/glew.h>

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

		/**
		* Reads a shader file and replaces the lines #include "file" with the content of the file
		* (relative to the folder of the including file, e.g. the shared lighting code in Shader/include)
		* @param error Description of a missing file
		* @return The source code, empty if a file is missing
		*/
		static string preprocess(const string& path, string& error);

	private:
		void loadShader(const string& shader, 
						GLenum shaderType,
						GLuint& handle);
		void link();

		GLuint vertexHandle;
		GLuint fragmentHandle;

	};
}