#ifndef BIBO_RESOURCES_SKINNEDMODEL_HPP_
#define BIBO_RESOURCES_SKINNEDMODEL_HPP_
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

#include "../Animation/Skeleton.hpp"

namespace bibo {
namespace resources {
/**
 * Mesh with a skeleton (e.g. Model/BearCub/BearCub.glb), loaded with Assimp.
 * Needs no graphics card: the vertices are skinned on the CPU (see skin), so
 * that the game (fur, physics) knows where they are.
 */
class SkinnedModel {
 public:
  /**
   * What the material of a vertex is (from the material name in the model file)
   */
  enum Surface {
    // fur color and length in the vertex color, bare skin where the fur length
    // is 0 (paw pads, lips), also the eyelids
    kFur = 0,
    // the vertex color is the direction from the center of the eye in eye space
    // (gaze along +Z), see bear_skin.frag
    kEye = 1,
    kNose = 2,
    kMouth = 3,  // inside of the mouth
    kTongue = 4,
    kTeeth = 5,
    kClaw = 6
  };

  /**
   * Triangles with the same material
   */
  struct Part {
    Surface surface;
    unsigned int first_index;
    unsigned int index_count;
  };

  animation::Skeleton skeleton;
  // In model space in the bind pose (one entry per vertex)
  std::vector<glm::vec3> positions;
  std::vector<glm::vec3> normals;
  // RGB: color (linear), A: fur length from 0 (no fur) to 1 (longest)
  std::vector<glm::vec4> colors;
  std::vector<float> surfaces;        // Surface
  std::vector<glm::ivec4> joints;     // the 4 bones that move a vertex most
  std::vector<glm::vec4> weights;     // their weights (sum 1)
  std::vector<unsigned int> indices;  // 3 per triangle
  std::vector<Part> parts;

  /**
   * @param error Why the file could not be loaded
   * @return nullptr if the file cannot be loaded or has no skinned mesh
   */
  static std::unique_ptr<SkinnedModel> Load(const std::string& file,
                                            std::string& error);

  /**
   * Moves the vertices with the bones (linear blend skinning)
   * @param skinMatrices Skeleton::SkinMatrices of the current pose
   * @param directions Optional directions per vertex (e.g. along the fur),
   * rotated like the normals
   */
  void Skin(const std::vector<glm::mat4>& skin_matrices,
            std::vector<glm::vec3>& skinned_positions,
            std::vector<glm::vec3>& skinned_normals,
            const std::vector<glm::vec3>* directions = nullptr,
            std::vector<glm::vec3>* skinned_directions = nullptr) const;

  /**
   * @return The indices of all triangles with this surface
   */
  std::vector<unsigned int> Triangles(Surface surface) const;
};
}  // namespace resources
}  // namespace bibo

#endif  // BIBO_RESOURCES_SKINNEDMODEL_HPP_
