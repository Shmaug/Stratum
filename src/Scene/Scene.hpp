#pragma once

#include "../Core/InputState.hpp"
#include "../Core/RenderPass.hpp"
#include "../Core/Material.hpp"
#include "../Core/Asset/Font.hpp"
#include "../Core/Asset/Mesh.hpp"
#include "../Core/Asset/Texture.hpp"

namespace stm {

class ObjectIntersector {
public:
	bool operator()(Object* object, const float4 frustum[6]);
	bool operator()(Object* object, const fRay& ray, float* t, bool any);
};
using ObjectBvh2 = bvh_t<float, Object*, ObjectIntersector>;

class Scene {
public:
	class Plugin {
	public:
		// Higher priority plugins get called first
		inline virtual int Priority() { return 50; }
		
	protected:
		friend class Scene;
		inline virtual void OnPreUpdate(CommandBuffer& commandBuffer) {}
		inline virtual void OnFixedUpdate(CommandBuffer& commandBuffer) {}
		inline virtual void OnUpdate(CommandBuffer& commandBuffer) {}
		inline virtual void OnLateUpdate(CommandBuffer& commandBuffer) {}
		// Called before the Scene begins rendering a frame, used to easily queue GUI drawing operations
		inline virtual void OnGui(CommandBuffer& commandBuffer, GuiContext& gui) {}
		// Called before the Scene begins rendering a frame
		inline virtual void OnPreRender(CommandBuffer& commandBuffer) {}
		// Called during a Subpass for each camera that renders to an attachment that the subpass outputs
		inline virtual void OnRenderCamera(CommandBuffer& commandBuffer, Camera& camera) {}
		// Called after the Scene ends a full RenderPass
		inline virtual void OnPostProcess(CommandBuffer& commandBuffer, shared_ptr<Framebuffer> framebuffer, const set<Camera*>& cameras) {}

		// Called before the window presents the next swapchain image, after the command buffer(s) are executed
		inline virtual void PrePresent() {}
	};

	struct LightingData {
		shared_ptr<Buffer> mLightBuffer;
		shared_ptr<Texture> mShadowAtlas;
		float3 mAmbientLight;
		uint32_t mLightCount;
	};

	STRATUM_API Scene(stm::Instance& instance);
	STRATUM_API ~Scene();

	inline stm::Instance& Instance() const { return mInstance; }

	template<class T, typename... Args> requires(derived_from<T, Object> && constructible_from<T, Args...>)
	inline T* CreateObject(const string& name, Args... args) {
		T* object = new T(name, *this, args...);
		mObjects.push_back(object);
		if (derived_from<Renderer, Object>) mRenderers.push_back((Renderer*)object);
		if (derived_from<Camera, Object>) mCameras.push_back((Camera*)object);
		if (derived_from<Light, Object>) mLights.push_back((Light*)object);
		return object;
	}
	STRATUM_API void RemoveObject(Object* object);

	STRATUM_API const deque<Subpass>& GetRenderNode(const string& nodeName) const { return mRenderNodes.at(nodeName).mSubpasses; }
	STRATUM_API void AssignRenderNode(const string& nodeName, const deque<Subpass>& subpasses);
	STRATUM_API void DeleteRenderNode(const string& nodeName);

	STRATUM_API void Update(CommandBuffer& commandBuffer);
	// Renders RenderNodes in the scene
	STRATUM_API void Render(CommandBuffer& commandBuffer);
	// Draw all renderers in view of a camera
	STRATUM_API void RenderCamera(CommandBuffer& commandBuffer, Camera& camera);

	inline Object* Intersect(const fRay& ray, float* t = nullptr, bool any = false) { return *BVH()->Intersect(ray, t, any); }

	// Setters
	STRATUM_API void SetAttachmentInfo(const RenderTargetIdentifier& name, const vk::Extent2D& extent, vk::ImageUsageFlags usage);
	inline void MainRenderExtent(const vk::Extent2D& extent) {
		SetAttachmentInfo("stm_main_render", { extent }, GetAttachmentInfo("stm_main_render").second);
		SetAttachmentInfo("stm_main_depth", { extent }, GetAttachmentInfo("stm_main_depth").second);
		SetAttachmentInfo("stm_main_resolve", { extent }, GetAttachmentInfo("stm_main_resolve").second);
	}
	inline void AmbientLight(const float3& t) { mLighting.mAmbientLight = t; }
	inline void EnvironmentTexture(shared_ptr<Texture> t) { mEnvironmentTexture = t; }
	inline void FixedTimeStep(float step) { mFixedTimeStep = step; }
	inline void PhysicsTimeLimitPerFrame(float t) { mPhysicsTimeLimitPerFrame = t; }
	inline void InvalidateBvh(Object* source = nullptr) { safe_delete(mBvh); }

	// Getters
	inline bool HasAttachment(const RenderTargetIdentifier& name) const { return mAttachments.count(name); }
	inline shared_ptr<Texture> GetAttachment(const RenderTargetIdentifier& name) const { return mAttachments.at(name); }
	STRATUM_API pair<vk::Extent2D, vk::ImageUsageFlags> GetAttachmentInfo(const RenderTargetIdentifier& name) const { return mAttachmentInfo.at(name); };
	inline float3 AmbientLight() const { return mLighting.mAmbientLight; }
	inline shared_ptr<Texture> EnvironmentTexture() const { return mEnvironmentTexture; }
	inline float FixedTimeStep() const { return mFixedTimeStep; }
	inline float PhysicsTimeLimitPerFrame() const { return mPhysicsTimeLimitPerFrame; }
	inline float FPS() const { return mFps; }
	inline float TotalTime() const { return mTotalTime; }
	inline float DeltaTime() const { return mDeltaTime; }
	STRATUM_API vector<Object*> Objects() const;
	STRATUM_API ObjectBvh2* BVH();
	inline uint64_t LastBvhBuild() { return mLastBvhBuild; }

	STRATUM_API Plugin* LoadPlugin(const fs::path& filename);

	template<class T> inline T* GetPlugin() const {
		for (const ScenePlugin* p : mPlugins)
			if (const T* t = dynamic_cast<T*>(p))
				return t;
		return nullptr;
	}

private:
	friend class Object;
	
	struct RenderGraphNode {
		deque<Subpass> mSubpasses;
		set<RenderTargetIdentifier> mNonSubpassDependencies;
		shared_ptr<RenderPass> mRenderPass;
		shared_ptr<Framebuffer> mFramebuffer;
	};

	STRATUM_API void AddObject(Object* object);
	STRATUM_API void BuildRenderGraph(CommandBuffer& commandBuffer);

	unordered_map<string, InputState> mInputStates;
	unordered_map<string, InputState> mInputStatesPrevious;
	
	stm::Instance& mInstance;
	vector<Plugin*> mPlugins;

	bool mRenderGraphDirty = true;
	vector<RenderGraphNode*> mRenderGraph;
	map<string, RenderGraphNode> mRenderNodes;
	map<RenderTargetIdentifier, shared_ptr<Texture>> mAttachments;
	map<RenderTargetIdentifier, pair<vk::Extent2D, vk::ImageUsageFlags>> mAttachmentInfo;
	
	deque<Object*> mObjects;
	deque<Light*> mLights;
	deque<Camera*> mCameras;
	deque<Renderer*> mRenderers;
	unordered_map<Camera*, GuiContext*> mGuiContexts;
	
	ObjectBvh2* mBvh = nullptr;
	uint64_t mLastBvhBuild = 0;

	Renderer* mSkybox = nullptr;
	shared_ptr<DescriptorSet> mPerCamera;
	LightingData mLighting;
	shared_ptr<Texture> mEnvironmentTexture;
	shared_ptr<Sampler> mShadowSampler;

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