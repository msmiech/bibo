#include "Scene.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include "BeaverModel.hpp"
#include "RaccoonModel.hpp"
#include "../Util/Console.hpp"
#include "../Util/Input.hpp"
#include "../World/Landmarks.hpp"
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
	const vec3 UP(0.0f, 1.0f, 0.0f);
	//The race goes along -z at the start (the bear looks there, Bear::Bear)
	const vec3 START_DIRECTION(0.0f, 0.0f, -1.0f);
	//The bear starts behind the starting line (left of the crate), the beaver sits next to the line at the left side of the track
	const float SPAWN_BEHIND_LINE = 7.0f, SPAWN_SIDE = -0.8f;
	const float BEAVER_SIDE = -5.0f, BEAVER_BEHIND_LINE = 1.5f;
	//In a conversation the bear keeps this distance to the beaver (center to center, so that their faces do not touch)
	const float TALK_SPACING = 3.6f;
	const std::size_t BEAVER_FUR_COUNT = 16000;
	//Height of the exclamation mark above the beaver
	const float MARKER_SCALE = 0.8f;
	//The race is lost when the bear is this deep in the water (its paws)
	const float IN_WATER_DEPTH = 0.35f;
	//Mishka is only animated while the camera is this close (far away its fur and its head are too small to see)
	const float MISHKA_UPDATE_DISTANCE = 90.0f;
	//Mishka looks at the bear when it comes this close, otherwise at the salmon of its pool
	const float MISHKA_ATTENTION_DISTANCE = 12.0f;
	//The bear pushes the plants aside within this distance of the center of its capsule (about down to its paws)
	const float BEAR_FOLIAGE_RADIUS = 1.3f;
	//Chonk is a well fed raccoon
	const float CHONK_GIRTH = 1.45f;
	const std::size_t CHONK_FUR_COUNT = 16000, FAN_FUR_COUNT = 9000;
	//The bear talks to Chonk from a bit closer than to the others (it is smaller), it inspects the flat stone from closer still
	const float CHONK_SPACING = 3.2f, STONE_SPACING = 2.4f;
	//The exclamation mark above the stone circle (the goal of the stones quest) is larger, to be seen from afar
	const float GOAL_MARKER_SCALE = 1.8f, GOAL_MARKER_HEIGHT = 3.2f;
	//The raccoons go on cheering this long after a quest was completed
	const float CHEER_LINGER = 4.5f;
	//Where the raccoons that cheer stand: ahead of the bear and beside its way (in the canyon of the track also up on its rim);
	//they come from this far further out
	const float FAN_AHEAD_MIN = 8.0f, FAN_AHEAD_MAX = 24.0f, FAN_SIDE_MIN = 4.5f, FAN_SIDE_MAX = 16.0f, FAN_WOODS_MIN = 9.0f, FAN_WOODS_MAX = 14.0f;
	//Hidden raccoons are drawn far under the island (out of every view)
	const vec3 HIDDEN_PLACE(0.0f, -1000.0f, 0.0f);

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
	initQuest();
	initMishka();
	initChonk();
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

void Scene::initQuest()
{
	raceQuest = make_unique<RaceQuest>(finishLineTrigger, START_DIRECTION);
	const vec3 behind = -START_DIRECTION;
	//The beaver sits at the side of the track next to the starting line and looks across it, towards the bear coming from the start
	vec3 seat = groundAt(finishLineTrigger.center + finishLineTrigger.across * BEAVER_SIDE + behind * BEAVER_BEHIND_LINE);
	beaver = make_unique<Beaver>(seat, normalize(finishLineTrigger.across * (BEAVER_SIDE > 0.0f ? -1.0f : 1.0f) + 0.6f * behind));
	beaverBody = physicsWorld->createKinematicSphere(beaver->getColliderCenter(), beaver->getColliderRadius());
	FurryRenderer::Strands strands;
	strands.furCount = BEAVER_FUR_COUNT;
	strands.furSeed = 91;
	FurryRenderer::Bounds bounds;
	bounds.center = vec3(0.0f, 0.5f, 0.0f);
	bounds.radius = 0.8f;
	beaverRenderer = make_unique<FurryRenderer>(BeaverModel::build(), 1, strands, bounds);

	//The bear starts behind the line, the beaver and the line are in front of it. In deep water it swims.
	BearCharacter& character = bear->getCharacter();
	character.setWaterLevel(world->getGenerator().getSeaLevel());
	vec3 spawn = groundAt(bear->getPosition() + behind * SPAWN_BEHIND_LINE + finishLineTrigger.across * SPAWN_SIDE) + UP * (BearCharacter::groundOffset() + 0.05f);
	character.setStart(spawn, START_DIRECTION);
	bear->reset();
}

void Scene::initMishka()
{
	const world::WorldGenerator& generator = world->getGenerator();
	mishkaPool = largestPool(salmon->getPools());
	vec3 spot, look;
	if (mishkaPool < 0 || !findShoreSpot(generator, salmon->getPools()[mishkaPool], spot, look))
	{
		util::fatalError("There is no place for Mishka next to the pools of water on the track");
	}
	std::string error;
	mishkaModel = resources::SkinnedModel::load(Bear::MODEL_FILE, error);
	if (!mishkaModel)
	{
		util::fatalError(std::string("Could not load ") + Bear::MODEL_FILE + ": " + error);
	}
	RestingBear::paintCoat(*mishkaModel);
	//Its paws and fur lie on the height field (the physics of the terrain is only streamed around the player's bear)
	GroundProbe heightField = [&generator](vec3 from, vec3 to, vec3& point, vec3& normal) {
		vec2 at(to.x, to.z);
		float height = generator.height(at);
		if (height > std::max(from.y, to.y) || height < std::min(from.y, to.y))
		{
			return false;
		}
		const float step = 0.3f;
		float dx = generator.height(at + vec2(step, 0.0f)) - generator.height(at - vec2(step, 0.0f));
		float dz = generator.height(at + vec2(0.0f, step)) - generator.height(at - vec2(0.0f, step));
		point = vec3(to.x, height, to.z);
		normal = normalize(vec3(-dx, 2.0f * step, -dz));
		return true;
	};
	mishka = make_unique<RestingBear>(*mishkaModel, spot, look, heightField);
	mishkaBody = physicsWorld->createKinematicSphere(mishka->getColliderCenter(), mishka->getColliderRadius());
	mishkaRenderer = make_unique<BearRenderer>(*mishkaModel, RestingBear::STRAND_COUNT);
	mishkaRenderer->setMaxFurLength(fur::MAX_LENGTH * RestingBear::FUR_LENGTH);
	salmonQuest = make_unique<SalmonQuest>(static_cast<int>(salmon->getPools().size()));
	cout << "Mishka lies at pool " << mishkaPool + 1 << " of " << salmon->getPools().size() << endl;
}

void Scene::initChonk()
{
	const world::WorldGenerator& generator = world->getGenerator();
	const world::Landmarks& landmarks = generator.getLandmarks();
	const vector<world::PointOfInterest>& points = landmarks.getPointsOfInterest();
	const vector<world::FootPath>& paths = landmarks.getPaths();
	int stones = -1;
	for (std::size_t i = 0; i < points.size(); ++i)
	{
		stones = points[i].kind == world::PointOfInterest::Kind::STANDING_STONES ? static_cast<int>(i) : stones;
	}
	if (stones < 0)
	{
		util::fatalError("The island has no circle of standing stones for Chonk's quest");
	}
	stonesCenter = points[stones].position;
	stonesRadius = points[stones].radius;

	//The path to the stones, back along the paths it branches off from to the one that leaves the track: Chonk sits at its
	//trailhead, across the path from the cairn, looking at the path
	int path = -1;
	for (std::size_t p = 0; p < paths.size(); ++p)
	{
		path = paths[p].pointOfInterest == stones ? static_cast<int>(p) : path;
	}
	for (int step = 0; path >= 0 && !paths[path].fromTrack && step < static_cast<int>(paths.size()); ++step)
	{
		vec2 start(paths[path].points.front().x, paths[path].points.front().z);
		int parent = -1;
		for (int p = 0; p < path && parent < 0; ++p)
		{
			for (const vec3& point : paths[p].points)
			{
				parent = distance(vec2(point.x, point.z), start) < 0.05f ? p : parent;
			}
		}
		path = parent;
	}
	if (path < 0 || static_cast<std::size_t>(path) >= landmarks.getMarkers().size())
	{
		util::fatalError("There is no foot path from the track to the standing stones for Chonk");
	}
	const vector<vec3>& line = paths[path].points;
	vec2 cairn(landmarks.getMarkers()[path].x, landmarks.getMarkers()[path].z);
	std::size_t marked = 0;
	for (std::size_t i = 0; i < line.size(); ++i)
	{
		marked = distance(vec2(line[i].x, line[i].z), cairn) < distance(vec2(line[marked].x, line[marked].z), cairn) ? i : marked;
	}
	vec3 seat(0.0f), look(0.0f);
	bool found = false;
	const world::TrackPath& track = generator.getPath();
	for (int offset = 0; offset < 12 && !found; ++offset)
	{
		std::size_t i = std::min(line.size() - 2, marked + static_cast<std::size_t>(offset));
		vec2 middle(line[i].x, line[i].z);
		vec2 along = normalize(vec2(line[i + 1].x, line[i + 1].z) - middle);
		vec2 across(-along.y, along.x);
		//On the other side than the cairn first
		float away = dot(cairn - middle, across) > 0.0f ? -1.0f : 1.0f;
		for (float side : { away, -away })
		{
			vec2 at = middle + across * (side * (world::Landmarks::PATH_HALF_WIDTH + 2.4f));
			float height, east, north;
			world::TrackPath::Nearest nearest = track.nearest(at);
			bool beside = !nearest.found || std::abs(nearest.trailLateral) > world::TrackPath::TRAIL_HALF_WIDTH + 2.0f;
			if (beside && raccoonGround(at, height) && raccoonGround(at + vec2(0.8f, 0.0f), east) && raccoonGround(at + vec2(0.0f, 0.8f), north)
				&& std::abs(east - height) < 0.5f && std::abs(north - height) < 0.5f)
			{
				seat = vec3(at.x, height, at.y);
				//Looks across the path, a bit towards the track
				vec2 back(line[std::max<std::size_t>(i, 4) - 4].x - middle.x, line[std::max<std::size_t>(i, 4) - 4].z - middle.y);
				vec2 facing = (middle - at) + 0.5f * (length(back) > 1e-3f ? normalize(back) : vec2(0.0f)) * length(middle - at);
				look = vec3(facing.x, 0.0f, facing.y);
				found = true;
				break;
			}
		}
	}
	if (!found)
	{
		util::fatalError("There is no place for Chonk at the trailhead of the paths");
	}
	Raccoon::Ground ground = [this](vec2 at, float& height) { return raccoonGround(at, height); };
	chonk = make_unique<Raccoon>(ground, 2024u, CHONK_GIRTH);
	chonk->sitAt(seat, look);
	chonkBody = physicsWorld->createKinematicSphere(chonk->getColliderCenter(), chonk->getColliderRadius());
	FurryRenderer::Strands strands;
	strands.furCount = CHONK_FUR_COUNT;
	strands.furSeed = 133;
	FurryRenderer::Bounds bounds;
	bounds.center = vec3(0.0f, 0.35f, 0.0f);
	bounds.radius = 0.8f;
	chonkRenderer = make_unique<FurryRenderer>(RaccoonModel::build(CHONK_GIRTH), 1, strands, bounds);
	stonesQuest = make_unique<StonesQuest>();

	fans = make_unique<RaccoonFans>(ground, [this](vec3 bear, vec3 heading, std::mt19937& random, vec2& from, vec2& spot) {
		return findFanSpot(bear, heading, random, from, spot);
	});
	strands.furCount = FAN_FUR_COUNT;
	strands.furSeed = 177;
	fansRenderer = make_unique<FurryRenderer>(RaccoonModel::build(), RaccoonFans::COUNT, strands, bounds);
	cout << "Chonk sits at the trailhead (" << seat.x << ", " << seat.z << "), the standing stones are at (" << stonesCenter.x << ", " << stonesCenter.z << ")" << endl;
}

bool Scene::raccoonGround(vec2 position, float& height) const
{
	const world::WorldGenerator& generator = world->getGenerator();
	height = generator.height(position);
	return height > generator.getSeaLevel() + 0.3f && generator.caveRoofTop(position) < -1000.0f;
}

bool Scene::findFanSpot(vec3 bear, vec3 heading, std::mt19937& random, vec2& from, vec2& spot)
{
	const world::WorldGenerator& generator = world->getGenerator();
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	vec2 forward = normalize(vec2(heading.x, heading.z));
	vec2 right(-forward.y, forward.x);
	ViewFrustum frustum = camera->getViewFrustum();
	//Of the spots that work the one closest to the height of the bear (it sees it there, not only up on the rim of a canyon)
	float best = numeric_limits<float>::max();
	for (int attempt = 0; attempt < 16; ++attempt)
	{
		float side = unit(random) < 0.5f ? -1.0f : 1.0f;
		vec2 out = right * side;
		vec2 at = vec2(bear.x, bear.z) + forward * glm::mix(FAN_AHEAD_MIN, FAN_AHEAD_MAX, unit(random)) + out * glm::mix(FAN_SIDE_MIN, FAN_SIDE_MAX, unit(random));
		//On level ground (not on the side of the canyon)
		float height, east, north;
		if (!raccoonGround(at, height) || !raccoonGround(at + vec2(0.7f, 0.0f), east) || !raccoonGround(at + vec2(0.0f, 0.7f), north)
			|| std::abs(east - height) > 0.4f || std::abs(north - height) > 0.4f)
		{
			continue;
		}
		//Not on the trail of the race (the raccoons have no collision, the bear would run through them)
		world::TrackPath::Nearest nearest = generator.getPath().nearest(at);
		if (nearest.found && std::abs(nearest.trailLateral) < world::TrackPath::TRAIL_HALF_WIDTH + 1.5f)
		{
			continue;
		}
		float turn = (unit(random) - 0.5f) * 0.8f;
		vec2 woods = out * std::cos(turn) + forward * std::sin(turn);
		vec2 start = at + woods * glm::mix(FAN_WOODS_MIN, FAN_WOODS_MAX, unit(random));
		float startHeight;
		if (!raccoonGround(start, startHeight))
		{
			continue;
		}
		//It can walk all the way (on land, not too steep)
		bool walkable = true;
		float last = startHeight;
		float steps = std::ceil(distance(start, at));
		for (float k = 1.0f; k <= steps && walkable; k += 1.0f)
		{
			float h;
			walkable = raccoonGround(glm::mix(start, at, k / steps), h) && std::abs(h - last) < 0.9f;
			last = h;
		}
		if (!walkable)
		{
			continue;
		}
		//Nothing in the way (trees, rocks): a hit is in the way when it is clearly above the ground
		vec3 high(start.x, startHeight + 0.9f, start.y), low(at.x, height + 0.9f, at.y);
		physics::RayHit hit;
		if (physicsWorld->castSphere(high, low - high, 0.3f, hit, true) && hit.position.y > generator.height(vec2(hit.position.x, hit.position.z)) + 0.45f)
		{
			continue;
		}
		//It comes out of the woods where the camera does not see it appear (out of the view, or behind something)
		vec3 eye = camera->getPosition();
		bool unseen = frustum.sphereInFrustum(high, 1.2f) == ViewFrustum::OUTSIDE
			|| (physicsWorld->castRay(eye, high - eye, hit, true) && hit.fraction < 0.97f);
		if (!unseen)
		{
			continue;
		}
		float score = std::abs(height - bear.y) + 0.1f * distance(at, vec2(bear.x, bear.z));
		if (score < best)
		{
			best = score;
			from = start;
			spot = at;
		}
	}
	return best < numeric_limits<float>::max();
}

void Scene::updateTalkPartner()
{
	//In a conversation: the one it talks to
	if (raceQuest->getStage() == RaceQuest::Stage::TALKING)
	{
		talkPartner = TalkPartner::BEAVER;
		return;
	}
	if (salmonQuest->getStage() == SalmonQuest::Stage::TALKING)
	{
		talkPartner = TalkPartner::MISHKA;
		return;
	}
	if (stonesQuest->getStage() == StonesQuest::Stage::TALKING || stonesQuest->isInspecting())
	{
		talkPartner = stonesQuest->isInspecting() ? TalkPartner::STONES : TalkPartner::CHONK;
		return;
	}
	//Otherwise the closest one it could talk to
	struct Candidate
	{
		TalkPartner partner;
		bool possible;
		vec3 position;
	};
	const Candidate candidates[] = {
		{ TalkPartner::BEAVER, raceQuest->canTalk(), beaver->getPosition() },
		{ TalkPartner::MISHKA, salmonQuest->canTalk(), mishka->getPosition() },
		{ TalkPartner::CHONK, stonesQuest->canTalk(), chonk->getPosition() },
		{ TalkPartner::STONES, stonesQuest->canInspect(), stonesCenter },
	};
	talkPartner = TalkPartner::NONE;
	float closest = numeric_limits<float>::max();
	vec3 here = bear->getPosition();
	for (const Candidate& candidate : candidates)
	{
		float away = length(vec2(candidate.position.x - here.x, candidate.position.z - here.z));
		if (candidate.possible && away < closest)
		{
			closest = away;
			talkPartner = candidate.partner;
		}
	}
}

vec3 Scene::groundAt(vec3 position) const
{
	return vec3(position.x, world->getGenerator().height(vec2(position.x, position.z)), position.z);
}

bool Scene::isBearInWater()
{
	return bear->getCharacter().getWaterDepth() > IN_WATER_DEPTH;
}

bool Scene::canTalkAt(vec3 position, float distance)
{
	//It can talk when it stands (not while it tumbles, swims or eats)
	const BearCharacter& character = bear->getCharacter();
	vec3 offset = position - bear->getPosition();
	return length(vec2(offset.x, offset.z)) < distance && character.getState() == BearCharacter::State::CONTROL
		&& !character.isSwimming() && character.getFish() == BearCharacter::Fish::NONE;
}

RaceQuest::Situation Scene::questSituation(const input::PlayerInput& player)
{
	RaceQuest::Situation situation;
	situation.bear = bear->getPosition();
	situation.nearBeaver = canTalkAt(beaver->getPosition(), RaceQuest::TALK_DISTANCE);
	situation.busy = salmonQuest->isActive() || stonesQuest->isActive();
	situation.swimming = isBearInWater();
	situation.onTrack = RaceQuest::isOnTrack(world->getGenerator().getPath(), situation.bear);
	situation.atStart = atStart;
	situation.interact = player.interact && talkPartner == TalkPartner::BEAVER;
	situation.dialog.confirm = player.confirm;
	situation.dialog.navigate = player.navigate;
	situation.dialog.cancel = player.cancel;
	return situation;
}

SalmonQuest::Situation Scene::salmonSituation(const input::PlayerInput& player)
{
	SalmonQuest::Situation situation;
	situation.nearMishka = canTalkAt(mishka->getPosition(), SalmonQuest::TALK_DISTANCE);
	situation.busy = raceQuest->getStage() != RaceQuest::Stage::IDLE || stonesQuest->isActive();
	situation.eatenPool = eatenPool;
	eatenPool = -1;
	situation.interact = player.interact && talkPartner == TalkPartner::MISHKA;
	situation.dialog.confirm = player.confirm;
	situation.dialog.navigate = player.navigate;
	situation.dialog.cancel = player.cancel;
	return situation;
}

StonesQuest::Situation Scene::stonesSituation(const input::PlayerInput& player)
{
	StonesQuest::Situation situation;
	situation.nearChonk = canTalkAt(chonk->getPosition(), StonesQuest::TALK_DISTANCE);
	vec3 offset = stonesCenter - bear->getPosition();
	situation.atStones = length(vec2(offset.x, offset.z)) < stonesRadius;
	situation.nearStone = canTalkAt(stonesCenter, StonesQuest::INSPECT_DISTANCE);
	situation.busy = raceQuest->getStage() != RaceQuest::Stage::IDLE || salmonQuest->isActive();
	situation.interact = player.interact && (talkPartner == TalkPartner::CHONK || talkPartner == TalkPartner::STONES);
	situation.dialog.confirm = player.confirm;
	situation.dialog.navigate = player.navigate;
	situation.dialog.cancel = player.cancel;
	return situation;
}

bool Scene::turnTowards(vec3 direction, BearInput& input)
{
	vec3 facing = bear->getDirection();
	direction.y = 0.0f;
	if (length(direction) < 1e-4f)
	{
		return true;
	}
	direction = normalize(direction);
	//Positive: the direction is to the left
	float angle = std::atan2(cross(facing, direction).y, dot(facing, direction));
	if (std::abs(angle) < 0.08f)
	{
		return true;
	}
	input.turn = std::copysign(std::clamp(std::abs(angle) * 2.0f, 0.35f, 1.0f), angle);
	return false;
}

BearInput Scene::bearInput(const input::PlayerInput& player)
{
	BearInput input;
	RaceQuest::Stage stage = raceQuest->getStage();
	if (stage != RaceQuest::Stage::WALKING_TO_START)
	{
		atStart = false;
	}
	if (isPlayerInControl())
	{
		input.move = input::cameraRelative(player.move, camera->getForward(), camera->getRight());
		//The button that answered the beaver must be released before the bear jumps
		jumpBlocked = jumpBlocked && player.jump;
		input.jump = player.jump && !jumpBlocked;
		return input;
	}
	jumpBlocked = true;
	if (talkPartner != TalkPartner::NONE && !(stage == RaceQuest::Stage::WALKING_TO_START || stage == RaceQuest::Stage::COUNTDOWN))
	{
		//It turns to the animal it talks to (or the flat stone), steps back a little if it came too close and walks up to it
		//if it is too far away, so that they talk face to face
		vec3 partner = bear->getPosition();
		float spacing = TALK_SPACING;
		switch (talkPartner)
		{
		case TalkPartner::BEAVER: partner = beaver->getPosition(); break;
		case TalkPartner::MISHKA: partner = mishka->getPosition(); break;
		case TalkPartner::CHONK: partner = chonk->getPosition(); spacing = CHONK_SPACING; break;
		case TalkPartner::STONES: partner = stonesCenter; spacing = STONE_SPACING; break;
		case TalkPartner::NONE: break;
		}
		vec3 toPartner = partner - bear->getPosition();
		float away = length(vec2(toPartner.x, toPartner.z));
		bool facing = turnTowards(toPartner, input);
		if (facing && away < spacing - 0.2f)
		{
			input.forward = -0.35f;
		}
		else if (facing && away > spacing + 0.6f)
		{
			input.forward = std::clamp(0.3f * (away - spacing), 0.15f, 0.35f);
		}
	}
	else if (stage == RaceQuest::Stage::WALKING_TO_START)
	{
		//Walks up to the starting line and turns along the track
		vec3 toStart = raceQuest->getStartPosition() - bear->getPosition();
		toStart.y = 0.0f;
		float distance = length(toStart);
		if (distance > 0.35f)
		{
			input.move = toStart / distance * glm::mix(0.15f, 0.35f, std::clamp(distance / 2.0f, 0.0f, 1.0f));
		}
		else
		{
			atStart = turnTowards(raceQuest->getStartDirection(), input);
		}
	}
	return input; //the countdown: it stands still
}

void Scene::updateCamera(float time_delta)
{
	RaceQuest::Stage stage = raceQuest->getStage();
	bool talking = stage == RaceQuest::Stage::TALKING || salmonQuest->getStage() == SalmonQuest::Stage::TALKING
		|| stonesQuest->getStage() == StonesQuest::Stage::TALKING || stonesQuest->isInspecting();
	if (camera->isLocked() && talking)
	{
		//Both from the side, a bit behind the bear (the animal it talks to faces the camera), from the side with more room
		vec3 bearHead = bear->getPosition() + UP * 0.4f;
		vec3 partnerHead = beaver->getHeadPosition();
		switch (talkPartner)
		{
		case TalkPartner::MISHKA: partnerHead = mishka->getFacePosition(); break;
		case TalkPartner::CHONK: partnerHead = chonk->getFacePosition(); break;
		case TalkPartner::STONES: partnerHead = stonesCenter + UP * 0.8f; break;
		default: break;
		}
		vec3 across = partnerHead - bearHead;
		across.y = 0.0f;
		across = length(across) > 1e-3f ? normalize(across) : START_DIRECTION;
		vec3 middle = 0.5f * (bearHead + partnerHead);
		vec3 best = middle;
		float bestRoom = -1.0f;
		for (float side : { 1.0f, -1.0f })
		{
			vec3 eye = middle + cross(across, UP) * (6.5f * side) - across * 2.0f + UP * 1.4f;
			physics::RayHit hit;
			float room = physicsWorld->castSphere(middle, eye - middle, Camera::COLLISION_RADIUS, hit, true) ? hit.fraction : 1.0f;
			if (room > bestRoom + 0.05f)
			{
				best = eye;
				bestRoom = room;
			}
		}
		camera->shot(best, middle + UP * 0.1f, time_delta);
	}
	else if (camera->isLocked() && stage == RaceQuest::Stage::WALKING_TO_START)
	{
		//From beyond the starting line: the bear walks up to it, the beaver cheers next to it
		vec3 start = groundAt(raceQuest->getStartPosition());
		vec3 forward = raceQuest->getStartDirection();
		vec3 right = cross(forward, UP);
		camera->shot(start + forward * 6.5f - right * 1.5f + UP * 2.4f, bear->getPosition() + UP * 0.2f, time_delta);
	}
	else if (camera->isLocked())
	{
		camera->follow(cameraTarget(), time_delta);
	}
	camera->update(time_delta);
}

void Scene::update(float time_delta, const input::PlayerInput& player) {
	physicsWorld->update(time_delta);
	//The camera turns first, the bear moves relative to where the camera looks then (not in a conversation or a cutscene)
	if (isPlayerInControl() || !camera->isLocked())
	{
		camera->rotate(player.look.x, player.look.y);
		camera->zoom(player.zoom);
		if (player.recenter)
		{
			camera->recenter();
		}
	}

	markerTime += time_delta;
	//The button starts a conversation with the closest animal only
	updateTalkPartner();

	//The race quest: the conversation with the beaver, the cutscene, the countdown and the race
	raceQuest->update(time_delta, questSituation(player));
	for (RaceQuest::Event event : raceQuest->takeEvents())
	{
		if (event == RaceQuest::Event::BEAVER_SOUND)
		{
			beaver->speak();
		}
		else if (event == RaceQuest::Event::ACCEPTED || event == RaceQuest::Event::COMPLETED)
		{
			beaver->cheer();
		}
		//The raccoons beside the track cheer at every checkpoint and at the finish
		if (event == RaceQuest::Event::CHECKPOINT || event == RaceQuest::Event::COMPLETED)
		{
			fans->celebrate();
		}
		if (event == RaceQuest::Event::COMPLETED)
		{
			cheerLinger = CHEER_LINGER;
		}
		events.push_back(event);
	}
	RaceQuest::Stage stage = raceQuest->getStage();
	if (stage != lastStage && stage == RaceQuest::Stage::COUNTDOWN)
	{
		//The race starts at the line, looking along the track (also if the bear did not get there in the cutscene, e.g. it was knocked over)
		vec3 start = raceQuest->getStartPosition();
		vec3 here = bear->getPosition();
		if (!atStart || length(vec2(start.x - here.x, start.z - here.z)) > 1.0f)
		{
			world->ensureCollision(start, camera->getPosition());
			bear->getCharacter().placeAt(groundAt(start) + UP * (BearCharacter::groundOffset() + 0.05f), raceQuest->getStartDirection());
		}
		camera->snapBehind(cameraTarget());
	}
	lastStage = stage;
	beaver->setTalking(raceQuest->getDialog().isOpen());
	//During the race Mishka and Chonk are no obstacles (they are beside the route, but a racer may cut close past them)
	bool racing = stage != RaceQuest::Stage::IDLE;
	for (auto [body, animal] : { std::pair<physics::BodyHandle*, vec4>(&mishkaBody, vec4(mishka->getColliderCenter(), mishka->getColliderRadius())),
		std::pair<physics::BodyHandle*, vec4>(&chonkBody, vec4(chonk->getColliderCenter(), chonk->getColliderRadius())) })
	{
		if (racing && *body != physics::INVALID_BODY)
		{
			physicsWorld->removeBody(*body);
			*body = physics::INVALID_BODY;
		}
		else if (!racing && *body == physics::INVALID_BODY)
		{
			*body = physicsWorld->createKinematicSphere(vec3(animal), animal.w);
		}
	}

	//Fishing for Mishka: the conversation and the salmon the bear eats up
	salmonQuest->update(time_delta, salmonSituation(player));
	for (SalmonQuest::Event event : salmonQuest->takeEvents())
	{
		if (event == SalmonQuest::Event::BEAR_SOUND)
		{
			mishka->speak();
		}
		//The raccoons cheer for every salmon of the quest
		if (event == SalmonQuest::Event::EATEN || event == SalmonQuest::Event::COMPLETED)
		{
			fans->celebrate();
		}
		if (event == SalmonQuest::Event::COMPLETED)
		{
			cheerLinger = CHEER_LINGER;
		}
		salmonEvents.push_back(event);
	}

	//The standing stones for Chonk: the conversations, the stone circle and the report
	stonesQuest->update(time_delta, stonesSituation(player));
	for (StonesQuest::Event event : stonesQuest->takeEvents())
	{
		if (event == StonesQuest::Event::CHONK_SOUND)
		{
			chonk->speak();
			++raccoonChatters;
		}
		else if (event == StonesQuest::Event::ACCEPTED || event == StonesQuest::Event::COMPLETED)
		{
			chonk->cheer();
		}
		stonesEvents.push_back(event);
	}
	const Dialog& stonesDialog = stonesQuest->getDialog();
	chonk->setTalking(stonesDialog.isOpen() && stonesDialog.getSpeaker() == StonesQuest::SPEAKER);

	//The bear looks at the animal it talks to (and the one it would talk to), the animals look at the bear when it is close
	switch (talkPartner)
	{
	case TalkPartner::BEAVER: bear->getCharacter().lookAt(beaver->getFacePosition()); break;
	case TalkPartner::MISHKA: bear->getCharacter().lookAt(mishka->getFacePosition()); break;
	case TalkPartner::CHONK: bear->getCharacter().lookAt(chonk->getFacePosition()); break;
	case TalkPartner::STONES: bear->getCharacter().lookAt(stonesCenter + UP * 0.3f); break;
	case TalkPartner::NONE: bear->getCharacter().stopLooking(); break;
	}
	bear->setInput(bearInput(player));

	for (unsigned int i = 0; i < geometries.size(); ++i)
	{
		geometries[i]->update(time_delta);
	}
	updateAnimals(time_delta);
	//The bear splashes into the water
	int newSplashes = bear->getCharacter().takeSplashes();
	if (newSplashes > 0)
	{
		salmon->splash(bear->getPosition(), bear->getCharacter().getWaterLevel(), 26);
		splashes += newSplashes;
	}
	updateCamera(time_delta);

	//The world streams around the camera, the physics around the bear. The bear and the animals push the plants aside.
	vector<world::FoliageInteraction::Body> bodies;
	bodies.push_back({ bear->getCharacter().getPosition(), BEAR_FOLIAGE_RADIUS });
	bodies.push_back({ beaver->getColliderCenter(), beaver->getColliderRadius() });
	bodies.push_back({ mishka->getColliderCenter(), mishka->getColliderRadius() });
	bodies.push_back({ chonk->getColliderCenter(), chonk->getColliderRadius() });
	for (const Raccoon& fan : fans->getRaccoons())
	{
		if (fan.isVisible())
		{
			bodies.push_back({ fan.getColliderCenter(), 0.8f * fan.getColliderRadius() });
		}
	}
	for (const Hedgehog& hedgehog : hedgehogs)
	{
		bodies.push_back({ hedgehog.getColliderCenter(), hedgehog.getColliderRadius() });
	}
	world->update(time_delta, camera->getPosition(), bear->getPosition(), bodies);
	placeAnimals();
}

vector<RaceQuest::Event> Scene::takeEvents()
{
	vector<RaceQuest::Event> result;
	result.swap(events);
	return result;
}

vector<SalmonQuest::Event> Scene::takeSalmonEvents()
{
	vector<SalmonQuest::Event> result;
	result.swap(salmonEvents);
	return result;
}

vector<StonesQuest::Event> Scene::takeStonesEvents()
{
	vector<StonesQuest::Event> result;
	result.swap(stonesEvents);
	return result;
}

int Scene::takeRaccoonChatters()
{
	int result = raccoonChatters;
	raccoonChatters = 0;
	return result;
}

int Scene::takeSplashes()
{
	int result = splashes;
	splashes = 0;
	return result;
}

void Scene::updateAnimals(float time_delta)
{
	//The hedgehogs stroll around, their collision spheres follow them (in the next update of the physics)
	//They scurry away when the bear comes close
	vector<fur::SphereCollider> obstacles;
	for (std::size_t i = 0; i < hedgehogs.size(); ++i)
	{
		hedgehogs[i].watch(bear->getPosition());
		hedgehogs[i].update(time_delta);
		physicsWorld->moveKinematic(hedgehogBodies[i], hedgehogs[i].getColliderCenter(), quat(1.0f, 0.0f, 0.0f, 0.0f), time_delta);
		obstacles.push_back({ hedgehogs[i].getColliderCenter(), hedgehogs[i].getColliderRadius() + SPINE_LENGTH });
	}
	bear->setObstacles(obstacles);

	//The beaver at the start turns to the bear and looks at its face
	beaver->update(time_delta, bear->getCharacter().getHeadPosition());
	physicsWorld->moveKinematic(beaverBody, beaver->getColliderCenter(), quat(1.0f, 0.0f, 0.0f, 0.0f), time_delta);

	//Chonk sits at the trailhead. On the race and while fishing raccoons come out of the woods to cheer (and for a while after
	//a quest was completed).
	vec3 bearHead = bear->getCharacter().getHeadPosition();
	chonk->update(time_delta, bearHead);
	raccoonChatters += chonk->takeChatters();
	if (chonkBody != physics::INVALID_BODY)
	{
		physicsWorld->moveKinematic(chonkBody, chonk->getColliderCenter(), quat(1.0f, 0.0f, 0.0f, 0.0f), time_delta);
	}
	cheerLinger = std::max(0.0f, cheerLinger - time_delta);
	bool cheering = raceQuest->getStage() == RaceQuest::Stage::RACING || salmonQuest->getStage() == SalmonQuest::Stage::FISHING || cheerLinger > 0.0f;
	vec3 heading = bear->getCharacter().getVelocity();
	heading.y = 0.0f;
	heading = length(heading) > 1.0f ? normalize(heading) : bear->getDirection();
	fans->update(time_delta, cheering, bear->getPosition(), bearHead, heading);
	raccoonChatters += fans->takeChatters();

	//Salmon jump out of the water when the bear comes close and it catches them. Not during the race (the bear has no time
	//to eat), on the salmon quest not out of the pools that are done.
	BearCharacter& character = bear->getCharacter();
	bool racing = raceQuest->getStage() != RaceQuest::Stage::IDLE;
	vector<bool> closed = racing ? vector<bool>(salmon->getPools().size(), true) : salmonQuest->isActive() ? salmonQuest->getDonePools() : vector<bool>();
	int pool = salmon->update(time_delta, character.getBoundsCenter(), character.getMouthPosition(), character.canCatchFish() && !racing, closed);
	if (pool >= 0 && character.catchFish())
	{
		carriedPool = pool;
	}
	//It counts for the quest when the bear ate it up (not when it dropped it)
	if (character.takeEatenFish() > 0 && carriedPool >= 0)
	{
		eatenPool = carriedPool;
		carriedPool = -1;
	}
	if (character.getFish() == BearCharacter::Fish::NONE)
	{
		carriedPool = -1;
	}

	//Mishka watches the salmon of its pool, and the bear when it comes close
	vec3 cameraPosition = camera->getPosition();
	bool talkingToMishka = salmonQuest->getStage() == SalmonQuest::Stage::TALKING;
	if (talkingToMishka || distance(cameraPosition, mishka->getPosition()) < MISHKA_UPDATE_DISTANCE)
	{
		vec3 bearHead = character.getHeadPosition();
		const vec3* watch = nullptr;
		vec3 salmonPosition;
		if (talkingToMishka || distance(bearHead, mishka->getFacePosition()) < MISHKA_ATTENTION_DISTANCE)
		{
			watch = &bearHead;
		}
		else
		{
			float highest = -numeric_limits<float>::max();
			for (const SalmonPools::Jump& jump : salmon->getJumps())
			{
				if (jump.pool == mishkaPool && jump.position().y > highest)
				{
					highest = jump.position().y;
					salmonPosition = jump.position();
					watch = &salmonPosition;
				}
			}
		}
		mishka->update(time_delta, watch);
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
	beaverRenderer->setPose(0, beaver->getPose());
	//The beaver offers the race: a golden exclamation mark above it, a blue one once the race was completed (Mishka the same)
	if (raceQuest->isOffered())
	{
		CreatureModel marker = raceQuest->hasCompleted() ? CreatureModel::QUEST_MARKER_REPEATABLE : CreatureModel::QUEST_MARKER;
		creatures->add(marker, beaver->getMarkerPosition(), beaver->getMarkerRotation(), MARKER_SCALE);
	}
	if (salmonQuest->isOffered())
	{
		CreatureModel marker = salmonQuest->hasCompleted() ? CreatureModel::QUEST_MARKER_REPEATABLE : CreatureModel::QUEST_MARKER;
		creatures->add(marker, mishka->getMarkerPosition(), mishka->getMarkerRotation(), MARKER_SCALE);
	}

	//Chonk offers its quest and waits for the report (the mark above it), a large mark shows the way to the stone circle
	//The vertices of the raccoons are moved on the CPU: Chonk only while the camera is close enough to see it move, a hidden fan
	//once when it goes back into the woods
	if (!chonkPosed || distance(camera->getPosition(), chonk->getPosition()) < MISHKA_UPDATE_DISTANCE)
	{
		chonkRenderer->setPose(0, chonk->getPose());
		chonkPosed = true;
	}
	if (stonesQuest->isOffered() || stonesQuest->hasReport())
	{
		creatures->add(CreatureModel::QUEST_MARKER, chonk->getMarkerPosition(), chonk->getMarkerRotation(), MARKER_SCALE);
	}
	if (stonesQuest->getStage() == StonesQuest::Stage::EXPLORING)
	{
		float bob = 0.15f * std::sin(2.0f * markerTime);
		creatures->add(CreatureModel::QUEST_MARKER, stonesCenter + UP * (GOAL_MARKER_HEIGHT + bob), chonk->getMarkerRotation(), GOAL_MARKER_SCALE);
	}
	const vector<Raccoon>& raccoons = fans->getRaccoons();
	fansHidden.resize(raccoons.size(), false);
	for (std::size_t i = 0; i < raccoons.size(); ++i)
	{
		if (raccoons[i].isVisible())
		{
			fansRenderer->setPose(i, raccoons[i].getPose());
		}
		else if (!fansHidden[i])
		{
			fansRenderer->setPose(i, vector<mat4>(RaccoonModel::PART_COUNT, glm::translate(mat4(1.0f), HIDDEN_PLACE) * glm::scale(mat4(1.0f), vec3(Raccoon::SCALE))));
		}
		fansHidden[i] = !raccoons[i].isVisible();
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
	carriedPool = eatenPool = -1;
	salmonQuest->abort();
	salmonEvents.clear();
	raceQuest->abort();
	lastStage = raceQuest->getStage();
	atStart = false;
	jumpBlocked = false;
	events.clear();
	splashes = 0;
	beaver->reset();
	physicsWorld->moveKinematic(beaverBody, beaver->getColliderCenter(), quat(1.0f, 0.0f, 0.0f, 0.0f), 0.0f);
	stonesQuest->abort();
	stonesEvents.clear();
	chonk->reset();
	fans->reset();
	cheerLinger = 0.0f;
	raccoonChatters = 0;
	talkPartner = TalkPartner::NONE;
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

void Scene::uploadMishka()
{
	if (mishkaUploaded != mishka->getVersion())
	{
		mishkaRenderer->upload(mishka->getPositions(), mishka->getNormals(), mishka->getFurOffsets(), mishka->getCombDirections());
		mishkaUploaded = mishka->getVersion();
	}
}

void Scene::drawShadowCasters(const render::ShadowCascade& cascade)
{
	world->drawShadows(cascade, camera->getPosition());
	creatures->drawShadow(cascade);
	depthShader->enable();
	glUniformMatrix4fv(depthShader->getUniformLocation("lightSpaceMatrix"), 1, GL_FALSE, value_ptr(cascade.lightSpace));
	hedgehogRenderer->drawSurfaces(depthShader.get());
	beaverRenderer->drawSurfaces(depthShader.get());
	chonkRenderer->drawSurfaces(depthShader.get());
	if (std::any_of(fans->getRaccoons().begin(), fans->getRaccoons().end(), [](const Raccoon& fan) { return fan.isVisible(); }))
	{
		fansRenderer->drawSurfaces(depthShader.get());
	}
	if (cascade.contains(mishka->getBoundsCenter(), mishka->getBoundsRadius()))
	{
		uploadMishka();
		mishkaRenderer->drawSurface(depthShader.get());
	}
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

	//3. The bears, the hedgehogs and the beaver: their fur and spines are blended with what is behind them
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
	beaverRenderer->drawShaded(context, frustum);
	chonkRenderer->drawShaded(context, frustum);
	fansRenderer->drawShaded(context, frustum);
	if (!viewFrustumCullingEnabled || frustum.sphereInFrustum(mishka->getBoundsCenter(), mishka->getBoundsRadius()) != ViewFrustum::OUTSIDE)
	{
		uploadMishka();
		mishkaRenderer->drawShaded(context, mishka->getBoundsCenter());
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
	//Ground under the sea (a large box whose top face is at y = -5), far out where the sea floor of the world ends
	physicsWorld->createStaticBox(vec3(0.0f, -6.0f, 0.0f), vec3(1000.0f, 1.0f, 1000.0f));

	//There is no wall at the start: the finish line is a trigger (see initFinishLine) and the checkpoints keep the bear from going backwards
	//(the bear starts a bit behind the line, see initQuest)

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
