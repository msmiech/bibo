#include "Vegetation.hpp"

#include <algorithm>
#include <cmath>

#include "Chunk.hpp"
#include "Noise.hpp"

namespace bibo::world {

namespace {
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);

// Height and radius of the models at scale 1 (VegetationMeshes builds them this
// size)
const struct {
  float height, radius;
} kModelSizes[kModelCount] = {{15.0f, 3.6f},   // kSpruceTall
                              {11.0f, 4.0f},   // kSpruceWide
                              {6.5f, 2.2f},    // kSpruceYoung
                              {17.0f, 3.4f},   // PINE
                              {1.3f, 1.4f},    // BUSH
                              {0.9f, 1.6f},    // JUNIPER
                              {0.9f, 1.0f},    // FERN
                              {0.55f, 0.35f},  // GRASS
                              {0.6f, 0.35f},   // kGrassDry
                              {0.32f, 0.16f},  // kMushroomRed
                              {0.24f, 0.14f},  // kMushroomBrown
                              {0.2f, 0.1f},    // kMushroomGlow
                              {0.8f, 0.6f},    // kStoneRound
                              {0.5f, 0.7f},    // kStoneFlat
                              {1.0f, 0.55f},   // kStoneSharp
                              {2.1f, 0.4f},    // kStoneStanding
                              {0.8f, 2.6f},    // LOG
                              {0.7f, 0.6f}};   // STUMP

float Smoothstep(float edge0, float edge1, float x) {
  float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

glm::quat RotationBetween(glm::vec3 from, glm::vec3 to) {
  float cosine = glm::dot(from, to);
  if (cosine > 0.99999f) {
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  }
  glm::vec3 axis = glm::cross(from, to);
  return glm::normalize(glm::quat(1.0f + cosine, axis.x, axis.y, axis.z));
}

/**
 * Where something could be placed and what is there
 */
struct Site {
  glm::vec3 position, normal;
  TrackPath::Nearest track;
  SurfaceMaterial material;
  float snow = 0.0f;
  float forest = 0.0f;    // density of the forest (0: clearing)
  bool on_floor = false;  // on the floor of the race track
  // from the edge of the trail (negative on the trail)
  float trail_distance = 1e6f;
  // from the edge of the closest foot path (negative on it)
  float path_distance = 1e6f;
  bool clearing = false;  // in a point of interest
};

class Scatterer {
 public:
  Scatterer(const WorldGenerator& world, ColumnKey column,
            std::vector<physics::ShapePart>* collision)
      : world_(world), column_(column), collision_(collision) {
    origin_ =
        glm::vec2(static_cast<float>(column.x), static_cast<float>(column.z)) *
        kChunkSize;
  }

  /**
   * Calls place(position, random) for the points of a jittered grid (one random
   * point per cell) that are in the column. The grid is the same for all
   * columns, so every point belongs to exactly one column.
   */
  template <typename Place>
  void ForEachPoint(float spacing, std::uint32_t salt, Place place) {
    int i0 = static_cast<int>(std::floor(origin_.x / spacing)),
        i1 = static_cast<int>(std::floor((origin_.x + kChunkSize) / spacing));
    int j0 = static_cast<int>(std::floor(origin_.y / spacing)),
        j1 = static_cast<int>(std::floor((origin_.y + kChunkSize) / spacing));
    for (int j = j0; j <= j1; ++j) {
      for (int i = i0; i <= i1; ++i) {
        noise::Random random(noise::Hash(i, j, WorldGenerator::kSeed ^ salt));
        glm::vec2 point =
            (glm::vec2(static_cast<float>(i), static_cast<float>(j)) +
             glm::vec2(random.Unit(), random.Unit())) *
            spacing;
        if (InColumn(point)) {
          place(point, random);
        }
      }
    }
  }

  bool InColumn(glm::vec2 point) const {
    return point.x >= origin_.x && point.y >= origin_.y &&
           point.x < origin_.x + kChunkSize && point.y < origin_.y + kChunkSize;
  }

  bool NearColumn(glm::vec2 point, float radius) const {
    return point.x > origin_.x - radius && point.y > origin_.y - radius &&
           point.x < origin_.x + kChunkSize + radius &&
           point.y < origin_.y + kChunkSize + radius;
  }

  /**
   * A stone, log or stump on the ground with its collision shape (like the ones
   * scattered in the forest)
   * @param alignment 0: upright, 1: like the ground
   * @param sink How deep it is in the ground (times its scale)
   * @param upright A standing stone (the collision is a capsule that goes up)
   */
  void Prop(Model model, glm::vec2 at, float yaw, float scale, float alignment,
            float sink, std::uint32_t seed, bool upright = false) {
    glm::vec3 position, normal;
    if (!InColumn(at) || !world_.Surface(at, position, normal)) {
      return;
    }
    noise::Random random(noise::Hash(static_cast<int>(std::floor(at.x * 8.0f)),
                                     static_cast<int>(std::floor(at.y * 8.0f)),
                                     seed));
    glm::quat rotation = Orientation(yaw, normal, alignment);
    glm::vec3 foot =
        position -
        glm::normalize(glm::mix(kUp, normal, alignment)) * (sink * scale);
    Solid(model, foot, rotation, scale, normal, upright, random);
  }

  /**
   * Flat stones stacked on each other, smaller to the top
   */
  void Cairn(glm::vec2 at, float size, int count, std::uint32_t seed) {
    glm::vec3 position, normal;
    if (!InColumn(at) || !world_.Surface(at, position, normal)) {
      return;
    }
    noise::Random random(noise::Hash(static_cast<int>(std::floor(at.x * 8.0f)),
                                     static_cast<int>(std::floor(at.y * 8.0f)),
                                     seed));
    glm::vec3 foot = position - glm::vec3(0.0f, 0.1f * size, 0.0f);
    for (int i = 0; i < count; ++i) {
      float scale =
          size * (1.0f - 0.7f * static_cast<float>(i) /
                             static_cast<float>(std::max(count - 1, 1)));
      glm::vec3 jitter(random.Range(-0.06f, 0.06f) * size, 0.0f,
                       random.Range(-0.06f, 0.06f) * size);
      Solid(Model::kStoneFlat, foot + jitter,
            Orientation(random.Range(0.0f, 6.2831853f), kUp, 0.0f), scale, kUp,
            false, random);
      // a flat stone is 0.5 high at scale 1, they lie a bit in each other
      foot.y += 0.36f * scale;
    }
  }

  void Solid(Model model, glm::vec3 foot, glm::quat rotation, float scale,
             glm::vec3 normal, bool upright, noise::Random& random) {
    Add(model, foot, rotation, scale, random);
    if (!collision_) {
      return;
    }
    physics::ShapePart part;
    if (model == Model::kLog) {
      // The log lies along its x axis, the capsule goes along y
      part.type = physics::ShapePart::kCapsule;
      part.radius = 0.36f * scale;
      part.half_height = 2.0f * scale;
      part.rotation =
          rotation * glm::angleAxis(1.5707963f, glm::vec3(0.0f, 0.0f, 1.0f));
      part.position = foot + rotation * glm::vec3(0.0f, part.radius, 0.0f);
    } else if (model == Model::kStump) {
      part.type = physics::ShapePart::kCapsule;
      part.radius = 0.42f * scale;
      part.half_height = 0.15f * scale;
      part.position =
          foot + glm::vec3(0.0f, part.radius + part.half_height, 0.0f);
    } else if (upright) {
      part.type = physics::ShapePart::kCapsule;
      part.radius = 0.9f * scale * ModelRadius(model);
      part.half_height =
          std::max(0.0f, 0.5f * scale * ModelHeight(model) - part.radius);
      part.position =
          foot + glm::vec3(0.0f, part.radius + part.half_height, 0.0f);
    } else {
      part.type = physics::ShapePart::kSphere;
      part.radius = 0.85f * scale * ModelRadius(model);
      part.position = foot + normal * (0.2f * scale);
    }
    collision_->push_back(part);
  }

  bool FindSite(glm::vec2 point, Site& result) const {
    if (!world_.Surface(point, result.position, result.normal)) {
      return false;
    }
    result.track = world_.GetPath().FindNearest(point);
    result.material = world_.Material(result.position, result.normal);
    result.forest =
        Smoothstep(-0.3f, 0.2f,
                   noise::Fbm(point * 0.011f, 3, WorldGenerator::kSeed + 60u));
    if (result.track.found) {
      float from_floor =
          std::abs(result.track.lateral) - result.track.floor_half_width;
      result.on_floor = from_floor < 1.0f &&
                        result.position.y < result.track.floor_height + 1.8f;
      result.trail_distance =
          std::abs(result.track.trail_lateral) - TrackPath::kTrailHalfWidth;
      if (from_floor > 1.0f && from_floor < 24.0f) {
        result.forest =
            std::max(result.forest,
                     0.92f);  // trees on the slopes left and right of the track
      }
    }
    const Landmarks& landmarks = world_.GetLandmarks();
    result.path_distance =
        landmarks.PathDistance(point) - Landmarks::kPathHalfWidth;
    result.clearing = landmarks.PointAt(point) >= 0;
    result.forest *=
        // bare mountain tops
        1.0f - Smoothstep(34.0f, 50.0f, result.position.y);
    result.snow =
        world_.SnowCover(result.position, result.normal, 1.0f, result.material);
    return true;
  }

  bool KeepsClear(glm::vec3 position, float radius) const {
    for (const KeepClear& object : world_.GetKeepClear()) {
      glm::vec2 offset(position.x - object.center.x,
                       position.z - object.center.z);
      if (glm::length(offset) < object.radius + radius &&
          position.y < object.center.y + object.radius + 1.0f) {
        return false;
      }
    }
    return true;
  }

  void Add(Model model, glm::vec3 position, glm::quat rotation, float scale,
           noise::Random& random) {
    Instance instance;
    instance.position_scale = glm::vec4(position, scale);
    instance.rotation =
        glm::vec4(rotation.x, rotation.y, rotation.z, rotation.w);
    instance.parameters = glm::vec4(random.Range(-1.0f, 1.0f), random.Unit(),
                                    static_cast<float>(model), 0.0f);
    instances.push_back(instance);
  }

  /**
   * Turned around the vertical axis, leaning with the ground (alignment 0:
   * upright, 1: like the ground)
   */
  static glm::quat Orientation(float yaw, glm::vec3 normal, float alignment) {
    glm::vec3 up = glm::normalize(glm::mix(kUp, normal, alignment));
    return RotationBetween(kUp, up) * glm::angleAxis(yaw, kUp);
  }

  void ScatterLarge() {
    const float sea_level = world_.GetSeaLevel();
    // Trees
    ForEachPoint(4.4f, 101u, [&](glm::vec2 point, noise::Random& random) {
      Site s;
      if (!FindSite(point, s) || s.on_floor || s.normal.y < 0.5f ||
          s.position.y < sea_level + 2.2f || s.material.sand > 0.25f ||
          s.material.cave > 0.5f ||
          (s.material.cliff > 0.5f && s.normal.y < 0.85f) ||
          s.path_distance < 1.8f || s.clearing ||
          random.Unit() > 0.9f * s.forest) {
        return;
      }
      if (s.track.found &&
          std::abs(s.track.lateral) < s.track.floor_half_width + 1.5f) {
        return;
      }
      float choice = random.Unit();
      // Mostly spruces, a few pines (more of them at the coast)
      float pines =
          0.04f +
          0.25f * (1.0f - Smoothstep(4.0f, 15.0f, s.position.y - sea_level));
      Model model = choice < pines   ? Model::kPine
                    : choice < 0.45f ? Model::kSpruceTall
                    : choice < 0.75f ? Model::kSpruceWide
                                     : Model::kSpruceYoung;
      float scale = random.Range(0.75f, 1.2f);
      if (!KeepsClear(s.position, ModelRadius(model) * scale * 0.6f)) {
        return;
      }
      float yaw = random.Range(0.0f, 6.2831853f);
      glm::quat rotation = glm::angleAxis(random.Range(-0.05f, 0.05f),
                                          glm::vec3(1.0f, 0.0f, 0.0f)) *
                           glm::angleAxis(yaw, kUp);
      glm::vec3 foot = s.position - glm::vec3(0.0f, 0.2f, 0.0f);
      Add(model, foot, rotation, scale, random);
      if (collision_) {
        // The trunk up to where the branches are dense
        physics::ShapePart trunk;
        trunk.type = physics::ShapePart::kCapsule;
        trunk.radius = (model == Model::kPine ? 0.3f : 0.26f) * scale;
        trunk.half_height = 2.6f * scale;
        trunk.position =
            foot + glm::vec3(0.0f, trunk.half_height + trunk.radius, 0.0f);
        collision_->push_back(trunk);
      }
    });
    // Bushes
    ForEachPoint(5.0f, 102u, [&](glm::vec2 point, noise::Random& random) {
      Site s;
      if (!FindSite(point, s) || s.normal.y < 0.55f ||
          s.position.y < sea_level + 1.6f || s.material.sand > 0.4f ||
          s.material.cave > 0.5f || s.trail_distance < 1.6f ||
          s.path_distance < 0.8f || s.clearing ||
          random.Unit() > 0.3f * (0.4f + 0.6f * s.forest)) {
        return;
      }
      Model model = random.Unit() < 0.6f ? Model::kBush : Model::kJuniper;
      float scale = random.Range(0.7f, 1.35f);
      if (!KeepsClear(s.position, ModelRadius(model) * scale)) {
        return;
      }
      Add(model, s.position - glm::vec3(0.0f, 0.1f, 0.0f),
          Orientation(random.Range(0.0f, 6.2831853f), s.normal, 0.3f), scale,
          random);
    });
    // Boulders, more of them on the cliffs and the mountains
    ForEachPoint(12.0f, 103u, [&](glm::vec2 point, noise::Random& random) {
      Site s;
      if (!FindSite(point, s) || s.on_floor || s.normal.y < 0.4f ||
          s.position.y < sea_level - 1.0f || s.material.cave > 0.5f ||
          s.path_distance < 2.0f || s.clearing) {
        return;
      }
      float chance = 0.2f + 0.45f * s.material.cliff +
                     0.35f * Smoothstep(30.0f, 45.0f, s.position.y);
      if (random.Unit() > chance ||
          (s.track.found &&
           std::abs(s.track.lateral) < s.track.floor_half_width + 2.0f)) {
        return;
      }
      float choice = random.Unit();
      Model model = choice < 0.4f    ? Model::kStoneRound
                    : choice < 0.75f ? Model::kStoneSharp
                                     : Model::kStoneFlat;
      float scale = random.Range(1.0f, 2.8f);
      if (!KeepsClear(s.position, scale)) {
        return;
      }
      glm::vec3 foot = s.position - s.normal * (0.25f * scale);
      Add(model, foot,
          Orientation(random.Range(0.0f, 6.2831853f), s.normal, 0.7f), scale,
          random);
      if (collision_) {
        physics::ShapePart stone;
        stone.type = physics::ShapePart::kSphere;
        stone.radius = 0.85f * scale * ModelRadius(model);
        stone.position = foot + s.normal * (0.2f * scale);
        collision_->push_back(stone);
      }
    });
    // Fallen logs and stumps in the forest
    ForEachPoint(20.0f, 104u, [&](glm::vec2 point, noise::Random& random) {
      Site s;
      if (!FindSite(point, s) || s.on_floor || s.normal.y < 0.82f ||
          s.forest < 0.45f || s.position.y < sea_level + 2.5f ||
          s.material.cave > 0.5f || s.path_distance < 2.0f || s.clearing ||
          random.Unit() > 0.38f ||
          (s.track.found &&
           std::abs(s.track.lateral) < s.track.floor_half_width + 3.0f)) {
        return;
      }
      bool is_log = random.Unit() < 0.55f;
      float scale = random.Range(0.8f, 1.2f);
      Model model = is_log ? Model::kLog : Model::kStump;
      if (!KeepsClear(s.position, ModelRadius(model) * scale)) {
        return;
      }
      float yaw = random.Range(0.0f, 6.2831853f);
      glm::quat rotation = Orientation(yaw, s.normal, is_log ? 1.0f : 0.2f);
      glm::vec3 foot =
          s.position - glm::vec3(0.0f, is_log ? 0.1f : 0.15f, 0.0f);
      Add(model, foot, rotation, scale, random);
      if (collision_) {
        physics::ShapePart part;
        part.type = physics::ShapePart::kCapsule;
        if (is_log) {
          // The log lies along its x axis, the capsule goes along y
          part.radius = 0.36f * scale;
          part.half_height = 2.0f * scale;
          part.rotation =
              rotation *
              glm::angleAxis(1.5707963f, glm::vec3(0.0f, 0.0f, 1.0f));
          part.position = foot + rotation * glm::vec3(0.0f, part.radius, 0.0f);
        } else {
          part.radius = 0.42f * scale;
          part.half_height = 0.15f * scale;
          part.position =
              foot + glm::vec3(0.0f, part.radius + part.half_height, 0.0f);
        }
        collision_->push_back(part);
      }
    });
  }

  /**
   * The things at the points of interest (cairns, a fire ring with log seats,
   * standing stones, driftwood) and the cairns where the foot paths begin.
   * Every prop has its own random numbers, so it is the same in every column.
   */
  void ScatterLandmarks() {
    const Landmarks& landmarks = world_.GetLandmarks();
    const std::vector<PointOfInterest>& points =
        landmarks.GetPointsOfInterest();
    const float tau = 6.2831853f;
    for (std::size_t p = 0; p < points.size(); ++p) {
      const PointOfInterest& point = points[p];
      glm::vec2 middle(point.position.x, point.position.z);
      if (!NearColumn(middle, point.radius + 8.0f)) {
        continue;
      }
      std::uint32_t salt =
          WorldGenerator::kSeed + 300u + static_cast<std::uint32_t>(p) * 101u;
      noise::Random layout(noise::Hash(static_cast<int>(p), 0, salt));
      glm::vec2 facing = point.facing, across(-point.facing.y, point.facing.x);
      auto around = [&](float angle, float radius) {
        return middle + radius * glm::vec2(std::cos(angle), std::sin(angle));
      };
      switch (point.kind) {
        case PointOfInterest::Kind::kSummit:
          Cairn(middle, 1.5f, 5, salt);
          for (int i = 0; i < 4; ++i) {
            float angle =
                tau * (static_cast<float>(i) + layout.Range(0.0f, 0.6f)) / 4.0f;
            Model model =
                layout.Unit() < 0.5f ? Model::kStoneRound : Model::kStoneSharp;
            Prop(model, around(angle, layout.Range(4.5f, 6.0f)),
                 layout.Range(0.0f, tau), layout.Range(1.2f, 2.0f), 0.7f, 0.25f,
                 salt + 10u + i);
          }
          break;
        case PointOfInterest::Kind::kCamp:
          // A ring of stones around the fire with two half burnt logs in it,
          // logs and stumps to sit on
          for (int i = 0; i < 10; ++i) {
            Prop(Model::kStoneRound,
                 around(tau * static_cast<float>(i) / 10.0f, 0.9f),
                 layout.Range(0.0f, tau), layout.Range(0.26f, 0.34f), 0.6f,
                 0.3f, salt + 10u + i);
          }
          Prop(Model::kLog, middle, layout.Range(0.0f, tau), 0.24f, 1.0f, 0.1f,
               salt + 30u);
          Prop(Model::kLog, middle, layout.Range(0.0f, tau), 0.22f, 1.0f, -0.2f,
               salt + 31u);
          for (int i = 0; i < 3; ++i) {
            float angle = tau * static_cast<float>(i) / 3.0f +
                          layout.Range(-0.15f, 0.15f);
            // Along the circle: its axis (x) is the tangent
            glm::vec2 tangent(-std::sin(angle), std::cos(angle));
            Prop(Model::kLog, around(angle, 3.3f),
                 std::atan2(-tangent.y, tangent.x), 0.55f, 1.0f, 0.1f,
                 salt + 40u + i);
          }
          Prop(Model::kStump, around(tau / 6.0f, 3.4f), layout.Range(0.0f, tau),
               0.85f, 0.2f, 0.15f, salt + 50u);
          Prop(Model::kStump, around(tau / 2.0f, 3.4f), layout.Range(0.0f, tau),
               0.8f, 0.2f, 0.15f, salt + 51u);
          break;
        case PointOfInterest::Kind::kStandingStones:
          for (int i = 0; i < 7; ++i) {
            float angle =
                tau * (static_cast<float>(i) + layout.Range(-0.15f, 0.15f)) /
                7.0f;
            Prop(Model::kStoneStanding, around(angle, 6.5f),
                 layout.Range(0.0f, tau), layout.Range(1.0f, 1.35f), 0.05f,
                 0.1f, salt + 10u + i, true);
          }
          Prop(Model::kStoneFlat, middle, layout.Range(0.0f, tau), 2.0f, 1.0f,
               0.2f, salt + 20u);
          break;
        case PointOfInterest::Kind::kBeach:
          // Driftwood and stones washed up on the sand
          for (int i = 0; i < 3; ++i) {
            glm::vec2 at = middle + facing * layout.Range(-1.0f, 4.0f) +
                           across * layout.Range(-6.0f, 6.0f);
            Prop(Model::kLog, at, layout.Range(0.0f, tau),
                 layout.Range(0.8f, 1.3f), 1.0f, 0.15f, salt + 10u + i);
          }
          Prop(Model::kStump,
               middle + facing * 2.0f + across * layout.Range(-5.0f, 5.0f),
               layout.Range(0.0f, tau), 1.0f, 0.6f, 0.2f, salt + 20u);
          for (int i = 0; i < 4; ++i) {
            glm::vec2 at = middle + facing * layout.Range(-2.0f, 5.0f) +
                           across * layout.Range(-7.0f, 7.0f);
            Prop(layout.Unit() < 0.5f ? Model::kStoneRound : Model::kStoneFlat,
                 at, layout.Range(0.0f, tau), layout.Range(0.5f, 1.1f), 0.8f,
                 0.3f, salt + 30u + i);
          }
          break;
        case PointOfInterest::Kind::kCliffView:
          // A log to sit on, looking at the sea, and a cairn at the edge
          Prop(Model::kLog, middle - facing * 1.5f,
               std::atan2(-across.y, across.x), 0.55f, 1.0f, 0.1f, salt + 10u);
          Cairn(middle + facing * 2.5f, 1.0f, 4, salt + 20u);
          Prop(Model::kStoneRound, middle + across * 4.5f,
               layout.Range(0.0f, tau), 1.6f, 0.7f, 0.25f, salt + 30u);
          Prop(Model::kStoneSharp, middle - across * 4.0f,
               layout.Range(0.0f, tau), 1.3f, 0.7f, 0.25f, salt + 31u);
          break;
      }
    }
    const std::vector<glm::vec3>& markers = landmarks.GetMarkers();
    for (std::size_t m = 0; m < markers.size(); ++m) {
      Cairn(glm::vec2(markers[m].x, markers[m].z), 0.75f, 3,
            WorldGenerator::kSeed + 400u + static_cast<std::uint32_t>(m) * 7u);
    }
  }

  void ScatterSmall() {
    const float sea_level = world_.GetSeaLevel();
    // Grass: thinner close to the trail, none in the snow and on the sand
    ForEachPoint(0.65f, 201u, [&](glm::vec2 point, noise::Random& random) {
      float meadow =
          0.3f + 0.7f * Smoothstep(-0.25f, 0.3f,
                                   noise::Fbm(point * 0.03f, 2,
                                              WorldGenerator::kSeed + 61u));
      if (random.Unit() > meadow) {
        return;  // before the (expensive) site
      }
      Site s;
      if (!FindSite(point, s) || s.normal.y < 0.6f ||
          s.position.y < sea_level + 1.0f || s.material.sand > 0.15f ||
          s.material.cave > 0.5f || s.material.cliff > 0.5f ||
          s.trail_distance < -0.6f || s.path_distance < -0.5f) {
        return;
      }
      float near_trail =
          Smoothstep(-0.6f, 1.6f, std::min(s.trail_distance, s.path_distance));
      if (random.Unit() > near_trail * (1.0f - 1.8f * s.snow) ||
          !KeepsClear(s.position, 0.3f)) {
        return;
      }
      Model model =
          random.Unit() < 0.3f + 0.4f * Smoothstep(20.0f, 40.0f, s.position.y)
              ? Model::kGrassDry
              : Model::kGrass;
      Add(model, s.position - glm::vec3(0.0f, 0.03f, 0.0f),
          Orientation(random.Range(0.0f, 6.2831853f), s.normal, 0.5f),
          random.Range(0.7f, 1.3f), random);
    });
    // Ferns in the forest
    ForEachPoint(2.6f, 202u, [&](glm::vec2 point, noise::Random& random) {
      Site s;
      if (!FindSite(point, s) || s.forest < 0.45f || s.normal.y < 0.6f ||
          s.trail_distance < 1.5f || s.path_distance < 0.8f || s.clearing ||
          s.snow > 0.3f || s.material.cave > 0.5f ||
          s.position.y < sea_level + 2.0f || random.Unit() > 0.35f * s.forest ||
          !KeepsClear(s.position, 0.8f)) {
        return;
      }
      Add(Model::kFern, s.position - glm::vec3(0.0f, 0.05f, 0.0f),
          Orientation(random.Range(0.0f, 6.2831853f), s.normal, 0.6f),
          random.Range(0.6f, 1.2f), random);
    });
    // Mushrooms in small groups
    ForEachPoint(7.0f, 203u, [&](glm::vec2 point, noise::Random& random) {
      Site s;
      if (!FindSite(point, s) || s.forest < 0.4f || s.normal.y < 0.75f ||
          s.trail_distance < 0.6f || s.path_distance < 0.3f ||
          s.material.cave > 0.5f || s.snow > 0.5f ||
          s.position.y < sea_level + 2.0f || random.Unit() > 0.4f) {
        return;
      }
      Model model =
          random.Unit() < 0.35f ? Model::kMushroomRed : Model::kMushroomBrown;
      int count = 2 + static_cast<int>(random.Unit() * 5.0f);
      for (int i = 0; i < count; ++i) {
        float angle = random.Range(0.0f, 6.2831853f),
              distance = random.Range(0.1f, 1.0f);
        glm::vec2 spot =
            point + distance * glm::vec2(std::cos(angle), std::sin(angle));
        glm::vec3 position, normal;
        if (!InColumn(spot) || !world_.Surface(spot, position, normal) ||
            !KeepsClear(position, 0.2f)) {
          continue;
        }
        Add(model, position - glm::vec3(0.0f, 0.02f, 0.0f),
            Orientation(random.Range(0.0f, 6.2831853f), normal, 0.3f),
            random.Range(0.6f, 1.3f), random);
      }
    });
    // Pebbles and small stones, also at the edges of the trail
    ForEachPoint(3.2f, 204u, [&](glm::vec2 point, noise::Random& random) {
      Site s;
      if (!FindSite(point, s) || s.normal.y < 0.55f ||
          s.trail_distance < -0.8f || s.position.y < sea_level - 0.5f ||
          random.Unit() > 0.3f + 0.3f * s.material.cliff ||
          !KeepsClear(s.position, 0.5f)) {
        return;
      }
      float choice = random.Unit();
      Model model = choice < 0.4f   ? Model::kStoneRound
                    : choice < 0.7f ? Model::kStoneFlat
                                    : Model::kStoneSharp;
      float scale = random.Range(0.15f, 0.5f);
      Add(model, s.position - s.normal * (0.2f * scale),
          Orientation(random.Range(0.0f, 6.2831853f), s.normal, 0.8f), scale,
          random);
    });
    // Glowing mushrooms along the walls of the caves (on the floor under the
    // roof)
    const TrackPath& path = world_.GetPath();
    for (std::size_t c = 0; c < world_.GetCaves().size(); ++c) {
      const Cave& cave = world_.GetCaves()[c];
      int steps = static_cast<int>((cave.end - cave.start - 4.0f) / 2.5f);
      for (int step = 0; step <= steps; ++step) {
        for (int side = -1; side <= 1; side += 2) {
          noise::Random random(noise::Hash(static_cast<int>(c),
                                           step * 2 + (side > 0 ? 1 : 0),
                                           WorldGenerator::kSeed + 205u));
          if (random.Unit() > 0.55f) {
            continue;
          }
          TrackPath::Sample sample =
              path.At(cave.start + 2.0f + 2.5f * static_cast<float>(step) +
                      random.Range(-1.0f, 1.0f));
          glm::vec2 right(-sample.tangent.y, sample.tangent.x);
          float half_width = side > 0 ? sample.floor_right : sample.floor_left;
          glm::vec2 center =
              glm::vec2(sample.position.x, sample.position.z) +
              right * (static_cast<float>(side) *
                       (half_width - 0.3f + random.Range(0.0f, 0.8f)));
          int count = 3 + static_cast<int>(random.Unit() * 6.0f);
          for (int i = 0; i < count; ++i) {
            float angle = random.Range(0.0f, 6.2831853f),
                  distance = random.Range(0.0f, 0.7f);
            glm::vec2 spot =
                center + distance * glm::vec2(std::cos(angle), std::sin(angle));
            if (!InColumn(spot)) {
              continue;
            }
            // The floor under the roof: the height field
            const float e = 0.25f;
            float y = world_.Height(spot);
            glm::vec3 normal = glm::normalize(
                glm::vec3(world_.Height(spot - glm::vec2(e, 0.0f)) -
                              world_.Height(spot + glm::vec2(e, 0.0f)),
                          2.0f * e,
                          world_.Height(spot - glm::vec2(0.0f, e)) -
                              world_.Height(spot + glm::vec2(0.0f, e))));
            if (y < sea_level + 0.2f) {
              continue;  // in a puddle
            }
            Add(Model::kMushroomGlow, glm::vec3(spot.x, y - 0.02f, spot.y),
                Orientation(random.Range(0.0f, 6.2831853f), normal, 0.4f),
                random.Range(1.2f, 2.6f), random);
          }
        }
      }
    }
  }

  std::vector<Instance> instances;

 private:
  const WorldGenerator& world_;
  ColumnKey column_;
  std::vector<physics::ShapePart>* collision_;
  glm::vec2 origin_;
};
}  // namespace

std::vector<Instance> Scatter(const WorldGenerator& world, ColumnKey column,
                              Layer layer,
                              std::vector<physics::ShapePart>* collision) {
  Scatterer scatterer(world, column, collision);
  if (layer == Layer::kLarge) {
    scatterer.ScatterLarge();
    scatterer.ScatterLandmarks();
  } else {
    scatterer.ScatterSmall();
  }
  return std::move(scatterer.instances);
}

float ModelHeight(Model model) {
  return kModelSizes[static_cast<int>(model)].height;
}

float ModelRadius(Model model) {
  return kModelSizes[static_cast<int>(model)].radius;
}

}  // namespace bibo::world
