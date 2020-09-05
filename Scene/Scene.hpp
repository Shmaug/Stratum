#pragma once

#include <Core/RenderPass.hpp>
#include <Data/AssetManager.hpp>
#include <Data/Font.hpp>
#include <Data/Material.hpp>
#include <Data/Mesh.hpp>
#include <Data/Texture.hpp>
#include <Input/InputManager.hpp>
#include <Scene/GuiContext.hpp>
#include <Scene/ObjectBvh2.hpp>

class Scene {
public:
	STRATUM_API Scene(::Instance* instance);
	STRATUM_API ~Scene();

	template<class T, typename... Args>
	inline T* CreateObject(Args... args) {
		static_assert(std::is_base_of<Object, T>::value);
		T* object = new T(args...);
		AddObjectInternal(object);
		return object;
	}
	STRATUM_API void DestroyObject(Object* object, bool freeptr = true);

	STRATUM_API const std::deque<Subpass>& GetRenderNode(const std::string& nodeName) const { return mRenderNodes.at(nodeName).mSubpasses; }
	STRATUM_API void AssignRenderNode(const std::string& nodeName, const std::deque<Subpass>& subpasses);
	STRATUM_API void DeleteRenderNode(const std::string& nodeName);

	// Updates physics, objects, and lighting 
	STRATUM_API void Update(stm_ptr<CommandBuffer> commandBuffer);
	// Renders RenderNodes in the scene
	STRATUM_API void Render(stm_ptr<CommandBuffer> commandBuffer);
	// Draw all renderers in view of a camera
	STRATUM_API void RenderCamera(stm_ptr<CommandBuffer> commandBuffer, Camera* camera);
	
	// Call commandBuffer->PushConstant() for any scene information i.e. AmbientLight, LightCount, etc.
	STRATUM_API void PushSceneConstants(stm_ptr<CommandBuffer> commandBuffer);


	inline Object* Raycast(const Ray& worldRay, float* t = nullptr, bool any = false, uint32_t mask = 0xFFFFFFFF) { return BVH()->Intersect(worldRay, t, any, mask); }

	// Setters
	STRATUM_API void SetAttachmentInfo(const RenderTargetIdentifier& name, const vk::Extent2D& extent, vk::ImageUsageFlags usage);
	STRATUM_API void MainRenderExtent(const vk::Extent2D& extent);

	inline void AmbientLight(const float3& t) { mAmbientLight = t; }
	inline void EnvironmentTexture(stm_ptr<Texture> t) { mEnvironmentTexture = t; }
	inline void FixedTimeStep(float step) { mFixedTimeStep = step; }
	inline void PhysicsTimeLimitPerFrame(float t) { mPhysicsTimeLimitPerFrame = t; }
	inline void BvhDirty(Object* reason) { safe_delete(mBvh); }

	// Getters
	inline bool HasAttachment(const RenderTargetIdentifier& name) const { return mAttachments.count(name); }
	inline stm_ptr<Texture> GetAttachment(const RenderTargetIdentifier& name) const { return mAttachments.at(name); }
	STRATUM_API std::pair<vk::Extent2D, vk::ImageUsageFlags> GetAttachmentInfo(const RenderTargetIdentifier& name) const { return mAttachmentInfo.at(name); };

	inline float3 AmbientLight() const { return mAmbientLight; }
	inline stm_ptr<Texture> EnvironmentTexture() const { return mEnvironmentTexture; }
	inline float FPS() const { return mFps; }
	inline float TotalTime() const { return mTotalTime; }
	inline float DeltaTime() const { return mDeltaTime; }
	inline float FixedTimeStep() const { return mFixedTimeStep; }
	inline float PhysicsTimeLimitPerFrame() const { return mPhysicsTimeLimitPerFrame; }

	STRATUM_API std::vector<Object*> Objects() const;
	STRATUM_API ObjectBvh2* BVH();
	inline uint64_t LastBvhBuild() { return mLastBvhBuild; }
	inline ::Instance* Instance() const { return mInstance; }

private:
	struct RenderGraphNode {
		std::deque<Subpass> mSubpasses;
		std::set<RenderTargetIdentifier> mNonSubpassDependencies;
		stm_ptr<RenderPass> mRenderPass;
		stm_ptr<Framebuffer> mFramebuffer;
	};

	STRATUM_API void AddObjectInternal(Object* object);
	STRATUM_API void BuildRenderGraph(stm_ptr<CommandBuffer> commandBuffer);

	::Instance* mInstance = nullptr;

	stm_ptr<Sampler> mShadowSampler;
	
	std::deque<Object*> mObjects;
	std::deque<Light*> mLights;
	std::deque<Camera*> mCameras;
	std::deque<Renderer*> mRenderers;
	
	bool mRenderGraphDirty = true;
	std::vector<RenderGraphNode*> mRenderGraph;
	std::unordered_map<std::string, RenderGraphNode> mRenderNodes;
	std::unordered_map<RenderTargetIdentifier, stm_ptr<Texture>> mAttachments;
	std::unordered_map<RenderTargetIdentifier, std::pair<vk::Extent2D, vk::ImageUsageFlags>> mAttachmentInfo;
	std::unordered_map<Camera*, GuiContext*> mGuiContexts;
	
	ObjectBvh2* mBvh = nullptr;
	uint64_t mLastBvhBuild = 0;

	MeshRenderer* mSkybox = nullptr;

	uint32_t mLightCount = 0;
	stm_ptr<Buffer> mLightBuffer;
	stm_ptr<Buffer> mShadowBuffer;
	stm_ptr<Texture> mShadowAtlas;
	stm_ptr<Texture> mEnvironmentTexture;
	float3 mAmbientLight = 0;

	float mPhysicsTimeLimitPerFrame = 0.2f;
	float mFixedAccumulator = 0;
	float mFixedTimeStep = 0.0025f;

	float mTotalTime = 0;
	float mDeltaTime = 0;

	std::chrono::high_resolution_clock mClock;
	std::chrono::high_resolution_clock::time_point mStartTime;
	std::chrono::high_resolution_clock::time_point mLastFrame;
	float mFrameTimeAccum = 0;
	uint32_t mFpsAccum = 0;
	float mFps = 0;
};