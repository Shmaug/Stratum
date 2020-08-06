#pragma once

#include <Data/Material.hpp>
#include <Data/Mesh.hpp>
#include <Scene/Renderer.hpp>

class ClothRenderer : public Renderer {
public:
	STRATUM_API ClothRenderer(const std::string& name);
	STRATUM_API ~ClothRenderer();

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

	STRATUM_API virtual void Mesh(::Mesh* m);
	STRATUM_API virtual void Mesh(std::shared_ptr<::Mesh> m);
	inline virtual ::Mesh* Mesh() const { return mMesh.index() == 0 ? std::get<::Mesh*>(mMesh) : std::get<std::shared_ptr<::Mesh>>(mMesh).get(); }

	inline virtual ::Material* Material() { return mMaterial.get(); }
	STRATUM_API virtual void Material(std::shared_ptr<::Material> m) { mMaterial = m; }


	inline virtual bool Visible(const std::string& pass) override { return Mesh() && mMaterial && Renderer::Visible(pass); }
	inline virtual uint32_t RenderQueue(const std::string& pass) override { return mMaterial ? mMaterial->GetPassPipeline(pass)->mShaderVariant->mRenderQueue : Renderer::RenderQueue(pass); }

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
	
protected:
	STRATUM_API virtual void OnFixedUpdate(CommandBuffer* commandBuffer) override;
	STRATUM_API virtual void OnLateUpdate(CommandBuffer* commandBuffer) override;
	STRATUM_API virtual void OnDraw(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera) override;

	STRATUM_API bool UpdateTransform() override;
};