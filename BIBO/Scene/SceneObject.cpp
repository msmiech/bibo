#include "SceneObject.hpp"


using namespace bibo::scene;



SceneObject::SceneObject(GLFWwindow& window) : modelMatrix(mat4(1.0f)), window(&window)
{
	
}
SceneObject::SceneObject(mat4 modelMatrix, GLFWwindow& window) : modelMatrix(modelMatrix), window(&window)
{
	
}


SceneObject::~SceneObject() {

}

vec3 SceneObject::getPosition()
{
	return vec3(modelMatrix[3]);
}


void SceneObject::setPosition(vec3 position)
{
	modelMatrix[3].x = position.x;
	modelMatrix[3].y = position.y;
	modelMatrix[3].z = position.z;
}


/**
* This method is based on https://www.opengl.org/discussion_boards/showthread.php/159215-Is-it-possible-to-extract-rotation-translation-scale-given-a-matrix
*/
vec3 SceneObject::getDirection()
{
	float sinPitch, cosPitch, sinRoll, cosRoll, sinYaw, cosYaw;

	sinPitch = -modelMatrix[2][0];
	cosPitch = sqrt(1 - sinPitch*sinPitch);

	if (abs(cosPitch) > 0.00001f)
	{
		sinRoll = modelMatrix[2][1] / cosPitch;
		cosRoll = modelMatrix[2][2] / cosPitch;
		sinYaw = modelMatrix[1][0] / cosPitch;
		cosYaw = modelMatrix[0][0] / cosPitch;
	}
	else
	{
		sinRoll = -modelMatrix[1][2];
		cosRoll = modelMatrix[1][1];
		sinYaw = 0;
		cosYaw = 1;
	}

	float yaw = atan2(sinYaw, cosYaw); //in radians
	float pitch = atan2(sinPitch, cosPitch);
	float roll = atan2(sinRoll, cosRoll);
	return vec3(yaw, pitch, roll);
}
