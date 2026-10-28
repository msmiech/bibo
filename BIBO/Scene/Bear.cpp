#include "Bear.hpp"

#include "../Util/Console.hpp"
#include "../Util/Util.hpp"

namespace bibo::scene {

using glm::mat4;
using glm::vec3;
using glm::vec4;

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

Bear::Bear(GLFWwindow& window, const aiScene* assimp_scene,
           physics::PhysicsWorld* physics_world,
           physics::CharacterController::HitCallback on_hit)
    : Geometry(mat4(1.0f), window), model_(LoadModel()) {
  // The bear starts looking along -z
  character_ = std::make_unique<BearCharacter>(
      *physics_world, *model_, StartPosition(assimp_scene),
      vec3(0.0f, 0.0f, -1.0f), std::move(on_hit));
  renderer_ = std::make_unique<BearRenderer>(*model_, kStrandCount);
  model_matrix[3] = vec4(character_->GetPosition(), 1.0f);
}

Bear::~Bear() = default;

physics::CharacterControllerDesc Bear::ControllerDesc(vec3 position) {
  return BearCharacter::ControllerDesc(position);
}

void Bear::Update(float time_delta) {
  character_->Update(time_delta, input_);
  model_matrix[3] = vec4(character_->GetPosition(), 1.0f);
}

void Bear::UploadIfChanged() {
  if (uploaded_version_ != character_->GetVersion()) {
    renderer_->Upload(character_->GetPositions(), character_->GetNormals(),
                      character_->GetFurOffsets(),
                      character_->GetCombDirections());
    uploaded_version_ = character_->GetVersion();
  }
}

void Bear::Draw(Shader* shader) {
  UploadIfChanged();
  renderer_->DrawSurface(shader);
}

void Bear::DrawShaded(const BearRenderContext& context) {
  UploadIfChanged();
  renderer_->DrawShaded(context, character_->GetBoundsCenter());
}

vec3 Bear::GetPosition() { return character_->GetPosition(); }

vec3 Bear::GetDirection() { return character_->GetDirection(); }

bool Bear::IsVisible(const ViewFrustum& frustum) {
  return frustum.SphereInFrustum(character_->GetBoundsCenter(),
                                 character_->GetBoundsRadius()) !=
         ViewFrustum::kOutside;
}

void Bear::Reset() {
  character_->Reset();
  model_matrix[3] = vec4(character_->GetPosition(), 1.0f);
}

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
