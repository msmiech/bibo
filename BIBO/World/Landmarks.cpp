#include "Landmarks.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <queue>

#include "Noise.hpp"
#include "Parallel.hpp"
#include "WorldGenerator.hpp"

namespace bibo::world {

namespace {
const float kPi = std::numbers::pi_v<float>;
// The paths are searched on a grid with cells of this size
const float kRouteCell = 2.0f;
// A cell that a path must not go through (sea, caves, the objects of the track)
const float kForbidden = 1e4f;
// Further from the middle of a path the ground is not changed (the widest cut
// with its slopes)
const float kReach = 12.0f;
// Parts of a path that are further apart along it than this are different legs
// (e.g. the two sides of a hairpin bend)
const int kLegLength = 30;
// index of a point of a path: path * kPathStride + point
const int kPathStride = 1000000;
// No path joins the track close to the start and finish
const float kStartClearance = 30.0f;
// Paths leave the track at least this far apart. A path branches off from
// another one if that is much cheaper than the way from the track.
const float kTrailheadSpacing = 25.0f, kBranchRatio = 0.45f;
// Beside the floor of the track (on the side of its canyon) a step costs this
// much more per unit
const float kCanyonBand = 6.0f, kCanyonPenalty = 10.0f;
// Along a path the search prefers to climb at most this much per unit (the
// paths may go up to kMaxGrade)
const float kPreferredGrade = 0.3f;
// The ground around the summit is blurred over this distance, the crags fade
// out within this radius
const float kSofteningBlur = 3.0f, kSummitSoftening = 34.0f;
// Ground steeper than this to the cells around a cell (up to kSteepCells away)
// makes a path through it more expensive
const float kSteepSlope = 0.7f;
const int kSteepCells = 2;

float Smoothstep(float edge0, float edge1, float x) {
  float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float AngleOf(glm::vec2 offset) { return std::atan2(offset.y, offset.x); }

float AngleBetween(float a, float b) {
  float difference = std::fmod(std::abs(a - b), 2.0f * kPi);
  return std::min(difference, 2.0f * kPi - difference);
}

glm::vec2 Ground(glm::vec3 position) {
  return glm::vec2(position.x, position.z);
}

std::vector<glm::vec2> ResampleLine(const std::vector<glm::vec2>& line,
                                    float spacing) {
  std::vector<float> distances(line.size(), 0.0f);
  for (std::size_t i = 1; i < line.size(); ++i) {
    distances[i] = distances[i - 1] + glm::distance(line[i - 1], line[i]);
  }
  float total = distances.back();
  int count = std::max(1, static_cast<int>(std::round(total / spacing)));
  std::vector<glm::vec2> result;
  std::size_t segment = 0;
  for (int k = 0; k <= count; ++k) {
    float d = total * static_cast<float>(k) / static_cast<float>(count);
    while (segment + 2 < line.size() && distances[segment + 1] < d) {
      ++segment;
    }
    float length = std::max(distances[segment + 1] - distances[segment], 1e-6f);
    result.push_back(
        glm::mix(line[segment], line[segment + 1],
                 std::clamp((d - distances[segment]) / length, 0.0f, 1.0f)));
  }
  return result;
}

/**
 * Moving average of the values (radius in points), the first and the last value
 * stay
 */
void SmoothValues(std::vector<float>& values, int radius, int passes) {
  std::vector<float> next(values.size());
  for (int pass = 0; pass < passes; ++pass) {
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i == 0 || i + 1 == values.size()) {
        next[i] = values[i];
        continue;
      }
      int r = std::min({radius, static_cast<int>(i),
                        static_cast<int>(values.size() - 1 - i)});
      float sum = 0.0f;
      for (int k = -r; k <= r; ++k) {
        sum += values[i + k];
      }
      next[i] = sum / static_cast<float>(2 * r + 1);
    }
    values.swap(next);
  }
}
}  // namespace

/**
 * The island for the search of the paths: the height of the ground, what a step
 * through a cell costs extra (per unit) and the network the paths join (the
 * floor of the race track and the paths that are already there)
 */
struct Landmarks::Grid {
  enum Network : std::uint8_t { kNone = 0, kTrack = 1, kPath = 2 };
  glm::vec2 origin = glm::vec2(0.0f);
  int columns = 0, rows = 0;
  std::vector<float> height, penalty;
  std::vector<std::uint8_t> network;

  glm::vec2 Position(int cell) const {
    return origin + kRouteCell * glm::vec2(static_cast<float>(cell % columns),
                                           static_cast<float>(cell / columns));
  }

  int CellOf(glm::vec2 point) const {
    int i = std::clamp(
        static_cast<int>(std::round((point.x - origin.x) / kRouteCell)), 0,
        columns - 1);
    int j = std::clamp(
        static_cast<int>(std::round((point.y - origin.y) / kRouteCell)), 0,
        rows - 1);
    return j * columns + i;
  }

  /**
   * Cheapest way from the cell to the network (Dijkstra, 16 neighbors so that
   * the paths can go in more directions). A step costs more the steeper it goes
   * up or down: the paths wind up the hills.
   * @param joins The parts of the network that count (TRACK, PATH or both)
   * @param cost Gets the cost of the way
   * @return The cells from the network to the cell, empty if there is no way
   */
  std::vector<int> Route(int from, std::uint8_t joins, float& cost) const {
    static const int kDi[16] = {1, -1, 0,  0,  1, 1,  -1, -1,
                                2, 2,  -2, -2, 1, -1, 1,  -1};
    static const int kDj[16] = {0, 0,  1, -1, 1, -1, 1,  -1,
                                1, -1, 1, -1, 2, 2,  -2, -2};
    std::vector<float> best(height.size(), std::numeric_limits<float>::max());
    std::vector<int> previous(height.size(), -1);
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    best[from] = 0.0f;
    open.push({0.0f, from});
    int reached = -1;
    while (!open.empty()) {
      auto [total, cell] = open.top();
      open.pop();
      if (total > best[cell]) {
        continue;
      }
      if ((network[cell] & joins) != 0) {
        reached = cell;
        cost = total;
        break;
      }
      int i = cell % columns, j = cell / columns;
      for (int n = 0; n < 16; ++n) {
        int ni = i + kDi[n], nj = j + kDj[n];
        if (ni < 0 || nj < 0 || ni >= columns || nj >= rows) {
          continue;
        }
        int neighbor = nj * columns + ni;
        if (penalty[neighbor] >= kForbidden) {
          continue;
        }
        float length =
            kRouteCell *
            std::sqrt(static_cast<float>(kDi[n] * kDi[n] + kDj[n] * kDj[n]));
        float grade = std::abs(height[neighbor] - height[cell]) / length;
        // Steeper than the paths may go it is very expensive: a longer way in
        // bends up the hill is cheaper
        float factor =
            1.0f + 30.0f * grade * grade +
            (grade > kPreferredGrade ? 400.0f * (grade - kPreferredGrade)
                                     : 0.0f);
        float step =
            length * (factor + 0.5f * (penalty[cell] + penalty[neighbor]));
        if (total + step < best[neighbor]) {
          best[neighbor] = total + step;
          previous[neighbor] = cell;
          open.push({best[neighbor], neighbor});
        }
      }
    }
    std::vector<int> cells;
    for (int cell = reached; cell != -1; cell = previous[cell]) {
      cells.push_back(cell);
    }
    return cells;  // from the network (reached) back to the start
  }
};

Landmarks::Landmarks(const WorldGenerator& world) {
  ChoosePoints(world);
  FindPaths(world);
  Rasterize(world);
  for (PointOfInterest& point : points_) {
    glm::vec2 at = Ground(point.position);
    point.position.y = Height(at, world.NaturalHeight(at));
  }
}

void Landmarks::Soften(const WorldGenerator& world, glm::vec2 center,
                       float radius) {
  // The natural ground on a grid (with a margin for the blur), blurred with a
  // box filter a few times (about a Gaussian)
  Softened area;
  area.center = center;
  area.radius = radius;
  const int blur = static_cast<int>(kSofteningBlur / kRasterCell);
  const float half = radius + static_cast<float>(blur) * kRasterCell * 3.0f;
  area.origin = center - glm::vec2(half);
  area.columns = static_cast<int>(2.0f * half / kRasterCell) + 1;
  const int n = area.columns;
  area.heights.resize(static_cast<std::size_t>(n) * n);
  ParallelFor(n, [&](int j) {
    for (int i = 0; i < n; ++i) {
      area.heights[static_cast<std::size_t>(j) * n + i] = world.NaturalHeight(
          area.origin + kRasterCell * glm::vec2(static_cast<float>(i),
                                                static_cast<float>(j)));
    }
  });
  std::vector<float> next(area.heights.size());
  for (int pass = 0; pass < 3; ++pass) {
    for (int axis = 0; axis < 2; ++axis) {
      for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
          float sum = 0.0f;
          for (int k = -blur; k <= blur; ++k) {
            int si = axis == 0 ? std::clamp(i + k, 0, n - 1) : i,
                sj = axis == 1 ? std::clamp(j + k, 0, n - 1) : j;
            sum += area.heights[static_cast<std::size_t>(sj) * n + si];
          }
          next[static_cast<std::size_t>(j) * n + i] =
              sum / static_cast<float>(2 * blur + 1);
        }
      }
      area.heights.swap(next);
    }
  }
  softened_.push_back(std::move(area));
}

float Landmarks::ApplySoftening(glm::vec2 position, float natural) const {
  for (const Softened& area : softened_) {
    float distance = glm::distance(position, area.center);
    if (distance >= area.radius) {
      continue;
    }
    glm::vec2 cell = (position - area.origin) / kRasterCell;
    int i = std::clamp(static_cast<int>(cell.x), 0, area.columns - 2),
        j = std::clamp(static_cast<int>(cell.y), 0, area.columns - 2);
    float fx = cell.x - static_cast<float>(i),
          fz = cell.y - static_cast<float>(j);
    auto at = [&](int di, int dj) {
      return area
          .heights[static_cast<std::size_t>(j + dj) * area.columns + i + di];
    };
    float smooth = glm::mix(glm::mix(at(0, 0), at(1, 0), fx),
                            glm::mix(at(0, 1), at(1, 1), fx), fz);
    natural =
        glm::mix(natural, smooth,
                 1.0f - Smoothstep(0.55f * area.radius, area.radius, distance));
  }
  return natural;
}

float Landmarks::NaturalGround(const WorldGenerator& world,
                               glm::vec2 position) const {
  return ApplySoftening(position, world.NaturalHeight(position));
}

float Landmarks::ApplyLeveling(glm::vec2 position, float height) const {
  for (const PointOfInterest& point : points_) {
    if (!point.level) {
      continue;
    }
    glm::vec2 offset = position - Ground(point.position);
    float outer = 1.3f * point.radius;
    if (glm::dot(offset, offset) < outer * outer) {
      height = glm::mix(
          height, point.position.y,
          1.0f - Smoothstep(0.55f * point.radius, outer, glm::length(offset)));
    }
  }
  return height;
}

float Landmarks::Leveled(const WorldGenerator& world,
                         glm::vec2 position) const {
  return ApplyLeveling(position, NaturalGround(world, position));
}

void Landmarks::ChoosePoints(const WorldGenerator& world) {
  const glm::vec2 center = world.GetCenter();
  const float sea = world.GetSeaLevel();
  // of the chosen points, seen from the middle of the island
  std::vector<float> angles;
  auto add = [&](PointOfInterest point) {
    glm::vec2 at = Ground(point.position);
    point.position.y = NaturalGround(world, at);
    points_.push_back(point);
    angles.push_back(AngleOf(at - center));
  };
  auto separation = [&](float angle) {
    float smallest = 2.0f * kPi;
    for (float other : angles) {
      smallest = std::min(smallest, AngleBetween(angle, other));
    }
    return smallest;
  };

  // 1. A lookout on the highest summit
  glm::vec2 summit = center;
  float highest = std::numeric_limits<float>::lowest();
  for (glm::vec2 peak : world.GetMountainPeaks()) {
    for (float x = -24.0f; x <= 24.0f; x += 2.0f) {
      for (float z = -24.0f; z <= 24.0f; z += 2.0f) {
        float h = world.NaturalHeight(peak + glm::vec2(x, z));
        if (h > highest) {
          highest = h;
          summit = peak + glm::vec2(x, z);
        }
      }
    }
  }
  // The crags around it are worn down a bit (the path winds up between rocks,
  // not along walls), its top is a small plateau
  Soften(world, summit, kSummitSoftening);
  PointOfInterest lookout;
  lookout.kind = PointOfInterest::Kind::kSummit;
  lookout.position = glm::vec3(summit.x, 0.0f, summit.y);
  lookout.facing = glm::normalize(summit - center);
  lookout.radius = 7.0f;
  lookout.level = true;
  add(lookout);

  // 2. The coast in stretches of directions with beaches or cliffs
  const int directions = 720;
  std::vector<float> cliffs(directions), coast_distance(directions);
  for (int i = 0; i < directions; ++i) {
    float angle = 2.0f * kPi * static_cast<float>(i) / directions;
    glm::vec2 direction(std::cos(angle), std::sin(angle));
    float inland, cliff;
    world.Coast(center + direction * 200.0f, inland, cliff);
    cliffs[i] = cliff;
    coast_distance[i] = 200.0f + inland;
  }
  struct Stretch {
    int middle;
    float span;
  };
  auto stretches = [&](const std::function<bool(int)>& belongs) {
    std::vector<Stretch> result;
    int start = 0;
    while (start < directions && belongs(start)) {
      ++start;
    }
    if (start == directions) {
      return result;  // all around: no stretch
    }
    int run_start = -1;
    for (int k = 1; k <= directions; ++k) {
      bool inside = k < directions && belongs((start + k) % directions);
      if (inside && run_start < 0) {
        run_start = k;
      } else if (!inside && run_start >= 0) {
        result.push_back(
            {(start + (run_start + k - 1) / 2) % directions,
             2.0f * kPi * static_cast<float>(k - run_start) / directions});
        run_start = -1;
      }
    }
    std::sort(
        result.begin(), result.end(),
        [](const Stretch& a, const Stretch& b) { return a.span > b.span; });
    return result;
  };
  auto direction_of = [&](int index) {
    float angle = 2.0f * kPi * static_cast<float>(index) / directions;
    return glm::vec2(std::cos(angle), std::sin(angle));
  };

  // Beaches on different sides of the island, on the dry sand above the water
  // line
  std::vector<float> beach_angles;
  for (const Stretch& stretch :
       stretches([&](int i) { return cliffs[i] < 0.12f; })) {
    float angle = 2.0f * kPi * static_cast<float>(stretch.middle) / directions;
    bool apart = true;
    for (float other : beach_angles) {
      apart = apart && AngleBetween(angle, other) > glm::radians(55.0f);
    }
    if (stretch.span < glm::radians(10.0f) || !apart ||
        beach_angles.size() >= 4) {
      continue;
    }
    glm::vec2 direction = direction_of(stretch.middle);
    float distance = coast_distance[stretch.middle] - 9.0f;
    for (int step = 0;
         step < 15 &&
         NaturalGround(world, center + direction * distance) < sea + 0.8f;
         ++step) {
      distance -= 1.0f;
    }
    PointOfInterest beach;
    beach.kind = PointOfInterest::Kind::kBeach;
    glm::vec2 at = center + direction * distance;
    beach.position = glm::vec3(at.x, 0.0f, at.y);
    beach.facing = direction;
    beach.radius = 8.0f;
    add(beach);
    beach_angles.push_back(angle);
  }

  // A view from the top of the cliffs, away from the other places
  float best_separation = -1.0f;
  PointOfInterest view;
  for (const Stretch& stretch :
       stretches([&](int i) { return cliffs[i] > 0.85f; })) {
    float angle = 2.0f * kPi * static_cast<float>(stretch.middle) / directions;
    if (stretch.span < glm::radians(10.0f) ||
        separation(angle) <= best_separation) {
      continue;
    }
    glm::vec2 direction = direction_of(stretch.middle);
    float distance = coast_distance[stretch.middle] - 13.0f;
    for (int step = 0;
         step < 10 &&
         NaturalGround(world, center + direction * distance) < sea + 10.0f;
         ++step) {
      distance -= 2.0f;
    }
    best_separation = separation(angle);
    glm::vec2 at = center + direction * distance;
    view.kind = PointOfInterest::Kind::kCliffView;
    view.position = glm::vec3(at.x, 0.0f, at.y);
    view.facing = direction;
    view.radius = 6.0f;
  }
  if (best_separation >= 0.0f) {
    add(view);
  }

  // 3. A camp on flat ground in the forest and a circle of standing stones on a
  // hill, between the track and the coast
  const TrackPath& track = world.GetPath();
  auto roughness = [&](glm::vec2 at, float radius) {
    float middle = NaturalGround(world, at), most = 0.0f;
    for (int k = 0; k < 8; ++k) {
      float angle = 2.0f * kPi * static_cast<float>(k) / 8.0f;
      for (float r : {0.5f * radius, radius}) {
        most = std::max(
            most,
            std::abs(NaturalGround(world, at + r * glm::vec2(std::cos(angle),
                                                             std::sin(angle))) -
                     middle));
      }
    }
    return most;
  };
  auto in_forest = [&](glm::vec2 at) {
    float inland, cliff;
    world.Coast(at, inland, cliff);
    float h = NaturalGround(world, at);
    TrackPath::Nearest nearest = track.FindNearest(at);
    return inland > 45.0f && h > sea + 5.0f && h < sea + 26.0f &&
           (!nearest.found ||
            std::abs(nearest.lateral) > nearest.floor_half_width + 25.0f) &&
           world.CaveRoofTop(at) < -1000.0f;
  };
  for (PointOfInterest::Kind kind :
       {PointOfInterest::Kind::kCamp, PointOfInterest::Kind::kStandingStones}) {
    float best_score = -1.0f;
    glm::vec2 best = center;
    for (float x = -220.0f; x <= 220.0f; x += 6.0f) {
      for (float z = -220.0f; z <= 220.0f; z += 6.0f) {
        glm::vec2 at = center + glm::vec2(x, z);
        float distance = glm::length(at - center);
        if (distance < 110.0f || distance > 210.0f || !in_forest(at)) {
          continue;
        }
        float score = separation(AngleOf(at - center));
        if (kind == PointOfInterest::Kind::kCamp) {
          float rough = roughness(at, 9.0f);
          if (rough > 2.0f) {
            continue;
          }
          score *= 1.0f - 0.25f * rough;
        } else {
          // The top of a hill: higher than the ground around it
          float h = NaturalGround(world, at);
          bool top = roughness(at, 7.0f) < 2.5f;
          for (int k = 0; k < 8 && top; ++k) {
            float angle = 2.0f * kPi * static_cast<float>(k) / 8.0f;
            top =
                NaturalGround(world, at + 14.0f * glm::vec2(std::cos(angle),
                                                            std::sin(angle))) <
                h - 0.8f;
          }
          if (!top) {
            continue;
          }
        }
        if (score > best_score) {
          best_score = score;
          best = at;
        }
      }
    }
    if (best_score < 0.0f) {
      continue;
    }
    PointOfInterest place;
    place.kind = kind;
    place.position = glm::vec3(best.x, 0.0f, best.y);
    place.facing = glm::normalize(best - center);
    place.radius = kind == PointOfInterest::Kind::kCamp ? 11.0f : 10.0f;
    place.level = true;
    add(place);
  }
}

void Landmarks::FindPaths(const WorldGenerator& world) {
  const float sea = world.GetSeaLevel();
  const TrackPath& track = world.GetPath();
  Box bounds = world.GetBounds();
  Grid grid;
  grid.origin = glm::vec2(bounds.minimum.x, bounds.minimum.z);
  grid.columns =
      static_cast<int>((bounds.maximum.x - bounds.minimum.x) / kRouteCell) + 1;
  grid.rows =
      static_cast<int>((bounds.maximum.z - bounds.minimum.z) / kRouteCell) + 1;
  std::size_t cell_count = static_cast<std::size_t>(grid.columns) * grid.rows;
  grid.height.resize(cell_count);
  grid.penalty.assign(cell_count, 0.0f);
  grid.network.assign(cell_count, Grid::kNone);
  // The natural ground of every cell, then every cell on its own (the rows in
  // parallel)
  std::vector<float> naturals(cell_count);
  ParallelFor(grid.rows, [&](int row) {
    for (std::size_t c = static_cast<std::size_t>(row) * grid.columns;
         c < static_cast<std::size_t>(row + 1) * grid.columns; ++c) {
      naturals[c] = NaturalGround(world, grid.Position(static_cast<int>(c)));
    }
  });
  ParallelFor(grid.rows, [&](int row) {
    for (std::size_t c = static_cast<std::size_t>(row) * grid.columns;
         c < static_cast<std::size_t>(row + 1) * grid.columns; ++c) {
      glm::vec2 at = grid.Position(static_cast<int>(c));
      float natural = naturals[c];
      grid.height[c] = ApplyLeveling(at, natural);
      float inland, cliff;
      world.Coast(at, inland, cliff);
      if (natural < sea + 0.6f) {
        grid.penalty[c] = kForbidden;  // the sea and the pools
        continue;
      }
      if (cliff > 0.3f && inland < 22.0f) {
        grid.penalty[c] = 30.0f * cliff;  // the edge of the cliffs
      }
      // Steep ground around the cell (also across the way): a path there would
      // be a ledge under a crag or above a drop. To the neighbors in 8
      // directions, one and two cells away.
      float steepest = 0.0f;
      const int i = static_cast<int>(c) % grid.columns;
      const int j = static_cast<int>(c) / grid.columns;
      for (int di = -1; di <= 1; ++di) {
        for (int dj = -1; dj <= 1; ++dj) {
          for (int reach = 1; reach <= kSteepCells && (di != 0 || dj != 0);
               ++reach) {
            int ni = i + di * reach, nj = j + dj * reach;
            if (ni >= 0 && nj >= 0 && ni < grid.columns && nj < grid.rows) {
              float distance = kRouteCell * static_cast<float>(reach) *
                               std::sqrt(static_cast<float>(di * di + dj * dj));
              steepest = std::max(
                  steepest,
                  std::abs(
                      naturals[static_cast<std::size_t>(nj) * grid.columns +
                               ni] -
                      natural) /
                      distance);
            }
          }
        }
      }
      if (steepest > kSteepSlope) {
        float excess = steepest - kSteepSlope;
        grid.penalty[c] += 40.0f * excess * excess + 8.0f * excess;
      }
      for (const KeepClear& object : world.GetKeepClear()) {
        if (glm::distance(at, Ground(object.center)) < object.radius + 4.0f) {
          grid.penalty[c] = kForbidden;
        }
      }
      if (world.CaveRoofTop(at) > -1000.0f) {
        grid.penalty[c] = kForbidden;
      }
      TrackPath::Nearest nearest = track.FindNearest(at);
      if (grid.penalty[c] < kForbidden && steepest < kSteepSlope &&
          nearest.found &&
          std::abs(nearest.lateral) < nearest.floor_half_width - 1.0f &&
          natural < nearest.floor_height + 1.0f &&
          nearest.distance > kStartClearance &&
          nearest.distance < track.GetLength() - kStartClearance) {
        grid.network[c] = Grid::kTrack;
      } else if (grid.penalty[c] < kForbidden && nearest.found &&
                 std::abs(nearest.lateral) <
                     nearest.floor_half_width + kCanyonBand) {
        // A path climbs out of the canyon of the track, it does not go along
        // its side next to the floor
        grid.penalty[c] += kCanyonPenalty;
      }
    }
  });

  // No path joins the track right at a pool on it
  const int shore_cells = 4;
  std::vector<std::uint8_t> network = grid.network;
  for (int j = 0; j < grid.rows; ++j) {
    for (int i = 0; i < grid.columns; ++i) {
      std::size_t c = static_cast<std::size_t>(j) * grid.columns + i;
      for (int dj = -shore_cells;
           dj <= shore_cells && network[c] == Grid::kTrack; ++dj) {
        for (int di = -shore_cells; di <= shore_cells; ++di) {
          int ni = std::clamp(i + di, 0, grid.columns - 1),
              nj = std::clamp(j + dj, 0, grid.rows - 1);
          if (grid.height[static_cast<std::size_t>(nj) * grid.columns + ni] <
              sea + 0.6f) {
            grid.network[c] = Grid::kNone;
          }
        }
      }
    }
  }

  // The closest places first, the paths to the far ones can branch off from
  // them
  std::vector<int> order(points_.size());
  for (std::size_t i = 0; i < points_.size(); ++i) {
    order[i] = static_cast<int>(i);
  }
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return glm::distance(Ground(points_[a].position), world.GetCenter()) <
           glm::distance(Ground(points_[b].position), world.GetCenter());
  });
  for (int index : order) {
    const PointOfInterest& point = points_[index];
    glm::vec2 target = Ground(point.position);
    // From the track, unless a path that is already there is much closer (then
    // it branches off from that one)
    float track_cost = std::numeric_limits<float>::max(),
          path_cost = std::numeric_limits<float>::max();
    std::vector<int> cells =
        grid.Route(grid.CellOf(target), Grid::kTrack, track_cost);
    std::vector<int> branch =
        paths_.empty()
            ? std::vector<int>()
            : grid.Route(grid.CellOf(target), Grid::kPath, path_cost);
    if (branch.size() >= 2 &&
        (cells.size() < 2 || path_cost < kBranchRatio * track_cost)) {
      cells.swap(branch);
    }
    if (cells.size() < 2) {
      continue;
    }
    FootPath path;
    path.point_of_interest = index;
    path.from_track = grid.network[cells.front()] == Grid::kTrack;
    std::vector<glm::vec2> line;
    for (int cell : cells) {
      line.push_back(grid.Position(cell));
    }
    line.back() = target;
    // A branch starts exactly on the path it branches off from
    float start_height = NaturalGround(world, line.front());
    if (!path.from_track) {
      float closest = std::numeric_limits<float>::max();
      const glm::vec2 reached = line.front();
      for (const FootPath& other : paths_) {
        for (const glm::vec3& p : other.points) {
          if (glm::distance(Ground(p), reached) < closest) {
            closest = glm::distance(Ground(p), reached);
            line.front() = Ground(p);
            start_height = p.y;
          }
        }
      }
    }
    // Round off the steps of the grid
    for (int iteration = 0; iteration < 10; ++iteration) {
      std::vector<glm::vec2> next = line;
      for (std::size_t i = 1; i + 1 < line.size(); ++i) {
        next[i] = 0.5f * line[i] + 0.25f * (line[i - 1] + line[i + 1]);
      }
      line.swap(next);
    }
    line = ResampleLine(line, 1.0f);

    // The height along the path: the ground smoothed, cut down where it would
    // be steeper than the maximum grade
    std::vector<float> heights(line.size());
    for (std::size_t i = 0; i < line.size(); ++i) {
      heights[i] = Leveled(world, line[i]);
    }
    heights.front() = start_height;
    SmoothValues(heights, 4, 4);
    // On the floor of the track (which the path does not change) it is as high
    // as the floor, so that it does not start with a step where it leaves the
    // floor (smoothed, the side of the canyon would lift it)
    for (std::size_t i = 0; i < line.size(); ++i) {
      TrackPath::Nearest nearest = track.FindNearest(line[i]);
      if (nearest.found &&
          std::abs(nearest.lateral) < nearest.floor_half_width + 0.5f) {
        heights[i] = Leveled(world, line[i]);
      }
    }
    for (std::size_t i = 1; i < heights.size(); ++i) {
      heights[i] = std::min(heights[i], heights[i - 1] + kMaxGrade);
    }
    for (std::size_t i = heights.size() - 1; i-- > 0;) {
      heights[i] = std::min(heights[i], heights[i + 1] + kMaxGrade);
    }
    for (float& h : heights) {
      h = std::max(h, sea + 0.3f);
    }
    heights.front() = start_height;
    SmoothValues(heights, 1, 2);
    for (std::size_t i = 0; i < line.size(); ++i) {
      path.points.push_back(glm::vec3(line[i].x, heights[i], line[i].y));
    }

    // A second path from the same place of the track would go next to this one:
    // the later paths leave the track elsewhere (or branch off from this one)
    if (path.from_track) {
      for (std::size_t c = 0; c < cell_count; ++c) {
        if (grid.network[c] == Grid::kTrack &&
            glm::distance(grid.Position(static_cast<int>(c)), line.front()) <
                kTrailheadSpacing) {
          grid.network[c] = Grid::kNone;
        }
      }
    }
    // The later paths may branch off from this one
    for (const glm::vec2& p : line) {
      int cell = grid.CellOf(p);
      int i = cell % grid.columns, j = cell / grid.columns;
      for (int dj = -1; dj <= 1; ++dj) {
        for (int di = -1; di <= 1; ++di) {
          int ni = std::clamp(i + di, 0, grid.columns - 1),
              nj = std::clamp(j + dj, 0, grid.rows - 1);
          std::uint8_t& network =
              grid.network[static_cast<std::size_t>(nj) * grid.columns + ni];
          network = network == Grid::kNone ? Grid::kPath : network;
        }
      }
    }

    // A cairn where the path begins: next to it where it leaves the floor of
    // the track, or a bit after the fork
    std::size_t marked = std::min<std::size_t>(3, line.size() - 2);
    if (path.from_track) {
      while (marked + 2 < line.size()) {
        TrackPath::Nearest nearest = track.FindNearest(line[marked]);
        if (!nearest.found ||
            std::abs(nearest.lateral) > nearest.floor_half_width + 1.5f) {
          break;
        }
        ++marked;
      }
    }
    glm::vec2 along = glm::normalize(line[marked + 1] - line[marked - 1]);
    glm::vec2 side = glm::vec2(-along.y, along.x) * (kPathHalfWidth + 1.1f);
    markers_.push_back(
        glm::vec3(line[marked].x + side.x, 0.0f, line[marked].y + side.y));
    paths_.push_back(std::move(path));
  }
}

void Landmarks::Rasterize(const WorldGenerator& world) {
  if (paths_.empty()) {
    return;
  }
  glm::vec2 low(std::numeric_limits<float>::max()),
      high(std::numeric_limits<float>::lowest());
  for (const FootPath& path : paths_) {
    for (const glm::vec3& p : path.points) {
      low = glm::min(low, Ground(p));
      high = glm::max(high, Ground(p));
    }
  }
  raster_origin_ = glm::floor(low) - glm::vec2(kReach + 2.0f);
  raster_columns_ =
      static_cast<int>((high.x - raster_origin_.x + kReach + 2.0f) /
                       kRasterCell) +
      1;
  raster_rows_ = static_cast<int>((high.y - raster_origin_.y + kReach + 2.0f) /
                                  kRasterCell) +
                 1;
  raster_.assign(static_cast<std::size_t>(raster_columns_) * raster_rows_,
                 Cell());
  std::vector<int> first_leg(raster_.size(), -1),
      second_leg(raster_.size(), -1);
  auto same_leg = [](int a, int b) {
    return b >= 0 && a / kPathStride == b / kPathStride &&
           std::abs(a - b) <= kLegLength;
  };

  // The two closest legs of the paths for every cell
  for (std::size_t p = 0; p < paths_.size(); ++p) {
    const std::vector<glm::vec3>& line = paths_[p].points;
    for (std::size_t s = 0; s + 1 < line.size(); ++s) {
      glm::vec2 a = Ground(line[s]), b = Ground(line[s + 1]);
      int i0 = std::max(
          0,
          static_cast<int>(std::floor(
              (std::min(a.x, b.x) - kReach - raster_origin_.x) / kRasterCell)));
      int i1 = std::min(
          raster_columns_ - 1,
          static_cast<int>(std::ceil(
              (std::max(a.x, b.x) + kReach - raster_origin_.x) / kRasterCell)));
      int j0 = std::max(
          0,
          static_cast<int>(std::floor(
              (std::min(a.y, b.y) - kReach - raster_origin_.y) / kRasterCell)));
      int j1 = std::min(
          raster_rows_ - 1,
          static_cast<int>(std::ceil(
              (std::max(a.y, b.y) + kReach - raster_origin_.y) / kRasterCell)));
      int leg = static_cast<int>(p) * kPathStride + static_cast<int>(s);
      for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
          glm::vec2 at =
              raster_origin_ + kRasterCell * glm::vec2(static_cast<float>(i),
                                                       static_cast<float>(j));
          glm::vec2 ab = b - a;
          float t = std::clamp(
              glm::dot(at - a, ab) / std::max(glm::dot(ab, ab), 1e-9f), 0.0f,
              1.0f);
          float distance = glm::distance(at, a + t * ab);
          if (distance >= kReach) {
            continue;
          }
          float h = glm::mix(line[s].y, line[s + 1].y, t);
          std::size_t c = static_cast<std::size_t>(j) * raster_columns_ + i;
          Cell& cell = raster_[c];
          if (same_leg(leg, first_leg[c])) {
            if (distance < cell.distance) {
              cell.distance = distance;
              cell.height = h;
              first_leg[c] = leg;
            }
          } else if (same_leg(leg, second_leg[c])) {
            if (distance < cell.second_distance) {
              cell.second_distance = distance;
              cell.second_height = h;
              second_leg[c] = leg;
            }
            if (cell.second_distance < cell.distance) {
              std::swap(cell.distance, cell.second_distance);
              std::swap(cell.height, cell.second_height);
              std::swap(first_leg[c], second_leg[c]);
            }
          } else if (distance < cell.distance) {
            cell.second_distance = cell.distance;
            cell.second_height = cell.height;
            second_leg[c] = first_leg[c];
            cell.distance = distance;
            cell.height = h;
            first_leg[c] = leg;
          } else if (distance < cell.second_distance) {
            cell.second_distance = distance;
            cell.second_height = h;
            second_leg[c] = leg;
          }
        }
      }
    }
  }

  // Cells without a leg get the ground (they change nothing when they are
  // interpolated with the others). The paths do not cut into the floor of the
  // race track and into the caves.
  const TrackPath& track = world.GetPath();
  ParallelFor(raster_rows_, [&](int row) {
    for (std::size_t c = static_cast<std::size_t>(row) * raster_columns_;
         c < static_cast<std::size_t>(row + 1) * raster_columns_; ++c) {
      Cell& cell = raster_[c];
      glm::vec2 at =
          raster_origin_ +
          kRasterCell * glm::vec2(static_cast<float>(c % raster_columns_),
                                  static_cast<float>(c / raster_columns_));
      if (cell.distance >= kReach) {
        cell.distance = kReach;
        cell.height = Leveled(world, at);
      }
      if (cell.second_distance >= kReach) {
        cell.second_distance = kReach;
        cell.second_height = cell.height;
      }
      if (cell.distance >= kReach) {
        continue;
      }
      TrackPath::Nearest nearest = track.FindNearest(at);
      if (nearest.found) {
        // Right from the edge of the floor: where a path climbs out of the
        // canyon, the wall is cut too (no steep step up)
        cell.allowed = Smoothstep(
            1.5f, 2.5f, std::abs(nearest.lateral) - nearest.floor_half_width);
      }
      if (world.CaveRoofTop(at) > -1000.0f) {
        cell.allowed = 0.0f;
      }
    }
  });
}

Landmarks::Cell Landmarks::CellAt(glm::vec2 position) const {
  Cell result;
  result.distance = result.second_distance = kReach;
  if (raster_.empty()) {
    return result;
  }
  float x = (position.x - raster_origin_.x) / kRasterCell,
        z = (position.y - raster_origin_.y) / kRasterCell;
  if (x < 0.0f || z < 0.0f || x >= static_cast<float>(raster_columns_ - 1) ||
      z >= static_cast<float>(raster_rows_ - 1)) {
    return result;
  }
  int i = static_cast<int>(x), j = static_cast<int>(z);
  float fx = x - static_cast<float>(i), fz = z - static_cast<float>(j);
  const Cell& a = raster_[static_cast<std::size_t>(j) * raster_columns_ + i];
  const Cell& b =
      raster_[static_cast<std::size_t>(j) * raster_columns_ + i + 1];
  const Cell& c =
      raster_[static_cast<std::size_t>(j + 1) * raster_columns_ + i];
  const Cell& d =
      raster_[static_cast<std::size_t>(j + 1) * raster_columns_ + i + 1];
  auto mix = [&](float Cell::* field) {
    return (a.*field * (1.0f - fx) + b.*field * fx) * (1.0f - fz) +
           (c.*field * (1.0f - fx) + d.*field * fx) * fz;
  };
  result.distance = mix(&Cell::distance);
  result.height = mix(&Cell::height);
  result.second_distance = mix(&Cell::second_distance);
  result.second_height = mix(&Cell::second_height);
  result.allowed = mix(&Cell::allowed);
  return result;
}

float Landmarks::Height(glm::vec2 position, float natural) const {
  float result = ApplyLeveling(position, ApplySoftening(position, natural));
  Cell cell = CellAt(position);
  if (cell.distance >= kReach) {
    return result;
  }
  // Flat across the middle of the path, the slopes at its sides are wider where
  // it is cut deeper (or filled higher)
  auto cut = [&](float ground, float distance, float path_height) {
    float depth = std::abs(ground - path_height);
    float shoulder =
        std::min(1.2f + 1.5f * depth, kReach - kPathHalfWidth - 0.5f);
    float weight = 1.0f - Smoothstep(0.75f * kPathHalfWidth,
                                     kPathHalfWidth + shoulder, distance);
    return glm::mix(ground, path_height, weight * cell.allowed);
  };
  // The further leg first, the closer one cuts into it (e.g. the slope between
  // the two sides of a hairpin bend)
  result = cut(result, cell.second_distance, cell.second_height);
  return cut(result, cell.distance, cell.height);
}

float Landmarks::PathDistance(glm::vec2 position) const {
  return CellAt(position).distance;
}

float Landmarks::Trail(glm::vec2 position) const {
  float result = 0.0f;
  float distance = PathDistance(position);
  if (distance < kReach) {
    float edge = distance + 0.35f * noise::Fbm(position * 0.35f, 2,
                                               WorldGenerator::kSeed + 70u);
    result =
        1.0f - Smoothstep(kPathHalfWidth - 0.45f, kPathHalfWidth + 0.45f, edge);
  }
  for (const PointOfInterest& point : points_) {
    if (point.kind == PointOfInterest::Kind::kCamp) {
      // Trodden ground around the fire
      float from_fire =
          glm::distance(position, Ground(point.position)) +
          1.2f * noise::Fbm(position * 0.4f, 2, WorldGenerator::kSeed + 71u);
      result = std::max(result, 1.0f - Smoothstep(3.0f, 5.5f, from_fire));
    }
  }
  return result;
}

int Landmarks::PointAt(glm::vec2 position) const {
  for (std::size_t i = 0; i < points_.size(); ++i) {
    glm::vec2 offset = position - Ground(points_[i].position);
    if (glm::dot(offset, offset) < points_[i].radius * points_[i].radius) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace bibo::world
