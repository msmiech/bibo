#include "Actors.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include "../Util/Console.hpp"
#include "../World/Landmarks.hpp"
#include "../World/World.hpp"
#include "HedgehogRenderer.hpp"

namespace bibo::scene {

using glm::quat;
using glm::vec2;
using glm::vec3;
using std::cout;
using std::endl;
using std::make_unique;
using std::numeric_limits;
using std::vector;

namespace {
const quat kNoRotation(1.0f, 0.0f, 0.0f, 0.0f);
// The fur of the bear lies on the spines of a hedgehog when it runs into it
const float kSpineLength = 0.8f * HedgehogRenderer::kSpineLength;
// Mishka looks at the bear when it comes this close, otherwise at the salmon of
// its pool
const float kMishkaAttentionDistance = 12.0f;
// The bear pushes the plants aside within this distance of the center of its
// capsule (about down to its paws)
const float kBearFoliageRadius = 1.3f;
// The raccoons go on cheering this long after a quest was completed
const float kCheerLinger = 4.5f;
// Where the raccoons that cheer stand: ahead of the bear and beside its way (in
// the canyon of the track also up on its rim); they come from this far further
// out
const float kFanAheadMin = 8.0f, kFanAheadMax = 24.0f, kFanSideMin = 4.5f,
            kFanSideMax = 16.0f, kFanWoodsMin = 9.0f, kFanWoodsMax = 14.0f;
}  // namespace

Actors::Actors(std::unique_ptr<Bear> bear, const Homes& homes,
               const world::World& world, physics::PhysicsWorld& physics,
               Camera& camera)
    : world_(world),
      physics_(physics),
      camera_(camera),
      bear_(std::move(bear)) {
  InitHedgehogs(homes);
  // The beaver sits next to the starting line
  beaver_ = make_unique<Beaver>(homes.beaver, homes.beaver_direction);
  Add(*beaver_, Actor());
  InitMishka();
  InitRaccoons();
}

Actors::~Actors() = default;

void Actors::Add(Animal& animal, Actor actor) {
  actor.animal = &animal;
  if (actor.solid) {
    actor.body = physics_.CreateKinematicSphere(animal.GetColliderCenter(),
                                                animal.GetColliderRadius());
  }
  actors_.push_back(actor);
}

void Actors::InitHedgehogs(const Homes& homes) {
  const world::WorldGenerator& generator = world_.GetGenerator();
  // The hedgehogs stay on the floor of the track and out of the water
  Hedgehog::Ground ground = Hedgehog::TrackFloor(generator);
  for (std::size_t i = 0; i < homes.hedgehogs.size(); ++i) {
    hedgehogs_.emplace_back(homes.hedgehogs[i],
                            Hedgehog::StartDirection(static_cast<int>(i)),
                            Hedgehog::SeedOf(static_cast<int>(i)), ground);
  }
  // Running into one knocks the bear over (ouch, the spines)
  Actor spiny;
  spiny.knocks_down = true;
  for (Hedgehog& hedgehog : hedgehogs_) {
    Add(hedgehog, spiny);
  }
  salmon_ = make_unique<SalmonPools>(FindWaterPools(generator), 4711u);
  cout << hedgehogs_.size() << " hedgehogs, " << salmon_->GetPools().size()
       << " pools with salmon" << endl;
}

void Actors::InitMishka() {
  const world::WorldGenerator& generator = world_.GetGenerator();
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
  Actor mishka;
  mishka.solid_while_racing = false;
  Add(*mishka_, mishka);
  cout << "Mishka lies at pool " << mishka_pool_ + 1 << " of "
       << salmon_->GetPools().size() << endl;
}

void Actors::InitRaccoons() {
  const world::WorldGenerator& generator = world_.GetGenerator();
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
  stones_ = StoneCircle(points[stones].position, points[stones].radius);

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
  Actor chonk;
  chonk.solid_while_racing = false;
  Add(*chonk_, chonk);

  fans_ = make_unique<RaccoonFans>(
      ground, [this](vec3 bear, vec3 heading, std::mt19937& random, vec2& from,
                     vec2& spot) {
        return FindFanSpot(bear, heading, random, from, spot);
      });
  // They have no collision (they stand beside the way of the bear), they push
  // the plants aside a little
  Actor fan;
  fan.solid = false;
  fan.foliage_scale = 0.8f;
  for (Raccoon& raccoon : fans_->GetRaccoons()) {
    Add(raccoon, fan);
  }
  vec3 center = stones_.GetPosition();
  cout << "Chonk sits at the trailhead (" << seat.x << ", " << seat.z
       << "), the standing stones are at (" << center.x << ", " << center.z
       << ")" << endl;
}

bool Actors::RaccoonGround(vec2 position, float& height) const {
  const world::WorldGenerator& generator = world_.GetGenerator();
  height = generator.Height(position);
  return height > generator.GetSeaLevel() + 0.3f &&
         generator.CaveRoofTop(position) < -1000.0f;
}

bool Actors::FindFanSpot(vec3 bear, vec3 heading, std::mt19937& random,
                         vec2& from, vec2& spot) {
  const world::WorldGenerator& generator = world_.GetGenerator();
  std::uniform_real_distribution<float> unit(0.0f, 1.0f);
  vec2 forward = normalize(vec2(heading.x, heading.z));
  vec2 right(-forward.y, forward.x);
  ViewFrustum frustum = camera_.GetViewFrustum();
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
    if (physics_.CastSphere(high, low - high, 0.3f, hit, true) &&
        hit.position.y >
            generator.Height(vec2(hit.position.x, hit.position.z)) + 0.45f) {
      continue;
    }
    // It comes out of the woods where the camera does not see it appear (out of
    // the view, or behind something)
    vec3 eye = camera_.GetPosition();
    bool unseen =
        frustum.SphereInFrustum(high, 1.2f) == ViewFrustum::kOutside ||
        (physics_.CastRay(eye, high - eye, hit, true) && hit.fraction < 0.97f);
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

void Actors::Update(float time_delta, const Situation& situation,
                    EventQueue& events) {
  bear_->Update(time_delta);
  BearCharacter& character = bear_->GetCharacter();
  vec3 bear = bear_->GetPosition();
  vec3 bear_head = character.GetHeadPosition();

  // The hedgehogs stroll around, they scurry away when the bear comes close.
  // The fur of the bear lies on their spines.
  vector<fur::SphereCollider> obstacles;
  for (Hedgehog& hedgehog : hedgehogs_) {
    hedgehog.Watch(bear);
    hedgehog.Update(time_delta);
  }
  for (const Actor& actor : actors_) {
    if (actor.knocks_down) {
      obstacles.push_back({actor.animal->GetColliderCenter(),
                           actor.animal->GetColliderRadius() + kSpineLength});
    }
  }
  bear_->SetObstacles(obstacles);

  // The beaver at the start turns to the bear and looks at its face, so does
  // Chonk at the trailhead
  beaver_->Update(time_delta, bear_head);
  chonk_->Update(time_delta, bear_head);
  for (int i = chonk_->TakeChatters(); i > 0; --i) {
    events.Push(AnimalEvent::kRaccoonChatter);
  }

  // On the race and while fishing raccoons come out of the woods to cheer (and
  // for a while after a quest was completed)
  if (situation.celebrate) {
    fans_->Celebrate();
  }
  if (situation.completed) {
    cheer_linger_ = kCheerLinger;
  }
  cheer_linger_ = std::max(0.0f, cheer_linger_ - time_delta);
  vec3 heading = character.GetVelocity();
  heading.y = 0.0f;
  heading = length(heading) > 1.0f ? normalize(heading) : bear_->GetDirection();
  fans_->Update(time_delta, situation.cheered_on || cheer_linger_ > 0.0f, bear,
                bear_head, heading);
  for (int i = fans_->TakeChatters(); i > 0; --i) {
    events.Push(AnimalEvent::kRaccoonChatter);
  }

  // Salmon jump out of the water when the bear comes close and it catches them
  // (not during the race: the bear has no time to eat)
  int pool = salmon_->Update(
      time_delta, character.GetBoundsCenter(), character.GetMouthPosition(),
      character.CanCatchFish() && !situation.racing, situation.closed_pools);
  if (pool >= 0 && character.CatchFish()) {
    carried_pool_ = pool;
  }
  if (character.TakeEatenFish() > 0 && carried_pool_ >= 0) {
    eaten_pool_ = carried_pool_;
    carried_pool_ = -1;
  }
  if (character.GetFish() == BearCharacter::Fish::kNone) {
    carried_pool_ = -1;
  }

  UpdateMishka(time_delta, situation.talking_to == mishka_.get());
  stones_.Update(time_delta);
  UpdateBodies(time_delta, situation.racing);

  // The bear splashes into the water
  int splashes = character.TakeSplashes();
  if (splashes > 0) {
    salmon_->Splash(bear_->GetPosition(), character.GetWaterLevel(), 26);
  }
  for (; splashes > 0; --splashes) {
    events.Push(AnimalEvent::kSplash);
  }
}

void Actors::UpdateMishka(float time_delta, bool talking) {
  if (!talking && distance(camera_.GetPosition(), mishka_->GetPosition()) >=
                      kAnimateDistance) {
    return;
  }
  vec3 bear_head = bear_->GetCharacter().GetHeadPosition();
  const vec3* watch = nullptr;
  vec3 salmon_position;
  if (talking || distance(bear_head, mishka_->GetFacePosition()) <
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

void Actors::UpdateBodies(float time_delta, bool racing) {
  for (Actor& actor : actors_) {
    vec3 center = actor.animal->GetColliderCenter();
    bool solid = actor.solid && (!racing || actor.solid_while_racing);
    if (!solid && actor.body != physics::kInvalidBody) {
      physics_.RemoveBody(actor.body);
      actor.body = physics::kInvalidBody;
    } else if (solid && actor.body == physics::kInvalidBody) {
      actor.body = physics_.CreateKinematicSphere(
          center, actor.animal->GetColliderRadius());
    } else if (solid) {
      // In the next update of the physics
      physics_.MoveKinematic(actor.body, center, kNoRotation, time_delta);
    }
  }
}

void Actors::Restart() {
  bear_->Reset();
  bear_->SetInput(BearInput());
  for (Actor& actor : actors_) {
    actor.animal->Reset();
    if (actor.body != physics::kInvalidBody) {
      physics_.MoveKinematic(actor.body, actor.animal->GetColliderCenter(),
                             kNoRotation, 0.0f);
    }
  }
  // The raccoons that cheer come out of the woods as at the start
  fans_->Reset();
  salmon_->Reset();
  carried_pool_ = eaten_pool_ = -1;
  cheer_linger_ = 0.0f;
}

void Actors::OnBearHit(const physics::CharacterController::Hit& hit) {
  for (Actor& actor : actors_) {
    if (actor.knocks_down && actor.body == hit.body) {
      bool was_down = bear_->IsKnockedDown();
      bear_->KnockDown(hit);
      if (!was_down) {
        actor.animal->Startle();
      }
      return;
    }
  }
}

vector<world::FoliageInteraction::Body> Actors::GetFoliageBodies() const {
  vector<world::FoliageInteraction::Body> bodies;
  bodies.push_back({bear_->GetCharacter().GetPosition(), kBearFoliageRadius});
  for (const Actor& actor : actors_) {
    // A hidden animal (a raccoon in the woods) keeps its place in the list,
    // the others stay the same bodies for the plants
    if (actor.foliage_scale > 0.0f) {
      bodies.push_back({actor.animal->GetColliderCenter(),
                        actor.foliage_scale * actor.animal->GetColliderRadius(),
                        actor.animal->IsVisible()});
    }
  }
  return bodies;
}

int Actors::TakeEatenPool() {
  int pool = eaten_pool_;
  eaten_pool_ = -1;
  return pool;
}

}  // namespace bibo::scene
