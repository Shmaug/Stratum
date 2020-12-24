#pragma once

#include "../Core/CommandBuffer.hpp"
#include "GuiContext.hpp"

namespace stm {

class Object {
public:
	Object() = delete; // Must be constructed by Scene
	STRATUM_API virtual ~Object();

	inline const string& Name() const { return mName; }
	inline stm::Scene& Scene() const { return mScene; }

	inline void LocalPosition(const float3& p) { mLocalPosition = p; InvalidateTransform(); }
	inline void LocalRotation(const fquat& r) { mLocalRotation = r; InvalidateTransform(); }
	inline void LocalScale(const float3& s) { mLocalScale = s; InvalidateTransform(); }
	inline void LocalPosition(float x, float y, float z) { LocalPosition(float3(x,y,z)); }
	inline void LocalScale(float x, float y, float z) { LocalScale(float3(x,y,z)); }
	
	inline float3 LocalPosition() { ValidateTransform(); return mLocalPosition; }
	inline fquat LocalRotation() { ValidateTransform(); return mLocalRotation; }
	inline float3 LocalScale() { ValidateTransform(); return mLocalScale; }
	inline float3 Position() { ValidateTransform(); return (float3)mCachedTransform[2]; }
	inline fquat Rotation() { ValidateTransform(); return mCachedRotation; }
	inline float4x4 LocalTransform() { ValidateTransform(); return mCachedLocalTransform; }
	inline float4x4 Transform() { ValidateTransform(); return mCachedTransform; }
	inline float4x4 InverseTransform() { ValidateTransform(); return mCachedInverseTransform; }

	inline Object* Parent() const { return mParent; }
	inline uint32_t ChildCount() const { return (uint32_t)mChildren.size(); }
	inline Object* Child(uint32_t index) const { return mChildren[index]; }
	STRATUM_API void AddChild(Object* obj);
	STRATUM_API void RemoveChild(Object* obj);

	STRATUM_API void Enabled(bool e) { mEnabled = e; }
	STRATUM_API bool Enabled() const { return mEnabled; }
	
	// If LayerMask != 0 then the object will be included in the scene's BVH and moving the object will trigger BVH builds
	// Note Renderers should OR this with their PassMask()
	inline virtual void LayerMask(uint32_t m) { mLayerMask = m; };
	inline virtual uint32_t LayerMask() { return mLayerMask; };

	inline virtual optional<fAABB> Bounds() { return {}; }

	// Returns true when an intersection occurs, assigns t to the intersection time if t is not null
	// If any is true, will return the first hit, otherwise will return the closest hit
	inline virtual bool Intersect(const fRay& ray, float* t, bool any) { return false; }
	inline virtual bool Intersect(const float4 frustum[6]) { return true; }

protected:
	inline Object(const string& name, stm::Scene& scene) : mName(name), mScene(scene) { InvalidateTransform(); }

	inline virtual void OnFixedUpdate(CommandBuffer& commandBuffer) {}
	inline virtual void OnUpdate(CommandBuffer& commandBuffer) {}
	inline virtual void OnLateUpdate(CommandBuffer& commandBuffer) {}
	inline virtual void OnGui(CommandBuffer& commandBuffer, GuiContext& gui) {}

	STRATUM_API virtual void InvalidateTransform();
	STRATUM_API virtual bool ValidateTransform();

private:
	string mName;
	stm::Scene& mScene;

	friend class Scene;
	float3 mLocalPosition = 0;
	fquat mLocalRotation = fquat(0,0,0,1);
	float3 mLocalScale = 1;
	uint32_t mLayerMask = 1;
	Object* mParent = nullptr;
	deque<Object*> mChildren;
	bool mEnabled = true;

	bool mCacheValid = false;
	float4x4 mCachedLocalTransform;
	float4x4 mCachedTransform;
	float4x4 mCachedInverseTransform;
	fquat mCachedRotation;
};

inline bool ObjectIntersector::operator()(Object* object, const float4 frustum[6]) { return object->Intersect(frustum); }
inline bool ObjectIntersector::operator()(Object* object, const fRay& ray, float* t, bool any) { return object->Intersect(ray, t, any); }

}