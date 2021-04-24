#pragma once

#include "RenderNode.hpp"

namespace stm {
  
class MeshRenderer : public Scene::Node {
private:
  using material_key_t = pair<const RenderNode*, SubpassIdentifier>;

  shared_ptr<Mesh> mMesh;
  unordered_map<material_key_t, shared_ptr<Material>> mMaterials;

public:
  inline MeshRenderer(Scene& scene, const string& name, const shared_ptr<Mesh>& mesh)
    : Node(scene, name), mMesh(mesh) {}
  inline MeshRenderer(Scene& scene, const string& name, const shared_ptr<Mesh>& mesh, const shared_ptr<Material>& defaultMaterial)
    : Node(scene, name), mMesh(mesh), mMaterials({ { { nullptr, "" }, defaultMaterial } }) {}

  inline auto& mesh() { return mMesh; }
  inline const auto& mesh() const { return mMesh; }

  inline auto& material(const RenderNode& renderNode, const string& subpass = "") {
    return mMaterials[material_key_t{ &renderNode, subpass }];
  }
  inline const auto& material(const RenderNode& renderNode, const string& subpass = "") const {
    return mMaterials.at(material_key_t{ &renderNode, subpass });
  }

  inline void draw(CommandBuffer& commandBuffer, const RenderNode& renderNode) {
    ProfilerRegion ps("MeshRenderer::draw " + name(), commandBuffer);
    auto it = mMaterials.find({ &renderNode, renderNode.at(commandBuffer.subpass_index()).first });
    if (it == mMaterials.end()) it = mMaterials.find({ &renderNode, "" });
    if (it == mMaterials.end()) it = mMaterials.find({ nullptr, "" });
    if (it == mMaterials.end()) return;
    it->second->bind(commandBuffer, mMesh->geometry());
    mMesh->draw(commandBuffer);
  }
};

}