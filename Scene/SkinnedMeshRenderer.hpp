#pragma once

#include <Data/Animation.hpp>
#include <Scene/MeshRenderer.hpp>

class SkinnedMeshRenderer : public Renderer {
public:
	STRATUM_API SkinnedMeshRenderer(const std::string& name);
	STRATUM_API ~SkinnedMeshRenderer();

	inline virtual float ShapeKey(const std::string& name) const { return mShapeKeys.at(name); };
	inline virtual void ShapeKey(const std::string& name, float val) { mShapeKeys[name] = val; };

	STRATUM_API virtual AnimationRig& Rig() { return mRig; };
	STRATUM_API virtual void Rig(const AnimationRig& rig);
	STRATUM_API virtual Bone* GetBone(const std::string& name) const;

	inline virtual void Mesh(::Mesh* m) { mMesh = m; DirtyTransform(); }
	inline virtual void Mesh(std::shared_ptr<::Mesh> m) { mMesh = m; DirtyTransform(); }
	inline virtual ::Mesh* Mesh() const { return mMesh.index() == 0 ? std::get<::Mesh*>(mMesh) : std::get<std::shared_ptr<::Mesh>>(mMesh).get(); }

	inline virtual ::Material* Material() { return mMaterial.get(); }
	STRATUM_API virtual void Material(std::shared_ptr<::Material> m) { mMaterial = m; }

	// Renderer functions
protected:
	inline virtual bool Visible(const std::string& pass) override { return Mesh() && mMaterial && Renderer::Visible(pass); }
	inline virtual uint32_t RenderQueue(const std::string& pass) override { return mMaterial ? mMaterial->GetPassPipeline(pass)->mShaderVariant->mRenderQueue : Renderer::RenderQueue(pass); }
	
	STRATUM_API virtual void OnLateUpdate(CommandBuffer* commandBuffer) override;
	STRATUM_API virtual void OnDraw(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera) override;
	
	STRATUM_API void OnGui(CommandBuffer* commandBuffer, Camera* camera, GuiContext* gui);

private:
	std::shared_ptr<::Material> mMaterial;
	std::variant<::Mesh*, std::shared_ptr<::Mesh>> mMesh;
	AABB mAABB;

	Buffer* mVertexBuffer;

	std::unordered_map<std::string, Bone*> mBoneMap;
	AnimationRig mRig;
	std::unordered_map<std::string, float> mShapeKeys;

	STRATUM_API bool UpdateTransform() override;
};