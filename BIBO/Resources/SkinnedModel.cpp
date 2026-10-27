#include "SkinnedModel.hpp"

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <assimp/Importer.hpp>
#include <functional>
#include <map>
#include <set>
#include <utility>

namespace bibo::resources {

using animation::Transform;

namespace {
glm::mat4 ToGlm(const aiMatrix4x4& m) {
  return glm::mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3,
                   m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
}

glm::mat4 GlobalTransform(const aiNode* node) {
  glm::mat4 transform(1.0f);
  for (; node; node = node->mParent) {
    transform = ToGlm(node->mTransformation) * transform;
  }
  return transform;
}

const aiNode* FindMeshNode(const aiNode* node, unsigned int mesh) {
  for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
    if (node->mMeshes[i] == mesh) {
      return node;
    }
  }
  for (unsigned int i = 0; i < node->mNumChildren; ++i) {
    if (const aiNode* found = FindMeshNode(node->mChildren[i], mesh)) {
      return found;
    }
  }
  return nullptr;
}

SkinnedModel::Surface SurfaceOf(const aiScene* scene, const aiMesh* mesh) {
  std::string name =
      mesh->mMaterialIndex < scene->mNumMaterials
          ? scene->mMaterials[mesh->mMaterialIndex]->GetName().C_Str()
          : "";
  if (name.find("Eye") != std::string::npos) return SkinnedModel::kEye;
  if (name.find("Nose") != std::string::npos) return SkinnedModel::kNose;
  if (name.find("Mouth") != std::string::npos) return SkinnedModel::kMouth;
  if (name.find("Tongue") != std::string::npos) return SkinnedModel::kTongue;
  if (name.find("Teeth") != std::string::npos) return SkinnedModel::kTeeth;
  if (name.find("Claw") != std::string::npos) return SkinnedModel::kClaw;
  return SkinnedModel::kFur;
}
}  // namespace

std::unique_ptr<SkinnedModel> SkinnedModel::Load(const std::string& file,
                                                 std::string& error) {
  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(
      file, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                aiProcess_LimitBoneWeights);
  if (!scene || !scene->mRootNode) {
    error = importer.GetErrorString();
    return nullptr;
  }

  // The skeleton is the node hierarchy below the topmost bone (all bones have
  // the names of their nodes)
  std::set<std::string> bone_names;
  for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
    for (unsigned int b = 0; b < scene->mMeshes[m]->mNumBones; ++b) {
      bone_names.insert(scene->mMeshes[m]->mBones[b]->mName.C_Str());
    }
  }
  if (bone_names.empty()) {
    error = "the model has no bones";
    return nullptr;
  }
  const aiNode* root_bone =
      scene->mRootNode->FindNode(bone_names.begin()->c_str());
  if (!root_bone) {
    error = "there is no node for the bone " + *bone_names.begin();
    return nullptr;
  }
  while (root_bone->mParent &&
         bone_names.contains(root_bone->mParent->mName.C_Str())) {
    root_bone = root_bone->mParent;
  }

  auto model = std::make_unique<SkinnedModel>();
  animation::Skeleton& skeleton = model->skeleton;
  skeleton.root_parent = GlobalTransform(root_bone->mParent);
  std::function<void(const aiNode*, int)> add_bones = [&](const aiNode* node,
                                                          int parent) {
    animation::Bone bone;
    bone.name = node->mName.C_Str();
    bone.parent = parent;
    bone.bind_local = Transform::FromMatrix(ToGlm(node->mTransformation));
    skeleton.bones.push_back(bone);
    int index = static_cast<int>(skeleton.bones.size()) - 1;
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
      add_bones(node->mChildren[i], index);
    }
  };
  add_bones(root_bone, -1);
  // The node transforms are the bind pose (the rest position of the armature)
  skeleton.ComputeInverseBind();

  for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
    const aiMesh* mesh = scene->mMeshes[m];
    if (!mesh->HasPositions() || !mesh->HasFaces()) {
      continue;
    }
    const aiNode* node = FindMeshNode(scene->mRootNode, m);
    glm::mat4 to_model = node ? GlobalTransform(node) : glm::mat4(1.0f);
    glm::mat3 normal_to_model =
        glm::transpose(glm::inverse(glm::mat3(to_model)));
    unsigned int first_vertex =
        static_cast<unsigned int>(model->positions.size());
    Surface surface = SurfaceOf(scene, mesh);

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
      const aiVector3D& p = mesh->mVertices[i];
      model->positions.push_back(
          glm::vec3(to_model * glm::vec4(p.x, p.y, p.z, 1.0f)));
      glm::vec3 normal = mesh->HasNormals() ? glm::vec3(mesh->mNormals[i].x,
                                                        mesh->mNormals[i].y,
                                                        mesh->mNormals[i].z)
                                            : glm::vec3(0.0f, 1.0f, 0.0f);
      model->normals.push_back(glm::normalize(normal_to_model * normal));
      if (mesh->HasVertexColors(0)) {
        const aiColor4D& c = mesh->mColors[0][i];
        model->colors.push_back(
            glm::clamp(glm::vec4(c.r, c.g, c.b, c.a), 0.0f, 1.0f));
      } else {
        model->colors.push_back(
            glm::vec4(0.2f, 0.12f, 0.06f, surface == kFur ? 1.0f : 0.0f));
      }
      model->surfaces.push_back(static_cast<float>(surface));
    }

    // The 4 largest weights of every vertex
    std::vector<std::vector<std::pair<float, int>>> influences(
        mesh->mNumVertices);
    for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
      const aiBone* bone = mesh->mBones[b];
      int index = skeleton.Find(bone->mName.C_Str());
      if (index < 0) {
        continue;
      }
      for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
        if (bone->mWeights[w].mVertexId < mesh->mNumVertices &&
            bone->mWeights[w].mWeight > 0.0f) {
          influences[bone->mWeights[w].mVertexId].push_back(
              {bone->mWeights[w].mWeight, index});
        }
      }
    }
    for (auto& vertex_influences : influences) {
      std::sort(vertex_influences.begin(), vertex_influences.end(),
                std::greater<>());
      glm::ivec4 joints(0);
      glm::vec4 weights(0.0f);
      for (std::size_t i = 0; i < vertex_influences.size() && i < 4; ++i) {
        joints[static_cast<int>(i)] = vertex_influences[i].second;
        weights[static_cast<int>(i)] = vertex_influences[i].first;
      }
      float sum = weights.x + weights.y + weights.z + weights.w;
      if (sum > 0.0f) {
        weights /= sum;
      } else {
        weights = glm::vec4(1.0f, 0.0f, 0.0f,
                            0.0f);  // not skinned: moves with the root bone
      }
      model->joints.push_back(joints);
      model->weights.push_back(weights);
    }

    Part part{surface, static_cast<unsigned int>(model->indices.size()), 0};
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
      const aiFace& face = mesh->mFaces[f];
      if (face.mNumIndices != 3) {
        continue;  // points and lines
      }
      for (unsigned int i = 0; i < 3; ++i) {
        model->indices.push_back(first_vertex + face.mIndices[i]);
      }
    }
    part.index_count =
        static_cast<unsigned int>(model->indices.size()) - part.first_index;
    model->parts.push_back(part);
  }
  if (model->positions.empty()) {
    error = "the model has no meshes";
    return nullptr;
  }
  return model;
}

void SkinnedModel::Skin(const std::vector<glm::mat4>& skin_matrices,
                        std::vector<glm::vec3>& skinned_positions,
                        std::vector<glm::vec3>& skinned_normals,
                        const std::vector<glm::vec3>* directions,
                        std::vector<glm::vec3>* skinned_directions) const {
  const std::size_t count = positions.size();
  skinned_positions.resize(count);
  skinned_normals.resize(count);
  bool with_directions =
      directions && skinned_directions && directions->size() == count;
  if (with_directions) {
    skinned_directions->resize(count);
  }
  for (std::size_t i = 0; i < count; ++i) {
    const glm::ivec4& j = joints[i];
    const glm::vec4& w = weights[i];
    // Most vertices follow fewer than 4 bones (the weights are sorted by size,
    // the unused ones are 0)
    glm::mat4 blended = skin_matrices[j.x] * w.x;
    if (w.y > 0.0f) {
      blended += skin_matrices[j.y] * w.y;
      if (w.z > 0.0f) {
        blended += skin_matrices[j.z] * w.z;
        if (w.w > 0.0f) {
          blended += skin_matrices[j.w] * w.w;
        }
      }
    }
    skinned_positions[i] = glm::vec3(blended * glm::vec4(positions[i], 1.0f));
    glm::mat3 rotation(blended);
    skinned_normals[i] = glm::normalize(rotation * normals[i]);
    if (with_directions) {
      glm::vec3 direction = rotation * (*directions)[i];
      float length = glm::length(direction);
      (*skinned_directions)[i] =
          length > 1e-6f ? direction / length : skinned_normals[i];
    }
  }
}

std::vector<unsigned int> SkinnedModel::Triangles(Surface surface) const {
  std::vector<unsigned int> result;
  for (const Part& part : parts) {
    if (part.surface == surface) {
      result.insert(result.end(), indices.begin() + part.first_index,
                    indices.begin() + part.first_index + part.index_count);
    }
  }
  return result;
}

}  // namespace bibo::resources
