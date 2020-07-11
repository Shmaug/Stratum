#pragma once

#include <Content/Material.hpp>
#include <Content/Mesh.hpp>
#include <Core/DescriptorSet.hpp>
#include <Scene/Renderer.hpp>
#include <Util/Util.hpp>

class ClothRenderer : public Renderer {
public:
bool mVisible;

	ENGINE_EXPORT ClothRenderer(const std::string& name);
	ENGINE_EXPORT ~ClothRenderer();

	inline void Drag(float d) { mDrag = d; }
	inline float Drag() const { return mDrag; }
	inline void Stiffness(float d) { mStiffness = d; }
	inline float Stiffness() const { return mStiffness; }
	inline void Damping(float d) { mDamping = d; }
	inline float Damping() const { return mDamping; }
	inline void Friction(float f) { mFriction = f; }
	inline float Friction() const { return mFriction; }
	inline void Gravity(const float3& g) { mGravity = g; }
	inline float3 Gravity() const { return mGravity; }
	inline void Pin(bool p) { mPin = p; }
	inline bool Pin() const { return mPin; }
	inline void Move(const float3& f) { mMove = f; }
	inline float3 Move() const { return mMove; }

	inline virtual void AddSphereCollider(Object* obj, float radius) { mSphereColliders.push_back(std::make_pair(obj, radius)); }

	ENGINE_EXPORT virtual void Mesh(::Mesh* m);
	ENGINE_EXPORT virtual void Mesh(std::shared_ptr<::Mesh> m);
	inline virtual ::Mesh* Mesh() const { return mMesh.index() == 0 ? std::get<::Mesh*>(mMesh) : std::get<std::shared_ptr<::Mesh>>(mMesh).get(); }

	inline virtual ::Material* Material() { return mMaterial.get(); }
	ENGINE_EXPORT virtual void Material(std::shared_ptr<::Material> m) { mMaterial = m; }

	// Renderer functions

	inline virtual PassType PassMask() override { return mMaterial ? mMaterial->PassMask() : Renderer::PassMask(); }
	inline virtual bool Visible() override { return mVisible && Mesh() && mMaterial && EnabledHierarchy(); }
	inline virtual uint32_t RenderQueue() override { return mMaterial ? mMaterial->RenderQueue() : Renderer::RenderQueue(); }
	
	ENGINE_EXPORT virtual void FixedUpdate(CommandBuffer* commandBuffer) override;
	ENGINE_EXPORT virtual void PreBeginRenderPass(CommandBuffer* commandBuffer, Camera* camera, PassType pass) override;
	ENGINE_EXPORT virtual void Draw(CommandBuffer* commandBuffer, Camera* camera, PassType pass) override;

private:
	std::shared_ptr<::Material> mMaterial;
	std::variant<::Mesh*, std::shared_ptr<::Mesh>> mMesh;
	AABB mAABB;

	Buffer* mVertexBuffer;
	Buffer* mVelocityBuffer;
	Buffer* mForceBuffer;
	Buffer* mColliderBuffer;
	Buffer* mEdgeBuffer;
	bool mCopyVertices;

	std::vector<std::pair<Object*, float>> mSphereColliders;

	bool mPin;
	float3 mMove;
	float mFriction;
	float mDrag;
	float mStiffness;
	float mDamping;
	float3 mGravity;

	ENGINE_EXPORT bool UpdateTransform() override;
};