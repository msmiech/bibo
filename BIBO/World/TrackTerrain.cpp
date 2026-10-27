#include "TrackTerrain.hpp"

#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bibo::world {

TrackTerrain::TrackTerrain(const std::vector<glm::vec3>& vertices,
                           const std::vector<unsigned int>& indices) {
  glm::vec3 low(std::numeric_limits<float>::max()),
      high(std::numeric_limits<float>::lowest());
  for (const glm::vec3& vertex : vertices) {
    low = glm::min(low, vertex);
    high = glm::max(high, vertex);
  }
  if (vertices.empty()) {
    low = high = glm::vec3(0.0f);
  }
  origin_ = glm::vec2(low.x, low.z);
  columns_ = static_cast<int>((high.x - low.x) / kCellSize) + 2;
  rows_ = static_cast<int>((high.z - low.z) / kCellSize) + 2;
  const float empty = std::numeric_limits<float>::lowest();
  heights_.assign(static_cast<std::size_t>(columns_) * rows_, empty);

  // Highest triangle above the center of every cell
  for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
    glm::vec3 a = vertices[indices[t]], b = vertices[indices[t + 1]],
              c = vertices[indices[t + 2]];
    float determinant = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
    if (std::abs(determinant) < 1e-9f) {
      continue;  // vertical or degenerate
    }
    int i0 = std::max(
        0, static_cast<int>(
               std::ceil((std::min({a.x, b.x, c.x}) - origin_.x) / kCellSize)));
    int i1 = std::min(
        columns_ - 1,
        static_cast<int>(
            std::floor((std::max({a.x, b.x, c.x}) - origin_.x) / kCellSize)));
    int j0 = std::max(
        0, static_cast<int>(
               std::ceil((std::min({a.z, b.z, c.z}) - origin_.y) / kCellSize)));
    int j1 = std::min(
        rows_ - 1, static_cast<int>(std::floor(
                       (std::max({a.z, b.z, c.z}) - origin_.y) / kCellSize)));
    for (int j = j0; j <= j1; ++j) {
      for (int i = i0; i <= i1; ++i) {
        float x = origin_.x + i * kCellSize, z = origin_.y + j * kCellSize;
        float l1 =
            ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / determinant;
        float l2 =
            ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / determinant;
        float l3 = 1.0f - l1 - l2;
        if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f) {
          continue;
        }
        float& cell = heights_[static_cast<std::size_t>(j) * columns_ + i];
        cell = std::max(cell, l1 * a.y + l2 * b.y + l3 * c.y);
      }
    }
  }

  // The ragged border of the mesh leaves some cells empty: they get the average
  // of their filled neighbors
  bool changed = true;
  while (changed) {
    changed = false;
    std::vector<float> filled = heights_;
    for (int j = 0; j < rows_; ++j) {
      for (int i = 0; i < columns_; ++i) {
        if (heights_[static_cast<std::size_t>(j) * columns_ + i] != empty) {
          continue;
        }
        float sum = 0.0f;
        int count = 0;
        for (int dj = -1; dj <= 1; ++dj) {
          for (int di = -1; di <= 1; ++di) {
            int ni = i + di, nj = j + dj;
            if (ni >= 0 && nj >= 0 && ni < columns_ && nj < rows_ &&
                heights_[static_cast<std::size_t>(nj) * columns_ + ni] !=
                    empty) {
              sum += heights_[static_cast<std::size_t>(nj) * columns_ + ni];
              ++count;
            }
          }
        }
        if (count > 0) {
          filled[static_cast<std::size_t>(j) * columns_ + i] =
              sum / static_cast<float>(count);
          changed = true;
        }
      }
    }
    heights_.swap(filled);
  }

  minimum_ = glm::vec2(low.x, low.z) + glm::vec2(kBorderMargin);
  maximum_ = glm::vec2(high.x, high.z) - glm::vec2(kBorderMargin);
  lowest_ = std::numeric_limits<float>::max();
  highest_ = std::numeric_limits<float>::lowest();
  for (float h : heights_) {
    lowest_ = std::min(lowest_, h == empty ? lowest_ : h);
    highest_ = std::max(highest_, h);
  }
  if (heights_.empty() || highest_ == empty) {
    lowest_ = highest_ = 0.0f;
  }

  // Blurred copy (box filter along both axes, twice: close to a Gaussian)
  smooth_heights_ = heights_;
  std::vector<float> line;
  const int radius = static_cast<int>(kSmoothRadius / kCellSize);
  for (int pass = 0; pass < 2; ++pass) {
    for (int axis = 0; axis < 2; ++axis) {
      int count = axis == 0 ? columns_ : rows_,
          lines = axis == 0 ? rows_ : columns_;
      line.resize(count);
      for (int l = 0; l < lines; ++l) {
        auto cell = [&](int k) -> float& {
          return axis == 0
                     ? smooth_heights_[static_cast<std::size_t>(l) * columns_ +
                                       k]
                     : smooth_heights_[static_cast<std::size_t>(k) * columns_ +
                                       l];
        };
        // Running sum, the border cells are repeated
        float sum = 0.0f;
        for (int k = -radius - 1; k < radius; ++k) {
          sum += cell(std::clamp(k, 0, count - 1));
        }
        for (int k = 0; k < count; ++k) {
          sum += cell(std::min(k + radius, count - 1)) -
                 cell(std::max(k - radius - 1, 0));
          line[k] = sum / static_cast<float>(2 * radius + 1);
        }
        for (int k = 0; k < count; ++k) {
          cell(k) = line[k];
        }
      }
    }
  }
}

bool TrackTerrain::LoadNodeMesh(const aiScene& scene, const char* node_name,
                                std::vector<glm::vec3>& vertices,
                                std::vector<unsigned int>& indices) {
  const aiNode* node =
      scene.mRootNode ? scene.mRootNode->FindNode(node_name) : nullptr;
  if (!node || node->mNumMeshes == 0) {
    return false;
  }
  // Global transformation: the transformations of all parents (like
  // resources::Model::TransformNode)
  aiMatrix4x4 transformation = node->mTransformation;
  for (const aiNode* parent = node->mParent; parent; parent = parent->mParent) {
    transformation = parent->mTransformation * transformation;
  }
  vertices.clear();
  indices.clear();
  for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
    const aiMesh* mesh = scene.mMeshes[node->mMeshes[m]];
    unsigned int first = static_cast<unsigned int>(vertices.size());
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
      aiVector3D v = transformation * mesh->mVertices[i];
      vertices.emplace_back(v.x, v.y, v.z);
    }
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
      const aiFace& face = mesh->mFaces[f];
      if (face.mNumIndices == 3) {
        indices.insert(indices.end(),
                       {first + face.mIndices[0], first + face.mIndices[1],
                        first + face.mIndices[2]});
      }
    }
  }
  return !indices.empty();
}

bool TrackTerrain::NodeSphere(const aiScene& scene, const char* node_name,
                              glm::vec3& center, float& radius) {
  std::vector<glm::vec3> vertices;
  std::vector<unsigned int> indices;
  if (!LoadNodeMesh(scene, node_name, vertices, indices)) {
    return false;
  }
  glm::vec3 low(std::numeric_limits<float>::max()),
      high(std::numeric_limits<float>::lowest());
  for (const glm::vec3& vertex : vertices) {
    low = glm::min(low, vertex);
    high = glm::max(high, vertex);
  }
  center = 0.5f * (low + high);
  radius = 0.0f;
  for (const glm::vec3& vertex : vertices) {
    radius = std::max(radius, glm::distance(center, vertex));
  }
  return true;
}

float TrackTerrain::Bilinear(const std::vector<float>& grid,
                             glm::vec2 position) const {
  if (grid.empty()) {
    return 0.0f;
  }
  float x = std::clamp((position.x - origin_.x) / kCellSize, 0.0f,
                       static_cast<float>(columns_ - 1) - 1e-3f);
  float z = std::clamp((position.y - origin_.y) / kCellSize, 0.0f,
                       static_cast<float>(rows_ - 1) - 1e-3f);
  int i = static_cast<int>(x), j = static_cast<int>(z);
  float fx = x - static_cast<float>(i), fz = z - static_cast<float>(j);
  int i1 = std::min(i + 1, columns_ - 1), j1 = std::min(j + 1, rows_ - 1);
  auto at = [&](int column, int row) {
    return grid[static_cast<std::size_t>(row) * columns_ + column];
  };
  return (at(i, j) * (1.0f - fx) + at(i1, j) * fx) * (1.0f - fz) +
         (at(i, j1) * (1.0f - fx) + at(i1, j1) * fx) * fz;
}

float TrackTerrain::Height(glm::vec2 position) const {
  return Bilinear(heights_, position);
}

float TrackTerrain::SmoothHeight(glm::vec2 position) const {
  return Bilinear(smooth_heights_, position);
}

float TrackTerrain::DistanceInside(glm::vec2 position) const {
  // Signed distance of a rectangle with rounded corners: the rectangle shrunk
  // by the radius, grown by it again
  glm::vec2 middle = 0.5f * (minimum_ + maximum_);
  glm::vec2 half = 0.5f * (maximum_ - minimum_) - glm::vec2(kCornerRadius);
  glm::vec2 q = glm::abs(position - middle) - half;
  float outside = glm::length(glm::max(q, glm::vec2(0.0f))) +
                  std::min(std::max(q.x, q.y), 0.0f) - kCornerRadius;
  return -outside;
}

glm::vec2 TrackTerrain::ClampInside(glm::vec2 position) const {
  glm::vec2 middle = 0.5f * (minimum_ + maximum_);
  glm::vec2 half = 0.5f * (maximum_ - minimum_) - glm::vec2(kCornerRadius);
  glm::vec2 offset = position - middle;
  glm::vec2 core = glm::clamp(offset, -half, half);
  glm::vec2 from_core = offset - core;
  float length = glm::length(from_core);
  if (length > kCornerRadius) {
    from_core *= kCornerRadius / length;
  }
  return middle + core + from_core;
}

}  // namespace bibo::world
