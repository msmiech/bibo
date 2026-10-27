#include "Scene.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include "../Util/Console.hpp"
#include "../Util/Input.hpp"
#include "../World/Landmarks.hpp"
#include "../World/MaterialTextures.hpp"
#include "../World/TrackTerrain.hpp"
#include "BeaverModel.hpp"
#include "RaccoonModel.hpp"

namespace bibo::scene {

using glm::mat3;
using glm::mat4;
using glm::quat;
using glm::vec2;
using glm::vec3;
using glm::vec4;
using resources::Mesh;
using resources::Model;
using std::cout;
using std::endl;
using std::make_unique;
using std::numeric_limits;
using std::string;
using std::to_string;
using std::unique_ptr;
using std::vector;

namespace {
const char* const kTrackFile = "Model/SnowTerrain/track1.dae";
// How long the loading may wait for the world around the start (it goes on
// streaming afterwards)
const float kWorldLoadingTimeout = 30.0f;
// The hedgehogs live where the rocks of the track are (the nodes Rock1 to
// Rock12)
const int kHedgehogCount = 12;
// The fur of the bear lies on the spines of a hedgehog when it runs into it
const float kSpineLength = 0.8f * HedgehogRenderer::kSpineLength;
const vec3 kUp(0.0f, 1.0f, 0.0f);
// The race goes along -z at the start (the bear looks there, Bear::Bear)
const vec3 kStartDirection(0.0f, 0.0f, -1.0f);
// The bear starts behind the starting line (left of the crate), the beaver sits
// next to the line at the left side of the track
const float kSpawnBehindLine = 7.0f, kSpawnSide = -0.8f;
const float kBeaverSide = -5.0f, kBeaverBehindLine = 1.5f;
// In a conversation the bear keeps this distance to the beaver (center to
// center, so that their faces do not touch)
const float kTalkSpacing = 3.6f;
const std::size_t kBeaverFurCount = 16000;
// Height of the exclamation mark above the beaver
const float kMarkerScale = 0.8f;
// The race is lost when the bear is this deep in the water (its paws)
const float kInWaterDepth = 0.35f;
// Mishka is only animated while the camera is this close (far away its fur and
// its head are too small to see)
const float kMishkaUpdateDistance = 90.0f;
// Mishka looks at the bear when it comes this close, otherwise at the salmon of
// its pool
const float kMishkaAttentionDistance = 12.0f;
// The bear pushes the plants aside within this distance of the center of its
// capsule (about down to its paws)
const float kBearFoliageRadius = 1.3f;
// Chonk is a well fed raccoon
const float kChonkGirth = 1.45f;
const std::size_t kChonkFurCount = 16000, kFanFurCount = 9000;
// The bear talks to Chonk from a bit closer than to the others (it is smaller),
// it inspects the flat stone from closer still
const float kChonkSpacing = 3.2f, kStoneSpacing = 2.4f;
// The exclamation mark above the stone circle (the goal of the stones quest) is
// larger, to be seen from afar
const float kGoalMarkerScale = 1.8f, kGoalMarkerHeight = 3.2f;
// The raccoons go on cheering this long after a quest was completed
const float kCheerLinger = 4.5f;
// Where the raccoons that cheer stand: ahead of the bear and beside its way (in
// the canyon of the track also up on its rim); they come from this far further
// out
const float kFanAheadMin = 8.0f, kFanAheadMax = 24.0f, kFanSideMin = 4.5f,
            kFanSideMax = 16.0f, kFanWoodsMin = 9.0f, kFanWoodsMax = 14.0f;
// Hidden raccoons are drawn far under the island (out of every view)
const vec3 kHiddenPlace(0.0f, -1000.0f, 0.0f);

// The vertices of the meshes of a geometry in model space (the model matrix is
// not applied)
vector<vec3> ModelVertices(Geometry* geometry) {
  vector<vec3> points;
  for (const Mesh* mesh : geometry->GetModel()->meshes) {
    for (unsigned int i = 0; mesh->vertices && i < mesh->num_vertices; ++i) {
      points.push_back(vec3(mesh->vertices[i * 3], mesh->vertices[i * 3 + 1],
                            mesh->vertices[i * 3 + 2]));
    }
  }
  return points;
}

// The vertices of the meshes of a geometry in world space
vector<vec3> WorldVertices(Geometry* geometry) {
  vector<vec3> points = ModelVertices(geometry);
  for (vec3& point : points) {
    point = vec3(geometry->model_matrix * vec4(point, 1.0f));
  }
  return points;
}
}  // namespace

Geometry* Scene::AddGeometry(const aiScene* track, const char* node_name,
                             Texture* texture, Texture* normal_map) {
  geometries_.push_back(make_unique<Geometry>(mat4(1.0f), main_shader_,
                                              *window_, track, node_name,
                                              texture, normal_map));
  return geometries_.back().get();
}

Scene::Scene(GLFWwindow* window, Shader* shader,
             physics::PhysicsWorld* physics_world)
    : window_(window), main_shader_(shader), physics_world_(physics_world) {
  shader->Enable();

  // All geometries are nodes of the same scene file, which is only loaded once
  // (it is ~20 MB of XML)
  util::ScenePtr track = util::LoadSceneFromFile(kTrackFile);
  if (!track) {
    util::FatalError(string("Could not load ") + kTrackFile);
  }
  const aiScene* track_scene = track.get();

  camera_ = make_unique<Camera>(vec3(0.0f, 1.0f, 2.0f), vec3(0.0f, 0.0f, -1.0f),
                                vec3(0.0f, 1.0f, 0.0f), *window);
  depth_shader_ = make_unique<Shader>("Shader/depth_shader.vert",
                                      "Shader/depth_shader.frag");
  light_shader_ = make_unique<Shader>("Shader/light_source_shader.vert",
                                      "Shader/light_source_shader.frag");

  // chequered flag (generated, no image file)
  textures_.push_back(Texture::CreateCheckerboard(256, 8, 0));
  Texture* checkerboard = textures_.back().get();
  textures_.push_back(make_unique<Texture>("Texture/wood16.jpg", 0));
  Texture* wood = textures_.back().get();
  textures_.push_back(make_unique<Texture>("Texture/wood16n.jpg", 2));
  crate_normal_map_ = textures_.back().get();

  geometries_.push_back(
      make_unique<Bear>(*window, track_scene, physics_world,
                        [this](const physics::CharacterController::Hit& hit) {
                          OnBearHit(hit);
                        }));
  bear_ = static_cast<Bear*>(geometries_[0].get());
  shader->Enable();  // the bear compiled its own shaders

  finish_line_ = AddGeometry(track_scene, "FinishLine", checkerboard);
  // A ring of stone in the forest
  Geometry* torus = AddGeometry(track_scene, "Torus", wood);
  torus->material_layer = world::kMaterialRock;
  torus->material_scale = 1.5f;
  crate_ = AddGeometry(track_scene, "Cube", wood, crate_normal_map_);

  for (int i = 0; i < 4; ++i) {
    arrows_.push_back(
        make_unique<Geometry>(mat4(1.0f), shader, *window, track_scene,
                              ("Pfeil" + to_string(i)).c_str(), checkerboard));
  }
  InitPhysicsBodies();
  InitFinishLine(finish_line_);

  // The camera stays out of the terrain and the trees
  camera_->SetCollisionProbe([this](vec3 from, vec3 to, float radius) {
    physics::RayHit hit;
    return this->physics_world_->CastSphere(from, to - from, radius, hit, true)
               ? hit.fraction
               : 1.0f;
  });

  InitWorld(*track_scene);
  InitAnimals();
  InitQuest();
  InitMishka();
  InitChonk();
  camera_->SnapBehind(CameraTarget());
  world_->FinishLoading(camera_->GetPosition(), bear_->GetPosition(),
                        kWorldLoadingTimeout);
  PlaceAnimals();
}

Scene::~Scene() = default;

void Scene::InitWorld(const aiScene& track) {
  cout << "Generating the island..." << endl;
  // The world must not cover the objects of the track
  vector<world::KeepClear> keep_clear;
  auto keep = [&](Geometry* geometry) {
    // Sphere around the vertices in world space (the model matrices of some
    // objects scale the axes differently)
    vector<vec3> points = WorldVertices(geometry);
    vec3 low(numeric_limits<float>::max()),
        high(numeric_limits<float>::lowest());
    for (const vec3& point : points) {
      low = glm::min(low, point);
      high = glm::max(high, point);
    }
    world::KeepClear object{0.5f * (low + high), 0.0f};
    for (const vec3& point : points) {
      object.radius = std::max(object.radius, distance(object.center, point));
    }
    if (!points.empty()) {
      keep_clear.push_back(object);
    }
  };
  for (const unique_ptr<Geometry>& geometry : geometries_) {
    if (geometry->GetModel()) {
      keep(geometry.get());  // the finish line, the crate and the stone ring
    }
  }
  for (const unique_ptr<Geometry>& arrow : arrows_) {
    keep(arrow.get());
  }
  // Where the rocks of the track are the hedgehogs live
  for (int i = 1; i <= kHedgehogCount; ++i) {
    world::KeepClear home;
    string name = "Rock" + to_string(i);
    if (!world::TrackTerrain::NodeSphere(track, name.c_str(), home.center,
                                         home.radius)) {
      util::FatalError("The track has no node " + name +
                       " (home of a hedgehog)");
    }
    keep_clear.push_back(home);
    hedgehog_homes_.push_back(vec2(home.center.x, home.center.z));
  }
  vector<glm::vec2> checkpoints(std::begin(rules::kCheckpoints),
                                std::end(rules::kCheckpoints));
  vec3 start = bear_->GetPosition();
  world_ = make_unique<world::World>(track, glm::vec2(start.x, start.z),
                                     glm::vec2(0.0f, -1.0f), checkpoints,
                                     keep_clear, physics_world_);
}

void Scene::InitAnimals() {
  creatures_ = make_unique<CreatureRenderer>();
  const world::WorldGenerator& generator = world_->GetGenerator();
  // The hedgehogs stay on the floor of the track and out of the water
  Hedgehog::Ground ground = Hedgehog::TrackFloor(generator);
  for (std::size_t i = 0; i < hedgehog_homes_.size(); ++i) {
    hedgehogs_.emplace_back(hedgehog_homes_[i],
                            Hedgehog::StartDirection(static_cast<int>(i)),
                            Hedgehog::SeedOf(static_cast<int>(i)), ground);
    hedgehog_bodies_.push_back(physics_world_->CreateKinematicSphere(
        hedgehogs_.back().GetColliderCenter(),
        hedgehogs_.back().GetColliderRadius()));
  }
  hedgehog_renderer_ = make_unique<HedgehogRenderer>(hedgehogs_.size());
  salmon_ = make_unique<SalmonPools>(FindWaterPools(generator), 4711u);
  cout << hedgehogs_.size() << " hedgehogs, " << salmon_->GetPools().size()
       << " pools with salmon" << endl;
}

void Scene::InitQuest() {
  race_quest_ = make_unique<RaceQuest>(finish_line_trigger_, kStartDirection);
  const vec3 behind = -kStartDirection;
  // The beaver sits at the side of the track next to the starting line and
  // looks across it, towards the bear coming from the start
  vec3 seat = GroundAt(finish_line_trigger_.center +
                       finish_line_trigger_.across * kBeaverSide +
                       behind * kBeaverBehindLine);
  beaver_ = make_unique<Beaver>(
      seat, normalize(finish_line_trigger_.across *
                          (kBeaverSide > 0.0f ? -1.0f : 1.0f) +
                      0.6f * behind));
  beaver_body_ = physics_world_->CreateKinematicSphere(
      beaver_->GetColliderCenter(), beaver_->GetColliderRadius());
  FurryRenderer::Strands strands{.fur_count = kBeaverFurCount, .fur_seed = 91};
  FurryRenderer::Bounds bounds{.center = vec3(0.0f, 0.5f, 0.0f),
                               .radius = 0.8f};
  beaver_renderer_ =
      make_unique<FurryRenderer>(BeaverModel::Build(), 1, strands, bounds);

  // The bear starts behind the line, the beaver and the line are in front of
  // it. In deep water it swims.
  BearCharacter& character = bear_->GetCharacter();
  character.SetWaterLevel(world_->GetGenerator().GetSeaLevel());
  vec3 spawn = GroundAt(bear_->GetPosition() + behind * kSpawnBehindLine +
                        finish_line_trigger_.across * kSpawnSide) +
               kUp * (BearCharacter::GroundOffset() + 0.05f);
  character.SetStart(spawn, kStartDirection);
  bear_->Reset();
}

void Scene::InitMishka() {
  const world::WorldGenerator& generator = world_->GetGenerator();
  mishka_pool_ = LargestPool(salmon_->GetPools());
  vec3 spot, look;
  if (mishka_pool_ < 0 ||
      !FindShoreSpot(generator, salmon_->GetPools()[mishka_pool_], spot,
                     look)) {
    util::FatalError(
        "There is no place for Mishka next to the pools of water on the track");
  }
  std::string error;
  mishka_model_ = resources::SkinnedModel::Load(Bear::kModelFile, error);
  if (!mishka_model_) {
    util::FatalError(std::string("Could not load ") + Bear::kModelFile + ": " +
                     error);
  }
  RestingBear::PaintCoat(*mishka_model_);
  // Its paws and fur lie on the height field (the physics of the terrain is
  // only streamed around the player's bear)
  GroundProbe height_field = [&generator](vec3 from, vec3 to, vec3& point,
                                          vec3& normal) {
    vec2 at(to.x, to.z);
    float height = generator.Height(at);
    if (height > std::max(from.y, to.y) || height < std::min(from.y, to.y)) {
      return false;
    }
    const float step = 0.3f;
    float dx = generator.Height(at + vec2(step, 0.0f)) -
               generator.Height(at - vec2(step, 0.0f));
    float dz = generator.Height(at + vec2(0.0f, step)) -
               generator.Height(at - vec2(0.0f, step));
    point = vec3(to.x, height, to.z);
    normal = normalize(vec3(-dx, 2.0f * step, -dz));
    return true;
  };
  mishka_ = make_unique<RestingBear>(*mishka_model_, spot, look, height_field);
  mishka_body_ = physics_world_->CreateKinematicSphere(
      mishka_->GetColliderCenter(), mishka_->GetColliderRadius());
  mishka_renderer_ =
      make_unique<BearRenderer>(*mishka_model_, RestingBear::kStrandCount);
  mishka_renderer_->SetMaxFurLength(fur::kMaxLength * RestingBear::kFurLength);
  salmon_quest_ =
      make_unique<SalmonQuest>(static_cast<int>(salmon_->GetPools().size()));
  cout << "Mishka lies at pool " << mishka_pool_ + 1 << " of "
       << salmon_->GetPools().size() << endl;
}

void Scene::InitChonk() {
  const world::WorldGenerator& generator = world_->GetGenerator();
  const world::Landmarks& landmarks = generator.GetLandmarks();
  const vector<world::PointOfInterest>& points =
      landmarks.GetPointsOfInterest();
  const vector<world::FootPath>& paths = landmarks.GetPaths();
  int stones = -1;
  for (std::size_t i = 0; i < points.size(); ++i) {
    stones = points[i].kind == world::PointOfInterest::Kind::kStandingStones
                 ? static_cast<int>(i)
                 : stones;
  }
  if (stones < 0) {
    util::FatalError(
        "The island has no circle of standing stones for Chonk's quest");
  }
  stones_center_ = points[stones].position;
  stones_radius_ = points[stones].radius;

  // The path to the stones, back along the paths it branches off from to the
  // one that leaves the track: Chonk sits at its trailhead, across the path
  // from the cairn, looking at the path
  int path = -1;
  for (std::size_t p = 0; p < paths.size(); ++p) {
    path = paths[p].point_of_interest == stones ? static_cast<int>(p) : path;
  }
  for (int step = 0; path >= 0 && !paths[path].from_track &&
                     step < static_cast<int>(paths.size());
       ++step) {
    vec2 start(paths[path].points.front().x, paths[path].points.front().z);
    int parent = -1;
    for (int p = 0; p < path && parent < 0; ++p) {
      for (const vec3& point : paths[p].points) {
        parent = distance(vec2(point.x, point.z), start) < 0.05f ? p : parent;
      }
    }
    path = parent;
  }
  if (path < 0 ||
      static_cast<std::size_t>(path) >= landmarks.GetMarkers().size()) {
    util::FatalError(
        "There is no foot path from the track to the standing stones for "
        "Chonk");
  }
  const vector<vec3>& line = paths[path].points;
  vec2 cairn(landmarks.GetMarkers()[path].x, landmarks.GetMarkers()[path].z);
  std::size_t marked = 0;
  for (std::size_t i = 0; i < line.size(); ++i) {
    marked = distance(vec2(line[i].x, line[i].z), cairn) <
                     distance(vec2(line[marked].x, line[marked].z), cairn)
                 ? i
                 : marked;
  }
  vec3 seat(0.0f), look(0.0f);
  bool found = false;
  const world::TrackPath& track = generator.GetPath();
  for (int offset = 0; offset < 12 && !found; ++offset) {
    std::size_t i =
        std::min(line.size() - 2, marked + static_cast<std::size_t>(offset));
    vec2 middle(line[i].x, line[i].z);
    vec2 along = normalize(vec2(line[i + 1].x, line[i + 1].z) - middle);
    vec2 across(-along.y, along.x);
    // On the other side than the cairn first
    float away = dot(cairn - middle, across) > 0.0f ? -1.0f : 1.0f;
    for (float side : {away, -away}) {
      vec2 at =
          middle + across * (side * (world::Landmarks::kPathHalfWidth + 2.4f));
      float height, east, north;
      world::TrackPath::Nearest nearest = track.FindNearest(at);
      bool beside =
          !nearest.found || std::abs(nearest.trail_lateral) >
                                world::TrackPath::kTrailHalfWidth + 2.0f;
      if (beside && RaccoonGround(at, height) &&
          RaccoonGround(at + vec2(0.8f, 0.0f), east) &&
          RaccoonGround(at + vec2(0.0f, 0.8f), north) &&
          std::abs(east - height) < 0.5f && std::abs(north - height) < 0.5f) {
        seat = vec3(at.x, height, at.y);
        // Looks across the path, a bit towards the track
        vec2 back(line[std::max<std::size_t>(i, 4) - 4].x - middle.x,
                  line[std::max<std::size_t>(i, 4) - 4].z - middle.y);
        vec2 facing =
            (middle - at) +
            0.5f * (length(back) > 1e-3f ? normalize(back) : vec2(0.0f)) *
                length(middle - at);
        look = vec3(facing.x, 0.0f, facing.y);
        found = true;
        break;
      }
    }
  }
  if (!found) {
    util::FatalError(
        "There is no place for Chonk at the trailhead of the paths");
  }
  Raccoon::Ground ground = [this](vec2 at, float& height) {
    return RaccoonGround(at, height);
  };
  chonk_ = make_unique<Raccoon>(ground, 2024u, kChonkGirth);
  chonk_->SitAt(seat, look);
  chonk_body_ = physics_world_->CreateKinematicSphere(
      chonk_->GetColliderCenter(), chonk_->GetColliderRadius());
  FurryRenderer::Strands strands{.fur_count = kChonkFurCount, .fur_seed = 133};
  FurryRenderer::Bounds bounds{.center = vec3(0.0f, 0.35f, 0.0f),
                               .radius = 0.8f};
  chonk_renderer_ = make_unique<FurryRenderer>(RaccoonModel::Build(kChonkGirth),
                                               1, strands, bounds);
  stones_quest_ = make_unique<StonesQuest>();

  fans_ = make_unique<RaccoonFans>(
      ground, [this](vec3 bear, vec3 heading, std::mt19937& random, vec2& from,
                     vec2& spot) {
        return FindFanSpot(bear, heading, random, from, spot);
      });
  strands.fur_count = kFanFurCount;
  strands.fur_seed = 177;
  fans_renderer_ = make_unique<FurryRenderer>(
      RaccoonModel::Build(), RaccoonFans::kCount, strands, bounds);
  cout << "Chonk sits at the trailhead (" << seat.x << ", " << seat.z
       << "), the standing stones are at (" << stones_center_.x << ", "
       << stones_center_.z << ")" << endl;
}

bool Scene::RaccoonGround(vec2 position, float& height) const {
  const world::WorldGenerator& generator = world_->GetGenerator();
  height = generator.Height(position);
  return height > generator.GetSeaLevel() + 0.3f &&
         generator.CaveRoofTop(position) < -1000.0f;
}

bool Scene::FindFanSpot(vec3 bear, vec3 heading, std::mt19937& random,
                        vec2& from, vec2& spot) {
  const world::WorldGenerator& generator = world_->GetGenerator();
  std::uniform_real_distribution<float> unit(0.0f, 1.0f);
  vec2 forward = normalize(vec2(heading.x, heading.z));
  vec2 right(-forward.y, forward.x);
  ViewFrustum frustum = camera_->GetViewFrustum();
  // Of the spots that work the one closest to the height of the bear (it sees
  // it there, not only up on the rim of a canyon)
  float best = numeric_limits<float>::max();
  for (int attempt = 0; attempt < 16; ++attempt) {
    float side = unit(random) < 0.5f ? -1.0f : 1.0f;
    vec2 out = right * side;
    vec2 at = vec2(bear.x, bear.z) +
              forward * glm::mix(kFanAheadMin, kFanAheadMax, unit(random)) +
              out * glm::mix(kFanSideMin, kFanSideMax, unit(random));
    // On level ground (not on the side of the canyon)
    float height, east, north;
    if (!RaccoonGround(at, height) ||
        !RaccoonGround(at + vec2(0.7f, 0.0f), east) ||
        !RaccoonGround(at + vec2(0.0f, 0.7f), north) ||
        std::abs(east - height) > 0.4f || std::abs(north - height) > 0.4f) {
      continue;
    }
    // Not on the trail of the race (the raccoons have no collision, the bear
    // would run through them)
    world::TrackPath::Nearest nearest = generator.GetPath().FindNearest(at);
    if (nearest.found && std::abs(nearest.trail_lateral) <
                             world::TrackPath::kTrailHalfWidth + 1.5f) {
      continue;
    }
    float turn = (unit(random) - 0.5f) * 0.8f;
    vec2 woods = out * std::cos(turn) + forward * std::sin(turn);
    vec2 start =
        at + woods * glm::mix(kFanWoodsMin, kFanWoodsMax, unit(random));
    float start_height;
    if (!RaccoonGround(start, start_height)) {
      continue;
    }
    // It can walk all the way (on land, not too steep)
    bool walkable = true;
    float last = start_height;
    float steps = std::ceil(distance(start, at));
    for (float k = 1.0f; k <= steps && walkable; k += 1.0f) {
      float h;
      walkable = RaccoonGround(glm::mix(start, at, k / steps), h) &&
                 std::abs(h - last) < 0.9f;
      last = h;
    }
    if (!walkable) {
      continue;
    }
    // Nothing in the way (trees, rocks): a hit is in the way when it is clearly
    // above the ground
    vec3 high(start.x, start_height + 0.9f, start.y),
        low(at.x, height + 0.9f, at.y);
    physics::RayHit hit;
    if (physics_world_->CastSphere(high, low - high, 0.3f, hit, true) &&
        hit.position.y >
            generator.Height(vec2(hit.position.x, hit.position.z)) + 0.45f) {
      continue;
    }
    // It comes out of the woods where the camera does not see it appear (out of
    // the view, or behind something)
    vec3 eye = camera_->GetPosition();
    bool unseen =
        frustum.SphereInFrustum(high, 1.2f) == ViewFrustum::kOutside ||
        (physics_world_->CastRay(eye, high - eye, hit, true) &&
         hit.fraction < 0.97f);
    if (!unseen) {
      continue;
    }
    float score =
        std::abs(height - bear.y) + 0.1f * distance(at, vec2(bear.x, bear.z));
    if (score < best) {
      best = score;
      from = start;
      spot = at;
    }
  }
  return best < numeric_limits<float>::max();
}

void Scene::UpdateTalkPartner() {
  // In a conversation: the one it talks to
  if (race_quest_->GetStage() == RaceQuest::Stage::kTalking) {
    talk_partner_ = TalkPartner::kBeaver;
    return;
  }
  if (salmon_quest_->GetStage() == SalmonQuest::Stage::kTalking) {
    talk_partner_ = TalkPartner::kMishka;
    return;
  }
  if (stones_quest_->GetStage() == StonesQuest::Stage::kTalking ||
      stones_quest_->IsInspecting()) {
    talk_partner_ = stones_quest_->IsInspecting() ? TalkPartner::kStones
                                                  : TalkPartner::kChonk;
    return;
  }
  // Otherwise the closest one it could talk to
  struct Candidate {
    TalkPartner partner;
    bool possible;
    vec3 position;
  };
  const Candidate candidates[] = {
      {TalkPartner::kBeaver, race_quest_->CanTalk(), beaver_->GetPosition()},
      {TalkPartner::kMishka, salmon_quest_->CanTalk(), mishka_->GetPosition()},
      {TalkPartner::kChonk, stones_quest_->CanTalk(), chonk_->GetPosition()},
      {TalkPartner::kStones, stones_quest_->CanInspect(), stones_center_},
  };
  talk_partner_ = TalkPartner::kNone;
  float closest = numeric_limits<float>::max();
  vec3 here = bear_->GetPosition();
  for (const Candidate& candidate : candidates) {
    float away = length(
        vec2(candidate.position.x - here.x, candidate.position.z - here.z));
    if (candidate.possible && away < closest) {
      closest = away;
      talk_partner_ = candidate.partner;
    }
  }
}

vec3 Scene::GroundAt(vec3 position) const {
  return vec3(position.x,
              world_->GetGenerator().Height(vec2(position.x, position.z)),
              position.z);
}

bool Scene::IsBearInWater() {
  return bear_->GetCharacter().GetWaterDepth() > kInWaterDepth;
}

bool Scene::CanTalkAt(vec3 position, float distance) {
  // It can talk when it stands (not while it tumbles, swims or eats)
  const BearCharacter& character = bear_->GetCharacter();
  vec3 offset = position - bear_->GetPosition();
  return length(vec2(offset.x, offset.z)) < distance &&
         character.GetState() == BearCharacter::State::kControl &&
         !character.IsSwimming() &&
         character.GetFish() == BearCharacter::Fish::kNone;
}

RaceQuest::Situation Scene::QuestSituation(const input::PlayerInput& player) {
  RaceQuest::Situation situation;
  situation.bear = bear_->GetPosition();
  situation.near_beaver =
      CanTalkAt(beaver_->GetPosition(), RaceQuest::kTalkDistance);
  situation.busy = salmon_quest_->IsActive() || stones_quest_->IsActive();
  situation.swimming = IsBearInWater();
  situation.on_track =
      RaceQuest::IsOnTrack(world_->GetGenerator().GetPath(), situation.bear);
  situation.at_start = at_start_;
  situation.interact = player.interact && talk_partner_ == TalkPartner::kBeaver;
  situation.dialog.confirm = player.confirm;
  situation.dialog.navigate = player.navigate;
  situation.dialog.cancel = player.cancel;
  return situation;
}

SalmonQuest::Situation Scene::SalmonSituation(
    const input::PlayerInput& player) {
  SalmonQuest::Situation situation;
  situation.near_mishka =
      CanTalkAt(mishka_->GetPosition(), SalmonQuest::kTalkDistance);
  situation.busy = race_quest_->GetStage() != RaceQuest::Stage::kIdle ||
                   stones_quest_->IsActive();
  situation.eaten_pool = eaten_pool_;
  eaten_pool_ = -1;
  situation.interact = player.interact && talk_partner_ == TalkPartner::kMishka;
  situation.dialog.confirm = player.confirm;
  situation.dialog.navigate = player.navigate;
  situation.dialog.cancel = player.cancel;
  return situation;
}

StonesQuest::Situation Scene::StonesSituation(
    const input::PlayerInput& player) {
  StonesQuest::Situation situation;
  situation.near_chonk =
      CanTalkAt(chonk_->GetPosition(), StonesQuest::kTalkDistance);
  vec3 offset = stones_center_ - bear_->GetPosition();
  situation.at_stones = length(vec2(offset.x, offset.z)) < stones_radius_;
  situation.near_stone =
      CanTalkAt(stones_center_, StonesQuest::kInspectDistance);
  situation.busy = race_quest_->GetStage() != RaceQuest::Stage::kIdle ||
                   salmon_quest_->IsActive();
  situation.interact =
      player.interact && (talk_partner_ == TalkPartner::kChonk ||
                          talk_partner_ == TalkPartner::kStones);
  situation.dialog.confirm = player.confirm;
  situation.dialog.navigate = player.navigate;
  situation.dialog.cancel = player.cancel;
  return situation;
}

bool Scene::TurnTowards(vec3 direction, BearInput& input) {
  vec3 facing = bear_->GetDirection();
  direction.y = 0.0f;
  if (length(direction) < 1e-4f) {
    return true;
  }
  direction = normalize(direction);
  // Positive: the direction is to the left
  float angle = std::atan2(cross(facing, direction).y, dot(facing, direction));
  if (std::abs(angle) < 0.08f) {
    return true;
  }
  input.turn =
      std::copysign(std::clamp(std::abs(angle) * 2.0f, 0.35f, 1.0f), angle);
  return false;
}

BearInput Scene::MakeBearInput(const input::PlayerInput& player) {
  BearInput input;
  RaceQuest::Stage stage = race_quest_->GetStage();
  if (stage != RaceQuest::Stage::kWalkingToStart) {
    at_start_ = false;
  }
  if (IsPlayerInControl()) {
    input.move = input::CameraRelative(player.move, camera_->GetForward(),
                                       camera_->GetRight());
    // The button that answered the beaver must be released before the bear
    // jumps
    jump_blocked_ = jump_blocked_ && player.jump;
    input.jump = player.jump && !jump_blocked_;
    return input;
  }
  jump_blocked_ = true;
  if (talk_partner_ != TalkPartner::kNone &&
      !(stage == RaceQuest::Stage::kWalkingToStart ||
        stage == RaceQuest::Stage::kCountdown)) {
    // It turns to the animal it talks to (or the flat stone), steps back a
    // little if it came too close and walks up to it if it is too far away, so
    // that they talk face to face
    vec3 partner = bear_->GetPosition();
    float spacing = kTalkSpacing;
    switch (talk_partner_) {
      case TalkPartner::kBeaver:
        partner = beaver_->GetPosition();
        break;
      case TalkPartner::kMishka:
        partner = mishka_->GetPosition();
        break;
      case TalkPartner::kChonk:
        partner = chonk_->GetPosition();
        spacing = kChonkSpacing;
        break;
      case TalkPartner::kStones:
        partner = stones_center_;
        spacing = kStoneSpacing;
        break;
      case TalkPartner::kNone:
        break;
    }
    vec3 to_partner = partner - bear_->GetPosition();
    float away = length(vec2(to_partner.x, to_partner.z));
    bool facing = TurnTowards(to_partner, input);
    if (facing && away < spacing - 0.2f) {
      input.forward = -0.35f;
    } else if (facing && away > spacing + 0.6f) {
      input.forward = std::clamp(0.3f * (away - spacing), 0.15f, 0.35f);
    }
  } else if (stage == RaceQuest::Stage::kWalkingToStart) {
    // Walks up to the starting line and turns along the track
    vec3 to_start = race_quest_->GetStartPosition() - bear_->GetPosition();
    to_start.y = 0.0f;
    float distance = length(to_start);
    if (distance > 0.35f) {
      input.move =
          to_start / distance *
          glm::mix(0.15f, 0.35f, std::clamp(distance / 2.0f, 0.0f, 1.0f));
    } else {
      at_start_ = TurnTowards(race_quest_->GetStartDirection(), input);
    }
  }
  return input;  // the countdown: it stands still
}

void Scene::UpdateCamera(float time_delta) {
  RaceQuest::Stage stage = race_quest_->GetStage();
  bool talking = stage == RaceQuest::Stage::kTalking ||
                 salmon_quest_->GetStage() == SalmonQuest::Stage::kTalking ||
                 stones_quest_->GetStage() == StonesQuest::Stage::kTalking ||
                 stones_quest_->IsInspecting();
  if (camera_->IsLocked() && talking) {
    // Both from the side, a bit behind the bear (the animal it talks to faces
    // the camera), from the side with more room
    vec3 bear_head = bear_->GetPosition() + kUp * 0.4f;
    vec3 partner_head = beaver_->GetHeadPosition();
    switch (talk_partner_) {
      case TalkPartner::kMishka:
        partner_head = mishka_->GetFacePosition();
        break;
      case TalkPartner::kChonk:
        partner_head = chonk_->GetFacePosition();
        break;
      case TalkPartner::kStones:
        partner_head = stones_center_ + kUp * 0.8f;
        break;
      default:
        break;
    }
    vec3 across = partner_head - bear_head;
    across.y = 0.0f;
    across = length(across) > 1e-3f ? normalize(across) : kStartDirection;
    vec3 middle = 0.5f * (bear_head + partner_head);
    vec3 best = middle;
    float best_room = -1.0f;
    for (float side : {1.0f, -1.0f}) {
      vec3 eye = middle + cross(across, kUp) * (6.5f * side) - across * 2.0f +
                 kUp * 1.4f;
      physics::RayHit hit;
      float room =
          physics_world_->CastSphere(middle, eye - middle,
                                     Camera::kCollisionRadius, hit, true)
              ? hit.fraction
              : 1.0f;
      if (room > best_room + 0.05f) {
        best = eye;
        best_room = room;
      }
    }
    camera_->Shot(best, middle + kUp * 0.1f, time_delta);
  } else if (camera_->IsLocked() &&
             stage == RaceQuest::Stage::kWalkingToStart) {
    // From beyond the starting line: the bear walks up to it, the beaver cheers
    // next to it
    vec3 start = GroundAt(race_quest_->GetStartPosition());
    vec3 forward = race_quest_->GetStartDirection();
    vec3 right = cross(forward, kUp);
    camera_->Shot(start + forward * 6.5f - right * 1.5f + kUp * 2.4f,
                  bear_->GetPosition() + kUp * 0.2f, time_delta);
  } else if (camera_->IsLocked()) {
    camera_->Follow(CameraTarget(), time_delta);
  }
  camera_->Update(time_delta);
}

void Scene::Update(float time_delta, const input::PlayerInput& player) {
  physics_world_->Update(time_delta);
  // The camera turns first, the bear moves relative to where the camera looks
  // then (not in a conversation or a cutscene)
  if (IsPlayerInControl() || !camera_->IsLocked()) {
    camera_->Rotate(player.look.x, player.look.y);
    camera_->Zoom(player.zoom);
    if (player.recenter) {
      camera_->Recenter();
    }
  }

  marker_time_ += time_delta;
  // The button starts a conversation with the closest animal only
  UpdateTalkPartner();

  // The race quest: the conversation with the beaver, the cutscene, the
  // countdown and the race
  race_quest_->Update(time_delta, QuestSituation(player));
  for (RaceQuest::Event event : race_quest_->TakeEvents()) {
    if (event == RaceQuest::Event::kBeaverSound) {
      beaver_->Speak();
    } else if (event == RaceQuest::Event::kAccepted ||
               event == RaceQuest::Event::kCompleted) {
      beaver_->Cheer();
    }
    // The raccoons beside the track cheer at every checkpoint and at the finish
    if (event == RaceQuest::Event::kCheckpoint ||
        event == RaceQuest::Event::kCompleted) {
      fans_->Celebrate();
    }
    if (event == RaceQuest::Event::kCompleted) {
      cheer_linger_ = kCheerLinger;
    }
    events_.push_back(event);
  }
  RaceQuest::Stage stage = race_quest_->GetStage();
  if (stage != last_stage_ && stage == RaceQuest::Stage::kCountdown) {
    // The race starts at the line, looking along the track (also if the bear
    // did not get there in the cutscene, e.g. it was knocked over)
    vec3 start = race_quest_->GetStartPosition();
    vec3 here = bear_->GetPosition();
    if (!at_start_ || length(vec2(start.x - here.x, start.z - here.z)) > 1.0f) {
      world_->EnsureCollision(start, camera_->GetPosition());
      bear_->GetCharacter().PlaceAt(
          GroundAt(start) + kUp * (BearCharacter::GroundOffset() + 0.05f),
          race_quest_->GetStartDirection());
    }
    camera_->SnapBehind(CameraTarget());
  }
  last_stage_ = stage;
  beaver_->SetTalking(race_quest_->GetDialog().IsOpen());
  // During the race Mishka and Chonk are no obstacles (they are beside the
  // route, but a racer may cut close past them)
  bool racing = stage != RaceQuest::Stage::kIdle;
  for (auto [body, animal] :
       {std::pair<physics::BodyHandle*, vec4>(
            &mishka_body_,
            vec4(mishka_->GetColliderCenter(), mishka_->GetColliderRadius())),
        std::pair<physics::BodyHandle*, vec4>(
            &chonk_body_,
            vec4(chonk_->GetColliderCenter(), chonk_->GetColliderRadius()))}) {
    if (racing && *body != physics::kInvalidBody) {
      physics_world_->RemoveBody(*body);
      *body = physics::kInvalidBody;
    } else if (!racing && *body == physics::kInvalidBody) {
      *body = physics_world_->CreateKinematicSphere(vec3(animal), animal.w);
    }
  }

  // Fishing for Mishka: the conversation and the salmon the bear eats up
  salmon_quest_->Update(time_delta, SalmonSituation(player));
  for (SalmonQuest::Event event : salmon_quest_->TakeEvents()) {
    if (event == SalmonQuest::Event::kBearSound) {
      mishka_->Speak();
    }
    // The raccoons cheer for every salmon of the quest
    if (event == SalmonQuest::Event::kEaten ||
        event == SalmonQuest::Event::kCompleted) {
      fans_->Celebrate();
    }
    if (event == SalmonQuest::Event::kCompleted) {
      cheer_linger_ = kCheerLinger;
    }
    salmon_events_.push_back(event);
  }

  // The standing stones for Chonk: the conversations, the stone circle and the
  // report
  stones_quest_->Update(time_delta, StonesSituation(player));
  for (StonesQuest::Event event : stones_quest_->TakeEvents()) {
    if (event == StonesQuest::Event::kChonkSound) {
      chonk_->Speak();
      ++raccoon_chatters_;
    } else if (event == StonesQuest::Event::kAccepted ||
               event == StonesQuest::Event::kCompleted) {
      chonk_->Cheer();
    }
    stones_events_.push_back(event);
  }
  const Dialog& stones_dialog = stones_quest_->GetDialog();
  chonk_->SetTalking(stones_dialog.IsOpen() &&
                     stones_dialog.GetSpeaker() == StonesQuest::kSpeaker);

  // The bear looks at the animal it talks to (and the one it would talk to),
  // the animals look at the bear when it is close
  switch (talk_partner_) {
    case TalkPartner::kBeaver:
      bear_->GetCharacter().LookAt(beaver_->GetFacePosition());
      break;
    case TalkPartner::kMishka:
      bear_->GetCharacter().LookAt(mishka_->GetFacePosition());
      break;
    case TalkPartner::kChonk:
      bear_->GetCharacter().LookAt(chonk_->GetFacePosition());
      break;
    case TalkPartner::kStones:
      bear_->GetCharacter().LookAt(stones_center_ + kUp * 0.3f);
      break;
    case TalkPartner::kNone:
      bear_->GetCharacter().StopLooking();
      break;
  }
  bear_->SetInput(MakeBearInput(player));

  for (const unique_ptr<Geometry>& geometry : geometries_) {
    geometry->Update(time_delta);
  }
  UpdateAnimals(time_delta);
  // The bear splashes into the water
  int new_splashes = bear_->GetCharacter().TakeSplashes();
  if (new_splashes > 0) {
    salmon_->Splash(bear_->GetPosition(), bear_->GetCharacter().GetWaterLevel(),
                    26);
    splashes_ += new_splashes;
  }
  UpdateCamera(time_delta);

  // The world streams around the camera, the physics around the bear. The bear
  // and the animals push the plants aside.
  vector<world::FoliageInteraction::Body> bodies;
  bodies.push_back({bear_->GetCharacter().GetPosition(), kBearFoliageRadius});
  bodies.push_back(
      {beaver_->GetColliderCenter(), beaver_->GetColliderRadius()});
  bodies.push_back(
      {mishka_->GetColliderCenter(), mishka_->GetColliderRadius()});
  bodies.push_back({chonk_->GetColliderCenter(), chonk_->GetColliderRadius()});
  for (const Raccoon& fan : fans_->GetRaccoons()) {
    if (fan.IsVisible()) {
      bodies.push_back(
          {fan.GetColliderCenter(), 0.8f * fan.GetColliderRadius()});
    }
  }
  for (const Hedgehog& hedgehog : hedgehogs_) {
    bodies.push_back(
        {hedgehog.GetColliderCenter(), hedgehog.GetColliderRadius()});
  }
  world_->Update(time_delta, camera_->GetPosition(), bear_->GetPosition(),
                 bodies);
  PlaceAnimals();
}

vector<RaceQuest::Event> Scene::TakeEvents() {
  vector<RaceQuest::Event> result;
  result.swap(events_);
  return result;
}

vector<SalmonQuest::Event> Scene::TakeSalmonEvents() {
  vector<SalmonQuest::Event> result;
  result.swap(salmon_events_);
  return result;
}

vector<StonesQuest::Event> Scene::TakeStonesEvents() {
  vector<StonesQuest::Event> result;
  result.swap(stones_events_);
  return result;
}

int Scene::TakeRaccoonChatters() {
  int result = raccoon_chatters_;
  raccoon_chatters_ = 0;
  return result;
}

int Scene::TakeSplashes() {
  int result = splashes_;
  splashes_ = 0;
  return result;
}

void Scene::UpdateAnimals(float time_delta) {
  // The hedgehogs stroll around, their collision spheres follow them (in the
  // next update of the physics) They scurry away when the bear comes close
  vector<fur::SphereCollider> obstacles;
  for (std::size_t i = 0; i < hedgehogs_.size(); ++i) {
    hedgehogs_[i].Watch(bear_->GetPosition());
    hedgehogs_[i].Update(time_delta);
    physics_world_->MoveKinematic(hedgehog_bodies_[i],
                                  hedgehogs_[i].GetColliderCenter(),
                                  quat(1.0f, 0.0f, 0.0f, 0.0f), time_delta);
    obstacles.push_back({hedgehogs_[i].GetColliderCenter(),
                         hedgehogs_[i].GetColliderRadius() + kSpineLength});
  }
  bear_->SetObstacles(obstacles);

  // The beaver at the start turns to the bear and looks at its face
  beaver_->Update(time_delta, bear_->GetCharacter().GetHeadPosition());
  physics_world_->MoveKinematic(beaver_body_, beaver_->GetColliderCenter(),
                                quat(1.0f, 0.0f, 0.0f, 0.0f), time_delta);

  // Chonk sits at the trailhead. On the race and while fishing raccoons come
  // out of the woods to cheer (and for a while after a quest was completed).
  vec3 bear_head = bear_->GetCharacter().GetHeadPosition();
  chonk_->Update(time_delta, bear_head);
  raccoon_chatters_ += chonk_->TakeChatters();
  if (chonk_body_ != physics::kInvalidBody) {
    physics_world_->MoveKinematic(chonk_body_, chonk_->GetColliderCenter(),
                                  quat(1.0f, 0.0f, 0.0f, 0.0f), time_delta);
  }
  cheer_linger_ = std::max(0.0f, cheer_linger_ - time_delta);
  bool cheering = race_quest_->GetStage() == RaceQuest::Stage::kRacing ||
                  salmon_quest_->GetStage() == SalmonQuest::Stage::kFishing ||
                  cheer_linger_ > 0.0f;
  vec3 heading = bear_->GetCharacter().GetVelocity();
  heading.y = 0.0f;
  heading = length(heading) > 1.0f ? normalize(heading) : bear_->GetDirection();
  fans_->Update(time_delta, cheering, bear_->GetPosition(), bear_head, heading);
  raccoon_chatters_ += fans_->TakeChatters();

  // Salmon jump out of the water when the bear comes close and it catches them.
  // Not during the race (the bear has no time to eat), on the salmon quest not
  // out of the pools that are done.
  BearCharacter& character = bear_->GetCharacter();
  bool racing = race_quest_->GetStage() != RaceQuest::Stage::kIdle;
  vector<bool> closed = racing ? vector<bool>(salmon_->GetPools().size(), true)
                        : salmon_quest_->IsActive()
                            ? salmon_quest_->GetDonePools()
                            : vector<bool>();
  int pool = salmon_->Update(time_delta, character.GetBoundsCenter(),
                             character.GetMouthPosition(),
                             character.CanCatchFish() && !racing, closed);
  if (pool >= 0 && character.CatchFish()) {
    carried_pool_ = pool;
  }
  // It counts for the quest when the bear ate it up (not when it dropped it)
  if (character.TakeEatenFish() > 0 && carried_pool_ >= 0) {
    eaten_pool_ = carried_pool_;
    carried_pool_ = -1;
  }
  if (character.GetFish() == BearCharacter::Fish::kNone) {
    carried_pool_ = -1;
  }

  // Mishka watches the salmon of its pool, and the bear when it comes close
  vec3 camera_position = camera_->GetPosition();
  bool talking_to_mishka =
      salmon_quest_->GetStage() == SalmonQuest::Stage::kTalking;
  if (talking_to_mishka || distance(camera_position, mishka_->GetPosition()) <
                               kMishkaUpdateDistance) {
    vec3 bear_head = character.GetHeadPosition();
    const vec3* watch = nullptr;
    vec3 salmon_position;
    if (talking_to_mishka || distance(bear_head, mishka_->GetFacePosition()) <
                                 kMishkaAttentionDistance) {
      watch = &bear_head;
    } else {
      float highest = -numeric_limits<float>::max();
      for (const SalmonPools::Jump& jump : salmon_->GetJumps()) {
        if (jump.pool == mishka_pool_ && jump.Position().y > highest) {
          highest = jump.Position().y;
          salmon_position = jump.Position();
          watch = &salmon_position;
        }
      }
    }
    mishka_->Update(time_delta, watch);
  }
}

void Scene::PlaceSalmon(vec3 center, const quat& rotation, int bites,
                        float wiggle) {
  const float scale = SalmonPools::kScale;
  if (bites < 1) {
    creatures_->Add(CreatureModel::kSalmonHead, center, rotation, scale);
  }
  if (bites < 2) {
    creatures_->Add(CreatureModel::kSalmonBody, center, rotation, scale);
  }
  if (bites < 3) {
    vec3 joint = center + rotation * vec3(0.0f, 0.0f, kSalmonTailJoint * scale);
    creatures_->Add(CreatureModel::kSalmonTail, joint,
                    rotation * angleAxis(wiggle, vec3(0.0f, 1.0f, 0.0f)),
                    scale);
  }
}

void Scene::PlaceAnimals() {
  creatures_->Clear();
  for (std::size_t i = 0; i < hedgehogs_.size(); ++i) {
    Hedgehog::Pose pose = hedgehogs_[i].GetPose();
    mat4 scale = glm::scale(mat4(1.0f), vec3(Hedgehog::kScale));
    mat4 body = glm::translate(mat4(1.0f), pose.body_position) *
                glm::mat4_cast(pose.body_rotation) * scale;
    std::array<mat4, 4> feet;
    for (std::size_t foot = 0; foot < feet.size(); ++foot) {
      feet[foot] = glm::translate(mat4(1.0f), pose.feet[foot]) *
                   glm::mat4_cast(pose.foot_rotation) * scale;
    }
    hedgehog_renderer_->SetPose(i, body, feet, pose.bristle);
  }
  for (const SalmonPools::Jump& jump : salmon_->GetJumps()) {
    PlaceSalmon(jump.Position(), jump.Rotation(), 0,
                0.5f * std::sin(18.0f * jump.time + jump.wiggle));
  }
  BearCharacter::HeldFish held;
  if (bear_->GetCharacter().GetHeldFish(held)) {
    PlaceSalmon(held.center, held.rotation, held.bites, held.wiggle);
  }
  for (const SalmonPools::Droplet& droplet : salmon_->GetDroplets()) {
    creatures_->Add(CreatureModel::kDroplet, droplet.position,
                    quat(1.0f, 0.0f, 0.0f, 0.0f), droplet.size);
  }
  beaver_renderer_->SetPose(0, beaver_->GetPose());
  // The beaver offers the race: a golden exclamation mark above it, a blue one
  // once the race was completed (Mishka the same)
  if (race_quest_->IsOffered()) {
    CreatureModel marker = race_quest_->HasCompleted()
                               ? CreatureModel::kQuestMarkerRepeatable
                               : CreatureModel::kQuestMarker;
    creatures_->Add(marker, beaver_->GetMarkerPosition(),
                    beaver_->GetMarkerRotation(), kMarkerScale);
  }
  if (salmon_quest_->IsOffered()) {
    CreatureModel marker = salmon_quest_->HasCompleted()
                               ? CreatureModel::kQuestMarkerRepeatable
                               : CreatureModel::kQuestMarker;
    creatures_->Add(marker, mishka_->GetMarkerPosition(),
                    mishka_->GetMarkerRotation(), kMarkerScale);
  }

  // Chonk offers its quest and waits for the report (the mark above it), a
  // large mark shows the way to the stone circle
  // The vertices of the raccoons are moved on the CPU: Chonk only while the
  // camera is close enough to see it move, a hidden fan once when it goes back
  // into the woods
  if (!chonk_posed_ || distance(camera_->GetPosition(), chonk_->GetPosition()) <
                           kMishkaUpdateDistance) {
    chonk_renderer_->SetPose(0, chonk_->GetPose());
    chonk_posed_ = true;
  }
  if (stones_quest_->IsOffered() || stones_quest_->HasReport()) {
    creatures_->Add(CreatureModel::kQuestMarker, chonk_->GetMarkerPosition(),
                    chonk_->GetMarkerRotation(), kMarkerScale);
  }
  if (stones_quest_->GetStage() == StonesQuest::Stage::kExploring) {
    float bob = 0.15f * std::sin(2.0f * marker_time_);
    creatures_->Add(CreatureModel::kQuestMarker,
                    stones_center_ + kUp * (kGoalMarkerHeight + bob),
                    chonk_->GetMarkerRotation(), kGoalMarkerScale);
  }
  const vector<Raccoon>& raccoons = fans_->GetRaccoons();
  fans_hidden_.resize(raccoons.size(), false);
  for (std::size_t i = 0; i < raccoons.size(); ++i) {
    if (raccoons[i].IsVisible()) {
      fans_renderer_->SetPose(i, raccoons[i].GetPose());
    } else if (!fans_hidden_[i]) {
      fans_renderer_->SetPose(
          i, vector<mat4>(RaccoonModel::kPartCount,
                          glm::translate(mat4(1.0f), kHiddenPlace) *
                              glm::scale(mat4(1.0f), vec3(Raccoon::kScale))));
    }
    fans_hidden_[i] = !raccoons[i].IsVisible();
  }
}

Camera::FollowTarget Scene::CameraTarget() {
  Camera::FollowTarget target;
  target.position = bear_->GetPosition();
  target.direction = bear_->GetDirection();
  target.velocity = bear_->GetCharacter().GetVelocity();
  target.airborne = bear_->GetCharacter().IsAirborne();
  return target;
}

void Scene::Restart() {
  bear_->Reset();
  bear_->SetInput(BearInput());
  camera_->SnapBehind(CameraTarget());
  // The ground at the start may have been streamed out of the physics world
  world_->EnsureCollision(bear_->GetPosition(), camera_->GetPosition());
  for (const DynamicBody& dynamic_body : dynamic_bodies_) {
    physics_world_->ResetBody(dynamic_body.body, dynamic_body.position,
                              dynamic_body.rotation);
  }
  for (std::size_t i = 0; i < hedgehogs_.size(); ++i) {
    hedgehogs_[i].Reset();
    physics_world_->MoveKinematic(hedgehog_bodies_[i],
                                  hedgehogs_[i].GetColliderCenter(),
                                  quat(1.0f, 0.0f, 0.0f, 0.0f), 0.0f);
  }
  salmon_->Reset();
  carried_pool_ = eaten_pool_ = -1;
  salmon_quest_->Abort();
  salmon_events_.clear();
  race_quest_->Abort();
  last_stage_ = race_quest_->GetStage();
  at_start_ = false;
  jump_blocked_ = false;
  events_.clear();
  splashes_ = 0;
  beaver_->Reset();
  physics_world_->MoveKinematic(beaver_body_, beaver_->GetColliderCenter(),
                                quat(1.0f, 0.0f, 0.0f, 0.0f), 0.0f);
  stones_quest_->Abort();
  stones_events_.clear();
  chonk_->Reset();
  fans_->Reset();
  cheer_linger_ = 0.0f;
  raccoon_chatters_ = 0;
  talk_partner_ = TalkPartner::kNone;
  PlaceAnimals();
}

render::CameraView Scene::CameraView() {
  render::CameraView view;
  view.view = camera_->GetViewMatrix();
  view.projection = camera_->GetProjectionMatrix();
  view.position = camera_->GetPosition();
  view.field_of_view = camera_->GetFieldOfView();
  view.aspect_ratio = camera_->GetAspectRatio();
  view.near_plane = camera_->GetNearPlane();
  return view;
}

void Scene::FillFrameUniforms(render::FrameUniforms& uniforms) {
  world_->FillFrameUniforms(uniforms);
}

void Scene::PrepareFrame() { world_->PrepareFrame(); }

void Scene::UploadMishka() {
  if (mishka_uploaded_ != mishka_->GetVersion()) {
    mishka_renderer_->Upload(mishka_->GetPositions(), mishka_->GetNormals(),
                             mishka_->GetFurOffsets(),
                             mishka_->GetCombDirections());
    mishka_uploaded_ = mishka_->GetVersion();
  }
}

void Scene::DrawShadowCasters(const render::ShadowCascade& cascade) {
  world_->DrawShadows(cascade, camera_->GetPosition());
  creatures_->DrawShadow(cascade);
  depth_shader_->Enable();
  glUniformMatrix4fv(depth_shader_->GetUniformLocation("lightSpaceMatrix"), 1,
                     GL_FALSE, value_ptr(cascade.light_space));
  hedgehog_renderer_->DrawSurfaces(depth_shader_.get());
  beaver_renderer_->DrawSurfaces(depth_shader_.get());
  chonk_renderer_->DrawSurfaces(depth_shader_.get());
  if (std::ranges::any_of(fans_->GetRaccoons(), &Raccoon::IsVisible)) {
    fans_renderer_->DrawSurfaces(depth_shader_.get());
  }
  if (cascade.Contains(mishka_->GetBoundsCenter(),
                       mishka_->GetBoundsRadius())) {
    UploadMishka();
    mishka_renderer_->DrawSurface(depth_shader_.get());
  }
  for (const unique_ptr<Geometry>& geometry : geometries_) {
    if (geometry.get() == bear_) {
      bear_->Draw(depth_shader_.get());
      continue;
    }
    Model* model = geometry->GetModel();
    float scale = std::max({length(vec3(geometry->model_matrix[0])),
                            length(vec3(geometry->model_matrix[1])),
                            length(vec3(geometry->model_matrix[2]))});
    if (cascade.Contains(vec3(geometry->model_matrix *
                              vec4(model->GetBoundingSphereCenter(), 1.0f)),
                         model->GetBoundingSphereRadius() * scale)) {
      geometry->Draw(depth_shader_.get());
    }
  }
}

void Scene::DrawMain(const render::RenderSettings& settings) {
  normal_mapping_enabled_ = settings.normal_mapping;
  mat4 view = camera_->GetViewMatrix();
  mat4 projection = camera_->GetProjectionMatrix();
  ViewFrustum frustum = camera_->GetViewFrustum();
  vec3 eye = camera_->GetPosition();

  // 1. Opaque: the terrain and the vegetation, the salmon (shaded like the
  // vegetation), the objects of the track and the signs (they glow)
  world_->DrawOpaque(frustum, eye, view_frustum_culling_enabled_);
  creatures_->Draw();
  main_shader_->Enable();
  drawn_geometry_count_ =
      DrawGeometries(main_shader_, view, projection,
                     view_frustum_culling_enabled_ ? &frustum : nullptr, false);
  light_shader_->Enable();
  glUniformMatrix4fv(light_shader_->GetUniformLocation("view"), 1, GL_FALSE,
                     value_ptr(view));
  glUniformMatrix4fv(light_shader_->GetUniformLocation("projection"), 1,
                     GL_FALSE, value_ptr(projection));
  for (const unique_ptr<Geometry>& arrow : arrows_) {
    arrow->Draw(light_shader_.get());
  }

  // 2. The sky behind everything (not in wireframe mode)
  if (!settings.wireframe) {
    world_->DrawSky();
  }

  // 3. The bears, the hedgehogs and the beaver: their fur and spines are
  // blended with what is behind them
  BearRenderContext context;
  context.view = view;
  context.projection = projection;
  context.eye = eye;
  if (!view_frustum_culling_enabled_ || bear_->IsVisible(frustum)) {
    bear_->DrawShaded(context);
    ++drawn_geometry_count_;
  }
  hedgehog_renderer_->DrawShaded(context, frustum);
  beaver_renderer_->DrawShaded(context, frustum);
  chonk_renderer_->DrawShaded(context, frustum);
  fans_renderer_->DrawShaded(context, frustum);
  if (!view_frustum_culling_enabled_ ||
      frustum.SphereInFrustum(mishka_->GetBoundsCenter(),
                              mishka_->GetBoundsRadius()) !=
          ViewFrustum::kOutside) {
    UploadMishka();
    mishka_renderer_->DrawShaded(context, mishka_->GetBoundsCenter());
  }

  // 4. Transparent: the water, the falling snow
  world_->DrawTransparent(settings.transparency);
  glActiveTexture(GL_TEXTURE0);
}

void Scene::RenderFromCamera(Shader* shader) {
  ViewFrustum frustum = camera_->GetViewFrustum();
  shader->Enable();
  drawn_geometry_count_ = DrawGeometries(
      shader, camera_->GetViewMatrix(), camera_->GetProjectionMatrix(),
      view_frustum_culling_enabled_ ? &frustum : nullptr, true);
}

int Scene::DrawGeometries(Shader* shader, const mat4& view,
                          const mat4& projection, const ViewFrustum* frustum,
                          bool with_bear) {
  GLint view_location = shader->GetUniformLocation("view");
  GLint projection_location = shader->GetUniformLocation("projection");
  GLint normal_mapping_enabled_location =
      shader->GetUniformLocation("normal_mapping_enabled_");
  GLint texture_transparent_location =
      shader->GetUniformLocation("textureTransparent");
  if (view_location >= 0 && projection_location >= 0) {
    glUniformMatrix4fv(view_location, 1, GL_FALSE, value_ptr(view));
    glUniformMatrix4fv(projection_location, 1, GL_FALSE, value_ptr(projection));
  }
  if (texture_transparent_location >= 0) {
    glUniform1i(texture_transparent_location, false);
  }

  int drawn_geometries = 0;
  for (const unique_ptr<Geometry>& geometry : geometries_) {
    if (geometry.get() == bear_) {
      if (with_bear && (!frustum || bear_->IsVisible(*frustum))) {
        bear_->Draw(shader);
        ++drawn_geometries;
      }
    } else if (DrawGeometry(shader, geometry.get(),
                            normal_mapping_enabled_location, frustum)) {
      ++drawn_geometries;
    }
  }

  if (normal_mapping_enabled_location >= 0) {
    glUniform1i(normal_mapping_enabled_location, normal_mapping_enabled_);
  }
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  return drawn_geometries;
}

bool Scene::DrawGeometry(Shader* shader, Geometry* geometry,
                         GLint normal_mapping_enabled_location,
                         const ViewFrustum* frustum) {
  if (frustum && !geometry->IsVisible(*frustum)) {
    return false;
  }
  if (normal_mapping_enabled_location >= 0) {
    glUniform1i(normal_mapping_enabled_location,
                normal_mapping_enabled_ && geometry->GetHasNormalMap());
  }
  geometry->Draw(shader);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  return true;
}

void Scene::SetViewportSize(int width, int height) {
  camera_->SetViewportSize(width, height);
}

void Scene::SetViewFrustumCullingEnabled(bool view_frustum_culling_enabled) {
  this->view_frustum_culling_enabled_ = view_frustum_culling_enabled;
}

bool Scene::IsViewFrustumCullingEnabled() {
  return view_frustum_culling_enabled_;
}

int Scene::GetDrawnGeometryCount() { return drawn_geometry_count_; }

int Scene::GetGeometryCount() { return static_cast<int>(geometries_.size()); }

void Scene::InitPhysicsBodies() {
  // Ground under the sea (a large box whose top face is at y = -5), far out
  // where the sea floor of the world ends
  physics_world_->CreateStaticBox(vec3(0.0f, -6.0f, 0.0f),
                                  vec3(1000.0f, 1.0f, 1000.0f));

  // There is no wall at the start: the finish line is a trigger (see
  // InitFinishLine) and the checkpoints keep the bear from going backwards (the
  // bear starts a bit behind the line, see InitQuest)

  // The crate as it is drawn: a box around its mesh, turned like its model
  vector<vec3> crate_vertices = ModelVertices(crate_);
  vec3 low(numeric_limits<float>::max()), high(numeric_limits<float>::lowest());
  for (const vec3& vertex : crate_vertices) {
    low = glm::min(low, vertex);
    high = glm::max(high, vertex);
  }
  const mat4& crate_matrix = crate_->model_matrix;
  vec3 scale(length(vec3(crate_matrix[0])), length(vec3(crate_matrix[1])),
             length(vec3(crate_matrix[2])));
  quat crate_rotation = normalize(quat_cast(
      mat3(vec3(crate_matrix[0]) / scale.x, vec3(crate_matrix[1]) / scale.y,
           vec3(crate_matrix[2]) / scale.z)));
  vec3 crate_center = vec3(crate_matrix * vec4(0.5f * (low + high), 1.0f));
  physics::BodyHandle box = physics_world_->CreateDynamicBox(
      crate_center, 0.5f * (high - low) * scale, 1.0f, crate_rotation);
  crate_->SetPhysicsBody(physics_world_, box);
  dynamic_bodies_.push_back({box, crate_center, crate_rotation});
  // The terrain and the trees are added by the world (streamed around the
  // bear), the hedgehogs by InitAnimals
}

void Scene::InitFinishLine(Geometry* board) {
  Mesh* board_mesh = board->GetModel()->meshes[0];
  float half_length = 0.0f;
  for (unsigned int i = 0; i < board_mesh->num_vertices; ++i) {
    half_length = std::max(half_length, std::abs(board_mesh->vertices[i * 3]));
  }
  finish_line_trigger_ =
      rules::MakeFinishLine(board->model_matrix, half_length);
}

/**
 * Bear collision callback: running into a hedgehog knocks the bear down (ouch,
 * the spines), it tumbles and gets up again (the time is lost)
 **/
void Scene::OnBearHit(const physics::CharacterController::Hit& hit) {
  auto found = find(hedgehog_bodies_.begin(), hedgehog_bodies_.end(), hit.body);
  if (found != hedgehog_bodies_.end()) {
    bool was_down = bear_->IsKnockedDown();
    bear_->KnockDown(hit);
    if (!was_down) {
      hedgehogs_[static_cast<std::size_t>(found - hedgehog_bodies_.begin())]
          .Startle();
    }
  }
}

bool Scene::IsBearKnockedDown() { return bear_->IsKnockedDown(); }

}  // namespace bibo::scene
