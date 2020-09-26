#pragma once

#include <Core/CommandBuffer.hpp>
#include <Core/CommandBuffer.hpp>

namespace stm {

// A hierarchical object in a Scene. Keeps track of a transform internally that is updated on-demand during getter functions
class Object {
public:
	const std::string mName;
	Scene* const mScene;

	Object() = delete;
	STRATUM_API virtual ~Object();

	inline float3 WorldPosition() { UpdateTransform(); return mWorldPosition; }
	inline quaternion WorldRotation() { UpdateTransform(); return mWorldRotation; }

	inline float3 LocalPosition() { UpdateTransform(); return mLocalPosition; }
	inline quaternion LocalRotation() { UpdateTransform(); return mLocalRotation; }
	inline float3 LocalScale() { UpdateTransform(); return mLocalScale; }
	inline float3 WorldScale() { UpdateTransform(); return mWorldScale; }

	inline float4x4 ObjectToParent() { UpdateTransform(); return mObjectToParent; }
	inline float4x4 ObjectToWorld() { UpdateTransform(); return mObjectToWorld; }
	inline float4x4 WorldToObject() { UpdateTransform(); return mWorldToObject; }

	inline void LocalPosition(const float3& p) { mLocalPosition = p; DirtyTransform(); }
	inline void LocalRotation(const quaternion& r) { mLocalRotation = r; DirtyTransform(); }
	inline void LocalScale(const float3& s) { mLocalScale = s; DirtyTransform(); }

	inline void LocalPosition(float x, float y, float z) { mLocalPosition.x = x; mLocalPosition.y = y; mLocalPosition.z = z; DirtyTransform(); }
	inline void LocalScale(float x, float y, float z) { mLocalScale.x = x; mLocalScale.y = y; mLocalScale.z = z; DirtyTransform(); }
	inline void LocalScale(float x) { mLocalScale.x = x; mLocalScale.y = x; mLocalScale.z = x; DirtyTransform(); }
	
	inline virtual std::optional<AABB> Bounds() { return {}; }

	inline Object* Parent() const { return mParent; }
	STRATUM_API void AddChild(Object* obj);
	STRATUM_API void RemoveChild(Object* obj);

	inline uint32_t ChildCount() const { return (uint32_t)mChildren.size(); }
	inline Object* Child(uint32_t index) const { return mChildren[index]; }

	inline bool EnabledSelf() const { return mEnabled; };
	STRATUM_API void EnabledSelf(bool e);
	STRATUM_API bool EnabledHierarchy() const { return mEnabledHierarchy; }
	
	// If LayerMask != 0 then the object will be included in the scene's BVH and moving the object will trigger BVH builds
	// Note Renderers should OR this with their PassMask()
	inline virtual void LayerMask(uint32_t m) { mLayerMask = m; };
	inline virtual uint32_t LayerMask() { return mLayerMask; };

	// Returns true when an intersection occurs, assigns t to the intersection time if t is not null
	// If any is true, will return the first hit, otherwise will return the closest hit
	inline virtual bool Intersect(const Ray& ray, float* t, bool any) { return false; }

private:
	friend class Scene;

	bool mEnabled = true;
	bool mEnabledHierarchy = true;
	bool mTransformDirty = true;
	float3 mLocalPosition = 0;
	quaternion mLocalRotation = quaternion(0,0,0,1);
	float3 mLocalScale = 1;
	float3 mWorldPosition;
	quaternion mWorldRotation;
	float3 mWorldScale;
	float4x4 mObjectToParent;
	float4x4 mObjectToWorld;
	float4x4 mWorldToObject;

	uint32_t mLayerMask = 1;

	Object* mParent = nullptr;
	std::deque<Object*> mChildren;

protected:
	STRATUM_API Object(const std::string& name, stm::Scene* scene);

	inline virtual void OnFixedUpdate(CommandBuffer& commandBuffer) {}
	inline virtual void OnUpdate(CommandBuffer& commandBuffer) {}
	inline virtual void OnLateUpdate(CommandBuffer& commandBuffer) {}
	inline virtual void OnGui(CommandBuffer& commandBuffer, Camera& camera, GuiContext& gui) {}

	STRATUM_API virtual void DirtyTransform();
	STRATUM_API virtual bool UpdateTransform();
};

}