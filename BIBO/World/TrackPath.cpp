#include "TrackPath.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

#include "Noise.hpp"

namespace bibo::world {

namespace {
// The path is searched on a grid with cells of this size (every second cell of
// the terrain)
const int kGridStep = 2;
const float kGridCell = TrackTerrain::kCellSize * kGridStep;
// The floor of the canyon is what is at most this much higher than the lowest
// point around
const float kFloorTolerance = 1.0f;
const float kFloorSearchRadius = 8.0f;

glm::vec2 RightOf(glm::vec2 direction) {
  // Driving direction (x, z) with y up: the right side is direction x up
  return glm::vec2(-direction.y, direction.x);
}

std::vector<glm::vec2> Resample(const std::vector<glm::vec2>& loop,
                                float spacing) {
  std::vector<float> distances(loop.size() + 1, 0.0f);
  for (std::size_t i = 0; i < loop.size(); ++i) {
    distances[i + 1] =
        distances[i] + glm::distance(loop[i], loop[(i + 1) % loop.size()]);
  }
  float total = distances.back();
  int count = std::max(3, static_cast<int>(std::round(total / spacing)));
  std::vector<glm::vec2> result;
  result.reserve(count);
  std::size_t segment = 0;
  for (int k = 0; k < count; ++k) {
    float d = total * static_cast<float>(k) / static_cast<float>(count);
    while (segment + 1 < loop.size() && distances[segment + 1] < d) {
      ++segment;
    }
    float segment_length =
        std::max(distances[segment + 1] - distances[segment], 1e-6f);
    float t = (d - distances[segment]) / segment_length;
    result.push_back(
        glm::mix(loop[segment], loop[(segment + 1) % loop.size()], t));
  }
  return result;
}

void Smooth(std::vector<glm::vec2>& loop, int iterations) {
  std::vector<glm::vec2> next(loop.size());
  for (int iteration = 0; iteration < iterations; ++iteration) {
    for (std::size_t i = 0; i < loop.size(); ++i) {
      const glm::vec2& previous = loop[(i + loop.size() - 1) % loop.size()];
      const glm::vec2& following = loop[(i + 1) % loop.size()];
      next[i] = 0.5f * loop[i] + 0.25f * (previous + following);
    }
    loop.swap(next);
  }
}

/**
 * Cheapest way through a grid of costs (Dijkstra, 8 neighbors)
 */
std::vector<int> CheapestPath(const std::vector<float>& cost, int columns,
                              int rows, int from, int to) {
  std::vector<float> best(cost.size(), std::numeric_limits<float>::max());
  std::vector<int> previous(cost.size(), -1);
  using Entry = std::pair<float, int>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
  best[from] = 0.0f;
  open.push({0.0f, from});
  const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
  const int dz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
  while (!open.empty()) {
    auto [total, cell] = open.top();
    open.pop();
    if (cell == to) {
      break;
    }
    if (total > best[cell]) {
      continue;
    }
    int i = cell % columns, j = cell / columns;
    for (int n = 0; n < 8; ++n) {
      int ni = i + dx[n], nj = j + dz[n];
      if (ni < 0 || nj < 0 || ni >= columns || nj >= rows) {
        continue;
      }
      int neighbor = nj * columns + ni;
      float step =
          (n < 4 ? 1.0f : 1.41421356f) * 0.5f * (cost[cell] + cost[neighbor]);
      if (total + step < best[neighbor]) {
        best[neighbor] = total + step;
        previous[neighbor] = cell;
        open.push({best[neighbor], neighbor});
      }
    }
  }
  std::vector<int> path;
  for (int cell = to; cell != -1; cell = previous[cell]) {
    path.push_back(cell);
    if (cell == from) {
      break;
    }
  }
  std::reverse(path.begin(), path.end());
  return path;
}
}  // namespace

TrackPath::TrackPath(const TrackTerrain& terrain, glm::vec2 start,
                     glm::vec2 start_direction,
                     const std::vector<glm::vec2>& waypoints,
                     float water_level) {
  // 1. Costs on a grid: cheap on the floor of the canyon, more expensive close
  // to the walls, very expensive up the walls
  const int columns = std::max(1, terrain.GetColumns() / kGridStep),
            rows = std::max(1, terrain.GetRows() / kGridStep);
  const glm::vec2 origin = terrain.GetOrigin();
  std::vector<float> heights(static_cast<std::size_t>(columns) * rows);
  for (int j = 0; j < rows; ++j) {
    for (int i = 0; i < columns; ++i) {
      heights[static_cast<std::size_t>(j) * columns + i] =
          terrain.HeightAt(i * kGridStep, j * kGridStep);
    }
  }
  // Lowest point around every cell (separable minimum filter)
  const int radius = static_cast<int>(kFloorSearchRadius / kGridCell);
  std::vector<float> row_minimum(heights.size()), floor_level(heights.size());
  for (int j = 0; j < rows; ++j) {
    for (int i = 0; i < columns; ++i) {
      float lowest = std::numeric_limits<float>::max();
      for (int k = std::max(0, i - radius);
           k <= std::min(columns - 1, i + radius); ++k) {
        lowest = std::min(lowest,
                          heights[static_cast<std::size_t>(j) * columns + k]);
      }
      row_minimum[static_cast<std::size_t>(j) * columns + i] = lowest;
    }
  }
  for (int j = 0; j < rows; ++j) {
    for (int i = 0; i < columns; ++i) {
      float lowest = std::numeric_limits<float>::max();
      for (int k = std::max(0, j - radius); k <= std::min(rows - 1, j + radius);
           ++k) {
        lowest = std::min(
            lowest, row_minimum[static_cast<std::size_t>(k) * columns + i]);
      }
      floor_level[static_cast<std::size_t>(j) * columns + i] = lowest;
    }
  }
  // Distance from the walls (chamfer distance transform of the floor)
  const float far_away = 1e6f;
  std::vector<float> wall_distance(heights.size());
  for (std::size_t c = 0; c < heights.size(); ++c) {
    wall_distance[c] =
        heights[c] - floor_level[c] < kFloorTolerance ? far_away : 0.0f;
  }
  auto relax = [&](int i, int j, int di, int dj, float step) {
    int ni = i + di, nj = j + dj;
    if (ni >= 0 && nj >= 0 && ni < columns && nj < rows) {
      float& value = wall_distance[static_cast<std::size_t>(j) * columns + i];
      value = std::min(
          value,
          wall_distance[static_cast<std::size_t>(nj) * columns + ni] + step);
    }
  };
  for (int j = 0; j < rows; ++j) {
    for (int i = 0; i < columns; ++i) {
      relax(i, j, -1, 0, 1.0f);
      relax(i, j, 0, -1, 1.0f);
      relax(i, j, -1, -1, 1.41421356f);
      relax(i, j, 1, -1, 1.41421356f);
    }
  }
  for (int j = rows - 1; j >= 0; --j) {
    for (int i = columns - 1; i >= 0; --i) {
      relax(i, j, 1, 0, 1.0f);
      relax(i, j, 0, 1, 1.0f);
      relax(i, j, 1, 1, 1.41421356f);
      relax(i, j, -1, 1, 1.41421356f);
    }
  }
  std::vector<float> cost(heights.size());
  for (std::size_t c = 0; c < heights.size(); ++c) {
    float above = std::max(0.0f, heights[c] - floor_level[c] - 0.4f);
    float distance_to_wall = std::min(wall_distance[c], 50.0f) * kGridCell;
    cost[c] = 1.0f + 30.0f * std::pow(above, 1.5f) +
              10.0f * std::exp(-0.6f * distance_to_wall);
  }

  // 2. Cheapest way from the start through all waypoints back to the start
  auto cell_of = [&](glm::vec2 position) {
    int i = std::clamp(
        static_cast<int>(std::round((position.x - origin.x) / kGridCell)), 0,
        columns - 1);
    int j = std::clamp(
        static_cast<int>(std::round((position.y - origin.y) / kGridCell)), 0,
        rows - 1);
    return j * columns + i;
  };
  std::vector<glm::vec2> stops = {start};
  stops.insert(stops.end(), waypoints.begin(), waypoints.end());
  stops.push_back(start);
  std::vector<glm::vec2> loop;
  for (std::size_t k = 0; k + 1 < stops.size(); ++k) {
    std::vector<int> cells = CheapestPath(
        cost, columns, rows, cell_of(stops[k]), cell_of(stops[k + 1]));
    for (std::size_t c = 0; c + 1 < cells.size();
         ++c)  // the last cell is the first of the next part
    {
      loop.push_back(origin +
                     kGridCell *
                         glm::vec2(static_cast<float>(cells[c] % columns),
                                   static_cast<float>(cells[c] / columns)));
    }
  }
  if (loop.size() < 3) {
    loop = {start, start + start_direction, start - start_direction};
  }

  // 3. Smooth the zigzag of the grid away and move the line into the middle of
  // the floor
  Smooth(loop, 40);
  loop = Resample(loop, kSampleSpacing);
  for (int pass = 0; pass < 3; ++pass) {
    std::vector<glm::vec2> centered(loop.size());
    for (std::size_t i = 0; i < loop.size(); ++i) {
      glm::vec2 direction =
          glm::normalize(loop[(i + 1) % loop.size()] -
                         loop[(i + loop.size() - 1) % loop.size()]);
      Sample sample;
      sample.position = glm::vec3(loop[i].x, 0.0f, loop[i].y);
      sample.tangent = direction;
      Measure(terrain, sample);
      centered[i] =
          loop[i] + RightOf(direction) *
                        (0.5f * (sample.floor_right - sample.floor_left));
    }
    loop = centered;
    Smooth(loop, 12);
    loop = Resample(loop, kSampleSpacing);
  }

  // 4. The first sample is at the start and the samples go in driving direction
  std::size_t first = 0;
  for (std::size_t i = 1; i < loop.size(); ++i) {
    if (glm::distance(loop[i], start) < glm::distance(loop[first], start)) {
      first = i;
    }
  }
  std::rotate(loop.begin(), loop.begin() + static_cast<std::ptrdiff_t>(first),
              loop.end());
  if (glm::dot(loop[1 % loop.size()] - loop[0], start_direction) < 0.0f) {
    std::reverse(loop.begin() + 1, loop.end());
  }

  samples_.resize(loop.size());
  float distance = 0.0f;
  for (std::size_t i = 0; i < loop.size(); ++i) {
    Sample& sample = samples_[i];
    glm::vec2 direction =
        loop[(i + 1) % loop.size()] - loop[(i + loop.size() - 1) % loop.size()];
    sample.tangent = glm::length(direction) > 1e-6f ? glm::normalize(direction)
                                                    : glm::vec2(0.0f, 1.0f);
    sample.position = glm::vec3(loop[i].x, terrain.Height(loop[i]), loop[i].y);
    sample.distance = distance;
    Measure(terrain, sample);
    distance += glm::distance(loop[i], loop[(i + 1) % loop.size()]);
  }
  length_ = distance;

  // The floor measured at a puddle is narrow (the water is lower than the floor
  // around it): the widest floor nearby counts
  const int window = 8;
  std::vector<Sample> measured = samples_;
  for (std::size_t i = 0; i < samples_.size(); ++i) {
    float left = 0.0f, right = 0.0f;
    for (int k = -window; k <= window; ++k) {
      const Sample& other =
          measured[(i + samples_.size() + k) % samples_.size()];
      left = std::max(left, other.floor_left);
      right = std::max(right, other.floor_right);
    }
    samples_[i].floor_left = left;
    samples_[i].floor_right = right;
  }

  // The trail winds on the floor (but stays on it)
  std::vector<float> offsets(samples_.size());
  for (std::size_t i = 0; i < samples_.size(); ++i) {
    const Sample& sample = samples_[i];
    float room =
        std::max(0.0f, std::min(sample.floor_left, sample.floor_right) -
                           kTrailHalfWidth - 1.0f);
    float wave =
        noise::Fbm(glm::vec2(sample.distance * 0.018f, 3.7f), 2, 4711u);
    offsets[i] = std::clamp(wave * 1.6f, -1.0f, 1.0f) * std::min(room, 3.0f);
  }
  for (int iteration = 0; iteration < 8; ++iteration) {
    std::vector<float> next(offsets.size());
    for (std::size_t i = 0; i < offsets.size(); ++i) {
      next[i] = 0.5f * offsets[i] +
                0.25f * (offsets[(i + offsets.size() - 1) % offsets.size()] +
                         offsets[(i + 1) % offsets.size()]);
    }
    offsets.swap(next);
  }
  AvoidWater(terrain, water_level, offsets);
  for (std::size_t i = 0; i < samples_.size(); ++i) {
    samples_[i].trail_offset = offsets[i];
  }
  BuildBuckets();
}

bool TrackPath::DryTrail(const TrackTerrain& terrain, float water_level,
                         const Sample& sample, float offset,
                         float half_width) const {
  glm::vec2 center(sample.position.x, sample.position.z);
  glm::vec2 right = RightOf(sample.tangent);
  for (float lateral = offset - half_width;
       lateral <= offset + half_width + 1e-3f; lateral += 0.25f) {
    if (terrain.Height(center + right * lateral) < water_level + 0.05f) {
      return false;
    }
  }
  return true;
}

void TrackPath::AvoidWater(const TrackTerrain& terrain, float water_level,
                           std::vector<float>& offsets) const {
  // Where a pool of water lies on the floor, the trail goes around it on the
  // dry side (if the floor has room there). Pools that fill the whole floor are
  // crossed (the race jumps over them). The trail swerves in a smooth curve:
  // the shift is fixed at the pool and fades out along the track before and
  // after it (screened diffusion).
  const std::size_t count = samples_.size();
  const float trail_clearance = kTrailHalfWidth + 0.2f;
  std::vector<float> shift(count, 0.0f);
  std::vector<bool> fixed(count, false);
  std::vector<float> fixed_shift(count, 0.0f);
  for (int round = 0; round < 6; ++round) {
    bool changed = false;
    // The closest dry place on the floor for the samples where the trail is wet
    // (the trail may reach over the edge of the floor, where the ground only
    // begins to rise), NaN if there is none. Where the dry side is narrow it
    // may come closer to the water.
    const float none = std::numeric_limits<float>::quiet_NaN();
    std::vector<bool> wet(count, false);
    std::vector<float> dry_offset(count, none);
    for (std::size_t i = 0; i < count; ++i) {
      const Sample& sample = samples_[i];
      float offset = offsets[i] + shift[i];
      wet[i] = !DryTrail(terrain, water_level, sample, offset, kTrailHalfWidth);
      for (float clearance : {trail_clearance, kTrailHalfWidth - 0.4f}) {
        float best_distance = std::numeric_limits<float>::max();
        for (float candidate = -sample.floor_left + kTrailHalfWidth - 1.5f;
             wet[i] && std::isnan(dry_offset[i]) &&
             candidate <= sample.floor_right - kTrailHalfWidth + 1.5f;
             candidate += 0.25f) {
          if (std::abs(candidate - offset) < best_distance &&
              DryTrail(terrain, water_level, sample, candidate, clearance)) {
            dry_offset[i] = candidate;
            best_distance = std::abs(candidate - offset);
          }
        }
      }
    }
    // A stretch of wet trail goes around the pool if (almost) every sample of
    // it has a dry place on the same side, the gaps are interpolated. Otherwise
    // the pool fills the floor and the trail goes straight through (the race
    // jumps over it). The start of the lap is never in the water.
    for (std::size_t first = 0; first < count;) {
      if (!wet[first]) {
        ++first;
        continue;
      }
      std::size_t end = first;
      int missing = 0;
      bool left = false, right = false;
      while (end < count && wet[end]) {
        missing += std::isnan(dry_offset[end]) ? 1 : 0;
        left = left || dry_offset[end] < -0.5f;
        right = right || dry_offset[end] > 0.5f;
        ++end;
      }
      if (left == right || static_cast<float>(missing) >
                               0.3f * static_cast<float>(end - first)) {
        first = end;
        continue;
      }
      for (std::size_t i = first; i < end; ++i) {
        float target = dry_offset[i];
        if (std::isnan(target)) {
          // Between the closest samples with a dry place
          std::size_t before = i, after = i;
          while (before > first && std::isnan(dry_offset[before])) {
            --before;
          }
          while (after + 1 < end && std::isnan(dry_offset[after])) {
            ++after;
          }
          float a = std::isnan(dry_offset[before]) ? dry_offset[after]
                                                   : dry_offset[before];
          float b = std::isnan(dry_offset[after]) ? a : dry_offset[after];
          target = after > before
                       ? glm::mix(a, b,
                                  static_cast<float>(i - before) /
                                      static_cast<float>(after - before))
                       : a;
        }
        if (!fixed[i] ||
            std::abs(fixed_shift[i] - (target - offsets[i])) > 0.1f) {
          fixed[i] = true;
          fixed_shift[i] = target - offsets[i];
          changed = true;
        }
      }
      first = end;
    }
    if (!changed) {
      break;
    }
    // Fades out over about 10 m
    std::vector<float> next(count);
    for (int iteration = 0; iteration < 1500; ++iteration) {
      for (std::size_t i = 0; i < count; ++i) {
        next[i] = fixed[i]
                      ? fixed_shift[i]
                      : 0.9985f * (0.5f * shift[i] +
                                   0.25f * (shift[(i + count - 1) % count] +
                                            shift[(i + 1) % count]));
      }
      shift.swap(next);
    }
  }
  for (std::size_t i = 0; i < count; ++i) {
    offsets[i] += shift[i];
  }
}

void TrackPath::Measure(const TrackTerrain& terrain, Sample& sample) const {
  glm::vec2 center(sample.position.x, sample.position.z);
  glm::vec2 right = RightOf(sample.tangent);
  const float step = 0.25f;
  // The lowest point of the floor close to the line
  float floor_height = std::numeric_limits<float>::max();
  for (float offset = -6.0f; offset <= 6.0f; offset += step) {
    floor_height =
        std::min(floor_height, terrain.Height(center + right * offset));
  }
  auto floor_width = [&](float side) {
    float offset = 0.0f;
    while (offset < 25.0f &&
           terrain.Height(center + right * (side * (offset + step))) <
               floor_height + kFloorTolerance) {
      offset += step;
    }
    return offset;
  };
  sample.floor_right = floor_width(1.0f);
  sample.floor_left = floor_width(-1.0f);
  auto wall_height = [&](float side, float floor_edge) {
    float highest = floor_height;
    for (float offset = floor_edge; offset <= floor_edge + 20.0f;
         offset += 1.0f) {
      highest =
          std::max(highest, terrain.Height(center + right * (side * offset)));
    }
    return highest - floor_height;
  };
  sample.wall_right = wall_height(1.0f, sample.floor_right);
  sample.wall_left = wall_height(-1.0f, sample.floor_left);
}

void TrackPath::BuildBuckets() {
  glm::vec2 low(std::numeric_limits<float>::max()),
      high(std::numeric_limits<float>::lowest());
  for (const Sample& sample : samples_) {
    low = glm::min(low, glm::vec2(sample.position.x, sample.position.z));
    high = glm::max(high, glm::vec2(sample.position.x, sample.position.z));
  }
  bucket_origin_ = low - glm::vec2(kSearchRadius);
  bucket_columns_ =
      static_cast<int>((high.x - low.x + 2.0f * kSearchRadius) / kBucketSize) +
      1;
  bucket_rows_ =
      static_cast<int>((high.y - low.y + 2.0f * kSearchRadius) / kBucketSize) +
      1;
  buckets_.assign(static_cast<std::size_t>(bucket_columns_) * bucket_rows_, {});
  for (std::size_t i = 0; i < samples_.size(); ++i) {
    glm::vec2 a(samples_[i].position.x, samples_[i].position.z);
    const Sample& next = samples_[(i + 1) % samples_.size()];
    glm::vec2 b(next.position.x, next.position.z);
    glm::vec2 segment_low =
        glm::min(a, b) - glm::vec2(kSearchRadius) - bucket_origin_;
    glm::vec2 segment_high =
        glm::max(a, b) + glm::vec2(kSearchRadius) - bucket_origin_;
    int i0 = std::max(0, static_cast<int>(segment_low.x / kBucketSize)),
        i1 = std::min(bucket_columns_ - 1,
                      static_cast<int>(segment_high.x / kBucketSize));
    int j0 = std::max(0, static_cast<int>(segment_low.y / kBucketSize)),
        j1 = std::min(bucket_rows_ - 1,
                      static_cast<int>(segment_high.y / kBucketSize));
    for (int j = j0; j <= j1; ++j) {
      for (int k = i0; k <= i1; ++k) {
        buckets_[static_cast<std::size_t>(j) * bucket_columns_ + k].push_back(
            static_cast<int>(i));
      }
    }
  }
}

TrackPath::Sample TrackPath::At(float distance) const {
  if (samples_.empty()) {
    return Sample();
  }
  distance = std::fmod(distance, length_);
  if (distance < 0.0f) {
    distance += length_;
  }
  // The samples are evenly spaced
  float index = distance / length_ * static_cast<float>(samples_.size());
  std::size_t i =
      std::min(static_cast<std::size_t>(index), samples_.size() - 1);
  const Sample& a = samples_[i];
  const Sample& b = samples_[(i + 1) % samples_.size()];
  float t = index - static_cast<float>(i);
  Sample result = a;
  result.position = glm::mix(a.position, b.position, t);
  glm::vec2 tangent = glm::mix(a.tangent, b.tangent, t);
  result.tangent =
      glm::length(tangent) > 1e-6f ? glm::normalize(tangent) : a.tangent;
  result.distance = distance;
  result.floor_left = glm::mix(a.floor_left, b.floor_left, t);
  result.floor_right = glm::mix(a.floor_right, b.floor_right, t);
  result.wall_left = glm::mix(a.wall_left, b.wall_left, t);
  result.wall_right = glm::mix(a.wall_right, b.wall_right, t);
  result.trail_offset = glm::mix(a.trail_offset, b.trail_offset, t);
  return result;
}

TrackPath::Nearest TrackPath::FindNearest(glm::vec2 position) const {
  Nearest result;
  glm::vec2 local = position - bucket_origin_;
  int i = static_cast<int>(std::floor(local.x / kBucketSize)),
      j = static_cast<int>(std::floor(local.y / kBucketSize));
  if (i < 0 || j < 0 || i >= bucket_columns_ || j >= bucket_rows_) {
    return result;
  }
  float best_squared = kSearchRadius * kSearchRadius;
  int best_segment = -1;
  float best_t = 0.0f;
  for (int segment :
       buckets_[static_cast<std::size_t>(j) * bucket_columns_ + i]) {
    const Sample& a = samples_[segment];
    const Sample& b = samples_[(segment + 1) % samples_.size()];
    glm::vec2 pa(a.position.x, a.position.z), pb(b.position.x, b.position.z);
    glm::vec2 ab = pb - pa;
    float t = std::clamp(
        glm::dot(position - pa, ab) / std::max(glm::dot(ab, ab), 1e-9f), 0.0f,
        1.0f);
    glm::vec2 offset = position - (pa + t * ab);
    float squared = glm::dot(offset, offset);
    if (squared < best_squared) {
      best_squared = squared;
      best_segment = segment;
      best_t = t;
    }
  }
  if (best_segment < 0) {
    return result;
  }
  const Sample& a = samples_[best_segment];
  const Sample& b = samples_[(best_segment + 1) % samples_.size()];
  glm::vec2 pa(a.position.x, a.position.z), pb(b.position.x, b.position.z);
  glm::vec2 closest = glm::mix(pa, pb, best_t);
  glm::vec2 direction =
      glm::length(pb - pa) > 1e-6f ? glm::normalize(pb - pa) : a.tangent;
  result.found = true;
  result.lateral = glm::dot(position - closest, RightOf(direction));
  float trail_offset = glm::mix(a.trail_offset, b.trail_offset, best_t);
  result.trail_lateral = result.lateral - trail_offset;
  result.distance = a.distance + best_t * glm::distance(pa, pb);
  result.floor_half_width = result.lateral >= 0.0f
                                ? glm::mix(a.floor_right, b.floor_right, best_t)
                                : glm::mix(a.floor_left, b.floor_left, best_t);
  result.floor_height = glm::mix(a.position.y, b.position.y, best_t);
  return result;
}

}  // namespace bibo::world
