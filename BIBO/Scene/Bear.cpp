#include "Bear.hpp"

#include "../Resources/Model.hpp"
#include "../Util/Console.hpp"
#include "../Util/Util.hpp"

namespace bibo::scene {

using glm::vec3;

const char* const Bear::kModelFile = "Model/BearCub/BearCub.glb";

namespace {
glm::vec3 StartPosition(const aiScene* track) {
  // The node of the bear in the track is only used as start position (center of
  // the character capsule)
  aiNode* node = track ? track->mRootNode->FindNode("BearBody") : nullptr;
  if (!node) {
    util::FatalError(
        "The track has no node BearBody (start position of the bear)");
  }
  aiMatrix4x4 transformation = node->mTransformation;
  resources::Model::TransformNode(&transformation, node);
  return glm::vec3(util::ConvertMatrix(transformation)[3]);
}

std::unique_ptr<resources::SkinnedModel> LoadModel() {
  std::string error;
  std::unique_ptr<resources::SkinnedModel> model =
      resources::SkinnedModel::Load(Bear::kModelFile, error);
  if (!model) {
    util::FatalError(std::string("Could not load ") + Bear::kModelFile + ": " +
                     error);
  }
  BearRig rig(model->skeleton);
  if (!rig.IsValid()) {
    util::FatalError(std::string("The skeleton of ") + Bear::kModelFile +
                     " has no bones " + rig.GetMissingBones());
  }
  return model;
}
}  // namespace

Bear::Bear(const aiScene* assimp_scene, physics::PhysicsWorld* physics_world,
           physics::CharacterController::HitCallback on_hit)
    : model_(LoadModel()) {
  // The bear starts looking along -z
  character_ = std::make_unique<BearCharacter>(
      *physics_world, *model_, StartPosition(assimp_scene),
      vec3(0.0f, 0.0f, -1.0f), std::move(on_hit));
}

Bear::~Bear() = default;

physics::CharacterControllerDesc Bear::ControllerDesc(vec3 position) {
  return BearCharacter::ControllerDesc(position);
}

void Bear::Update(float time_delta) { character_->Update(time_delta, input_); }

vec3 Bear::GetPosition() const { return character_->GetPosition(); }

vec3 Bear::GetDirection() const { return character_->GetDirection(); }

void Bear::Reset() { character_->Reset(); }

void Bear::KnockDown(const physics::CharacterController::Hit& hit) {
  character_->KnockDown(hit);
}

bool Bear::IsKnockedDown() const {
  return character_->GetState() != BearCharacter::State::kControl;
}

void Bear::SetObstacles(const std::vector<fur::SphereCollider>& obstacles) {
  character_->SetObstacles(obstacles);
}

}  // namespace bibo::scene
