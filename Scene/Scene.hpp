#pragma once

#include <Core/RenderPass.hpp>
#include <Data/Animation.hpp>
#include <Data/AssetManager.hpp>
#include <Data/Font.hpp>
#include <Data/Material.hpp>
#include <Data/Mesh.hpp>
#include <Data/Texture.hpp>
#include <Input/InputManager.hpp>
#include <Scene/GuiContext.hpp>
#include <Scene/ObjectBvh2.hpp>

// Holds scene Objects. In general, plugins will add objects during their lifetime, and remove objects during or at the end of their lifetime.
// This makes the shared_ptr destroy when the plugin removes the object, allowing the plugin's module to free the memory.
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

	STRATUM_API void Update(CommandBuffer* commandBuffer);
	// Renders all the cameras and RenderPasses in the scene
	STRATUM_API void Render(CommandBuffer* commandBuffer);
	// Draw all renderers in view of a camera
	STRATUM_API void RenderCamera(CommandBuffer* commandBuffer, Camera* camera);
	
	STRATUM_API void PushSceneConstants(CommandBuffer* commandBuffer);

	inline Object* Raycast(const Ray& worldRay, float* t = nullptr, bool any = false, uint32_t mask = 0xFFFFFFFF) { return BVH()->Intersect(worldRay, t, any, mask); }

	// Setters
	STRATUM_API void SetAttachmentInfo(const RenderTargetIdentifier& name, const vk::Extent2D& extent, vk::ImageUsageFlags usage);
	STRATUM_API void MainRenderExtent(const vk::Extent2D& extent);

	inline void AmbientLight(const float3& t) { mAmbientLight = t; }
	inline void EnvironmentTexture(Texture* t) { mEnvironmentTexture = t; }
	inline void FixedTimeStep(float step) { mFixedTimeStep = step; }
	inline void PhysicsTimeLimitPerFrame(float t) { mPhysicsTimeLimitPerFrame = t; }

	// Getters
	inline bool HasAttachment(const RenderTargetIdentifier& name) const { return mAttachments.count(name); }
	inline Texture* GetAttachment(const RenderTargetIdentifier& name) const { return mAttachments.at(name); }
	STRATUM_API std::pair<vk::Extent2D, vk::ImageUsageFlags> GetAttachmentInfo(const RenderTargetIdentifier& name) const { return mAttachmentInfo.at(name); };

	inline float3 AmbientLight() const { return mAmbientLight; }
	inline Texture* EnvironmentTexture() const { return mEnvironmentTexture; }
	inline float FPS() const { return mFps; }
	inline float TotalTime() const { return mTotalTime; }
	inline float DeltaTime() const { return mDeltaTime; }
	inline float FixedTimeStep() const { return mFixedTimeStep; }
	inline float PhysicsTimeLimitPerFrame() const { return mPhysicsTimeLimitPerFrame; }

	// All objects, in order of insertion
	STRATUM_API std::vector<Object*> Objects() const;

	STRATUM_API ObjectBvh2* BVH();
	// Frame id of the last bvh build
	inline uint64_t LastBvhBuild() { return mLastBvhBuild; }

	inline void BvhDirty(/* unused */ Object* reason) { mBvhDirty = true; }

	inline ::Instance* Instance() const { return mInstance; }

private:
	struct RenderGraphNode {
		std::deque<Subpass> mSubpasses;
		std::set<RenderTargetIdentifier> mNonSubpassDependencies;
		RenderPass* mRenderPass;
		Framebuffer* mFramebuffer;
	};

	STRATUM_API void AddObjectInternal(Object* object);
	STRATUM_API void BuildRenderGraph(CommandBuffer* commandBuffer);

	::Instance* mInstance;

	Sampler* mShadowSampler;
	
	std::deque<Object*> mObjects;
	std::deque<Light*> mLights;
	std::deque<Camera*> mCameras;
	std::deque<Renderer*> mRenderers;
	
	std::unordered_map<std::string, RenderGraphNode> mRenderNodes;
	bool mRenderGraphDirty;
	std::vector<RenderGraphNode*> mRenderGraph;
	std::unordered_map<RenderTargetIdentifier, Texture*> mAttachments;
	std::unordered_map<RenderTargetIdentifier, std::pair<vk::Extent2D, vk::ImageUsageFlags>> mAttachmentInfo;
	std::unordered_map<Camera*, GuiContext*> mGuiContexts;
	
	ObjectBvh2* mBvh;
	uint64_t mLastBvhBuild;
	bool mBvhDirty;

	MeshRenderer* mSkybox;

	uint32_t mLightCount;
	Buffer* mLightBuffer;
	Buffer* mShadowBuffer;
	Texture* mShadowAtlas;
	Texture* mEnvironmentTexture;
	float3 mAmbientLight;

	// fps calculation
	std::chrono::high_resolution_clock mClock;
	std::chrono::high_resolution_clock::time_point mStartTime;
	std::chrono::high_resolution_clock::time_point mLastFrame;
	float mTotalTime;
	float mDeltaTime;
	float mFrameTimeAccum;
	uint32_t mFpsAccum;
	float mFps;
	float mPhysicsTimeLimitPerFrame;
	float mFixedAccumulator;
	float mFixedTimeStep;
};