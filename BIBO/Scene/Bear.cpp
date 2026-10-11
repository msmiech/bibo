#include "Bear.hpp"
#include <cmath>
#include <iostream>
#include <glm/gtx/rotate_vector.hpp>
#include "../Util/Util.hpp"

using namespace bibo;
using namespace scene;

//movement attributes (per second)
const float forwardVelocity = 10.f, backwardVelocity = -7.5f, maxJumpTime = 0.5f;
//The jump and the fall were tuned at 60 FPS: rising by 10 per second minus the gravity of 2 frames, falling by 0.981 per frame and second of falling
const float jumpVelocity = 10.f - 2.f * 0.981f, gravity = -0.981f * 60.f;
float jumpTimeLeft = 0, totalFallingTime = 0;
bool standingOnGround = false;

//animation attributes
float frontAngle = 0.0f;
float backAngle = 0.0f;
float rotationYAngle = 0.0f;
bool reverseAnim = false;
bool characterMoved = false;
bool characterRotated = false;
bool characterJumped = false;

Bear::Bear(mat4 modelMatrix, Shader* shader, GLFWwindow& window, const aiScene* assimpScene, Texture* texture, physics::PhysicsWorld* physicsWorld, physics::CharacterController::HitCallback onHit)
	: Geometry(modelMatrix, shader, window, assimpScene, "BearBody", texture), initialTransform(this->modelMatrix), direction(vec3(0.0f, 0.0f, -1.0f)),
	leftHand(mat4(1.f), shader, window, assimpScene, "LeftHand", texture),
	rightHand(mat4(1.f), shader, window, assimpScene, "RightHand", texture),
	leftLeg(mat4(1.f), shader, window, assimpScene, "LeftLeg", texture),
	rightLeg(mat4(1.f), shader, window, assimpScene, "RightLeg", texture)
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

	characterController = make_unique<physics::CharacterController>(*physicsWorld, characterControllerDesc(position), std::move(onHit));

	Material legMaterial;
	legMaterial.ambient = vec3(0.8f, 0.8f, 0.8f);
	legMaterial.diffuse = vec3(0.8f, 0.8f, 0.8f);
	legMaterial.specular = vec3(0.87f, 1.0f, 0.65f);
	legMaterial.shininess = 10.0f;
	leftLeg.getModel()->meshes[0]->setMaterial(legMaterial);
	rightLeg.getModel()->meshes[0]->setMaterial(legMaterial);
	leftHand.getModel()->meshes[0]->setMaterial(legMaterial);
	rightHand.getModel()->meshes[0]->setMaterial(legMaterial);
}

Bear::~Bear()
{
}

physics::CharacterControllerDesc Bear::characterControllerDesc(vec3 position)
{
	physics::CharacterControllerDesc desc; //A controller is a kinematic capsule, moved by the game logic
	desc.position = position; //initial position
	desc.contactOffset = 0.05f; //controller skin within which contacts generated
	desc.stepOffset = 1.0f; //max obstacle height the character can climb
	desc.slopeLimit = radians(45.0f); // max slope the character can walk
	desc.radius = 1.0f; //radius of the capsule
	desc.height = 0.6f; //height of the controller
	return desc;
}

void Bear::update(float time_delta)
{
	auto rightKey = glfwGetKey(window, GLFW_KEY_RIGHT);
	auto leftKey = glfwGetKey(window, GLFW_KEY_LEFT);
	auto forwardKey = glfwGetKey(window, GLFW_KEY_UP);
	auto backKey = glfwGetKey(window, GLFW_KEY_DOWN);
	auto jumpKey = glfwGetKey(window, GLFW_KEY_SPACE);
	characterMoved = false;
	characterRotated = false;
	characterJumped = false;

	/*
	 The controller is moved once per frame with characterController->move(displacement, time_delta):
	 the first parameter is the displacement (direction and distance) of this frame, the result tells
	 whether the bear stands on the ground after the move (collision below the capsule).*/

	vec3 displacement(0.0f);

	if (rightKey == GLFW_PRESS)
	{
		modelMatrix = rotate(modelMatrix, radians(-90.0f) * time_delta, vec3(0.0f, 0.0f, 1.0f));
		direction = glm::rotateY(direction, radians(-90.0f) * time_delta);
		direction = normalize(direction);
		characterRotated = true;
		rotationYAngle -= 100 * time_delta;
	}
	if (leftKey == GLFW_PRESS)
	{
		modelMatrix = rotate(modelMatrix, radians(90.0f) * time_delta, vec3(0.0f, 0.0f, 1.0f));
		direction = glm::rotateY(direction, radians(90.0f) * time_delta);
		direction = normalize(direction);
		characterRotated = true;
		rotationYAngle += 100 * time_delta;
	}
	if (forwardKey == GLFW_PRESS)
	{
		displacement += direction * forwardVelocity * time_delta;
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
	if (backKey == GLFW_PRESS)
	{
		displacement += direction * backwardVelocity * time_delta;
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
	//The bear rises while SPACE is held (at most maxJumpTime), the next jump is only possible after landing
	if (jumpKey == GLFW_PRESS && jumpTimeLeft > 0)
	{
		jumpTimeLeft -= time_delta;
		totalFallingTime = 0.0f;
		displacement.y += jumpVelocity * time_delta;
		characterJumped = true;
	}
	else
	{
		if (!standingOnGround)
		{
			jumpTimeLeft = 0.0f; //SPACE was released in the air
		}
		//The longer the bear falls, the faster it falls (on the ground the controller ignores the downward movement)
		totalFallingTime += time_delta;
		displacement.y += gravity * totalFallingTime * time_delta;
	}

	standingOnGround = characterController->move(displacement, time_delta);
	if (standingOnGround && !characterJumped)
	{
		jumpTimeLeft = maxJumpTime;
		totalFallingTime = 1.0f; //falls quickly when walking over an edge
	}

	setPosition(characterController->getPosition());

	if (std::abs(frontAngle) > 50.f || std::abs(backAngle) > 50.f)
	{
		frontAngle = frontAngle > 0 ? 50.f : -50.f;
		backAngle = backAngle > 0 ? 50.f : -50.f;
		reverseAnim = !reverseAnim;
	}
	if (std::abs(rotationYAngle) > 50.f)
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

bool Bear::isVisible(const ViewFrustum& frustum)
{
	//The limbs stick out of the body
	return Geometry::isVisible(frustum) || leftHand.isVisible(frustum) || rightHand.isVisible(frustum) || leftLeg.isVisible(frustum) || rightLeg.isVisible(frustum);
}

void Bear::reset()
{
	this->modelMatrix = initialTransform;
	direction = vec3(0.0f, 0.0f, -1.0f);
	characterController->setPosition(SceneObject::getPosition());

	jumpTimeLeft = 0.0f;
	totalFallingTime = 0.0f;
	standingOnGround = false;
	frontAngle = 0.0f;
	backAngle = 0.0f;
	rotationYAngle = 0.0f;
	reverseAnim = false;
}
