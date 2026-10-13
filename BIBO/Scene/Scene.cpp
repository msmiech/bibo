#include "Scene.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include "../Util/Console.hpp"
#include "../Util/Input.hpp"
#include "../World/MaterialTextures.hpp"

using namespace bibo;
using namespace scene;

namespace
{
	const char* const TRACK_FILE = "Model/SnowTerrain/track1.dae";
	//How long the loading may wait for the world around the start (it goes on streaming afterwards)
	const float WORLD_LOADING_TIMEOUT = 30.0f;
}

Geometry* Scene::addGeometry(const aiScene* track, const char* nodeName, Texture* texture, Texture* normalMap)
{
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), mainShader, *window, track, nodeName, texture, normalMap));
	return geometries.back().get();
}

Scene::Scene(GLFWwindow* window, Shader* shader, physics::PhysicsWorld* physicsWorld) : window(window), mainShader(shader), physicsWorld(physicsWorld)
{
	shader->enable();

	//All geometries are nodes of the same scene file, which is only loaded once (it is ~20 MB of XML)
	util::ScenePtr track = util::loadSceneFromFile(TRACK_FILE);
	if (!track)
	{
		util::fatalError(string("Could not load ") + TRACK_FILE);
	}
	const aiScene* trackScene = track.get();

	camera = make_unique<Camera>(vec3(0.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, -1.0f), vec3(0.0f, 1.0f, 0.0f), *window);
	depthShader = make_unique<Shader>("Shader/depth_shader.vert", "Shader/depth_shader.frag");
	lightShader = make_unique<Shader>("Shader/light_source_shader.vert", "Shader/light_source_shader.frag");

	textures.push_back(Texture::createCheckerboard(256, 8, 0)); //chequered flag (generated, no image file)
	Texture* checkerboard = textures.back().get();
	textures.push_back(make_unique<Texture>("Texture/wood16.jpg", 0));
	Texture* wood = textures.back().get();
	textures.push_back(make_unique<Texture>("Texture/wood16n.jpg", 2));
	crateNormalMap = textures.back().get();
	initAnimatedTextures();

	geometries.push_back(make_unique<Bear>(*window, trackScene, physicsWorld, [this](const physics::CharacterController::Hit& hit) { onBearHit(hit); }));
	bear = static_cast<Bear*>(geometries[0].get());
	shader->enable(); //the bear compiled its own shaders

	debugCube = make_unique<Cube>(mat4(1.0f), shader, *window, wood);

	finishLine = addGeometry(trackScene, "FinishLine", checkerboard);
	for (int i = 1; i <= 12; ++i)
	{
		//The obstacles are made of the rock of the island
		Geometry* rock = addGeometry(trackScene, ("Rock" + to_string(i)).c_str(), wood);
		rock->materialLayer = world::MATERIAL_ROCK;
		rock->materialScale = 1.2f;
		for (Mesh* mesh : rock->getModel()->meshes)
		{
			mesh->smoothNormals(); //the model is flat shaded
		}
		rocks.push_back(rock);
	}
	//A ring of stone in the forest
	Geometry* torus = addGeometry(trackScene, "Torus", wood);
	torus->materialLayer = world::MATERIAL_ROCK;
	torus->materialScale = 1.5f;
	crate = addGeometry(trackScene, "Cube", wood, crateNormalMap);
	monitor1 = addGeometry(trackScene, "Monitor1", checkerboard);
	Material monitorMaterial;
	monitorMaterial.ambient = vec3(0.8f, 0.8f, 0.8f);
	monitorMaterial.diffuse = vec3(0.8f, 0.8f, 0.8f);
	monitorMaterial.specular = vec3(0.8f, 0.8f, 0.8f);
	monitorMaterial.shininess = 1.0f;
	monitor1->getModel()->meshes[0]->setMaterial(monitorMaterial);

	for (int i = 0; i < 4; ++i)
	{
		arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, ("Pfeil" + to_string(i)).c_str(), checkerboard));
	}
	initPhysicsBodies();
	initFinishLine(finishLine);

	//The fur lies on the rocks when the bear runs into one
	vector<fur::SphereCollider> obstacles;
	for (Geometry* rock : rocks)
	{
		obstacles.push_back({ rock->getPosition(), 0.8f });
	}
	bear->setObstacles(obstacles);

	//The camera stays out of the terrain, the trees and the rocks
	camera->setCollisionProbe([this](vec3 from, vec3 to, float radius) {
		physics::RayHit hit;
		return this->physicsWorld->castSphere(from, to - from, radius, hit, true) ? hit.fraction : 1.0f;
	});

	rotatingRock = make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, "Rock1", wood);
	rotatingRock->modelMatrix = translate(rotatingRock->modelMatrix, vec3(10, 25, 1));
	rotatingRock->materialLayer = world::MATERIAL_ROCK;
	rotatingRock->materialScale = 1.2f;
	for (Mesh* mesh : rotatingRock->getModel()->meshes)
	{
		mesh->smoothNormals();
	}

	initWorld(*trackScene);
	camera->snapBehind(cameraTarget());
	world->finishLoading(camera->getPosition(), bear->getPosition(), WORLD_LOADING_TIMEOUT);
}

Scene::~Scene() = default;

void Scene::initWorld(const aiScene& track)
{
	cout << "Generating the island..." << endl;
	//The world must not cover the objects of the track
	vector<world::KeepClear> keepClear;
	auto keep = [&](Geometry* geometry) {
		//Sphere around the vertices in world space (the model matrices of some objects scale the axes differently)
		vector<vec3> points;
		for (const Mesh* mesh : geometry->getModel()->meshes)
		{
			for (unsigned int i = 0; mesh->vertices && i < mesh->numVertices; ++i)
			{
				points.push_back(vec3(geometry->modelMatrix * vec4(mesh->vertices[i * 3], mesh->vertices[i * 3 + 1], mesh->vertices[i * 3 + 2], 1.0f)));
			}
		}
		vec3 low(numeric_limits<float>::max()), high(numeric_limits<float>::lowest());
		for (const vec3& point : points)
		{
			low = glm::min(low, point);
			high = glm::max(high, point);
		}
		world::KeepClear object{ 0.5f * (low + high), 0.0f };
		for (const vec3& point : points)
		{
			object.radius = std::max(object.radius, distance(object.center, point));
		}
		if (!points.empty())
		{
			keepClear.push_back(object);
		}
	};
	keep(finishLine);
	keep(crate);
	keep(monitor1);
	for (const unique_ptr<Geometry>& geometry : geometries)
	{
		if (geometry->getModel() && geometry.get() != finishLine && geometry.get() != crate && geometry.get() != monitor1)
		{
			keep(geometry.get()); //rocks and the stone ring
		}
	}
	for (const unique_ptr<Geometry>& arrow : arrows)
	{
		keep(arrow.get());
	}
	vector<glm::vec2> checkpoints(std::begin(rules::CHECKPOINTS), std::end(rules::CHECKPOINTS));
	vec3 start = bear->getPosition();
	world = make_unique<world::World>(track, glm::vec2(start.x, start.z), glm::vec2(0.0f, -1.0f), checkpoints, keepClear, physicsWorld);
}

void Scene::update(float time_delta, const input::PlayerInput& player) {
	physicsWorld->update(time_delta);
	if (input::isKeyDown(window, GLFW_KEY_R))
	{
		restart();
	}
	if (gameWon != true && gameOver != true){
	//The camera turns first, the bear moves relative to where the camera looks then
	camera->rotate(player.look.x, player.look.y);
	camera->zoom(player.zoom);
	if (player.recenter)
	{
		camera->recenter();
	}
	BearInput bearInput;
	bearInput.move = input::cameraRelative(player.move, camera->getForward(), camera->getRight());
	bearInput.jump = player.jump;
	bear->setInput(bearInput);

	for (unsigned int i = 0; i < geometries.size(); ++i)
	{
		geometries[i]->update(time_delta);
	}
	accumulatedVideoTime += time_delta;
	if (accumulatedVideoTime > 0.1f)
	{
		accumulatedVideoTime = 0.f;
		monitor1->getModel()->meshes[0]->setTexture(monitorVideo[++monitorFrame % monitorVideo.size()].get());
	}

	debugCube->update(time_delta);

	if (camera->isLocked())
	{
		camera->follow(cameraTarget(), time_delta);
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
		if (rules::hasCompletedLap(nextCheckpoint, playerPosition, finishLineTrigger))
		{
			gameWon = true;
		}
	}
	}
	//The world streams around the camera, the physics around the bear
	world->update(time_delta, camera->getPosition(), bear->getPosition());
}

Camera::FollowTarget Scene::cameraTarget()
{
	Camera::FollowTarget target;
	target.position = bear->getPosition();
	target.direction = bear->getDirection();
	target.velocity = bear->getCharacter().getVelocity();
	target.airborne = bear->getCharacter().isAirborne();
	return target;
}

void Scene::restart()
{
	bear->reset();
	bear->setInput(BearInput());
	//The ground at the start may have been streamed out of the physics world
	world->ensureCollision(bear->getPosition());
	camera->snapBehind(cameraTarget());
	for (const auto& dynamicBody : dynamicBodies)
	{
		physicsWorld->resetBody(dynamicBody.first, dynamicBody.second, quat(1.0f, 0.0f, 0.0f, 0.0f));
	}
	nextCheckpoint = 0;
	gameOver = false;
	gameWon = false;
}

render::CameraView Scene::cameraView()
{
	render::CameraView view;
	view.view = camera->getViewMatrix();
	view.projection = camera->getProjectionMatrix();
	view.position = camera->getPosition();
	view.fieldOfView = camera->getFieldOfView();
	view.aspectRatio = camera->getAspectRatio();
	view.nearPlane = camera->getNearPlane();
	return view;
}

void Scene::fillFrameUniforms(render::FrameUniforms& uniforms)
{
	world->fillFrameUniforms(uniforms);
}

void Scene::prepareFrame()
{
	world->prepareFrame();
}

void Scene::drawShadowCasters(const render::ShadowCascade& cascade)
{
	world->drawShadows(cascade, camera->getPosition());
	depthShader->enable();
	glUniformMatrix4fv(depthShader->getUniformLocation("lightSpaceMatrix"), 1, GL_FALSE, value_ptr(cascade.lightSpace));
	for (const unique_ptr<Geometry>& geometry : geometries)
	{
		if (geometry.get() == bear)
		{
			bear->draw(depthShader.get());
			continue;
		}
		Model* model = geometry->getModel();
		float scale = std::max({ length(vec3(geometry->modelMatrix[0])), length(vec3(geometry->modelMatrix[1])), length(vec3(geometry->modelMatrix[2])) });
		if (cascade.contains(vec3(geometry->modelMatrix * vec4(model->getBoundingSphereCenter(), 1.0f)), model->getBoundingSphereRadius() * scale))
		{
			geometry->draw(depthShader.get());
		}
	}
	rotatingRock->draw(depthShader.get());
	glUniformMatrix4fv(depthShader->getUniformLocation("model"), 1, GL_FALSE, value_ptr(debugCube->modelMatrix));
	debugCube->draw(depthShader.get());
}

void Scene::drawMain(const render::RenderSettings& settings)
{
	normalMappingEnabled = settings.normalMapping;
	mat4 view = camera->getViewMatrix();
	mat4 projection = camera->getProjectionMatrix();
	ViewFrustum frustum = camera->getViewFrustum();
	vec3 eye = camera->getPosition();

	//1. Opaque: the terrain and the vegetation, the objects of the track and the signs (they glow)
	world->drawOpaque(frustum, eye, viewFrustumCullingEnabled);
	mainShader->enable();
	drawnGeometryCount = drawGeometries(mainShader, view, projection, viewFrustumCullingEnabled ? &frustum : nullptr, false);
	lightShader->enable();
	glUniformMatrix4fv(lightShader->getUniformLocation("view"), 1, GL_FALSE, value_ptr(view));
	glUniformMatrix4fv(lightShader->getUniformLocation("projection"), 1, GL_FALSE, value_ptr(projection));
	for (const unique_ptr<Geometry>& arrow : arrows)
	{
		arrow->draw(lightShader.get());
	}

	//2. The sky behind everything (not in wireframe mode)
	if (!settings.wireframe)
	{
		world->drawSky();
	}

	//3. The bear: its fur is blended with what is behind it
	if (!viewFrustumCullingEnabled || bear->isVisible(frustum))
	{
		BearRenderContext context;
		context.view = view;
		context.projection = projection;
		context.eye = eye;
		bear->drawShaded(context);
		++drawnGeometryCount;
	}

	//4. Transparent: the water, the falling snow
	world->drawTransparent(settings.transparency);
	glActiveTexture(GL_TEXTURE0);
}

void Scene::renderFromCamera(Shader* shader)
{
	ViewFrustum frustum = camera->getViewFrustum();
	shader->enable();
	drawnGeometryCount = drawGeometries(shader, camera->getViewMatrix(), camera->getProjectionMatrix(), viewFrustumCullingEnabled ? &frustum : nullptr, true);
}

int Scene::drawGeometries(Shader* shader, const mat4& view, const mat4& projection, const ViewFrustum* frustum, bool withBear)
{
	GLint viewLocation = shader->getUniformLocation("view");
	GLint projectionLocation = shader->getUniformLocation("projection");
	GLint normalMappingEnabledLocation = shader->getUniformLocation("normalMappingEnabled");
	GLint textureTransparentLocation = shader->getUniformLocation("textureTransparent");
	if (viewLocation >= 0 && projectionLocation >= 0)
	{
		glUniformMatrix4fv(viewLocation, 1, GL_FALSE, value_ptr(view));
		glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, value_ptr(projection));
	}
	if (textureTransparentLocation >= 0)
	{
		glUniform1i(textureTransparentLocation, false);
	}

	int drawnGeometries = 0;
	for (const unique_ptr<Geometry>& geometry : geometries)
	{
		if (geometry.get() == bear)
		{
			if (withBear && (!frustum || bear->isVisible(*frustum)))
			{
				bear->draw(shader);
				++drawnGeometries;
			}
		}
		else if (drawGeometry(shader, geometry.get(), normalMappingEnabledLocation, frustum))
		{
			++drawnGeometries;
		}
	}
	drawGeometry(shader, rotatingRock.get(), normalMappingEnabledLocation, frustum);

	if (normalMappingEnabledLocation >= 0)
	{
		glUniform1i(normalMappingEnabledLocation, normalMappingEnabled);
	}
	GLint modelLocation = shader->getUniformLocation("model");
	if (modelLocation >= 0)
	{
		glUniformMatrix4fv(modelLocation, 1, GL_FALSE, value_ptr(debugCube->modelMatrix));
	}
	GLint layerLocation = shader->getUniformLocation("materialLayer");
	if (layerLocation >= 0)
	{
		glUniform1i(layerLocation, -1);
	}
	crateNormalMap->bind();
	debugCube->draw(shader);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	return drawnGeometries;
}

bool Scene::drawGeometry(Shader* shader, Geometry* geometry, GLint normalMappingEnabledLocation, const ViewFrustum* frustum)
{
	if (frustum && !geometry->isVisible(*frustum))
	{
		return false;
	}
	if (normalMappingEnabledLocation >= 0)
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
	//Ground under the sea (a large box whose top face is at y = -5): the bear lands on it when it falls into the sea
	physicsWorld->createStaticBox(vec3(0.0f, -6.0f, 0.0f), vec3(1000.0f, 1.0f, 1000.0f));

	vec3 boxPosition = bear->getPosition() - vec3(0.0f, -10.0f, 5.0f);
	physics::BodyHandle box = physicsWorld->createDynamicBox(boxPosition, vec3(0.4f), 1.0f);
	debugCube->setPhysicsBody(physicsWorld, box);
	dynamicBodies.emplace_back(box, boxPosition);

	//There is no wall at the start: the finish line is a trigger (see initFinishLine) and the checkpoints keep the bear from going backwards

	physics::BodyHandle box2 = physicsWorld->createDynamicBox(crate->getPosition(), vec3(0.8f), 1.0f);
	crate->setPhysicsBody(physicsWorld, box2);
	dynamicBodies.emplace_back(box2, crate->getPosition());

	for (Geometry* rock : rocks)
	{
		physics::BodyHandle body = physicsWorld->createStaticSphere(rock->getPosition(), 0.7f);
		rock->setPhysicsBody(physicsWorld, body);
		bodiesRock.push_back(body);
	}
	//The terrain and the trees are added by the world (streamed around the bear)
}

void Scene::initFinishLine(Geometry* board)
{
	Mesh* boardMesh = board->getModel()->meshes[0];
	float halfLength = 0.0f;
	for (unsigned int i = 0; i < boardMesh->numVertices; ++i)
	{
		halfLength = std::max(halfLength, std::abs(boardMesh->vertices[i * 3]));
	}
	finishLineTrigger = rules::finishLine(board->modelMatrix, halfLength);
}

void Scene::initAnimatedTextures()
{
	//TV static on the monitor (generated, no image files)
	for (unsigned int i = 0; i < 14; ++i)
	{
		monitorVideo.push_back(Texture::createNoise(128, i + 1, 0));
	}
}

/**
* Bear collision callback: the rocks knock the bear down, it tumbles and gets up again (the time is lost)
**/
void Scene::onBearHit(const physics::CharacterController::Hit& hit)
{
	if (find(bodiesRock.begin(), bodiesRock.end(), hit.body) != bodiesRock.end())
	{
		bear->knockDown(hit);
	}
}

bool Scene::isBearKnockedDown()
{
	return bear->isKnockedDown();
}
