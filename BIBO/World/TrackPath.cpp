#include "TrackPath.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

#include "Noise.hpp"

using namespace bibo::world;

namespace {
// The path is searched on a grid with cells of this size (every second cell of
// the terrain)
const int GRID_STEP = 2;
const float GRID_CELL = TrackTerrain::CELL_SIZE * GRID_STEP;
// The floor of the canyon is what is at most this much higher than the lowest
// point around
const float FLOOR_TOLERANCE = 1.0f;
const float FLOOR_SEARCH_RADIUS = 8.0f;

glm::vec2 rightOf(glm::vec2 direction) {
  // Driving direction (x, z) with y up: the right side is direction x up
  return glm::vec2(-direction.y, direction.x);
}

std::vector<glm::vec2> resample(const std::vector<glm::vec2>& loop,
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
    float segmentLength =
        std::max(distances[segment + 1] - distances[segment], 1e-6f);
    float t = (d - distances[segment]) / segmentLength;
    result.push_back(
        glm::mix(loop[segment], loop[(segment + 1) % loop.size()], t));
  }
  return result;
}

void smooth(std::vector<glm::vec2>& loop, int iterations) {
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
std::vector<int> cheapestPath(const std::vector<float>& cost, int columns,
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
                     glm::vec2 startDirection,
                     const std::vector<glm::vec2>& waypoints,
                     float waterLevel) {
  // 1. Costs on a grid: cheap on the floor of the canyon, more expensive close
  // to the walls, very expensive up the walls
  const int columns = std::max(1, terrain.getColumns() / GRID_STEP),
            rows = std::max(1, terrain.getRows() / GRID_STEP);
  const glm::vec2 origin = terrain.getOrigin();
  std::vector<float> heights(static_cast<std::size_t>(columns) * rows);
  for (int j = 0; j < rows; ++j) {
    for (int i = 0; i < columns; ++i) {
      heights[static_cast<std::size_t>(j) * columns + i] =
          terrain.heightAt(i * GRID_STEP, j * GRID_STEP);
    }
  }
  // Lowest point around every cell (separable minimum filter)
  const int radius = static_cast<int>(FLOOR_SEARCH_RADIUS / GRID_CELL);
  std::vector<float> rowMinimum(heights.size()), floorLevel(heights.size());
  for (int j = 0; j < rows; ++j) {
    for (int i = 0; i < columns; ++i) {
      float lowest = std::numeric_limits<float>::max();
      for (int k = std::max(0, i - radius);
           k <= std::min(columns - 1, i + radius); ++k) {
        lowest = std::min(lowest,
                          heights[static_cast<std::size_t>(j) * columns + k]);
      }
      rowMinimum[static_cast<std::size_t>(j) * columns + i] = lowest;
    }
  }
  for (int j = 0; j < rows; ++j) {
    for (int i = 0; i < columns; ++i) {
      float lowest = std::numeric_limits<float>::max();
      for (int k = std::max(0, j - radius); k <= std::min(rows - 1, j + radius);
           ++k) {
        lowest = std::min(
            lowest, rowMinimum[static_cast<std::size_t>(k) * columns + i]);
      }
      floorLevel[static_cast<std::size_t>(j) * columns + i] = lowest;
    }
  }
  // Distance from the walls (chamfer distance transform of the floor)
  const float FAR_AWAY = 1e6f;
  std::vector<float> wallDistance(heights.size());
  for (std::size_t c = 0; c < heights.size(); ++c) {
    wallDistance[c] =
        heights[c] - floorLevel[c] < FLOOR_TOLERANCE ? FAR_AWAY : 0.0f;
  }
  auto relax = [&](int i, int j, int di, int dj, float step) {
    int ni = i + di, nj = j + dj;
    if (ni >= 0 && nj >= 0 && ni < columns && nj < rows) {
      float& value = wallDistance[static_cast<std::size_t>(j) * columns + i];
      value = std::min(
          value,
          wallDistance[static_cast<std::size_t>(nj) * columns + ni] + step);
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
    float above = std::max(0.0f, heights[c] - floorLevel[c] - 0.4f);
    float distanceToWall = std::min(wallDistance[c], 50.0f) * GRID_CELL;
    cost[c] = 1.0f + 30.0f * std::pow(above, 1.5f) +
              10.0f * std::exp(-0.6f * distanceToWall);
  }

  // 2. Cheapest way from the start through all waypoints back to the start
  auto cellOf = [&](glm::vec2 position) {
    int i = std::clamp(
        static_cast<int>(std::round((position.x - origin.x) / GRID_CELL)), 0,
        columns - 1);
    int j = std::clamp(
        static_cast<int>(std::round((position.y - origin.y) / GRID_CELL)), 0,
        rows - 1);
    return j * columns + i;
  };
  std::vector<glm::vec2> stops = {start};
  stops.insert(stops.end(), waypoints.begin(), waypoints.end());
  stops.push_back(start);
  std::vector<glm::vec2> loop;
  for (std::size_t k = 0; k + 1 < stops.size(); ++k) {
    std::vector<int> cells = cheapestPath(cost, columns, rows, cellOf(stops[k]),
                                          cellOf(stops[k + 1]));
    for (std::size_t c = 0; c + 1 < cells.size();
         ++c)  // the last cell is the first of the next part
    {
      loop.push_back(origin +
                     GRID_CELL *
                         glm::vec2(static_cast<float>(cells[c] % columns),
                                   static_cast<float>(cells[c] / columns)));
    }
  }
  if (loop.size() < 3) {
    loop = {start, start + startDirection, start - startDirection};
  }

  // 3. Smooth the zigzag of the grid away and move the line into the middle of
  // the floor
  smooth(loop, 40);
  loop = resample(loop, SAMPLE_SPACING);
  for (int pass = 0; pass < 3; ++pass) {
    std::vector<glm::vec2> centered(loop.size());
    for (std::size_t i = 0; i < loop.size(); ++i) {
      glm::vec2 direction =
          glm::normalize(loop[(i + 1) % loop.size()] -
                         loop[(i + loop.size() - 1) % loop.size()]);
      Sample sample;
      sample.position = glm::vec3(loop[i].x, 0.0f, loop[i].y);
      sample.tangent = direction;
      measure(terrain, sample);
      centered[i] =
          loop[i] +
          rightOf(direction) * (0.5f * (sample.floorRight - sample.floorLeft));
    }
    loop = centered;
    smooth(loop, 12);
    loop = resample(loop, SAMPLE_SPACING);
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
  if (glm::dot(loop[1 % loop.size()] - loop[0], startDirection) < 0.0f) {
    std::reverse(loop.begin() + 1, loop.end());
  }

  samples.resize(loop.size());
  float distance = 0.0f;
  for (std::size_t i = 0; i < loop.size(); ++i) {
    Sample& sample = samples[i];
    glm::vec2 direction =
        loop[(i + 1) % loop.size()] - loop[(i + loop.size() - 1) % loop.size()];
    sample.tangent = glm::length(direction) > 1e-6f ? glm::normalize(direction)
                                                    : glm::vec2(0.0f, 1.0f);
    sample.position = glm::vec3(loop[i].x, terrain.height(loop[i]), loop[i].y);
    sample.distance = distance;
    measure(terrain, sample);
    distance += glm::distance(loop[i], loop[(i + 1) % loop.size()]);
  }
  length = distance;

  // The floor measured at a puddle is narrow (the water is lower than the floor
  // around it): the widest floor nearby counts
  const int WINDOW = 8;
  std::vector<Sample> measured = samples;
  for (std::size_t i = 0; i < samples.size(); ++i) {
    float left = 0.0f, right = 0.0f;
    for (int k = -WINDOW; k <= WINDOW; ++k) {
      const Sample& other = measured[(i + samples.size() + k) % samples.size()];
      left = std::max(left, other.floorLeft);
      right = std::max(right, other.floorRight);
    }
    samples[i].floorLeft = left;
    samples[i].floorRight = right;
  }

  // The trail winds on the floor (but stays on it)
  std::vector<float> offsets(samples.size());
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const Sample& sample = samples[i];
    float room = std::max(0.0f, std::min(sample.floorLeft, sample.floorRight) -
                                    TRAIL_HALF_WIDTH - 1.0f);
    float wave =
        noise::fbm(glm::vec2(sample.distance * 0.018f, 3.7f), 2, 4711u);
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
  avoidWater(terrain, waterLevel, offsets);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    samples[i].trailOffset = offsets[i];
  }
  buildBuckets();
}

bool TrackPath::dryTrail(const TrackTerrain& terrain, float waterLevel,
                         const Sample& sample, float offset,
                         float halfWidth) const {
  glm::vec2 center(sample.position.x, sample.position.z);
  glm::vec2 right = rightOf(sample.tangent);
  for (float lateral = offset - halfWidth;
       lateral <= offset + halfWidth + 1e-3f; lateral += 0.25f) {
    if (terrain.height(center + right * lateral) < waterLevel + 0.05f) {
      return false;
    }
  }
  return true;
}

void TrackPath::avoidWater(const TrackTerrain& terrain, float waterLevel,
                           std::vector<float>& offsets) const {
  // Where a pool of water lies on the floor, the trail goes around it on the
  // dry side (if the floor has room there). Pools that fill the whole floor are
  // crossed (the race jumps over them). The trail swerves in a smooth curve:
  // the shift is fixed at the pool and fades out along the track before and
  // after it (screened diffusion).
  const std::size_t count = samples.size();
  const float CLEARANCE = TRAIL_HALF_WIDTH + 0.2f;
  std::vector<float> shift(count, 0.0f);
  std::vector<bool> fixed(count, false);
  std::vector<float> fixedShift(count, 0.0f);
  for (int round = 0; round < 6; ++round) {
    bool changed = false;
    // The closest dry place on the floor for the samples where the trail is wet
    // (the trail may reach over the edge of the floor, where the ground only
    // begins to rise), NaN if there is none. Where the dry side is narrow it
    // may come closer to the water.
    const float NONE = std::numeric_limits<float>::quiet_NaN();
    std::vector<bool> wet(count, false);
    std::vector<float> dryOffset(count, NONE);
    for (std::size_t i = 0; i < count; ++i) {
      const Sample& sample = samples[i];
      float offset = offsets[i] + shift[i];
      wet[i] = !dryTrail(terrain, waterLevel, sample, offset, TRAIL_HALF_WIDTH);
      for (float clearance : {CLEARANCE, TRAIL_HALF_WIDTH - 0.4f}) {
        float bestDistance = std::numeric_limits<float>::max();
        for (float candidate = -sample.floorLeft + TRAIL_HALF_WIDTH - 1.5f;
             wet[i] && std::isnan(dryOffset[i]) &&
             candidate <= sample.floorRight - TRAIL_HALF_WIDTH + 1.5f;
             candidate += 0.25f) {
          if (std::abs(candidate - offset) < bestDistance &&
              dryTrail(terrain, waterLevel, sample, candidate, clearance)) {
            dryOffset[i] = candidate;
            bestDistance = std::abs(candidate - offset);
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
        missing += std::isnan(dryOffset[end]) ? 1 : 0;
        left = left || dryOffset[end] < -0.5f;
        right = right || dryOffset[end] > 0.5f;
        ++end;
      }
      if (left == right || static_cast<float>(missing) >
                               0.3f * static_cast<float>(end - first)) {
        first = end;
        continue;
      }
      for (std::size_t i = first; i < end; ++i) {
        float target = dryOffset[i];
        if (std::isnan(target)) {
          // Between the closest samples with a dry place
          std::size_t before = i, after = i;
          while (before > first && std::isnan(dryOffset[before])) {
            --before;
          }
          while (after + 1 < end && std::isnan(dryOffset[after])) {
            ++after;
          }
          float a = std::isnan(dryOffset[before]) ? dryOffset[after]
                                                  : dryOffset[before];
          float b = std::isnan(dryOffset[after]) ? a : dryOffset[after];
          target = after > before
                       ? glm::mix(a, b,
                                  static_cast<float>(i - before) /
                                      static_cast<float>(after - before))
                       : a;
        }
        if (!fixed[i] ||
            std::abs(fixedShift[i] - (target - offsets[i])) > 0.1f) {
          fixed[i] = true;
          fixedShift[i] = target - offsets[i];
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
                      ? fixedShift[i]
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

void TrackPath::measure(const TrackTerrain& terrain, Sample& sample) const {
  glm::vec2 center(sample.position.x, sample.position.z);
  glm::vec2 right = rightOf(sample.tangent);
  const float STEP = 0.25f;
  // The lowest point of the floor close to the line
  float floorHeight = std::numeric_limits<float>::max();
  for (float offset = -6.0f; offset <= 6.0f; offset += STEP) {
    floorHeight =
        std::min(floorHeight, terrain.height(center + right * offset));
  }
  auto floorWidth = [&](float side) {
    float offset = 0.0f;
    while (offset < 25.0f &&
           terrain.height(center + right * (side * (offset + STEP))) <
               floorHeight + FLOOR_TOLERANCE) {
      offset += STEP;
    }
    return offset;
  };
  sample.floorRight = floorWidth(1.0f);
  sample.floorLeft = floorWidth(-1.0f);
  auto wallHeight = [&](float side, float floorEdge) {
    float highest = floorHeight;
    for (float offset = floorEdge; offset <= floorEdge + 20.0f;
         offset += 1.0f) {
      highest =
          std::max(highest, terrain.height(center + right * (side * offset)));
    }
    return highest - floorHeight;
  };
  sample.wallRight = wallHeight(1.0f, sample.floorRight);
  sample.wallLeft = wallHeight(-1.0f, sample.floorLeft);
}

void TrackPath::buildBuckets() {
  glm::vec2 low(std::numeric_limits<float>::max()),
      high(std::numeric_limits<float>::lowest());
  for (const Sample& sample : samples) {
    low = glm::min(low, glm::vec2(sample.position.x, sample.position.z));
    high = glm::max(high, glm::vec2(sample.position.x, sample.position.z));
  }
  bucketOrigin = low - glm::vec2(SEARCH_RADIUS);
  bucketColumns =
      static_cast<int>((high.x - low.x + 2.0f * SEARCH_RADIUS) / BUCKET_SIZE) +
      1;
  bucketRows =
      static_cast<int>((high.y - low.y + 2.0f * SEARCH_RADIUS) / BUCKET_SIZE) +
      1;
  buckets.assign(static_cast<std::size_t>(bucketColumns) * bucketRows, {});
  for (std::size_t i = 0; i < samples.size(); ++i) {
    glm::vec2 a(samples[i].position.x, samples[i].position.z);
    const Sample& next = samples[(i + 1) % samples.size()];
    glm::vec2 b(next.position.x, next.position.z);
    glm::vec2 segmentLow =
        glm::min(a, b) - glm::vec2(SEARCH_RADIUS) - bucketOrigin;
    glm::vec2 segmentHigh =
        glm::max(a, b) + glm::vec2(SEARCH_RADIUS) - bucketOrigin;
    int i0 = std::max(0, static_cast<int>(segmentLow.x / BUCKET_SIZE)),
        i1 = std::min(bucketColumns - 1,
                      static_cast<int>(segmentHigh.x / BUCKET_SIZE));
    int j0 = std::max(0, static_cast<int>(segmentLow.y / BUCKET_SIZE)),
        j1 = std::min(bucketRows - 1,
                      static_cast<int>(segmentHigh.y / BUCKET_SIZE));
    for (int j = j0; j <= j1; ++j) {
      for (int k = i0; k <= i1; ++k) {
        buckets[static_cast<std::size_t>(j) * bucketColumns + k].push_back(
            static_cast<int>(i));
      }
    }
  }
}

TrackPath::Sample TrackPath::at(float distance) const {
  if (samples.empty()) {
    return Sample();
  }
  distance = std::fmod(distance, length);
  if (distance < 0.0f) {
    distance += length;
  }
  // The samples are evenly spaced
  float index = distance / length * static_cast<float>(samples.size());
  std::size_t i = std::min(static_cast<std::size_t>(index), samples.size() - 1);
  const Sample& a = samples[i];
  const Sample& b = samples[(i + 1) % samples.size()];
  float t = index - static_cast<float>(i);
  Sample result = a;
  result.position = glm::mix(a.position, b.position, t);
  glm::vec2 tangent = glm::mix(a.tangent, b.tangent, t);
  result.tangent =
      glm::length(tangent) > 1e-6f ? glm::normalize(tangent) : a.tangent;
  result.distance = distance;
  result.floorLeft = glm::mix(a.floorLeft, b.floorLeft, t);
  result.floorRight = glm::mix(a.floorRight, b.floorRight, t);
  result.wallLeft = glm::mix(a.wallLeft, b.wallLeft, t);
  result.wallRight = glm::mix(a.wallRight, b.wallRight, t);
  result.trailOffset = glm::mix(a.trailOffset, b.trailOffset, t);
  return result;
}

TrackPath::Nearest TrackPath::nearest(glm::vec2 position) const {
  Nearest result;
  glm::vec2 local = position - bucketOrigin;
  int i = static_cast<int>(std::floor(local.x / BUCKET_SIZE)),
      j = static_cast<int>(std::floor(local.y / BUCKET_SIZE));
  if (i < 0 || j < 0 || i >= bucketColumns || j >= bucketRows) {
    return result;
  }
  float bestSquared = SEARCH_RADIUS * SEARCH_RADIUS;
  int bestSegment = -1;
  float bestT = 0.0f;
  for (int segment : buckets[static_cast<std::size_t>(j) * bucketColumns + i]) {
    const Sample& a = samples[segment];
    const Sample& b = samples[(segment + 1) % samples.size()];
    glm::vec2 pa(a.position.x, a.position.z), pb(b.position.x, b.position.z);
    glm::vec2 ab = pb - pa;
    float t = std::clamp(
        glm::dot(position - pa, ab) / std::max(glm::dot(ab, ab), 1e-9f), 0.0f,
        1.0f);
    glm::vec2 offset = position - (pa + t * ab);
    float squared = glm::dot(offset, offset);
    if (squared < bestSquared) {
      bestSquared = squared;
      bestSegment = segment;
      bestT = t;
    }
  }
  if (bestSegment < 0) {
    return result;
  }
  const Sample& a = samples[bestSegment];
  const Sample& b = samples[(bestSegment + 1) % samples.size()];
  glm::vec2 pa(a.position.x, a.position.z), pb(b.position.x, b.position.z);
  glm::vec2 closest = glm::mix(pa, pb, bestT);
  glm::vec2 direction =
      glm::length(pb - pa) > 1e-6f ? glm::normalize(pb - pa) : a.tangent;
  result.found = true;
  result.lateral = glm::dot(position - closest, rightOf(direction));
  float trailOffset = glm::mix(a.trailOffset, b.trailOffset, bestT);
  result.trailLateral = result.lateral - trailOffset;
  result.distance = a.distance + bestT * glm::distance(pa, pb);
  result.floorHalfWidth = result.lateral >= 0.0f
                              ? glm::mix(a.floorRight, b.floorRight, bestT)
                              : glm::mix(a.floorLeft, b.floorLeft, bestT);
  result.floorHeight = glm::mix(a.position.y, b.position.y, bestT);
  return result;
}
