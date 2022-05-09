#pragma once

#include "Scene.hpp"
#include "Denoiser.hpp"

#pragma pack(push)
#pragma pack(1)
#include <Shaders/bdpt.h>
#pragma pack(pop)

namespace stm {

class BDPT {
public:
	STRATUM_API BDPT(Node& node);

	inline Node& node() const { return mNode; }

	STRATUM_API void create_pipelines();

	STRATUM_API void on_inspector_gui();
	STRATUM_API void update(CommandBuffer& commandBuffer, const float deltaTime);
	STRATUM_API void render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<pair<ViewData,TransformData>>& views);

private:
	Node& mNode;

	shared_ptr<ComputePipelineState> mVisibilityPipeline;
	shared_ptr<ComputePipelineState> mTraceStepPipeline;
	shared_ptr<ComputePipelineState> mTonemapPipeline;

	array<unordered_map<string, uint32_t>, 2> mDescriptorMap;
	array<shared_ptr<DescriptorSetLayout>, 2> mDescriptorSetLayouts;
	BDPTPushConstants mPushConstants;

	bool mRandomPerFrame = true;
	bool mDenoise = true;
	uint32_t mSamplingFlags = BDPT_FLAG_REMAP_THREADS | BDPT_FLAG_RAY_CONES;
	uint32_t mDebugMode = 0;

	struct FrameResources {
		shared_ptr<Fence> mFence;

		shared_ptr<DescriptorSet> mSceneDescriptors;
		shared_ptr<DescriptorSet> mViewDescriptors;

		shared_ptr<Scene::SceneData> mSceneData;

		Buffer::View<ViewData> mViews;
		Buffer::View<TransformData> mViewTransforms;
		Buffer::View<TransformData> mViewInverseTransforms;
		Buffer::View<uint32_t> mViewMediumIndices;
		Image::View mRadiance;
		Image::View mAlbedo;
		Image::View mDebugImage;

		unordered_map<string, Buffer::View<byte>> mPathData;

		Image::View mDenoiseResult;
		Image::View mTonemapResult;
		uint32_t mFrameNumber;
	};

	Buffer::View<uint32_t> mRayCount;
	uint32_t mPrevCounterValue;
	float mRaysPerSecond;
	float mRaysPerSecondTimer;

	vector<shared_ptr<FrameResources>> mFrameResources;
	shared_ptr<FrameResources> mPrevFrame;
	shared_ptr<FrameResources> mCurFrame;
};

}