#include "Scene.hpp"
#include <algorithm>
#include <cmath>
#include "../Util/Console.hpp"

using namespace bibo;
using namespace scene;

namespace
{
	const char* const TRACK_FILE = "Model/SnowTerrain/track1.dae";
}

MeshTransform scene::terrainTransform(const mat4& modelMatrix)
{
	//Jolt only scales triangle meshes uniformly, so the scale of the x axis is used for all axes
	float scale = length(vec3(modelMatrix[0]));
	return { scale, quat_cast(mat3(modelMatrix) / scale), vec3(modelMatrix[3]) };
}

Scene::Scene(GLFWwindow* window, Shader* shader, physics::PhysicsWorld* physicsWorld) : window(window), physicsWorld(physicsWorld)
{
	shader->enable();

	geometries.clear();
	arrows.clear();

	// Clear the screen
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	//All geometries are nodes of the same scene file, which is only loaded once (it is ~20 MB of XML)
	util::ScenePtr track = util::loadSceneFromFile(TRACK_FILE);
	if (!track)
	{
		util::fatalError(string("Could not load ") + TRACK_FILE);
	}
	const aiScene* trackScene = track.get();

	lights = util::retrieveLightFromScene(*window, shader, trackScene);
	if (lights.empty())
	{
		util::fatalError(string("There is no light source in ") + TRACK_FILE);
	}
	camera = make_unique<Camera>(vec3(0.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, -1.0f), vec3(0.0f, 1.0f, 0.0f), *window);

	textures.push_back(Texture::createCheckerboard(256, 8, 0)); //0, chequered flag (generated, no image file)
	Texture* checkerboardTexPtr = textures[0].get();

	textures.push_back(make_unique<Texture>("Texture/cfur01.jpg", 0)); //1
	textures.push_back(make_unique<Texture>("Texture/sky1Tex.jpg", 0)); //2
	textures.push_back(make_unique<Texture>("Texture/snow.jpg", 0)); //3
	textures.push_back(make_unique<Texture>("Texture/snow-normal.jpg", 2)); //4
	textures.push_back(make_unique<Texture>("Texture/ground07.jpg", 0)); //5
	textures.push_back(make_unique<Texture>("Texture/wood16.jpg", 0)); //6
	textures.push_back(make_unique<Texture>("Texture/wood16n.jpg", 2)); //7
	textures.push_back(make_unique<Texture>("Texture/Water.png", 0));//8
	textures.push_back(make_unique<Texture>("Texture/ground07n.jpg", 2)); //9
	initAnimatedTextures();

	geometries.push_back(make_unique<Bear>(mat4(1.0f), shader, *window, trackScene, textures[1].get(), physicsWorld, [this](physics::BodyHandle body) { onBearHit(body); })); //0
	bear = static_cast<Bear*>(geometries[0].get());
	Material bearMaterial;
	bearMaterial.ambient = vec3(0.8f, 0.8f, 0.8f);
	bearMaterial.diffuse = vec3(0.8f, 0.8f, 0.8f);
	bearMaterial.specular = vec3(0.8f, 0.8f, 0.8f);
	bearMaterial.shininess = 10.0f;
	bear->getModel()->meshes[0]->setMaterial(bearMaterial);

	debugCube = make_unique<Cube>(mat4(1.0f), shader, *window, textures[6].get());


	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Boden", textures[3].get()));//1
	world = geometries[1].get();

	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "FinishLine", checkerboardTexPtr));//2
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock1", textures[5].get(), textures[9].get())); //3
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock2", textures[5].get(), textures[9].get())); //4
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock3", textures[5].get(), textures[9].get())); //5
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock4", textures[5].get(), textures[9].get())); //6
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock5", textures[5].get(), textures[9].get())); //7
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock6", textures[5].get(), textures[9].get())); //8
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock7", textures[5].get(), textures[9].get())); //9
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock8", textures[5].get(), textures[9].get())); //10
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock9", textures[5].get(), textures[9].get())); //11
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock10", textures[5].get(), textures[9].get())); //12
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock11", textures[5].get(), textures[9].get())); //13
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock12", textures[5].get(), textures[9].get())); //14
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Torus", textures[3].get())); //15
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Water", textures[8].get())); //16
	water = geometries[16].get();
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Cube", textures[6].get(), textures[7].get())); //17

	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Monitor1", checkerboardTexPtr)); //18

	monitor1 = geometries[18].get();
	Material monitorMaterial;
	monitorMaterial.ambient = vec3(0.8f, 0.8f, 0.8f);
	monitorMaterial.diffuse = vec3(0.8f, 0.8f, 0.8f);
	monitorMaterial.specular = vec3(0.8f, 0.8f, 0.8f);
	monitorMaterial.shininess = 1.0f;
	monitor1->getModel()->meshes[0]->setMaterial(monitorMaterial);


	arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Pfeil0", checkerboardTexPtr)); //18
	arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Pfeil1", checkerboardTexPtr)); //19
	arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Pfeil2", checkerboardTexPtr)); //20
	arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Pfeil3", checkerboardTexPtr)); //21
	initPhysicsBodies();
	initFinishLine(geometries[2].get());

	rotatingRock = make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock1", textures[5].get(), textures[9].get());
	rotatingRock->modelMatrix = translate(rotatingRock->modelMatrix, vec3(10, 25, 1));
}

void Scene::update(float time_delta) {
	physicsWorld->update(time_delta);
	if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)
	{
		restart();
	}
	if (gameWon != true && gameOver != true){
	for (unsigned int i = 0; i < geometries.size(); ++i)
	{
		geometries[i]->update(time_delta);
	}
	accumulatedVideoTime += time_delta;
	if (accumulatedVideoTime > 0.1f)
	{
		accumulatedVideoTime = 0.f;
		monitor1->getModel()->meshes[0]->setTexture(monitorVideo[++monitorFrame % monitorVideo.size()].get());
		water->getModel()->meshes[0]->setTexture(waterVideo[++waterFrame % waterVideo.size()].get());
	}

	debugCube->update(time_delta);

	if (camera->isLocked())
	{
		camera->setTarget(bear);
	}
	camera->update(time_delta);
	rotatingRock->modelMatrix = rotate(rotatingRock->modelMatrix, radians(90.0f) * time_delta, vec3(1, 1, 0));

	vec3 playerPosition = bear->getPosition();
	if (rules::hasFallenOff(playerPosition))
	{
		gameOver = true;
	}
	else
	{
		nextCheckpoint = rules::passCheckpoint(playerPosition, nextCheckpoint);
		if (rules::hasCompletedLap(nextCheckpoint, playerPosition, finishLine))
		{
			gameWon = true;
		}
	}
	}
}

void Scene::restart()
{
	bear->reset();
	for (const auto& dynamicBody : dynamicBodies)
	{
		physicsWorld->resetBody(dynamicBody.first, dynamicBody.second, quat(1.0f, 0.0f, 0.0f, 0.0f));
	}
	nextCheckpoint = 0;
	gameOver = false;
	gameWon = false;
}

void Scene::renderFromCamera(Shader* shader)
{
	mat4 view = camera->getViewMatrix();
	mat4 projection = camera->getProjectionMatrix();
	ViewFrustum frustum = camera->getViewFrustum();
	drawnGeometryCount = draw(shader, view, projection, viewFrustumCullingEnabled ? &frustum : nullptr);
}

void Scene::renderFromLightSource(Shader* shader)
{
	GLfloat near_plane = 1.0f, far_plane = 75.0f;
	mat4 lightProjectionMatrix = ortho(-100.0f, 100.0f, -100.0f, 100.0f, near_plane, far_plane);
	vec3 lightViewEye = bear->getPosition() + vec3(0.0f, 40.0f, 0.0f);
	mat4 lightViewMatrix = lookAt(lightViewEye, lightViewEye + lights[0]->getDirection(), vec3(0, 1, 0));
	//Everything casts shadows, also what the camera does not see
	draw(shader, lightViewMatrix, lightProjectionMatrix, nullptr);
}

void Scene::renderLightSourcesOnly(Shader* shader)
{
	GLuint viewLoc = shader->getUniformLocation("view");
	GLuint projLoc = shader->getUniformLocation("projection");
	// Set matrices
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, value_ptr(camera->getViewMatrix()));
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, value_ptr(camera->getProjectionMatrix()));

	lights[0]->draw(shader);


	for (unsigned int i = 0; i < arrows.size(); ++i)
	{
		arrows[i]->draw(shader);
	}
}

int Scene::draw(Shader* shader, mat4 view, mat4 projection, const ViewFrustum* frustum)
{
	GLuint modelLocation = shader->getUniformLocation("model");
	GLuint viewLocation = shader->getUniformLocation("view");
	GLuint projectionLocation = shader->getUniformLocation("projection");
	GLuint shadowMappingEnabledLocation = shader->getUniformLocation("shadowMappingEnabled");
	GLuint normalMappingEnabledLocation = shader->getUniformLocation("normalMappingEnabled");
	GLuint lightSpaceMatrixLocation = shader->getUniformLocation("lightSpaceMatrix");
	GLuint lightPositionLocation = shader->getUniformLocation("lightPosition");
	GLuint worldEyeLocation = shader->getUniformLocation("worldEye");

	GLfloat near_plane = 1.0f, far_plane = 75.0f;
	mat4 lightProjectionMatrix = ortho(-100.0f, 100.0f, -100.0f, 100.0f, near_plane, far_plane);
	vec3 lightViewEye = bear->getPosition() + vec3(0.0f, 40.0f, 0.0f);
	mat4 lightViewMatrix = lookAt(lightViewEye, lightViewEye + lights[0]->getDirection(), vec3(0, 1, 0));
	mat4 lightSpaceMatrix = lightProjectionMatrix * lightViewMatrix;


	if (shadowMappingEnabledLocation != 0xFFFFFFFF) {

		glUniform1i(shadowMappingEnabledLocation, shadowMappingEnabled);
	}

	if (lightSpaceMatrixLocation != 0xFFFFFFFF)
	{
		glUniformMatrix4fv(lightSpaceMatrixLocation, 1, GL_FALSE, value_ptr(lightSpaceMatrix));
	}

	if (lightPositionLocation != 0xFFFFFFFF)
	{
		auto lightPos = lights[0]->getPosition();
		glUniform3f(lightPositionLocation, lightPos.x, lightPos.y, lightPos.z);
	}

	if (worldEyeLocation != 0xFFFFFFFF)
	{
		glUniform3fv(worldEyeLocation, 1, &camera->getPosition()[0]);
	}



	if (viewLocation != 0xFFFFFFFF &&
		projectionLocation != 0xFFFFFFFF)
	{
		glUniformMatrix4fv(viewLocation, 1, GL_FALSE, value_ptr(view));
		glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, value_ptr(projection));
	}
	GLuint textureTransparentLocation = shader->getUniformLocation("textureTransparent");

	lights[0]->loadUniforms(shader);

	int drawnGeometries = 0;
	for (unsigned int i = 0; i < geometries.size(); ++i)
	{
		if (geometries[i].get() != water && drawGeometry(shader, geometries[i].get(), normalMappingEnabledLocation, frustum))
		{
			++drawnGeometries;
		}
	}
	drawGeometry(shader, rotatingRock.get(), normalMappingEnabledLocation, frustum);


	if (normalMappingEnabledLocation != 0xFFFFFFFF) {

		glUniform1i(normalMappingEnabledLocation, false);
	}
	auto& debugModel = debugCube->modelMatrix;
	if (modelLocation != 0xFFFFFFFF)
	{
		glUniformMatrix4fv(modelLocation, 1, GL_FALSE, value_ptr(debugModel));
	}
	textures[7].get()->bind();
	debugCube->draw(shader);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);

	//The transparent water is drawn last, so that the geometry behind it is already in the depth buffer and blended with it
	if (transparencyEnabled)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		if (textureTransparentLocation != 0xFFFFFFFF)
		{
			glUniform1i(textureTransparentLocation, true);
		}
	}
	if (drawGeometry(shader, water, normalMappingEnabledLocation, frustum))
	{
		++drawnGeometries;
	}
	glDisable(GL_BLEND);
	if (textureTransparentLocation != 0xFFFFFFFF)
	{
		glUniform1i(textureTransparentLocation, false);
	}
	return drawnGeometries;
}

bool Scene::drawGeometry(Shader* shader, Geometry* geometry, GLuint normalMappingEnabledLocation, const ViewFrustum* frustum)
{
	if (frustum && !geometry->isVisible(*frustum))
	{
		return false;
	}
	if (normalMappingEnabledLocation != 0xFFFFFFFF)
	{
		glUniform1i(normalMappingEnabledLocation, normalMappingEnabled && geometry->getHasNormalMap());
	}
	geometry->draw(shader);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	return true;
}

void Scene::setViewportSize(int width, int height)
{
	camera->setViewportSize(width, height);
}

void Scene::setTransparencyEnabled(bool transparency)
{
	this->transparencyEnabled = transparency;
}

void Scene::setNormalMappingEnabled(bool normalMappingEnabled)
{
	this->normalMappingEnabled = normalMappingEnabled;
}

void Scene::setShadowMappingEnabled(bool shadowMappingEnabled)
{
	this->shadowMappingEnabled = shadowMappingEnabled;
}


bool Scene::isTransparencyEnabled()
{
	return transparencyEnabled;
}


bool Scene::isShadowMappingEnabled()
{
	return shadowMappingEnabled;
}


bool Scene::isNormalMappingEnabled()
{
	return normalMappingEnabled;
}

void Scene::setViewFrustumCullingEnabled(bool viewFrustumCullingEnabled)
{
	this->viewFrustumCullingEnabled = viewFrustumCullingEnabled;
}

bool Scene::isViewFrustumCullingEnabled()
{
	return viewFrustumCullingEnabled;
}

bool Scene::isGameOver()
{
	return gameOver;
}

bool Scene::isGameWon()
{
	return gameWon;
}


int Scene::getDrawnGeometryCount()
{
	return drawnGeometryCount;
}

int Scene::getGeometryCount()
{
	return static_cast<int>(geometries.size());
}

int Scene::getCheckpointsPassed()
{
	return static_cast<int>(nextCheckpoint);
}

int Scene::getCheckpointCount()
{
	return static_cast<int>(rules::CHECKPOINT_COUNT);
}


void Scene::initPhysicsBodies()
{
	//Create bodies
	//1) Create ground plane (a large box whose top face is at y = -5)
	physicsWorld->createStaticBox(vec3(0.0f, -6.0f, 0.0f), vec3(1000.0f, 1.0f, 1000.0f));


	//2) Create cube
	vec3 boxPosition = bear->getPosition() - vec3(0.0f, -10.0f, 5.0f);
	physics::BodyHandle box = physicsWorld->createDynamicBox(boxPosition, vec3(0.4f), 1.0f);
	debugCube->setPhysicsBody(physicsWorld, box);
	dynamicBodies.emplace_back(box, boxPosition);

	//There is no wall at the start: the finish line is a trigger (see initFinishLine) and the checkpoints keep the bear from going backwards

	Geometry* geomBox2 = geometries[17].get();
	physics::BodyHandle box2 = physicsWorld->createDynamicBox(geomBox2->getPosition(), vec3(0.8f), 1.0f);
	geomBox2->setPhysicsBody(physicsWorld, box2);
	dynamicBodies.emplace_back(box2, geomBox2->getPosition());

	for (int i = 3; i <= 14; i++)
	{
		Geometry* geomRock = geometries[i].get();
		physics::BodyHandle rock = physicsWorld->createStaticSphere(geomRock->getPosition(), 0.7f);
		geomRock->setPhysicsBody(physicsWorld, rock);
		bodiesRock.push_back(rock);
	}



	//The terrain is a static triangle mesh, placed like the rendered terrain
	Mesh* worldMesh = world->getModel()->meshes[0];
	MeshTransform worldTransform = terrainTransform(world->modelMatrix);
	physicsWorld->createStaticMesh(worldMesh->vertices, worldMesh->numVertices, worldMesh->indices, worldMesh->numFaces,
		worldTransform.scale, worldTransform.position, worldTransform.rotation);

	physicsWorld->optimizeBroadPhase();
}

void Scene::initFinishLine(Geometry* board)
{
	Mesh* boardMesh = board->getModel()->meshes[0];
	float halfLength = 0.0f;
	for (unsigned int i = 0; i < boardMesh->numVertices; ++i)
	{
		halfLength = std::max(halfLength, std::abs(boardMesh->vertices[i * 3]));
	}
	finishLine = rules::finishLine(board->modelMatrix, halfLength);
}

void Scene::initAnimatedTextures()
{
	//TV static on the monitor (generated, no image files)
	for (unsigned int i = 0; i < 14; ++i)
	{
		monitorVideo.push_back(Texture::createNoise(128, i + 1, 0));
	}

	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water01.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water02.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water03.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water04.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water05.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water06.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water07.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water08.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water09.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water10.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water11.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water12.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water13.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water14.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water15.png", 0, true));
	waterVideo.push_back(make_unique<Texture>("Texture/video/water/Water16.png", 0, true));
}

/**
* Bear collision callback!
**/
void Scene::onBearHit(physics::BodyHandle body)
{
	if (find(bodiesRock.begin(), bodiesRock.end(), body) != bodiesRock.end())
	{
		gameOver = true;
	}
}
