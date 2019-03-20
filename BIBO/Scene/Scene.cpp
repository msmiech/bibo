#include "Scene.hpp"

using namespace bibo;
using namespace scene;

Geometry* rotatingRock;

Scene::Scene(GLFWwindow* window, Shader* shader, PxPhysics* mPhysics, PxFoundation* mFoundation, PxCooking* mCooking) : window(window), mPhysics(mPhysics), mFoundation(mFoundation), mCooking(mCooking)
{
	shader->enable();

	initPxScene();

	geometries.clear();
	arrows.clear();

	// Clear the screen
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	lights = util::retrieveLightFromScene(*window, shader, "Model/SnowTerrain/track1.dae");
	camera = make_unique<Camera>(vec3(0.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, -1.0f), vec3(0.0f, 1.0f, 0.0f), shader, *window);

	textures.push_back(make_unique<Texture>(L"Texture/pusheen.png", 0)); //0
	Texture* pusheenTexPtr = textures[0].get();

	textures.push_back(make_unique<Texture>(L"Texture/cfur01.jpg", 0)); //1
	textures.push_back(make_unique<Texture>(L"Texture/sky1Tex.jpg", 0)); //2
	textures.push_back(make_unique<Texture>(L"Texture/snow.jpg", 0)); //3
	textures.push_back(make_unique<Texture>(L"Texture/snow-normal.jpg", 2)); //4
	textures.push_back(make_unique<Texture>(L"Texture/ground07.jpg", 0)); //5
	textures.push_back(make_unique<Texture>(L"Texture/wood16.jpg", 0)); //6
	textures.push_back(make_unique<Texture>(L"Texture/wood16n.jpg", 2)); //7
	textures.push_back(make_unique<Texture>(L"Texture/Water.png", 0));//8
	textures.push_back(make_unique<Texture>(L"Texture/ground07n.jpg", 2)); //9
	initAnimatedTextures();

	geometries.push_back(make_unique<Bear>(mat4(1.0f), shader, *window, textures[1].get(), mPhysics, pxManager, this)); //0
	bear = static_cast<Bear*>(geometries[0].get());
	Material bearMaterial;
	bearMaterial.ambient = vec3(0.8f, 0.8f, 0.8f);
	bearMaterial.diffuse = vec3(0.8f, 0.8f, 0.8f);
	bearMaterial.specular = vec3((0.8f, 0.8f, 0.8f));
	bearMaterial.shininess = 10.0f;
	bear->getModel()->meshes[0]->setMaterial(bearMaterial);

	debugCube = make_unique<Cube>(mat4(1.0f), shader, *window, textures[6].get());

	/*geometries[1].get()->modelMatrix = scale(geometries[1].get()->modelMatrix, vec3(2.0f, 2.0f, 2.0f));
	geometries[1].get()->modelMatrix = translate(geometries[1].get()->modelMatrix, vec3(0.3f, 0.5f, 2.5f));*/


	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Boden", textures[3].get()));//1
	world = geometries[1].get();

	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "FinishLine", pusheenTexPtr));//2
	finishLine = geometries[2].get();
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock1", textures[5].get(), textures[9].get())); //3
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock2", textures[5].get(), textures[9].get())); //4
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock3", textures[5].get(), textures[9].get())); //5
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock4", textures[5].get(), textures[9].get())); //6
	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock5", textures[5].get(), textures[9].get())); //7
	//geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "salmon1", textures[5].get())); //10
	//geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "salmon3", textures[5].get())); //11
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

	geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Monitor1", pusheenTexPtr)); //18
	//geometries.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Monitor2", pusheenTexPtr)); //19

	monitor1 = geometries[18].get();
	Material monitorMaterial;
	monitorMaterial.ambient = vec3(0.8f, 0.8f, 0.8f);
	monitorMaterial.diffuse = vec3(0.8f, 0.8f, 0.8f);
	monitorMaterial.specular = vec3((0.8f, 0.8f, 0.8f));
	monitorMaterial.shininess = 1.0f;
	monitor1->getModel()->meshes[0]->setMaterial(monitorMaterial);


	arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Pfeil0", pusheenTexPtr)); //18
	arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Pfeil1", pusheenTexPtr)); //19
	arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Pfeil2", pusheenTexPtr)); //20
	arrows.push_back(make_unique<Geometry>(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Pfeil3", pusheenTexPtr)); //21
	initPxActors();

	rotatingRock = new Geometry(mat4(1.0f), shader, *window, "Model/SnowTerrain/track1.dae", "Rock1", textures[5].get(), textures[9].get());
	rotatingRock->modelMatrix = translate(rotatingRock->modelMatrix, vec3(10, 25, 1));
}

Scene::~Scene()
{
	bear = nullptr;
	debugCube = nullptr;
	world = nullptr;
	finishLine = nullptr;
	water = nullptr;
	geometries.clear();
	lights.clear();
}


void Scene::update(float time_delta) {
	pxScene->simulate(time_delta);
	pxScene->fetchResults(true);
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
		monitor1->getModel()->meshes[0]->setTexture(crashVideo[++crashFrame % crashVideo.size()].get());
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
	if (playerPosition.y < -1.1f)
	{
		gameOver = true;
	}
	else if (playerPosition.x > goalPosition.x - 0.5f &&
		playerPosition.y > goalPosition.y - 0.5f &&
		playerPosition.z > goalPosition.z - 0.5f &&
		playerPosition.x < goalPosition.x + 0.5f &&
		playerPosition.y < goalPosition.y + 3.5f &&
		playerPosition.z < goalPosition.z + 0.5f /*&&
		userScore >= 10*/)
	{
		gameWon = true;
	}
	}
}

void Scene::renderFromCamera(Shader* shader)
{
	mat4 view = camera->getViewMatrix();
	mat4 projection = camera->getProjectionMatrix();
	draw(shader, view, projection);
}

void Scene::renderFromLightSource(Shader* shader)
{
	GLfloat near_plane = 1.0f, far_plane = 75.0f;
	mat4 lightProjectionMatrix = ortho(-100.0f, 100.0f, -100.0f, 100.0f, near_plane, far_plane);
	//mat4 lightViewMatrix = lookAt(lights[0]->getPosition(), vec3(0, 0, 0), vec3(0, 1, 0));
	vec3 lightViewEye = bear->getPosition() + vec3(0.0f, 40.0f, 0.0f);
	mat4 lightViewMatrix = lookAt(lightViewEye, lightViewEye + lights[0]->getDirection(), vec3(0, 1, 0));
	GLuint lightViewLocation = shader->getUniformLocation("lightView");
	if (lightViewLocation != 0xFFFFFFFF) {
		glUniformMatrix4fv(lightViewLocation, 1, GL_FALSE, value_ptr(lightViewMatrix));
	}
	draw(shader, lightViewMatrix, lightProjectionMatrix);
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

void Scene::draw(Shader* shader, mat4 view, mat4 projection)
{
	//shader->enable(); Assuming this happens before

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
	//mat4 lightViewMatrix = lookAt(vec3(0.0f, 40.0f, 25.0f), vec3(0, 0, 0), vec3(0, 1, 0));
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

	for (unsigned int i = 0; i < geometries.size(); ++i)
	{
		auto gameObject = geometries[i].get();


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

	if (normalMappingEnabledLocation != 0xFFFFFFFF) {

		glUniform1i(normalMappingEnabledLocation, normalMappingEnabled && rotatingRock->getHasNormalMap());
	}
	rotatingRock->draw(shader);


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

bool Scene::isGameOver()
{
	return gameOver;
}

bool Scene::isGameWon()
{
	return gameWon;
}


int Scene::getUserScore()
{
	return userScore;
}


/**
* The following method is based on: http://mmmovania.blogspot.co.at/2013/01/some-status-updates-and-source-code.html
**/
void Scene::initPxScene()
{
	//Create the scene
	PxSceneDesc sceneDesc(mPhysics->getTolerancesScale());
	sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);

	if (!sceneDesc.cpuDispatcher) {
		PxDefaultCpuDispatcher* mCpuDispatcher = PxDefaultCpuDispatcherCreate(1);
		if (!mCpuDispatcher)
			cerr << "PxDefaultCpuDispatcherCreate failed!" << endl;
		sceneDesc.cpuDispatcher = mCpuDispatcher;
	}
	if (!sceneDesc.filterShader)
		sceneDesc.filterShader = PxDefaultSimulationFilterShader;


	pxScene = mPhysics->createScene(sceneDesc);
	if (!pxScene)
		cerr << "createScene failed!" << endl;

	pxManager = PxCreateControllerManager(*pxScene);

	pxScene->setVisualizationParameter(PxVisualizationParameter::eSCALE, 1.0);
	pxScene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);
	//pxScene->setVisualizationParameter(PxVisualizationParameter::eDEFORMABLE_MESH, 1.0f);
	//pxScene->setVisualizationParameter(PxVisualizationParameter::eDEFORMABLE_SELFCOLLISIONS, 1.0f);
	//pxScene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_DYNAMIC, 1.0f);
	//pxScene->setVisualizationParameter(PxVisualizationParameter::eDEFORMABLE_SHAPES, 1.0f);
}

void Scene::initPxActors()
{
	PxMaterial* mMaterial = mPhysics->createMaterial(0.5, 0.5, 0.5);



	//Create actors 
	//1) Create ground plane
	PxTransform planePos = PxTransform(PxVec3(0.0f, -5.0f, 0.0f), PxQuat(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f)));
	PxRigidStatic* plane = mPhysics->createRigidStatic(planePos);
	plane->createShape(PxPlaneGeometry(), *mMaterial);
	pxScene->addActor(*plane);


	//2) Create cube
	PxTransform boxPos(util::convert(bear->getPosition() - vec3(0.0f, -10.0f, 5.0f)));
	PxBoxGeometry boxGeometry(PxVec3(0.4f));
	PxRigidDynamic* gBox = PxCreateDynamic(*mPhysics, boxPos, boxGeometry, *mMaterial, 1.0f);
	pxScene->addActor(*gBox);
	debugCube->gBox = gBox;

	//2) Create Wall
	PxTransform wallPos(util::convert(bear->getPosition() - vec3(0.0f, 0, -3.0f)));
	PxBoxGeometry wallGeometry(PxVec3(7.0f, 7.0f, 0.01));
	PxRigidStatic* gWall = PxCreateStatic(*mPhysics, wallPos, wallGeometry, *mMaterial);
	pxScene->addActor(*gWall);



	Geometry* geomBox2 = geometries[17].get();
	PxTransform box2Pos(util::convert(geomBox2->getPosition()));
	PxBoxGeometry box2Geometry(PxVec3(0.8f));
	PxRigidDynamic* gBox2 = PxCreateDynamic(*mPhysics, box2Pos, box2Geometry, *mMaterial, 1.0f);
	pxScene->addActor(*gBox2);
	geomBox2->setStatic(false);
	geomBox2->setDynamicActor(gBox2);

	for (int i = 3; i <= 14; i++)
	{
		Geometry* geomRock = geometries[i].get();
		PxTransform rockPos(util::convert(geomRock->getPosition()));
		PxSphereGeometry rockPXGeometry(PxReal(0.7f));
		PxRigidStatic* gRock = PxCreateStatic(*mPhysics, rockPos, rockPXGeometry, *mMaterial);
		pxScene->addActor(*gRock);
		geomRock->setStatic(true);
		geomRock->setStaticActor(gRock);
		gRock->setName("Rock");
		actorsRock.push_back(gRock);
	}
	


	Mesh* worldMesh = world->getModel()->meshes[0];
	PxTriangleMesh* pxMesh = util::convert(worldMesh, mPhysics, mCooking);
	//aiMesh* mesh = world->model->meshes[0]->source;
	//PxTriangleMesh* pxMesh = util::convert(mesh, mPhysics, mCooking);

	if (pxMesh)
	{
		PxTriangleMeshGeometry triGeom(pxMesh);
		triGeom.scale = PxMeshScale(125.0f);
		if (triGeom.isValid())
		{
			PxMat44 pxModelMat = util::convert(world->modelMatrix);
			PxTransform worldPose = PxTransform(pxModelMat); //worldPose.q
			PxTransform pose = PxTransform(util::convert(world->getPosition()), PxQuat(PxHalfPi, PxVec3(-1.0f, 0.0f, 0.0f)));
			PxRigidStatic* worldActor = mPhysics->createRigidStatic(pose);
			PxShape* shape = worldActor->createShape(triGeom, *mMaterial);
			PX_UNUSED(shape);
			pxScene->addActor(*worldActor);
		}
	}
}

void Scene::initAnimatedTextures()
{
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00421.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00431.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00441.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00451.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00461.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00471.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00481.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00491.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00501.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00511.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00521.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00531.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00541.png", 0));
	crashVideo.push_back(make_unique<Texture>(L"Texture/video/crash/scene00551.png", 0));

	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water01.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water02.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water03.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water04.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water05.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water06.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water07.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water08.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water09.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water10.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water11.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water12.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water13.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water14.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water15.png", 0, true));
	waterVideo.push_back(make_unique<Texture>(L"Texture/video/water/Water16.png", 0, true));
}

/**
* Bear collision callbacks!
**/
void Scene::onShapeHit(const PxControllerShapeHit& hit)
{
	
	/*
	if (actorName)
	{
	for (int i = 0; i < actorsFish.size(); i++)
	{
	if (strcmp(actorName, actorsFish[i]->getName()))
	{
	int index = stoi(actorName);
	geometries[index]->drawable = false;
	pxScene->removeActor(*hit.actor, false);
	userScore += 1;
	}
	}*/
	
	if (hit.actor->getName() != nullptr){
		const char* actorName = hit.actor->getName();
		if (strcmp(actorName, "Rock") == 0)
		{
			gameOver = true;
		}
	}

	//TODO implement fish collision
	//cout << "Bear hit shape" << endl;

}

void Scene::onControllerHit(const PxControllersHit& hit)
{

}
void Scene::onObstacleHit(const PxControllerObstacleHit& hit)
{

}