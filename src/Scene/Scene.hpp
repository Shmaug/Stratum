#pragma once

#include <Core/RenderPass.hpp>
#include <Data/Font.hpp>
#include <Data/Material.hpp>
#include <Data/Mesh.hpp>
#include <Data/Texture.hpp>
#include <Input/InputManager.hpp>
#include <Scene/GuiContext.hpp>
#include <Scene/ObjectBvh2.hpp>

namespace stm {

class Scene {
public:
	Instance* const mInstance;

	struct LightingData {
		std::shared_ptr<Buffer> mLightBuffer;
		std::shared_ptr<Buffer> mShadowBuffer;
		std::shared_ptr<Texture> mShadowAtlas;
		LightingBuffer mLightingData;
	};

	STRATUM_API Scene(stm::Instance* instance);
	STRATUM_API ~Scene();

	template<class T, typename... Args>
	inline T* CreateObject(const std::string& name, Args... args) {
		static_assert(std::is_base_of<Object, T>::value);
		T* object = new T(name, this, args...);
		safe_delete(mBvh);
		mObjects.push_back(object);
		if (std::is_base_of<Renderer, T>::value) mRenderers.push_back((Renderer*)object);
		if (std::is_base_of<Camera, T>::value) mCameras.push_back((Camera*)object);
		if (std::is_base_of<Light, T>::value) mLights.push_back((Light*)object);
		return object;
	}
	STRATUM_API void RemoveObject(Object* object);

	STRATUM_API const std::deque<Subpass>& GetRenderNode(const std::string& nodeName) const { return mRenderNodes.at(nodeName).mSubpasses; }
	STRATUM_API void AssignRenderNode(const std::string& nodeName, const std::deque<Subpass>& subpasses);
	STRATUM_API void DeleteRenderNode(const std::string& nodeName);

	// Updates physics, objects, and lighting 
	STRATUM_API void Update(CommandBuffer& commandBuffer);
	// Renders RenderNodes in the scene
	STRATUM_API void Render(CommandBuffer& commandBuffer);
	// Draw all renderers in view of a camera
	STRATUM_API void RenderCamera(CommandBuffer& commandBuffer, Camera& camera);

	inline Object* Raycast(const Ray& worldRay, float* t = nullptr, bool any = false, uint32_t mask = 0xFFFFFFFF) { return BVH()->Intersect(worldRay, t, any, mask); }

	// Setters
	STRATUM_API void SetAttachmentInfo(const RenderTargetIdentifier& name, const vk::Extent2D& extent, vk::ImageUsageFlags usage);
	STRATUM_API void MainRenderExtent(const vk::Extent2D& extent);

	inline void AmbientLight(const float3& t) { mAmbientLight = t; }
	inline void EnvironmentTexture(std::shared_ptr<Texture> t) { mEnvironmentTexture = t; }
	inline void FixedTimeStep(float step) { mFixedTimeStep = step; }
	inline void PhysicsTimeLimitPerFrame(float t) { mPhysicsTimeLimitPerFrame = t; }
	inline void BvhDirty(Object* reason) { safe_delete(mBvh); }

	// Getters
	inline bool HasAttachment(const RenderTargetIdentifier& name) const { return mAttachments.count(name); }
	inline std::shared_ptr<Texture> GetAttachment(const RenderTargetIdentifier& name) const { return mAttachments.at(name); }
	STRATUM_API std::pair<vk::Extent2D, vk::ImageUsageFlags> GetAttachmentInfo(const RenderTargetIdentifier& name) const { return mAttachmentInfo.at(name); };

	inline float3 AmbientLight() const { return mAmbientLight; }
	inline std::shared_ptr<Texture> EnvironmentTexture() const { return mEnvironmentTexture; }
	inline float FPS() const { return mFps; }
	inline float TotalTime() const { return mTotalTime; }
	inline float DeltaTime() const { return mDeltaTime; }
	inline float FixedTimeStep() const { return mFixedTimeStep; }
	inline float PhysicsTimeLimitPerFrame() const { return mPhysicsTimeLimitPerFrame; }

	STRATUM_API std::vector<Object*> Objects() const;
	STRATUM_API ObjectBvh2* BVH();
	inline uint64_t LastBvhBuild() { return mLastBvhBuild; }

private:
	friend class Object;
	
	struct RenderGraphNode {
		std::deque<Subpass> mSubpasses;
		std::set<RenderTargetIdentifier> mNonSubpassDependencies;
		std::shared_ptr<RenderPass> mRenderPass;
		std::shared_ptr<Framebuffer> mFramebuffer;
	};

	STRATUM_API void AddObject(Object* object);
	STRATUM_API void BuildRenderGraph(CommandBuffer& commandBuffer);
	
	std::deque<Object*> mObjects;
	std::deque<Light*> mLights;
	std::deque<Camera*> mCameras;
	std::deque<Renderer*> mRenderers;
	
	bool mRenderGraphDirty = true;
	std::vector<RenderGraphNode*> mRenderGraph;
	std::map<std::string, RenderGraphNode> mRenderNodes;
	std::map<RenderTargetIdentifier, std::shared_ptr<Texture>> mAttachments;
	std::map<RenderTargetIdentifier, std::pair<vk::Extent2D, vk::ImageUsageFlags>> mAttachmentInfo;
	std::unordered_map<Camera*, GuiContext*> mGuiContexts;
	
	ObjectBvh2* mBvh = nullptr;
	uint64_t mLastBvhBuild = 0;

	MeshRenderer* mSkybox = nullptr;
	std::shared_ptr<DescriptorSet> mPerCamera;
	LightingData mLighting;
	std::shared_ptr<Texture> mEnvironmentTexture;
	float3 mAmbientLight = 0;
	std::shared_ptr<Sampler> mShadowSampler;

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

}