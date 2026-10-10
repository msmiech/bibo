#include "Scene.hpp"
#include <algorithm>

using namespace bibo;
using namespace scene;

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

	lights = util::retrieveLightFromScene(*window, shader, "Model/SnowTerrain/track1.dae");
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

	geometries.push_back(make_unique<Bear>(mat4(1.0f), shader, *window, textures[1].get(), physicsWorld, [this](physics::BodyHandle body) { onBearHit(body); })); //0
	bear = static_cast<Bear*>(geometries[0].get());
	Material bearMaterial;
	bearMaterial.ambient = vec3(0.8f, 0.8f, 0.8f);
	bearMaterial.diffuse = vec3(0.8f, 0.8f, 0.8f);
	bearMaterial.specular = vec3(0.8f, 0.8f, 0.8f);
	bearMaterial.shininess = 10.0f;
	bear->getModel()->meshes[0]->setMaterial(bearMaterial);

	debugCube = make_unique<Cube>(mat4(1.0f), shader, *window, textures[6].get());


	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Boden", textures[3].get()));//1
	world = geometries[1].get();

	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "FinishLine", checkerboardTexPtr));//2
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock1", textures[5].get(), textures[9].get())); //3
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock2", textures[5].get(), textures[9].get())); //4
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock3", textures[5].get(), textures[9].get())); //5
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock4", textures[5].get(), textures[9].get())); //6
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock5", textures[5].get(), textures[9].get())); //7
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock6", textures[5].get(), textures[9].get())); //8
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock7", textures[5].get(), textures[9].get())); //9
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock8", textures[5].get(), textures[9].get())); //10
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock9", textures[5].get(), textures[9].get())); //11
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock10", textures[5].get(), textures[9].get())); //12
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock11", textures[5].get(), textures[9].get())); //13
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock12", textures[5].get(), textures[9].get())); //14
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Torus", textures[3].get())); //15
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Water", textures[8].get())); //16
	water = geometries[16].get();
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Cube", textures[6].get(), textures[7].get())); //17

	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Monitor1", checkerboardTexPtr)); //18

	monitor1 = geometries[18].get();
	Material monitorMaterial;
	monitorMaterial.ambient = vec3(0.8f, 0.8f, 0.8f);
	monitorMaterial.diffuse = vec3(0.8f, 0.8f, 0.8f);
	monitorMaterial.specular = vec3(0.8f, 0.8f, 0.8f);
	monitorMaterial.shininess = 1.0f;
	monitor1->getModel()->meshes[0]->setMaterial(monitorMaterial);


	arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Pfeil0", checkerboardTexPtr)); //18
	arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Pfeil1", checkerboardTexPtr)); //19
	arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Pfeil2", checkerboardTexPtr)); //20
	arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Pfeil3", checkerboardTexPtr)); //21
	initPhysicsBodies();

	rotatingRock = make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock1", textures[5].get(), textures[9].get());
	rotatingRock->modelMatrix = translate(rotatingRock->modelMatrix, vec3(10, 25, 1));
}

void Scene::update(float time_delta) {
	physicsWorld->update(time_delta);
	if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)
	{
		gameOver = false;
		gameWon = false;
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
	vec3 goalPosition = geometries[17]->getPosition();
	if (rules::hasFallenOff(playerPosition))
	{
		gameOver = true;
	}
	else if (rules::hasReachedGoal(playerPosition, goalPosition))
	{
		gameWon = true;
	}
	}
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
		auto gameObject = geometries[i].get();
		if (frustum && !gameObject->isVisible(*frustum))
		{
			continue;
		}
		++drawnGeometries;

		if (i == 16)
		{
			if (transparencyEnabled)
			{
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				if (textureTransparentLocation != 0xFFFFFFFF)
				{
					glUniform1i(textureTransparentLocation, true);
				}
			}
		}
		if (normalMappingEnabledLocation != 0xFFFFFFFF) {

			glUniform1i(normalMappingEnabledLocation, normalMappingEnabled && gameObject->getHasNormalMap());
		}

		gameObject->draw(shader);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glDisable(GL_BLEND);
		if (textureTransparentLocation != 0xFFFFFFFF)
		{
			glUniform1i(textureTransparentLocation, false);
		}
	}

	if (!frustum || rotatingRock->isVisible(*frustum))
	{
		if (normalMappingEnabledLocation != 0xFFFFFFFF) {

			glUniform1i(normalMappingEnabledLocation, normalMappingEnabled && rotatingRock->getHasNormalMap());
		}
		rotatingRock->draw(shader);
	}


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
	return drawnGeometries;
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


void Scene::initPhysicsBodies()
{
	//Create bodies
	//1) Create ground plane (a large box whose top face is at y = -5)
	physicsWorld->createStaticBox(vec3(0.0f, -6.0f, 0.0f), vec3(1000.0f, 1.0f, 1000.0f));


	//2) Create cube
	physics::BodyHandle box = physicsWorld->createDynamicBox(bear->getPosition() - vec3(0.0f, -10.0f, 5.0f), vec3(0.4f), 1.0f);
	debugCube->setPhysicsBody(physicsWorld, box);

	//2) Create Wall
	physicsWorld->createStaticBox(bear->getPosition() - vec3(0.0f, 0, -3.0f), vec3(7.0f, 7.0f, 0.01f));



	Geometry* geomBox2 = geometries[17].get();
	physics::BodyHandle box2 = physicsWorld->createDynamicBox(geomBox2->getPosition(), vec3(0.8f), 1.0f);
	geomBox2->setPhysicsBody(physicsWorld, box2);

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
