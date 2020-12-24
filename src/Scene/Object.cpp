#include "Object.hpp"
#include "Camera.hpp"


using namespace stm;

Object::~Object() {
	while (mChildren.size()) {
		Object* c = mChildren.back();
		mChildren.pop_back();
		c->mParent = nullptr;
		c->InvalidateTransform();
	}
	if (mParent) mParent->RemoveChild(this);
	mScene.RemoveObject(this);
}

bool Object::ValidateTransform() {
	if (mCacheValid) return false;
	mCachedLocalTransform = float4x4::TRS(mLocalPosition, mLocalRotation, mLocalScale);
	if (mParent) {
		mParent->ValidateTransform();
		mCachedTransform = mParent->mCachedTransform * mCachedLocalTransform;
		mCachedRotation = mParent->mCachedRotation * mLocalRotation;
	} else {
		mCachedTransform = mCachedLocalTransform;
		mCachedRotation = mLocalRotation;
	}
	mCachedInverseTransform = inverse(mCachedTransform);
	mCacheValid = true;
	return true;
}

void Object::AddChild(Object* c) {
	if (c->mParent == this) return;
	if (c->mParent) c->mParent->RemoveChild(c);
	mChildren.push_back(c);
	c->mParent = this;
	c->InvalidateTransform();
}
void Object::RemoveChild(Object* c) {
	if (c->mParent != this) return;
	mChildren.erase(ranges::find(mChildren, c));
	c->mParent = nullptr;
	c->InvalidateTransform();
}

void Object::InvalidateTransform() {
	if (Bounds()) mScene.InvalidateBvh(this);
	mCacheValid = false;
	if (mChildren.size()) {
		queue<Object*> objs;
		for (Object* c : mChildren) objs.push(c);
		while (!objs.empty()) {
			Object* c = objs.front();
			objs.pop();
			c->mCacheValid = false;
			if (c->Bounds()) mScene.InvalidateBvh(this);
			for (Object* o : c->mChildren) objs.push(o);
		}
	}
}