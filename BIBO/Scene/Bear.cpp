#include "Bear.hpp"
#include <iostream>
#include "../Util/Util.hpp"

using namespace bibo;
using namespace scene;

//movement attributes
const float forwardVelocity = 10.f, backwardVelocity = -7.5f, jumpVelocity = 10.f, gravity = -0.981f, maxJumpTime = 0.5f, maxJumpTimeOut = 0.33f;
float jumpTimeLeft = 0, totalFallingTime = 0, jumpTimeOut = 0.0f;

//animation attributes
float frontAngle = 0.0f;
float backAngle = 0.0f;
float rotationYAngle = 0.0f;
bool reverseAnim = false;
bool characterMoved = false;
bool characterRotated = false;
bool characterJumped = false;

Bear::Bear(mat4 modelMatrix, Shader* shader, GLFWwindow& window, Texture* texture, PxPhysics* mPhysics, PxControllerManager* manager, PxUserControllerHitReport* scene)
	: Geometry(modelMatrix, shader, window, "Model/SnowTerrain/track1.dae", "BearBody", texture), mPhysics(mPhysics), initialTransform(modelMatrix), direction(vec3(0.0f, 0.0f, -1.0f)),
	leftHand(Geometry(mat4(1.f), shader, window, "Model/SnowTerrain/track1.dae", "LeftHand", texture)),
	rightHand(Geometry(mat4(1.f), shader, window, "Model/SnowTerrain/track1.dae", "RightHand", texture)),
	leftLeg(Geometry(mat4(1.f), shader, window, "Model/SnowTerrain/track1.dae", "LeftLeg", texture)),
	rightLeg(Geometry(mat4(1.f), shader, window, "Model/SnowTerrain/track1.dae", "RightLeg", texture))
{

	vec3 position = SceneObject::getPosition();
	float temp;
	leftHandOffSet = leftHand.getPosition() - position;
	temp = leftHandOffSet.y;
	leftHandOffSet.y = leftHandOffSet.z;
	leftHandOffSet.z = temp;
	leftHandOffSet.x = leftHandOffSet.x + 0.7f;
	rightHandOffSet = rightHand.getPosition() - position;
	temp = rightHandOffSet.y;
	rightHandOffSet.y = rightHandOffSet.z;
	rightHandOffSet.z = temp;
	rightHandOffSet.x = rightHandOffSet.x + 0.7f;
	leftLegOffSet = leftLeg.getPosition() - position;
	temp = leftLegOffSet.y;
	leftLegOffSet.y = leftLegOffSet.z;
	leftLegOffSet.z = temp;
	leftLegOffSet.x = leftLegOffSet.x + 1.25f;
	rightLegOffSet = rightLeg.getPosition() - position;
	temp = rightLegOffSet.y;
	rightLegOffSet.y = rightLegOffSet.z;
	rightLegOffSet.z = temp;
	rightLegOffSet.x = rightLegOffSet.x - 1.25f;

	PxCapsuleControllerDesc desc; //A controller is a special kinematic PxRigidDynamic
	desc.position = PxExtendedVec3(position.x, position.y, position.z); //initial position
	desc.contactOffset = 0.05f; //controller skin within which contacts generated
	desc.stepOffset = 1.0f; //max obstacle height the character can climb
	desc.slopeLimit = cosf(radians(45.0f)); // max slope the character can walk
	desc.radius = 1.0f; //radius of the capsule
	desc.height = 0.6f; //height of the controller
	desc.upDirection = PxVec3(0.0f, 1.0f, 0.0f); // Specifies the 'up' direction
	desc.reportCallback = scene;


	PxMaterial* testMat = mPhysics->createMaterial(10.0f, 10.0f, 10.0f);
	desc.material = testMat; // material defines physical properties like friction, bounciness etc. - see PhysX materials

	pxController = manager->createController(desc);
	pxActor = pxController->getActor();
	pxActor->setGlobalPose(PxTransform(util::convert(modelMatrix)));
	pxControllerFilters = new PxControllerFilters();

	Material legMaterial;
	legMaterial.ambient = vec3(0.8f, 0.8f, 0.8f);
	legMaterial.diffuse = vec3(0.8f, 0.8f, 0.8f);
	legMaterial.specular = vec3((0.87f, 1.0f, 0.65f));
	legMaterial.shininess = 10.0f;
	leftLeg.getModel()->meshes[0]->setMaterial(legMaterial);
	rightLeg.getModel()->meshes[0]->setMaterial(legMaterial);
	leftHand.getModel()->meshes[0]->setMaterial(legMaterial);
	rightHand.getModel()->meshes[0]->setMaterial(legMaterial);
}

Bear::~Bear()
{
	pxController->release();
}

void Bear::update(float time_delta)
{
	auto rightKey = glfwGetKey(window, GLFW_KEY_RIGHT);
	auto leftKey = glfwGetKey(window, GLFW_KEY_LEFT);
	auto forwardKey = glfwGetKey(window, GLFW_KEY_UP);
	auto backKey = glfwGetKey(window, GLFW_KEY_DOWN);
	auto fallKey = glfwGetKey(window, GLFW_KEY_C);
	auto jumpKey = glfwGetKey(window, GLFW_KEY_SPACE);
	characterMoved = false;
	characterRotated = false;
	characterJumped = false;

	/*
	 Bewegung des Controllers funktionert mit Hilfe der move Methode:
	 flags = c->move(direction * speed * deltaT, minDistance, deltaT, controllerFilters);
	 Der erste Parameter umfasst nicht nur die Richtung der Bewegung, sondern auch die Distanz (daher muss auch der Speed bzw. deltaT miteinberechnet werden). minDistance ist die Distanz, bei der der rekursive Algorithmus für die Kollision gestoppt wird, am besten auf einen Wert zwischen 0.001f und 0.1f setzen (einfach ein wenig testen). Zum Schluss gibt es noch die Möglichkeit die Kollisionen einzuschränken mit den controllerFilters. Diese sind vom Typ PxControllerFilters und rein der Aufruf des Standardkonstruktors reicht um den Controller mit allen Objekten in der Szene kollidieren zu lassen. Als Rückgabetype liefert die move Methode flags vom Typ PxControllerFlag mit denen sich diverse Informationen festgestellen lassen, ob und wie der Character kollidiert ist.*/

	PxControllerCollisionFlags pxMoveResult;

	if (rightKey == GLFW_PRESS)
	{
		modelMatrix = rotate(modelMatrix, radians(-90.0f) * time_delta, vec3(0.0f, 0.0f, 1.0f));
		direction = util::rotateY(direction, radians(-90.0f) * time_delta);
		direction = normalize(direction);
		characterRotated = true;
		rotationYAngle -= 100 * time_delta;
	}
	if (leftKey == GLFW_PRESS)
	{
		modelMatrix = rotate(modelMatrix, radians(90.0f) * time_delta, vec3(0.0f, 0.0f, 1.0f));
		direction = util::rotateY(direction, radians(90.0f) * time_delta);
		direction = normalize(direction);
		characterRotated = true;
		rotationYAngle += 100 * time_delta;
	}
	if (forwardKey == GLFW_PRESS)
	{
		if (pxController)
		{
			pxMoveResult = pxController->move(util::convert(direction * forwardVelocity * time_delta), 0.00001f, time_delta, nullptr);
			if (reverseAnim)
			{
				frontAngle -= 125 * time_delta;
				backAngle += 125 * time_delta;
			}
			else
			{
				frontAngle += 150 * time_delta;
				backAngle -= 150 * time_delta;
			}
			characterMoved = true;

		}
	}
	if (backKey == GLFW_PRESS)
	{
		if (pxController)
		{
			pxMoveResult = pxController->move(util::convert(direction * backwardVelocity * time_delta), 0.00001f, time_delta, nullptr);
			if (reverseAnim)
			{
				frontAngle += 100 * time_delta;
				backAngle -= 100 * time_delta;
			}
			else
			{
				frontAngle -= 100 * time_delta;
				backAngle += 100 * time_delta;
			}
			characterMoved = true;
		}
	}
	if (jumpKey == GLFW_PRESS)
	{
		if (pxController && jumpTimeLeft > 0)
		{
			jumpTimeLeft -= time_delta;
			if (jumpTimeLeft <= 0)
			{
  				jumpTimeOut = maxJumpTimeOut;
			}
			if(!characterJumped) {
				totalFallingTime = time_delta;
			}
			pxMoveResult = pxController->move(PxVec3(0.0f, jumpVelocity * time_delta, 0.0f), 0.00001f, time_delta, nullptr);
			characterJumped = true;
		}
	}
	else characterJumped = false;

	if (pxMoveResult != PxControllerCollisionFlag::eCOLLISION_DOWN) //bear is airborne
	{
		totalFallingTime += time_delta;
		if (jumpTimeOut > 0)
		{
			jumpTimeOut -= time_delta;
		}
		pxController->move(PxVec3(0.0f, gravity * totalFallingTime, 0.0f), 0.00001f, time_delta, nullptr);
	}
	if (pxMoveResult == PxControllerCollisionFlag::eCOLLISION_DOWN)
	{
		jumpTimeLeft = maxJumpTime;
		jumpTimeOut = 0.0f;
		totalFallingTime = 1.0f;
	}
	if (jumpTimeOut <= 0 && !characterJumped)
	{
		jumpTimeOut = 0.0f;
		jumpTimeLeft = maxJumpTime;
	}

	setPosition(util::convert(pxController->getPosition()));

	if (abs(frontAngle) > 50.f || abs(backAngle) > 50.f)
	{
		frontAngle = frontAngle > 0 ? 50.f : -50.f;
		backAngle = backAngle > 0 ? 50.f : -50.f;
		reverseAnim = !reverseAnim;
	}
	if (abs(rotationYAngle) > 50.f)
	{
		rotationYAngle = rotationYAngle > 0 ? 45.f : -45.f;
	}

	leftHand.modelMatrix = translate(modelMatrix, leftHandOffSet);
	leftLeg.modelMatrix = translate(modelMatrix, leftLegOffSet);
	rightHand.modelMatrix = translate(modelMatrix, rightHandOffSet);
	rightLeg.modelMatrix = translate(modelMatrix, rightLegOffSet);

	if (characterJumped)
	{
		//Optional: Display jump animation
	}
	else if (characterMoved)
	{
		leftHand.modelMatrix = rotate(leftHand.modelMatrix, radians(frontAngle), vec3(1, 0, 0));
		rightHand.modelMatrix = rotate(rightHand.modelMatrix, radians(frontAngle), vec3(1, 0, 0));
		leftLeg.modelMatrix = rotate(leftLeg.modelMatrix, radians(backAngle), vec3(1, 0, 0));
		rightLeg.modelMatrix = rotate(rightLeg.modelMatrix, radians(backAngle), vec3(1, 0, 0));
	}
	else if (characterRotated)
	{
		leftHand.modelMatrix = rotate(leftHand.modelMatrix, radians(rotationYAngle), vec3(0, 0, 1));
		rightHand.modelMatrix = rotate(rightHand.modelMatrix, radians(rotationYAngle), vec3(0, 0, 1));
		leftLeg.modelMatrix = rotate(leftLeg.modelMatrix, radians(rotationYAngle), vec3(0, 0, 1));
		rightLeg.modelMatrix = rotate(rightLeg.modelMatrix, radians(rotationYAngle), vec3(0, 0, 1));
	}
	else
	{
		frontAngle = 0.f;
		backAngle = 0.f;
		rotationYAngle = 0.f;
	}
}

void Bear::draw(Shader* shader)
{
	rightHand.draw(shader);
	leftHand.draw(shader);
	leftLeg.draw(shader);
	rightLeg.draw(shader);

	Geometry::draw(shader);
}

vec3 Bear::getDirection()
{
	return direction;
}

void Bear::reset()
{
	this->modelMatrix = initialTransform;
}
