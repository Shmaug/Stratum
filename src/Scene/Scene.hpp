#pragma once

#include "../Core/InputState.hpp"
#include "../Core/Material.hpp"

namespace stm {

class Scene;

class SceneNode {
public:
	class Component;
	
private:
	deque<unique_ptr<Component>> mComponents;
	deque<unique_ptr<SceneNode>> mChildren;
	SceneNode* mParent = nullptr;

	string mName;
	bool mEnabled = true;
	uint32_t mLayerMask = 1;
	
	Matrix4f mLocalTransform;
	Matrix4f mGlobalTransform;
	TransformTraits mLocalTransformType = TransformTraits::Isometry;
	TransformTraits mGlobalTransformType = TransformTraits::Isometry;
	bool mTransformValid = false;
	
public:
	stm::Scene& mScene;

	inline SceneNode(stm::Scene& scene, const string& name) : mScene(scene), mName(name) {}
	inline ~SceneNode() {
		mComponents.clear();
		mChildren.clear();
		if (mParent) mParent->RemoveChild(*this);
	}

	inline const string& Name() const { return mName; }
	inline string FullName() const { return mParent ? mParent->FullName()+"/"+mName : mName; }

	inline bool Enabled() const { return mEnabled; }
	inline void Enabled(bool e) { mEnabled = e; }

	// If LayerMask != 0 then the object will be included in the scene's BVH and moving the object will trigger BVH builds
	// Note Renderers should OR this with their PassMask()
	inline virtual void LayerMask(uint32_t m) { mLayerMask = m; };
	inline virtual uint32_t LayerMask() { return mLayerMask; };

	
	inline SceneNode* Parent() const { return mParent; }
	inline const deque<unique_ptr<SceneNode>>& Children() const { return mChildren; }

	inline SceneNode& AddChild(unique_ptr<SceneNode>&& n) {
		if (n->mParent == this) return *n;
		if (n->mParent) n = move(n->mParent->RemoveChild(*n));
		auto& ptr = mChildren.emplace_back(move(n));
		ptr->mParent = this;
		ptr->InvalidateTransform();
		ranges::for_each(mComponents, [&ptr](auto& c){ c->OnAddChild(*ptr); });
		return *ptr;
	}
	inline unique_ptr<SceneNode> RemoveChild(SceneNode& n) {
		auto it = ranges::find_if(mChildren, [&](const auto& c){ return c.get() == &n; });
		if (it != mChildren.end()) {
			unique_ptr<SceneNode> ptr = move(*it);
			ptr->mParent = nullptr;
			ptr->InvalidateTransform();
			erase(mChildren, nullptr);
			ranges::for_each(mComponents, [&ptr](auto& c){ c->OnRemoveChild(*ptr); });
			return ptr;
		}
		return nullptr;
	}

	template<typename Callable>
	inline void for_each_ancestor(Callable&& fn) {
		SceneNode* n = mParent;
		while (n) {
			fn(*n);
			n = n->mParent;
		}
	}
	template<typename Callable>
	inline void for_each_descendant(Callable&& fn) {
		if (mChildren.empty()) return;
		deque<SceneNode*> nodes(mChildren.size());
		ranges::transform(mChildren, nodes.begin(), [](const auto& c){return c.get();});
		while (!nodes.empty()) {
			auto n = nodes.front();
			nodes.pop_front();
			fn(*n);
			if (n->mChildren.empty()) continue;
			nodes.resize(nodes.size() + n->mChildren.size());
			ranges::copy_backward(views::transform(n->mChildren, [](auto& n){return n.get();}), nodes.end());
		}
	}

	#pragma region Transform
	inline const Vector3f Translation() const {
		switch (mLocalTransformType) {
			default:
			case TransformTraits::Projective: return Projective3f(mLocalTransform).translation();
			case TransformTraits::Affine: 	  return Affine3f(mLocalTransform).translation();
			case TransformTraits::Isometry:   return Isometry3f(mLocalTransform).translation();
		}
	}
	inline const Matrix3f Rotation() const {
		switch (mLocalTransformType) {
			default:
			case TransformTraits::Projective: return Projective3f(mLocalTransform).rotation();
			case TransformTraits::Affine: 	  return Affine3f(mLocalTransform).rotation();
			case TransformTraits::Isometry:   return Isometry3f(mLocalTransform).rotation();
		}
	}

	inline const Matrix4f& LocalToParent() const { return mLocalTransform; }
	inline const Matrix4f& LocalToGlobal() { ValidateTransform(); return mGlobalTransform; }

	template<int Cols>
	inline const Matrix<float,4,Cols> LocalToParent(const Matrix<float,4,Cols>& v) const {
		switch (mLocalTransformType) {
			default:
			case TransformTraits::Projective: return Projective3f(mLocalTransform) * v;
			case TransformTraits::Affine: 	  return Affine3f(mLocalTransform) * v;
			case TransformTraits::Isometry:   return Isometry3f(mLocalTransform) * v;
		}
	}
	template<int Mode>
	inline const Matrix4f LocalToParent(const Eigen::Transform<float,3,Mode>& v) const {
		switch (mLocalTransformType) {
			default:
			case TransformTraits::Projective: return (Projective3f(mLocalTransform) * v).matrix();
			case TransformTraits::Affine: 	  return (Affine3f(mLocalTransform) * v).matrix();
			case TransformTraits::Isometry:   return (Isometry3f(mLocalTransform) * v).matrix();
		}
	}
	
	template<int Cols>
	inline const Matrix<float,4,Cols> LocalToGlobal(const Matrix<float,4,Cols>& v) {
		ValidateTransform();
		switch (mGlobalTransformType) {
			default:
			case TransformTraits::Projective: return Projective3f(mGlobalTransform) * v;
			case TransformTraits::Affine: 		return Affine3f(mGlobalTransform) * v;
			case TransformTraits::Isometry: 	return Isometry3f(mGlobalTransform) * v;
		}
	}
	template<int Mode>
	inline const Matrix4f LocalToGlobal(const Eigen::Transform<float,3,Mode>& v) {
		ValidateTransform();
		switch (mGlobalTransformType) {
			default:
			case TransformTraits::Projective: return (Projective3f(mGlobalTransform) * v).matrix();
			case TransformTraits::Affine: 		return (Affine3f(mGlobalTransform) * v).matrix();
			case TransformTraits::Isometry: 	return (Isometry3f(mGlobalTransform) * v).matrix();
		}
	}

	inline void InvalidateTransform() { mTransformValid = false; }
	inline void ValidateTransform() {
		if (mTransformValid) return;
		if (mParent) {
			switch (mLocalTransformType) {
				default:
				case TransformTraits::Projective: mGlobalTransform = mParent->LocalToGlobal(Projective3f(mLocalTransform)); break;
				case TransformTraits::Affine: 		mGlobalTransform = mParent->LocalToGlobal(Affine3f(mLocalTransform)); break;
				case TransformTraits::Isometry: 	mGlobalTransform = mParent->LocalToGlobal(Isometry3f(mLocalTransform)); break;
			}
			mGlobalTransformType = max(mParent->mGlobalTransformType, mLocalTransformType);
		} else {
			mGlobalTransform = mLocalTransform;
			mGlobalTransformType = mLocalTransformType;
		}
		mTransformValid = true;
		ranges::for_each(mComponents, [&](auto& c) { c->OnValidateTransform(mGlobalTransform, mGlobalTransformType); });
	}
	#pragma endregion

	inline const deque<unique_ptr<Component>>& Components() const { return mComponents; }

	template<class C, typename... Args> requires(is_base_of_v<Component,C> && constructible_from<C,SceneNode&,Args...>)
	inline C& CreateComponent(Args&&... args) {
		return **mComponents.emplace_back(make_unique<C>(*this, forward<Args>(args)...));
	}
	template<class C> requires(is_base_of_v<Component,C>)
	inline unique_ptr<C> RemoveComponent(C& c) {
		auto it = ranges::find(mComponents, &c);
		if (it != mComponents.end()) {
			unique_ptr<C> r = move(*it);
			erase(mComponents, nullptr);
			return r;
		} else
			return nullptr;
	}

	template<class T, typename Callable> requires(is_base_of_v<Component,T>)
	inline void for_each_component(Callable&& fn) const {
		for (auto& c : mComponents)
			if (T* t = dynamic_cast<T*>(c.get()))
				fn(t);
	}

	template<class T> requires(is_base_of_v<Component,T>)
	inline T* get_component() const {
		for (auto& c : mComponents)
			if (T* t = dynamic_cast<T*>(c.get()))
				return t;
		return nullptr;
	}
	
	template<class T, ranges::range R> requires(is_base_of_v<Component,T>)
	inline R& get_components(R& dst) const {
		for_each_component([&](auto& c) { dst.push_back(c); });
		return dst;
	}
	template<class T, ranges::range R = vector<T>> requires(is_base_of_v<Component,T>)
	inline R get_components() const {
		R dst;
		return get_components<T>(dst);
	}

	class Component {
	private:
		string mName;
		bool mEnabled = true;

	public:
		SceneNode& mNode;

		inline Component(SceneNode& node, const string& name) : mNode(node), mName(name) {}

		inline const string& Name() const { return mName; }
		inline string FullName() const { return mNode.FullName()+"/"+mName; }

		inline bool Enabled() const { return mEnabled; }
		inline void Enabled(bool e) { mEnabled = e; }

	protected:
		friend class Scene;
		friend class SceneNode;

		inline virtual void OnAddChild(SceneNode& n) {}
		inline virtual void OnRemoveChild(SceneNode& n) {}
		
		inline virtual void OnFixedUpdate(CommandBuffer& commandBuffer) {}
		inline virtual void OnUpdate(CommandBuffer& commandBuffer) {}

		inline virtual void OnValidateTransform(Matrix4f& globalTransform, TransformTraits& globalTransformTraits) {}
		template<typename T, int Mode> inline void OnValidateTransform(Transform<T,3,Mode>& p) { OnValidateTransform(p.matrix(), Mode); }
	};
};

class Scene {
public:
	stm::Instance& mInstance;

	STRATUM_API Scene(stm::Instance& instance);
	STRATUM_API ~Scene();

	inline SceneNode& Root() { return mRoot; }
	inline const SceneNode& Root() const { return mRoot; }

	STRATUM_API void Update(CommandBuffer& commandBuffer);

	inline void FixedTimeStep(float step) { mFixedTimeStep = step; }
	inline void PhysicsTimeLimitPerFrame(float t) { mPhysicsTimeLimitPerFrame = t; }
	
	inline float FixedTimeStep() const { return mFixedTimeStep; }
	inline float PhysicsTimeLimitPerFrame() const { return mPhysicsTimeLimitPerFrame; }
	inline float FPS() const { return mFps; }
	inline float TotalTime() const { return mTotalTime; }
	inline float DeltaTime() const { return mDeltaTime; }

	template<class T, ranges::range R> requires(is_base_of_v<SceneNode::Component,T>)
	inline R& get_components(R& dst) const {
		mRoot.for_each_descendant([&](auto& n){ n.get_all<T>(dst); });
		return dst;
	}
	template<class T, ranges::range R = vector<T>> requires(is_base_of_v<SceneNode::Component,T>)
	inline R get_components() const {
		R dst;
		return get_components<T>(dst);
	}

private:
	SceneNode mRoot;
	
	unordered_map<string, InputState> mInputStates;
	unordered_map<string, InputState> mInputStatesPrevious;
	
	float mPhysicsTimeLimitPerFrame = 0.1f;
	float mFixedAccumulator = 0;
	float mFixedTimeStep = 1.f/60.f;

	float mTotalTime = 0;
	float mDeltaTime = 0;

	chrono::high_resolution_clock mClock;
	chrono::high_resolution_clock::time_point mStartTime;
	chrono::high_resolution_clock::time_point mLastFrame;
	float mFrameTimeAccum = 0;
	uint32_t mFpsAccum = 0;
	float mFps = 0;
};

}