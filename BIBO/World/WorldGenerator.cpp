#include "WorldGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Noise.hpp"
#include "Parallel.hpp"

namespace bibo::world {

namespace {
// The coast is this far from the middle of the track, plus or minus its noise
const float kCoastRadius = 262.0f;
const float kCoastVariation = 34.0f;
// The track terrain turns into its blurred version in this band inside of its
// border (the floor of the track is further inside), the procedural land takes
// over in this band outside of it
const float kTrackBlendInside = 8.0f, kTrackBlendOutside = 55.0f;
const float kTrackBlendWarp = 26.0f;
// Mountains around the track (direction from the middle in degrees, distance,
// height, radius)
const struct Mountain {
  float angle, distance, height, radius;
} kMountains[] = {{128.0f, 190.0f, 44.0f, 48.0f},
                  {18.0f, 196.0f, 34.0f, 40.0f},
                  {252.0f, 182.0f, 30.0f, 42.0f},
                  {310.0f, 200.0f, 22.0f, 34.0f}};
// Caves
const float kCaveLengths[] = {52.0f, 42.0f};
// above the floor of the track (the bear jumps about 1.5 high, the camera needs
// room)
const float kCaveCeiling = 7.5f;
const float kCaveMinWalls = 10.0f;
const float kCaveStartClearance = 45.0f;  // no cave at the start and finish
const float kCaveSeparation = 60.0f;      // between the ends of two caves

float Smoothstep(float edge0, float edge1, float x) {
  float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// Polynomial smooth maximum: like max, but rounded where the two are close (k:
// width of the rounding)
float SmoothMax(float a, float b, float k) {
  float h = std::max(k - std::abs(a - b), 0.0f) / k;
  return std::max(a, b) + h * h * k * 0.25f;
}

/**
 * Directions to look for the sky (sky visibility): spread over the upper
 * hemisphere, more of them steep (cosine weighted)
 */
struct Hemisphere {
  glm::vec3 directions[10];
  Hemisphere() {
    const float golden_angle = 2.39996323f;
    for (int i = 0; i < 10; ++i) {
      float radius = std::sqrt((static_cast<float>(i) + 0.5f) / 10.0f);
      float angle = static_cast<float>(i) * golden_angle;
      glm::vec2 disk = radius * glm::vec2(std::cos(angle), std::sin(angle));
      directions[i] = glm::vec3(
          disk.x, std::sqrt(std::max(0.0f, 1.0f - glm::dot(disk, disk))),
          disk.y);
    }
  }
};
}  // namespace

WorldGenerator::WorldGenerator(Description description)
    : track_(std::move(description.track)),
      keep_clear_(std::move(description.keep_clear)),
      sea_level_(description.sea_level) {
  center_ = track_->GetCenter();
  path_ = std::make_unique<TrackPath>(*track_, description.start,
                                      description.start_direction,
                                      description.checkpoints, sea_level_);
  ChooseCaves();

  float reach = kCoastRadius + kCoastVariation + 50.0f;
  bounds_.minimum =
      glm::vec3(center_.x - reach, kSeaFloor - 8.0f, center_.y - reach);
  bounds_.maximum = glm::vec3(center_.x + reach, 80.0f, center_.y + reach);
  landmarks_ = std::make_unique<Landmarks>(*this);
  ComputeColumnRanges();
}

WorldGenerator::~WorldGenerator() = default;

float WorldGenerator::CoastRadius(glm::vec2 direction) const {
  return kCoastRadius +
         kCoastVariation * noise::Fbm(direction * 1.3f + glm::vec2(5.1f, 7.3f),
                                      3, kSeed + 11u) +
         0.3f * kCoastVariation *
             noise::Fbm(direction * 5.0f + glm::vec2(-2.7f, 1.9f), 2,
                        kSeed + 12u);
}

void WorldGenerator::Coast(glm::vec2 position, float& inland,
                           float& cliff) const {
  glm::vec2 offset = position - center_;
  float distance = glm::length(offset);
  glm::vec2 direction =
      distance > 1e-3f ? offset / distance : glm::vec2(1.0f, 0.0f);
  inland = CoastRadius(direction) - distance;
  cliff = Smoothstep(
      -0.1f, 0.22f,
      noise::Fbm(direction * 2.2f + glm::vec2(31.7f, -4.2f), 2, kSeed + 13u));
}

float WorldGenerator::ProceduralHeight(glm::vec2 position) const {
  float inland, cliff;
  Coast(position, inland, cliff);

  // Forest hills, with a few higher ridges
  float land = 11.5f + 11.0f * noise::Fbm(position * 0.0042f, 4, kSeed + 1u) +
               11.0f * noise::Ridged(position * 0.0105f, 4, kSeed + 2u) +
               2.0f * noise::Fbm(position * 0.045f, 2, kSeed + 3u);
  for (const Mountain& mountain : kMountains) {
    float angle = glm::radians(mountain.angle);
    glm::vec2 peak = center_ + mountain.distance *
                                   glm::vec2(std::cos(angle), std::sin(angle));
    float falloff = glm::distance(position, peak) / mountain.radius;
    if (falloff < 3.0f) {
      float rough =
          0.75f + 0.25f * noise::Ridged(position * 0.03f, 3, kSeed + 8u);
      land += mountain.height * std::exp(-falloff * falloff) * rough;
    }
  }
  land =
      std::max(land, 4.5f + 1.5f * noise::Fbm(position * 0.02f, 2,
                                              kSeed + 4u));  // no lakes inland

  // Beach: a gentle slope out of the sea, then the forest
  float beach = inland < 0.0f
                    ? sea_level_ + 0.15f * inland - 0.0022f * inland * inland
                    : sea_level_ + 0.10f * inland;
  float beach_land =
      glm::mix(beach, std::max(beach, land), Smoothstep(12.0f, 55.0f, inland));

  // Cliff: a plateau that drops steeply into the sea
  float cliff_top = std::max(
      land, 17.0f + 5.0f * noise::Fbm(position * 0.03f, 2, kSeed + 5u));
  float cliff_foot = sea_level_ - 9.0f + 0.12f * std::min(inland, 0.0f);
  float cliff_height =
      glm::mix(cliff_foot, cliff_top, Smoothstep(-6.0f, 4.0f, inland));

  float height = glm::mix(beach_land, cliff_height, cliff);
  float sea_floor =
      kSeaFloor + 2.0f * noise::Fbm(position * 0.02f, 2, kSeed + 6u);
  return SmoothMax(height, sea_floor, 4.0f);
}

std::vector<glm::vec2> WorldGenerator::GetMountainPeaks() const {
  std::vector<glm::vec2> peaks;
  for (const Mountain& mountain : kMountains) {
    float angle = glm::radians(mountain.angle);
    peaks.push_back(center_ + mountain.distance *
                                  glm::vec2(std::cos(angle), std::sin(angle)));
  }
  return peaks;
}

float WorldGenerator::Height(glm::vec2 position) const {
  float natural = NaturalHeight(position);
  return landmarks_ ? landmarks_->Height(position, natural) : natural;
}

float WorldGenerator::NaturalHeight(glm::vec2 position) const {
  // Inside: the track terrain as it is. At its border it turns into its blurred
  // version, which goes on outside of the border (the closest blurred height
  // inside) and turns into the procedural land further out.
  float inside = track_->DistanceInside(position);
  if (inside >= kTrackBlendInside) {
    return track_->Height(position);
  }
  if (inside >= 0.0f) {
    return glm::mix(track_->SmoothHeight(position), track_->Height(position),
                    Smoothstep(0.0f, kTrackBlendInside, inside));
  }
  // The closest blurred height inside, averaged along the border the more the
  // further out (no straight stripes outwards)
  glm::vec2 border = track_->ClampInside(position);
  glm::vec2 out = position - border;
  float outside = glm::length(out);
  glm::vec2 along = outside > 1e-3f ? glm::vec2(-out.y, out.x) / outside
                                    : glm::vec2(1.0f, 0.0f);
  float continued = 0.0f;
  for (int k = -2; k <= 2; ++k) {
    continued += track_->SmoothHeight(track_->ClampInside(
        border + along * (0.3f * outside * static_cast<float>(k))));
  }
  continued /= 5.0f;
  // The band where the land takes over is uneven, so that the rectangle of the
  // track terrain does not show
  float warp = kTrackBlendWarp * noise::Fbm(position * 0.011f, 3, kSeed + 9u) *
               (1.0f - Smoothstep(-20.0f, 0.0f, inside));
  float weight = Smoothstep(-kTrackBlendOutside, 0.0f, inside + warp);
  if (weight <= 0.0f) {
    return ProceduralHeight(position);
  }
  // Some unevenness, so that the continued heights do not form straight ridges
  float detail =
      9.0f * (noise::Ridged(position * 0.0105f, 4, kSeed + 2u) - 0.35f) +
      3.0f * noise::Fbm(position * 0.03f, 3, kSeed + 7u);
  continued += detail * (1.0f - Smoothstep(-10.0f, 0.0f, inside));
  return glm::mix(ProceduralHeight(position), continued, weight);
}

float WorldGenerator::CliffNoise(glm::vec3 position, float inland,
                                 float cliff) const {
  float band = 1.0f - Smoothstep(5.0f, 18.0f, std::abs(inland));
  // Only on the face of the cliff, not on top of it (no floating rocks)
  float face = Smoothstep(sea_level_ - 15.0f, sea_level_ - 7.0f, position.y) *
               (1.0f - Smoothstep(11.0f, 16.5f, position.y));
  float weight = cliff * band * face;
  if (weight <= 0.0f) {
    return 0.0f;
  }
  return weight * 7.0f *
         noise::Fbm(position * glm::vec3(0.07f, 0.11f, 0.07f), 3, kSeed + 20u);
}

WorldGenerator::Column WorldGenerator::ColumnAt(glm::vec2 position) const {
  Column result;
  result.height = Height(position);
  Coast(position, result.inland, result.cliff);
  result.rugged = result.cliff >= 0.02f && std::abs(result.inland) <= 18.0f;
  for (const Cave& cave : caves_) {
    if (position.x < cave.bounds.minimum.x ||
        position.x > cave.bounds.maximum.x ||
        position.y < cave.bounds.minimum.z ||
        position.y > cave.bounds.maximum.z || result.roof_count == 4) {
      continue;
    }
    TrackPath::Nearest nearest = path_->FindNearest(position);
    if (!nearest.found || nearest.distance < cave.start - 6.0f ||
        nearest.distance > cave.end + 6.0f) {
      continue;
    }
    TrackPath::Sample sample = path_->At(nearest.distance);
    float half_span = std::max(sample.floor_left, sample.floor_right) + 3.5f;
    float floor_height = std::max(sample.position.y, cave.floor_height - 1.5f);
    float lateral = std::abs(nearest.lateral);
    float arch =
        1.0f - std::min(1.0f, (lateral / half_span) * (lateral / half_span));
    Column::Roof& roof = result.roofs[result.roof_count++];
    roof.ceiling = floor_height + cave.ceiling_height + 1.6f * arch;
    roof.top = floor_height + cave.roof_height -
               0.7f * std::max(0.0f, lateral - half_span) +
               1.5f * noise::Fbm(position * 0.06f, 2, kSeed + 31u);
    roof.side = half_span + 12.0f - lateral;
    roof.ends =
        std::min(nearest.distance - cave.start, cave.end - nearest.distance);
    roof.bottom = cave.bounds.minimum.y;
    roof.upper = cave.bounds.maximum.y;
  }
  return result;
}

float WorldGenerator::DensityIn(const Column& column,
                                glm::vec3 position) const {
  float result = column.height - position.y;
  if (column.rugged) {
    result += CliffNoise(position, column.inland, column.cliff);
  }
  for (int i = 0; i < column.roof_count; ++i) {
    const Column::Roof& roof = column.roofs[i];
    if (position.y < roof.bottom || position.y > roof.upper) {
      continue;
    }
    // The noise moves the ceiling by less than 1.5 and the ends by less
    // than 1.8: if the roof cannot come close to the ground density, the smooth
    // maximum is the ground density (exactly, so skipping the noise changes
    // nothing)
    float upper_bound = std::min(
        std::min(roof.top - position.y, position.y - roof.ceiling + 1.5f),
        std::min(roof.side, roof.ends + 1.8f));
    if (upper_bound < result - 2.0f) {
      continue;
    }
    float ceiling =
        roof.ceiling + 1.0f * noise::Fbm(position * 0.13f, 2, kSeed + 30u);
    float ends =
        roof.ends + 1.2f * noise::Fbm(position * 0.17f, 2, kSeed + 32u);
    float density =
        std::min(std::min(roof.top - position.y, position.y - ceiling),
                 std::min(roof.side, ends));
    result = SmoothMax(result, density, 2.0f);
  }
  return result;
}

float WorldGenerator::Density(glm::vec3 position) const {
  return DensityIn(ColumnAt(glm::vec2(position.x, position.z)), position);
}

void WorldGenerator::DensityColumn(float x, float z, int first_index,
                                   float step, int count,
                                   float* densities) const {
  Column info = ColumnAt(glm::vec2(x, z));
  for (int i = 0; i < count; ++i) {
    densities[i] = DensityIn(
        info, glm::vec3(x, static_cast<float>(first_index + i) * step, z));
  }
}

glm::vec3 WorldGenerator::Normal(glm::vec3 position) const {
  const float e = 0.2f;
  glm::vec3 gradient(Density(position + glm::vec3(e, 0, 0)) -
                         Density(position - glm::vec3(e, 0, 0)),
                     Density(position + glm::vec3(0, e, 0)) -
                         Density(position - glm::vec3(0, e, 0)),
                     Density(position + glm::vec3(0, 0, e)) -
                         Density(position - glm::vec3(0, 0, e)));
  float length = glm::length(gradient);
  // The density decreases towards the air
  return length > 1e-9f ? -gradient / length : glm::vec3(0.0f, 1.0f, 0.0f);
}

bool WorldGenerator::TopSurface(glm::vec2 position, glm::vec3& surface,
                                glm::vec3& surface_normal) const {
  float y = std::max(Height(position), CaveRoofTop(position)) + 9.0f;
  float lowest = kSeaFloor - 8.0f;
  if (Density(glm::vec3(position.x, y, position.y)) > 0.0f) {
    y += 10.0f;  // very rough cliff
  }
  while (y > lowest &&
         Density(glm::vec3(position.x, y - 1.0f, position.y)) <= 0.0f) {
    y -= 1.0f;
  }
  if (y <= lowest) {
    return false;
  }
  float above = y, below = y - 1.0f;
  for (int i = 0; i < 10; ++i) {
    float middle = 0.5f * (above + below);
    (Density(glm::vec3(position.x, middle, position.y)) > 0.0f ? below
                                                               : above) =
        middle;
  }
  surface = glm::vec3(position.x, 0.5f * (above + below), position.y);
  surface_normal = Normal(surface);
  return true;
}

bool WorldGenerator::Surface(glm::vec2 position, glm::vec3& point,
                             glm::vec3& surface_normal) const {
  Column info = ColumnAt(position);
  if (info.roof_count > 0 || info.rugged) {
    return TopSurface(position, point, surface_normal);
  }
  // density = height - y: the normal is (-dh/dx, 1, -dh/dz) normalized
  const float e = 0.25f;
  float dx = Height(position + glm::vec2(e, 0.0f)) -
             Height(position - glm::vec2(e, 0.0f));
  float dz = Height(position + glm::vec2(0.0f, e)) -
             Height(position - glm::vec2(0.0f, e));
  point = glm::vec3(position.x, info.height, position.y);
  surface_normal =
      glm::normalize(glm::vec3(-dx / (2.0f * e), 1.0f, -dz / (2.0f * e)));
  return true;
}

float WorldGenerator::SnowCover(glm::vec3 position, glm::vec3 surface_normal,
                                float visibility,
                                const SurfaceMaterial& surface) const {
  if (surface.cave > 0.5f) {
    return 0.0f;
  }
  glm::vec2 ground(position.x, position.z);
  float patches = noise::Fbm(ground * 0.06f, 4, kSeed + 50u);
  float altitude = Smoothstep(18.0f, 45.0f, position.y);
  float cover = Smoothstep(0.12f, 0.32f, patches + 0.8f * altitude - 0.08f);
  cover *= Smoothstep(0.68f, 0.9f, surface_normal.y);
  cover *= Smoothstep(0.2f, 0.6f, visibility);
  cover *= 1.0f - 0.85f * surface.trail;
  cover *= 1.0f - surface.sand;
  return std::clamp(cover, 0.0f, 1.0f);
}

float WorldGenerator::CaveRoofTop(glm::vec2 position) const {
  for (const Cave& cave : caves_) {
    if (position.x < cave.bounds.minimum.x ||
        position.x > cave.bounds.maximum.x ||
        position.y < cave.bounds.minimum.z ||
        position.y > cave.bounds.maximum.z) {
      continue;
    }
    TrackPath::Nearest nearest = path_->FindNearest(position);
    if (!nearest.found || nearest.distance < cave.start - 1.0f ||
        nearest.distance > cave.end + 1.0f) {
      continue;
    }
    TrackPath::Sample sample = path_->At(nearest.distance);
    float half_span = std::max(sample.floor_left, sample.floor_right) + 3.5f;
    if (std::abs(nearest.lateral) < half_span + 1.0f) {
      return std::max(sample.position.y, cave.floor_height - 1.5f) +
             cave.roof_height - 2.0f;
    }
  }
  return -1e6f;
}

bool WorldGenerator::IsInCave(glm::vec3 position) const {
  for (const Cave& cave : caves_) {
    if (!cave.bounds.Contains(position)) {
      continue;
    }
    TrackPath::Nearest nearest =
        path_->FindNearest(glm::vec2(position.x, position.z));
    if (!nearest.found || nearest.distance < cave.start ||
        nearest.distance > cave.end) {
      continue;
    }
    TrackPath::Sample sample = path_->At(nearest.distance);
    float half_span = std::max(sample.floor_left, sample.floor_right) + 3.5f;
    float floor_height = std::max(sample.position.y, cave.floor_height - 1.5f);
    if (std::abs(nearest.lateral) < half_span + 3.0f &&
        position.y > floor_height - 3.0f &&
        position.y < floor_height + cave.ceiling_height + 3.0f) {
      return true;
    }
  }
  return false;
}

SurfaceMaterial WorldGenerator::Material(glm::vec3 position,
                                         glm::vec3 surface_normal) const {
  SurfaceMaterial result;
  glm::vec2 ground(position.x, position.z);
  float inland, cliff;
  Coast(ground, inland, cliff);
  float beach = 1.0f - Smoothstep(10.0f, 24.0f, inland);
  result.sand =
      beach * (1.0f - Smoothstep(0.35f, 0.75f, cliff)) *
      (1.0f - Smoothstep(sea_level_ + 3.0f, sea_level_ + 5.5f, position.y));
  if (position.y < sea_level_ - 0.6f) {
    result.sand = std::max(result.sand, 1.0f - 0.7f * cliff);  // sea floor
  }
  result.cliff = cliff * (1.0f - Smoothstep(16.0f, 32.0f, inland));

  TrackPath::Nearest nearest = path_->FindNearest(ground);
  if (nearest.found && position.y < nearest.floor_height + 2.5f) {
    float edge = TrackPath::kTrailHalfWidth +
                 0.5f * noise::Fbm(glm::vec2(nearest.distance * 0.15f, 1.7f), 2,
                                   kSeed + 40u);
    result.trail = 1.0f - Smoothstep(edge - 0.8f, edge + 0.7f,
                                     std::abs(nearest.trail_lateral));
  }
  if (landmarks_ && position.y > Height(ground) - 1.5f) {
    result.trail = std::max(result.trail, landmarks_->Trail(ground));
  }
  // The trail ends at the water line (wet earth at the shore, the shader
  // darkens it)
  result.trail *= Smoothstep(sea_level_ - 0.5f, sea_level_ + 0.05f, position.y);
  if (IsInCave(position) || position.y < CaveRoofTop(ground) - 1.0f) {
    result.cave = 1.0f;
  }
  return result;
}

float WorldGenerator::SkyVisibility(glm::vec3 position,
                                    glm::vec3 surface_normal) const {
  static const Hemisphere kHemisphere;
  const float radii[] = {1.5f, 4.0f, 10.0f};
  const float weights[] = {1.0f, 0.8f, 0.6f};
  // Basis around the normal
  glm::vec3 up = surface_normal;
  glm::vec3 tangent = glm::normalize(
      std::abs(up.y) < 0.99f ? glm::cross(up, glm::vec3(0.0f, 1.0f, 0.0f))
                             : glm::cross(up, glm::vec3(1.0f, 0.0f, 0.0f)));
  glm::vec3 bitangent = glm::cross(up, tangent);
  glm::vec3 origin = position + 0.35f * surface_normal;
  float occluded = 0.0f, total = 0.0f;
  for (const glm::vec3& local : kHemisphere.directions) {
    glm::vec3 direction =
        local.x * tangent + local.y * up + local.z * bitangent;
    for (int r = 0; r < 3; ++r) {
      total += weights[r];
      if (Density(origin + direction * radii[r]) > 0.0f) {
        occluded += weights[r];
        break;  // the farther samples are hidden behind this one
      }
    }
  }
  // Every direction that is blocked close by also blocks the samples behind it
  return std::clamp(1.0f - occluded / total * 2.2f, 0.0f, 1.0f);
}

void WorldGenerator::ChooseCaves() {
  const std::vector<TrackPath::Sample>& samples = path_->GetSamples();
  if (samples.size() < 10) {
    return;
  }
  const float length = path_->GetLength();
  auto blocked = [&](const TrackPath::Sample& sample) {
    // Something of the track that is higher than the floor would be inside of
    // the roof
    for (const KeepClear& object : keep_clear_) {
      float horizontal =
          glm::distance(glm::vec2(object.center.x, object.center.z),
                        glm::vec2(sample.position.x, sample.position.z));
      // Rocks on the floor fit under a roof, signs higher up would be inside of
      // it
      bool above_floor =
          object.center.y + object.radius > sample.position.y + 4.5f;
      if (above_floor &&
          horizontal < std::max(sample.floor_left, sample.floor_right) + 16.0f +
                           object.radius) {
        return true;
      }
    }
    return std::min(sample.wall_left, sample.wall_right) < kCaveMinWalls;
  };
  std::vector<bool> usable(samples.size());
  for (std::size_t i = 0; i < samples.size(); ++i) {
    usable[i] = samples[i].distance > kCaveStartClearance &&
                samples[i].distance < length - kCaveStartClearance &&
                !blocked(samples[i]);
  }

  for (float cave_length : kCaveLengths) {
    int window = static_cast<int>(cave_length / TrackPath::kSampleSpacing);
    float best_score = -1.0f;
    int best_start = -1;
    for (int first = 0; first + window < static_cast<int>(samples.size());
         ++first) {
      float score = std::numeric_limits<float>::max();
      for (int i = first; i <= first + window && score > best_score; ++i) {
        if (!usable[i]) {
          score = -1.0f;
          break;
        }
        score = std::min(score,
                         std::min(samples[i].wall_left, samples[i].wall_right));
      }
      float middle = samples[first + window / 2].distance;
      for (const Cave& other : caves_) {
        // Apart from the other caves (also around the end of the lap)
        float apart = std::abs(middle - 0.5f * (other.start + other.end));
        if (std::min(apart, length - apart) <
            0.5f * (other.end - other.start + cave_length) + kCaveSeparation) {
          score = -1.0f;
        }
      }
      if (score > best_score) {
        best_score = score;
        best_start = first;
      }
    }
    if (best_start < 0) {
      continue;
    }
    Cave cave;
    cave.start = samples[best_start].distance;
    cave.end = samples[best_start + window].distance;
    float floor_sum = 0.0f, wall_sum = 0.0f;
    glm::vec3 low(std::numeric_limits<float>::max()),
        high(std::numeric_limits<float>::lowest());
    for (int i = best_start - 8; i <= best_start + window + 8; ++i) {
      const TrackPath::Sample& sample =
          samples[(i + samples.size()) % samples.size()];
      float reach =
          std::max(sample.floor_left, sample.floor_right) + 3.5f + 14.0f;
      low = glm::min(low, sample.position - glm::vec3(reach, 0.0f, reach));
      high = glm::max(high, sample.position + glm::vec3(reach, 0.0f, reach));
      if (i >= best_start && i <= best_start + window) {
        floor_sum += sample.position.y;
        wall_sum += std::min(sample.wall_left, sample.wall_right);
      }
    }
    cave.floor_height = floor_sum / static_cast<float>(window + 1);
    cave.ceiling_height = kCaveCeiling;
    cave.roof_height = std::clamp(
        wall_sum / static_cast<float>(window + 1) + 0.5f, 14.0f, 20.0f);
    cave.bounds.minimum = glm::vec3(low.x, low.y - 4.0f, low.z);
    cave.bounds.maximum =
        glm::vec3(high.x, high.y + cave.roof_height + 6.0f, high.z);
    caves_.push_back(cave);
  }
  std::sort(caves_.begin(), caves_.end(),
            [](const Cave& a, const Cave& b) { return a.start < b.start; });
}

void WorldGenerator::ComputeColumnRanges() {
  glm::vec2 low(bounds_.minimum.x, bounds_.minimum.z);
  range_columns_ = static_cast<int>(
      std::ceil((bounds_.maximum.x - bounds_.minimum.x) / kRangeCell));
  range_rows_ = static_cast<int>(
      std::ceil((bounds_.maximum.z - bounds_.minimum.z) / kRangeCell));
  column_ranges_.assign(static_cast<std::size_t>(range_columns_) * range_rows_,
                        glm::vec2(0.0f));
  // The rows in parallel
  ParallelFor(range_rows_, [&](int j) {
    for (int i = 0; i < range_columns_; ++i) {
      float lowest = std::numeric_limits<float>::max(),
            highest = std::numeric_limits<float>::lowest();
      float steepest = 0.0f;
      const int steps = 8;
      for (int sj = 0; sj <= steps; ++sj) {
        for (int si = 0; si <= steps; ++si) {
          glm::vec2 position =
              low + kRangeCell * glm::vec2(i + si / static_cast<float>(steps),
                                           j + sj / static_cast<float>(steps));
          float h = Height(position);
          lowest = std::min(lowest, h);
          highest = std::max(highest, h);
          float inland, cliff;
          Coast(position, inland, cliff);
          if (cliff > 0.02f && std::abs(inland) < 20.0f) {
            steepest = 8.0f;  // the noise of the cliff moves the surface
          }
          highest = std::max(highest, CaveRoofTop(position) + 6.0f);
        }
      }
      // Between the samples the height changes, the margin covers the slope
      // over a sample distance
      float margin = 3.0f + steepest + 0.25f * (highest - lowest);
      column_ranges_[static_cast<std::size_t>(j) * range_columns_ + i] =
          glm::vec2(lowest - margin, highest + margin);
    }
  });
}

bool WorldGenerator::MayContainSurface(const Box& box) const {
  int i0 = std::max(0, static_cast<int>(std::floor(
                           (box.minimum.x - bounds_.minimum.x) / kRangeCell)));
  int i1 = std::min(range_columns_ - 1,
                    static_cast<int>(std::floor(
                        (box.maximum.x - bounds_.minimum.x) / kRangeCell)));
  int j0 = std::max(0, static_cast<int>(std::floor(
                           (box.minimum.z - bounds_.minimum.z) / kRangeCell)));
  int j1 = std::min(range_rows_ - 1,
                    static_cast<int>(std::floor(
                        (box.maximum.z - bounds_.minimum.z) / kRangeCell)));
  if (i0 > i1 || j0 > j1) {
    return false;
  }
  float lowest = std::numeric_limits<float>::max(),
        highest = std::numeric_limits<float>::lowest();
  for (int j = j0; j <= j1; ++j) {
    for (int i = i0; i <= i1; ++i) {
      const glm::vec2& range =
          column_ranges_[static_cast<std::size_t>(j) * range_columns_ + i];
      lowest = std::min(lowest, range.x);
      highest = std::max(highest, range.y);
    }
  }
  return box.maximum.y >= lowest && box.minimum.y <= highest;
}

}  // namespace bibo::world
