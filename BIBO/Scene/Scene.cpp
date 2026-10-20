#include "Scene.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include "../Util/Console.hpp"
#include "../Util/Input.hpp"
#include "../World/MaterialTextures.hpp"
#include "../World/TrackTerrain.hpp"

using namespace bibo;
using namespace scene;

namespace
{
	const char* const TRACK_FILE = "Model/SnowTerrain/track1.dae";
	//How long the loading may wait for the world around the start (it goes on streaming afterwards)
	const float WORLD_LOADING_TIMEOUT = 30.0f;
	//The hedgehogs live where the rocks of the track are (the nodes Rock1 to Rock12)
	const int HEDGEHOG_COUNT = 12;
	//The fur of the bear lies on the spines of a hedgehog when it runs into it
	const float SPINE_LENGTH = 0.8f * HedgehogRenderer::SPINE_LENGTH;

	//The vertices of the meshes of a geometry in model space (the model matrix is not applied)
	vector<vec3> modelVertices(Geometry* geometry)
	{
		vector<vec3> points;
		for (const Mesh* mesh : geometry->getModel()->meshes)
		{
			for (unsigned int i = 0; mesh->vertices && i < mesh->numVertices; ++i)
			{
				points.push_back(vec3(mesh->vertices[i * 3], mesh->vertices[i * 3 + 1], mesh->vertices[i * 3 + 2]));
			}
		}
		return points;
	}

	//The vertices of the meshes of a geometry in world space
	vector<vec3> worldVertices(Geometry* geometry)
	{
		vector<vec3> points = modelVertices(geometry);
		for (vec3& point : points)
		{
			point = vec3(geometry->modelMatrix * vec4(point, 1.0f));
		}
		return points;
	}
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

	geometries.push_back(make_unique<Bear>(*window, trackScene, physicsWorld, [this](const physics::CharacterController::Hit& hit) { onBearHit(hit); }));
	bear = static_cast<Bear*>(geometries[0].get());
	shader->enable(); //the bear compiled its own shaders

	finishLine = addGeometry(trackScene, "FinishLine", checkerboard);
	//A ring of stone in the forest
	Geometry* torus = addGeometry(trackScene, "Torus", wood);
	torus->materialLayer = world::MATERIAL_ROCK;
	torus->materialScale = 1.5f;
	crate = addGeometry(trackScene, "Cube", wood, crateNormalMap);

	for (int i = 0; i < 4; ++i)
	{
		arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, trackScene, ("Pfeil" + to_string(i)).c_str(), checkerboard));
	}
	initPhysicsBodies();
	initFinishLine(finishLine);

	//The camera stays out of the terrain and the trees
	camera->setCollisionProbe([this](vec3 from, vec3 to, float radius) {
		physics::RayHit hit;
		return this->physicsWorld->castSphere(from, to - from, radius, hit, true) ? hit.fraction : 1.0f;
	});

	initWorld(*trackScene);
	initAnimals();
	camera->snapBehind(cameraTarget());
	world->finishLoading(camera->getPosition(), bear->getPosition(), WORLD_LOADING_TIMEOUT);
	placeAnimals();
}

Scene::~Scene() = default;

void Scene::initWorld(const aiScene& track)
{
	cout << "Generating the island..." << endl;
	//The world must not cover the objects of the track
	vector<world::KeepClear> keepClear;
	auto keep = [&](Geometry* geometry) {
		//Sphere around the vertices in world space (the model matrices of some objects scale the axes differently)
		vector<vec3> points = worldVertices(geometry);
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
	for (const unique_ptr<Geometry>& geometry : geometries)
	{
		if (geometry->getModel())
		{
			keep(geometry.get()); //the finish line, the crate and the stone ring
		}
	}
	for (const unique_ptr<Geometry>& arrow : arrows)
	{
		keep(arrow.get());
	}
	//Where the rocks of the track are the hedgehogs live
	for (int i = 1; i <= HEDGEHOG_COUNT; ++i)
	{
		world::KeepClear home;
		string name = "Rock" + to_string(i);
		if (!world::TrackTerrain::nodeSphere(track, name.c_str(), home.center, home.radius))
		{
			util::fatalError("The track has no node " + name + " (home of a hedgehog)");
		}
		keepClear.push_back(home);
		hedgehogHomes.push_back(vec2(home.center.x, home.center.z));
	}
	vector<glm::vec2> checkpoints(std::begin(rules::CHECKPOINTS), std::end(rules::CHECKPOINTS));
	vec3 start = bear->getPosition();
	world = make_unique<world::World>(track, glm::vec2(start.x, start.z), glm::vec2(0.0f, -1.0f), checkpoints, keepClear, physicsWorld);
}

void Scene::initAnimals()
{
	creatures = make_unique<CreatureRenderer>();
	const world::WorldGenerator& generator = world->getGenerator();
	//The hedgehogs stay on the floor of the track and out of the water
	Hedgehog::Ground ground = Hedgehog::trackFloor(generator);
	for (std::size_t i = 0; i < hedgehogHomes.size(); ++i)
	{
		hedgehogs.emplace_back(hedgehogHomes[i], Hedgehog::startDirection(static_cast<int>(i)), Hedgehog::seedOf(static_cast<int>(i)), ground);
		hedgehogBodies.push_back(physicsWorld->createKinematicSphere(hedgehogs.back().getColliderCenter(), hedgehogs.back().getColliderRadius()));
	}
	hedgehogRenderer = make_unique<HedgehogRenderer>(hedgehogs.size());
	salmon = make_unique<SalmonPools>(findWaterPools(generator), 4711u);
	cout << hedgehogs.size() << " hedgehogs, " << salmon->getPools().size() << " pools with salmon" << endl;
}

void Scene::update(float time_delta, const input::PlayerInput& player) {
	physicsWorld->update(time_delta);
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
	updateAnimals(time_delta);

	if (camera->isLocked())
	{
		camera->follow(cameraTarget(), time_delta);
	}
	camera->update(time_delta);

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
	placeAnimals();
}

void Scene::updateAnimals(float time_delta)
{
	//The hedgehogs stroll around, their collision spheres follow them (in the next update of the physics)
	vector<fur::SphereCollider> obstacles;
	for (std::size_t i = 0; i < hedgehogs.size(); ++i)
	{
		hedgehogs[i].update(time_delta);
		physicsWorld->moveKinematic(hedgehogBodies[i], hedgehogs[i].getColliderCenter(), quat(1.0f, 0.0f, 0.0f, 0.0f), time_delta);
		obstacles.push_back({ hedgehogs[i].getColliderCenter(), hedgehogs[i].getColliderRadius() + SPINE_LENGTH });
	}
	bear->setObstacles(obstacles);

	//Salmon jump out of the water when the bear comes close, one per pool can be caught for bonus points
	BearCharacter& character = bear->getCharacter();
	int pool = salmon->update(time_delta, character.getBoundsCenter(), character.getMouthPosition(), character.canCatchFish());
	if (pool >= 0 && character.catchFish())
	{
		score += rules::SALMON_POINTS;
	}
}

void Scene::placeSalmon(vec3 center, const quat& rotation, int bites, float wiggle)
{
	const float scale = SalmonPools::SCALE;
	if (bites < 1)
	{
		creatures->add(CreatureModel::SALMON_HEAD, center, rotation, scale);
	}
	if (bites < 2)
	{
		creatures->add(CreatureModel::SALMON_BODY, center, rotation, scale);
	}
	if (bites < 3)
	{
		vec3 joint = center + rotation * vec3(0.0f, 0.0f, SALMON_TAIL_JOINT * scale);
		creatures->add(CreatureModel::SALMON_TAIL, joint, rotation * angleAxis(wiggle, vec3(0.0f, 1.0f, 0.0f)), scale);
	}
}

void Scene::placeAnimals()
{
	creatures->clear();
	for (std::size_t i = 0; i < hedgehogs.size(); ++i)
	{
		Hedgehog::Pose pose = hedgehogs[i].getPose();
		mat4 scale = glm::scale(mat4(1.0f), vec3(Hedgehog::SCALE));
		mat4 body = glm::translate(mat4(1.0f), pose.bodyPosition) * glm::mat4_cast(pose.bodyRotation) * scale;
		std::array<mat4, 4> feet;
		for (std::size_t foot = 0; foot < feet.size(); ++foot)
		{
			feet[foot] = glm::translate(mat4(1.0f), pose.feet[foot]) * glm::mat4_cast(pose.footRotation) * scale;
		}
		hedgehogRenderer->setPose(i, body, feet, pose.bristle);
	}
	for (const SalmonPools::Jump& jump : salmon->getJumps())
	{
		placeSalmon(jump.position(), jump.rotation(), 0, 0.5f * std::sin(18.0f * jump.time + jump.wiggle));
	}
	BearCharacter::HeldFish held;
	if (bear->getCharacter().getHeldFish(held))
	{
		placeSalmon(held.center, held.rotation, held.bites, held.wiggle);
	}
	for (const SalmonPools::Droplet& droplet : salmon->getDroplets())
	{
		creatures->add(CreatureModel::DROPLET, droplet.position, quat(1.0f, 0.0f, 0.0f, 0.0f), droplet.size);
	}
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
	camera->snapBehind(cameraTarget());
	//The ground at the start may have been streamed out of the physics world
	world->ensureCollision(bear->getPosition(), camera->getPosition());
	for (const DynamicBody& dynamicBody : dynamicBodies)
	{
		physicsWorld->resetBody(dynamicBody.body, dynamicBody.position, dynamicBody.rotation);
	}
	for (std::size_t i = 0; i < hedgehogs.size(); ++i)
	{
		hedgehogs[i].reset();
		physicsWorld->moveKinematic(hedgehogBodies[i], hedgehogs[i].getColliderCenter(), quat(1.0f, 0.0f, 0.0f, 0.0f), 0.0f);
	}
	salmon->reset();
	score = 0;
	nextCheckpoint = 0;
	gameOver = false;
	gameWon = false;
	placeAnimals();
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
	creatures->drawShadow(cascade);
	depthShader->enable();
	glUniformMatrix4fv(depthShader->getUniformLocation("lightSpaceMatrix"), 1, GL_FALSE, value_ptr(cascade.lightSpace));
	hedgehogRenderer->drawSurfaces(depthShader.get());
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
}

void Scene::drawMain(const render::RenderSettings& settings)
{
	normalMappingEnabled = settings.normalMapping;
	mat4 view = camera->getViewMatrix();
	mat4 projection = camera->getProjectionMatrix();
	ViewFrustum frustum = camera->getViewFrustum();
	vec3 eye = camera->getPosition();

	//1. Opaque: the terrain and the vegetation, the salmon (shaded like the vegetation), the objects of the track and the signs (they glow)
	world->drawOpaque(frustum, eye, viewFrustumCullingEnabled);
	creatures->draw();
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

	//3. The bear and the hedgehogs: their fur and spines are blended with what is behind them
	BearRenderContext context;
	context.view = view;
	context.projection = projection;
	context.eye = eye;
	if (!viewFrustumCullingEnabled || bear->isVisible(frustum))
	{
		bear->drawShaded(context);
		++drawnGeometryCount;
	}
	hedgehogRenderer->drawShaded(context, frustum);

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

	if (normalMappingEnabledLocation >= 0)
	{
		glUniform1i(normalMappingEnabledLocation, normalMappingEnabled);
	}
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

	//There is no wall at the start: the finish line is a trigger (see initFinishLine) and the checkpoints keep the bear from going backwards

	//The crate as it is drawn: a box around its mesh, turned like its model
	vector<vec3> crateVertices = modelVertices(crate);
	vec3 low(numeric_limits<float>::max()), high(numeric_limits<float>::lowest());
	for (const vec3& vertex : crateVertices)
	{
		low = glm::min(low, vertex);
		high = glm::max(high, vertex);
	}
	const mat4& crateMatrix = crate->modelMatrix;
	vec3 scale(length(vec3(crateMatrix[0])), length(vec3(crateMatrix[1])), length(vec3(crateMatrix[2])));
	quat crateRotation = normalize(quat_cast(mat3(vec3(crateMatrix[0]) / scale.x, vec3(crateMatrix[1]) / scale.y, vec3(crateMatrix[2]) / scale.z)));
	vec3 crateCenter = vec3(crateMatrix * vec4(0.5f * (low + high), 1.0f));
	physics::BodyHandle box = physicsWorld->createDynamicBox(crateCenter, 0.5f * (high - low) * scale, 1.0f, crateRotation);
	crate->setPhysicsBody(physicsWorld, box);
	dynamicBodies.push_back({ box, crateCenter, crateRotation });
	//The terrain and the trees are added by the world (streamed around the bear), the hedgehogs by initAnimals
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

/**
* Bear collision callback: running into a hedgehog knocks the bear down (ouch, the spines), it tumbles and gets up again (the time is lost)
**/
void Scene::onBearHit(const physics::CharacterController::Hit& hit)
{
	auto found = find(hedgehogBodies.begin(), hedgehogBodies.end(), hit.body);
	if (found != hedgehogBodies.end())
	{
		bool wasDown = bear->isKnockedDown();
		bear->knockDown(hit);
		if (!wasDown)
		{
			hedgehogs[static_cast<std::size_t>(found - hedgehogBodies.begin())].startle();
		}
	}
}

bool Scene::isBearKnockedDown()
{
	return bear->isKnockedDown();
}
