#pragma once

#include "Scene.hpp"
#include "Denoiser.hpp"

#include <Shaders/vcm.h>

namespace stm {

template<typename T>
struct FrameResourcePool {
	list<shared_ptr<T>> mResources;
	shared_ptr<T> mPrev, mCur;

	inline void advance(const shared_ptr<Fence>& fence) {
		// reuse old frame resources
		ProfilerRegion ps("FrameResourcePool::advance");
		if (mCur) {
			mResources.push_front(mCur);
			mPrev = mCur;
		}
		mCur.reset();

		for (auto it = mResources.begin(); it != mResources.end(); it++) {
			if (*it != mPrev && (*it)->mFence->status() == vk::Result::eSuccess) {
				mCur = *it;
				mResources.erase(it);
				break;
			}
		}
		if (!mCur) mCur = make_shared<T>();

		if (mPrev)
			mCur->mFrameNumber = mPrev->mFrameNumber + 1;
		else
			mCur->mFrameNumber = 0;

		mCur->mFence = fence;
	}
};

class VCM {
public:
	STRATUM_API VCM(Node& node);

	inline Node& node() const { return mNode; }

	inline Image::View prev_result() { return mResources.mPrev ? mResources.mPrev->mTonemapResult : Image::View(); }

	STRATUM_API void create_pipelines();

	STRATUM_API void on_inspector_gui();
	STRATUM_API void update(CommandBuffer& commandBuffer, const float deltaTime);
	STRATUM_API void render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<pair<ViewData,TransformData>>& views);

private:
	Node& mNode;

	enum RenderPipelineIndex {
		eLightTrace,
		eCameraTrace,
		eAddLightTrace,
		ePipelineCount
	};
	array<shared_ptr<ComputePipelineState>, RenderPipelineIndex::ePipelineCount> mRenderPipelines;

	shared_ptr<ComputePipelineState> mTonemapPipeline;
	shared_ptr<ComputePipelineState> mTonemapMaxReducePipeline;

	array<unordered_map<string, uint32_t>, 2> mDescriptorMap;
	array<shared_ptr<DescriptorSetLayout>, 2> mDescriptorSetLayouts;
	VCMPushConstants mPushConstants;

	bool mHalfColorPrecision = false;
	bool mPauseRendering = false;
	bool mRandomPerFrame = true;
	bool mDenoise = true;
	uint32_t mSamplingFlags = VCM_FLAG_REMAP_THREADS | VCM_FLAG_USE_VC | VCM_FLAG_USE_MIS;
	VCMDebugMode mDebugMode = VCMDebugMode::eNone;
	uint32_t mLightTraceQuantization = 65536;

	struct FrameResources {
		shared_ptr<Fence> mFence;
		uint32_t mFrameNumber;

		shared_ptr<DescriptorSet> mSceneDescriptors;
		shared_ptr<DescriptorSet> mViewDescriptors;

		shared_ptr<Scene::SceneData> mSceneData;

		Buffer::View<ViewData> mViews;
		Buffer::View<TransformData> mViewTransforms;
		Buffer::View<TransformData> mViewInverseTransforms;
		Buffer::View<uint32_t> mViewMediumIndices;
		Image::View mRadiance;
		Image::View mAlbedo;
		Image::View mPrevUVs;
		Image::View mDebugImage;

		unordered_map<string, Buffer::View<byte>> mPathData;
		Buffer::View<VisibilityInfo> mSelectionData;
		bool mSelectionDataValid;

		Image::View mDenoiseResult;
		Buffer::View<uint4> mTonemapMax;
		Image::View mTonemapResult;
	};

	Buffer::View<uint32_t> mRayCount;
	uint32_t mPrevCounterValue;
	float mRaysPerSecond;
	float mRaysPerSecondTimer;

	FrameResourcePool<FrameResources> mResources;
};

}