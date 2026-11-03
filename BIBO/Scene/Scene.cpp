#include "Scene.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include "../Util/Console.hpp"
#include "../World/MaterialTextures.hpp"
#include "../World/TrackTerrain.hpp"

namespace bibo::scene {

using glm::mat3;
using glm::mat4;
using glm::quat;
using glm::vec2;
using glm::vec3;
using glm::vec4;
using resources::Mesh;
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
const vec3 kUp(0.0f, 1.0f, 0.0f);
// The race goes along -z at the start (the bear looks there, Bear::Bear)
const vec3 kStartDirection(0.0f, 0.0f, -1.0f);
// The bear starts behind the starting line (left of the crate), the beaver sits
// next to the line at the left side of the track
const float kSpawnBehindLine = 7.0f, kSpawnSide = -0.8f;
const float kBeaverSide = -5.0f, kBeaverBehindLine = 1.5f;
// In a conversation the bear keeps this distance to the animal (center to
// center, so that their faces do not touch). It talks to Chonk from a bit
// closer (it is smaller), it inspects the flat stone from closer still.
const float kTalkSpacing = 3.6f, kChonkSpacing = 3.2f, kStoneSpacing = 2.4f;
// The race is lost when the bear is this deep in the water (its paws)
const float kInWaterDepth = 0.35f;

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
  geometries_.push_back(make_unique<Geometry>(mat4(1.0f), main_shader_, track,
                                              node_name, texture, normal_map));
  return geometries_.back().get();
}

Scene::Scene(GLFWwindow* window, Shader* shader,
             physics::PhysicsWorld* physics_world)
    : main_shader_(shader), physics_world_(physics_world) {
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

  // chequered flag (generated, no image file)
  textures_.push_back(Texture::CreateCheckerboard(256, 8, 0));
  Texture* checkerboard = textures_.back().get();
  textures_.push_back(make_unique<Texture>("Texture/wood16.jpg", 0));
  Texture* wood = textures_.back().get();
  textures_.push_back(make_unique<Texture>("Texture/wood16n.jpg", 2));
  Texture* crate_normal_map = textures_.back().get();

  // The bear (Actors) is knocked down when it runs into a hedgehog
  auto bear =
      make_unique<Bear>(track_scene, physics_world,
                        [this](const physics::CharacterController::Hit& hit) {
                          actors_->OnBearHit(hit);
                        });

  finish_line_ = AddGeometry(track_scene, "FinishLine", checkerboard);
  // A ring of stone in the forest
  Geometry* torus = AddGeometry(track_scene, "Torus", wood);
  torus->material_layer = world::kMaterialRock;
  torus->material_scale = 1.5f;
  crate_ = AddGeometry(track_scene, "Cube", wood, crate_normal_map);

  for (int i = 0; i < 4; ++i) {
    arrows_.push_back(make_unique<Geometry>(mat4(1.0f), shader, track_scene,
                                            ("Pfeil" + to_string(i)).c_str(),
                                            checkerboard));
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

  vector<vec2> hedgehog_homes = InitWorld(*track_scene, *bear);
  Actors::Homes homes = InitStart(*bear);
  homes.hedgehogs = hedgehog_homes;
  actors_ = make_unique<Actors>(std::move(bear), homes, *world_,
                                *physics_world_, *camera_);
  InitQuests();
  renderer_ = make_unique<SceneRenderer>(main_shader_, *camera_, *world_,
                                         *actors_, geometries_, arrows_);
  camera_->SnapBehind(CameraTarget());
  world_->FinishLoading(camera_->GetPosition(), GetBear().GetPosition(),
                        kWorldLoadingTimeout);
  renderer_->Place(*quests_);
}

Scene::~Scene() = default;

vector<vec2> Scene::InitWorld(const aiScene& track, const Bear& bear) {
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
  // the finish line, the crate and the stone ring
  for (const unique_ptr<Geometry>& geometry : geometries_) {
    keep(geometry.get());
  }
  for (const unique_ptr<Geometry>& arrow : arrows_) {
    keep(arrow.get());
  }
  // Where the rocks of the track are the hedgehogs live
  vector<vec2> hedgehog_homes;
  for (int i = 1; i <= kHedgehogCount; ++i) {
    world::KeepClear home;
    string name = "Rock" + to_string(i);
    if (!world::TrackTerrain::NodeSphere(track, name.c_str(), home.center,
                                         home.radius)) {
      util::FatalError("The track has no node " + name +
                       " (home of a hedgehog)");
    }
    keep_clear.push_back(home);
    hedgehog_homes.push_back(vec2(home.center.x, home.center.z));
  }
  vector<glm::vec2> checkpoints(std::begin(rules::kCheckpoints),
                                std::end(rules::kCheckpoints));
  vec3 start = bear.GetPosition();
  world_ = make_unique<world::World>(track, glm::vec2(start.x, start.z),
                                     glm::vec2(0.0f, -1.0f), checkpoints,
                                     keep_clear, physics_world_);
  return hedgehog_homes;
}

Actors::Homes Scene::InitStart(Bear& bear) {
  Actors::Homes homes;
  const vec3 behind = -kStartDirection;
  // The beaver sits at the side of the track next to the starting line and
  // looks across it, towards the bear coming from the start
  homes.beaver = GroundAt(finish_line_trigger_.center +
                          finish_line_trigger_.across * kBeaverSide +
                          behind * kBeaverBehindLine);
  homes.beaver_direction = normalize(finish_line_trigger_.across *
                                         (kBeaverSide > 0.0f ? -1.0f : 1.0f) +
                                     0.6f * behind);

  // The bear starts behind the line, the beaver and the line are in front of
  // it. In deep water it swims.
  BearCharacter& character = bear.GetCharacter();
  character.SetWaterLevel(world_->GetGenerator().GetSeaLevel());
  vec3 spawn = GroundAt(bear.GetPosition() + behind * kSpawnBehindLine +
                        finish_line_trigger_.across * kSpawnSide) +
               kUp * (BearCharacter::GroundOffset() + 0.05f);
  character.SetStart(spawn, kStartDirection);
  bear.Reset();
  return homes;
}

void Scene::InitQuests() {
  quests_ = make_unique<QuestDirector>([this](vec3 position, float distance) {
    return CanTalkAt(position, distance);
  });
  Actors& actors = *actors_;
  // The race around the track: Bober, the beaver at the start, offers it
  race_quest_ = &quests_->Add(
      make_unique<RaceQuest>(finish_line_trigger_, kStartDirection),
      {{&actors.GetBeaver(), RaceQuest::kTalkDistance, kTalkSpacing}},
      [this](RaceQuest::Situation& situation, const vector<bool>& near) {
        situation.bear = GetBear().GetPosition();
        situation.near_beaver = near[0];
        situation.swimming = IsBearInWater();
        situation.on_track = RaceQuest::IsOnTrack(
            world_->GetGenerator().GetPath(), situation.bear);
        situation.at_start = at_start_;
      });
  // Fishing in all pools for Mishka, who lies at the largest one
  salmon_quest_ = &quests_->Add(
      make_unique<SalmonQuest>(
          static_cast<int>(actors.GetSalmon().GetPools().size())),
      {{&actors.GetMishka(), SalmonQuest::kTalkDistance, kTalkSpacing}},
      [this](SalmonQuest::Situation& situation, const vector<bool>& near) {
        situation.near_mishka = near[0];
        situation.eaten_pool = actors_->TakeEatenPool();
      });
  // The standing stones for Chonk, the raccoon at the trailhead of the paths
  stones_quest_ = &quests_->Add(
      make_unique<StonesQuest>(),
      {{&actors.GetChonk(), StonesQuest::kTalkDistance, kChonkSpacing},
       {&actors.GetStoneCircle(), StonesQuest::kInspectDistance,
        kStoneSpacing}},
      [this](StonesQuest::Situation& situation, const vector<bool>& near) {
        situation.near_chonk = near[StonesQuest::kChonk];
        situation.near_stone = near[StonesQuest::kFlatStone];
        situation.at_stones =
            actors_->GetStoneCircle().Contains(GetBear().GetPosition());
      });
}

const TalkPartner* Scene::GetTalkPartner() const {
  const QuestDirector::Partner* partner = quests_->GetTalkPartner();
  return partner ? partner->who : nullptr;
}

vec3 Scene::GroundAt(vec3 position) const {
  return vec3(position.x,
              world_->GetGenerator().Height(vec2(position.x, position.z)),
              position.z);
}

bool Scene::IsBearInWater() const {
  return GetBear().GetCharacter().GetWaterDepth() > kInWaterDepth;
}

bool Scene::CanTalkAt(vec3 position, float distance) const {
  // It can talk when it stands (not while it tumbles, swims or eats)
  const BearCharacter& character = GetBear().GetCharacter();
  vec3 offset = position - GetBear().GetPosition();
  return length(vec2(offset.x, offset.z)) < distance &&
         character.GetState() == BearCharacter::State::kControl &&
         !character.IsSwimming() &&
         character.GetFish() == BearCharacter::Fish::kNone;
}

bool Scene::TurnTowards(vec3 direction, BearInput& input) {
  vec3 facing = GetBear().GetDirection();
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
  if (quests_->IsPlayerInControl()) {
    input.move = input::CameraRelative(player.move, camera_->GetForward(),
                                       camera_->GetRight());
    // The button that answered the beaver must be released before the bear
    // jumps
    jump_blocked_ = jump_blocked_ && player.jump;
    input.jump = player.jump && !jump_blocked_;
    return input;
  }
  jump_blocked_ = true;
  const QuestDirector::Partner* partner = quests_->GetTalkPartner();
  if (partner != nullptr && !(stage == RaceQuest::Stage::kWalkingToStart ||
                              stage == RaceQuest::Stage::kCountdown)) {
    // It turns to the animal it talks to (or the flat stone), steps back a
    // little if it came too close and walks up to it if it is too far away, so
    // that they talk face to face
    vec3 to_partner = partner->who->GetPosition() - GetBear().GetPosition();
    float away = length(vec2(to_partner.x, to_partner.z));
    bool facing = TurnTowards(to_partner, input);
    if (facing && away < partner->spacing - 0.2f) {
      input.forward = -0.35f;
    } else if (facing && away > partner->spacing + 0.6f) {
      input.forward =
          std::clamp(0.3f * (away - partner->spacing), 0.15f, 0.35f);
    }
  } else if (stage == RaceQuest::Stage::kWalkingToStart) {
    // Walks up to the starting line and turns along the track
    vec3 to_start = race_quest_->GetStartPosition() - GetBear().GetPosition();
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

void Scene::PlaceAtStart() {
  vec3 start = race_quest_->GetStartPosition();
  vec3 here = GetBear().GetPosition();
  if (!at_start_ || length(vec2(start.x - here.x, start.z - here.z)) > 1.0f) {
    world_->EnsureCollision(start, camera_->GetPosition());
    GetBear().GetCharacter().PlaceAt(
        GroundAt(start) + kUp * (BearCharacter::GroundOffset() + 0.05f),
        race_quest_->GetStartDirection());
  }
  camera_->SnapBehind(CameraTarget());
}

void Scene::UpdateCamera(float time_delta) {
  RaceQuest::Stage stage = race_quest_->GetStage();
  const QuestDirector::Partner* partner = quests_->GetTalkPartner();
  if (camera_->IsLocked() && quests_->IsInConversation() && partner) {
    // Both from the side, a bit behind the bear (the animal it talks to faces
    // the camera), from the side with more room
    vec3 bear_head = GetBear().GetPosition() + kUp * 0.4f;
    vec3 partner_head = partner->who->GetHeadPosition();
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
                  GetBear().GetPosition() + kUp * 0.2f, time_delta);
  } else if (camera_->IsLocked()) {
    camera_->Follow(CameraTarget(), time_delta);
  }
  camera_->Update(time_delta);
}

void Scene::Update(float time_delta, const input::PlayerInput& player) {
  physics_world_->Update(time_delta);
  // The camera turns first, the bear moves relative to where the camera looks
  // then (not in a conversation or a cutscene)
  if (quests_->IsPlayerInControl() || !camera_->IsLocked()) {
    camera_->Rotate(player.look.x, player.look.y);
    camera_->Zoom(player.zoom);
    if (player.recenter) {
      camera_->Recenter();
    }
  }

  // The quests: the conversations, the cutscene and the countdown of the race,
  // the race, fishing and the standing stones
  Bear& bear = GetBear();
  QuestDirector::News news =
      quests_->Update(time_delta, bear.GetPosition(), player, events_);
  RaceQuest::Stage stage = race_quest_->GetStage();
  if (stage != last_stage_ && stage == RaceQuest::Stage::kCountdown) {
    PlaceAtStart();
  }
  last_stage_ = stage;

  // The bear looks at the one it talks to (and the one it would talk to), the
  // animals look at the bear when it is close
  const TalkPartner* partner = GetTalkPartner();
  if (partner) {
    bear.GetCharacter().LookAt(partner->GetFacePosition());
  } else {
    bear.GetCharacter().StopLooking();
  }
  bear.SetInput(MakeBearInput(player));

  // The bear and the animals: no salmon jump during the race (no time for a
  // snack), on the salmon quest not out of the pools that are done
  Actors::Situation situation;
  situation.racing = race_quest_->IsActive();
  situation.cheered_on = quests_->IsCheeredOn();
  situation.celebrate = news.celebrate;
  situation.completed = news.completed;
  std::size_t pools = actors_->GetSalmon().GetPools().size();
  situation.closed_pools = situation.racing ? vector<bool>(pools, true)
                           : salmon_quest_->IsActive()
                               ? salmon_quest_->GetDonePools()
                               : vector<bool>();
  const QuestDirector::Partner* talking = quests_->GetConversationPartner();
  situation.talking_to = talking ? talking->who : nullptr;
  actors_->Update(time_delta, situation, events_);
  for (const unique_ptr<Geometry>& geometry : geometries_) {
    geometry->Update();
  }
  UpdateCamera(time_delta);

  // The world streams around the camera, the physics around the bear. The bear
  // and the animals push the plants aside.
  world_->Update(time_delta, camera_->GetPosition(), bear.GetPosition(),
                 actors_->GetFoliageBodies());
  renderer_->Place(*quests_);
}

Camera::FollowTarget Scene::CameraTarget() const {
  const BearCharacter& character = GetBear().GetCharacter();
  Camera::FollowTarget target;
  target.position = GetBear().GetPosition();
  target.direction = GetBear().GetDirection();
  target.velocity = character.GetVelocity();
  target.airborne = character.IsAirborne();
  return target;
}

void Scene::Restart() {
  actors_->Restart();
  camera_->SnapBehind(CameraTarget());
  // The ground at the start may have been streamed out of the physics world
  world_->EnsureCollision(GetBear().GetPosition(), camera_->GetPosition());
  for (const DynamicBody& dynamic_body : dynamic_bodies_) {
    physics_world_->ResetBody(dynamic_body.body, dynamic_body.position,
                              dynamic_body.rotation);
  }
  quests_->Abort();
  events_.Clear();
  last_stage_ = race_quest_->GetStage();
  at_start_ = false;
  jump_blocked_ = false;
  renderer_->Place(*quests_);
}

void Scene::SetViewportSize(int width, int height) {
  camera_->SetViewportSize(width, height);
}

void Scene::InitPhysicsBodies() {
  // Ground under the sea (a large box whose top face is at y = -5), far out
  // where the sea floor of the world ends
  physics_world_->CreateStaticBox(vec3(0.0f, -6.0f, 0.0f),
                                  vec3(1000.0f, 1.0f, 1000.0f));

  // There is no wall at the start: the finish line is a trigger (see
  // InitFinishLine) and the checkpoints keep the bear from going backwards (the
  // bear starts a bit behind the line, see InitStart)

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
  // bear), the animals by Actors
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

}  // namespace bibo::scene
