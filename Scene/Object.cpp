#include <Scene/Object.hpp>
#include <Scene/Camera.hpp>
#include <Scene/Scene.hpp>
#include <Core/EnginePlugin.hpp>

using namespace std;

Object::Object(const string& name) : mName(name), mScene(nullptr), mLayerMask(0x01), mParent(nullptr), 
	mLocalPosition(float3()), mLocalRotation(quaternion(0, 0, 0, 1)), mLocalScale(float3(1)), mWorldPosition(float3()), 
	mWorldRotation(quaternion(0, 0, 0, 1)), mObjectToWorld(float4x4(1)), mWorldToObject(float4x4(1)), mTransformDirty(true), mEnabled(true), mEnabledHierarchy(true) {
	DirtyTransform();
}
Object::~Object() {
	while (mChildren.size()) {
		Object* c = mChildren.back();
		mChildren.pop_back();
		c->mParent = nullptr;
		c->DirtyTransform();
		c->EnabledSelf(c->mEnabled);
	}
	if (mParent) mParent->RemoveChild(this);
	if (mScene) mScene->DestroyObject(this, false);
}

bool Object::UpdateTransform() {
	if (!mTransformDirty) return false;

	mObjectToParent = float4x4::TRS(mLocalPosition, mLocalRotation, mLocalScale);

	if (mParent) {
		mObjectToWorld = mParent->ObjectToWorld() * mObjectToParent;
		mWorldPosition = (mParent->mObjectToWorld * float4(mLocalPosition, 1.f)).xyz;
		mWorldRotation = mParent->mWorldRotation * mLocalRotation;
	} else {
		mObjectToWorld = mObjectToParent;
		mWorldPosition = mLocalPosition;
		mWorldRotation = mLocalRotation;
	}

	mWorldToObject = inverse(mObjectToWorld);
	mWorldScale.x = length(mObjectToWorld[0].xyz);
	mWorldScale.y = length(mObjectToWorld[1].xyz);
	mWorldScale.z = length(mObjectToWorld[2].xyz);
	mBounds = AABB(mWorldPosition, mWorldPosition);

	mTransformDirty = false;
	return true;
}

void Object::AddChild(Object* c) {
	if (c->mParent == this) return;
	if (c->mParent) c->mParent->RemoveChild(c);
	mChildren.push_back(c);
	c->mParent = this;
	c->DirtyTransform();
}
void Object::RemoveChild(Object* c) {
	if (c->mParent != this) return;
	for (auto it = mChildren.begin(); it != mChildren.end(); it++)
		if (*it == c) { mChildren.erase(it); break; }
	c->mParent = nullptr;
	c->DirtyTransform();
	c->EnabledSelf(c->mEnabled);
}

void Object::DirtyTransform() {
	if (mScene && LayerMask()) mScene->BvhDirty(this);
	mTransformDirty = true;
	queue<Object*> objs;
	for (Object* c : mChildren) objs.push(c);
	while (!objs.empty()) {
		Object* c = objs.front();
		objs.pop();
		c->mTransformDirty = true;
		if (mScene && c->LayerMask() && !c->BypassCulling()) mScene->BvhDirty(this);
		for (Object* o : c->mChildren) objs.push(o);
	}
}

void Object::EnabledSelf(bool e) {
	mEnabled = e;
	mEnabledHierarchy = mEnabled && (!mParent || mParent->mEnabledHierarchy);
	queue<Object*> objs;
	for (Object* c : mChildren) objs.push(c);
	while (!objs.empty()) {
		Object* c = objs.front();
		objs.pop();
		c->mEnabledHierarchy = c->mEnabled && c->mParent->mEnabledHierarchy;
		if (mScene && c->LayerMask() && !c->BypassCulling()) mScene->BvhDirty(this);
		for (Object* o : c->mChildren) objs.push(o);
	}
}