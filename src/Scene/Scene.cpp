#include "Scene.hpp"

#include <assimp/scene.h>
#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

#include "../Core/Window.hpp"

#include "Bone.hpp"
#include "Light.hpp"
#include "Renderers/MeshRenderer.hpp"

using namespace stm;

constexpr uint32_t gMaxLightCount = 64;

Scene::Scene(stm::Instance& instance) : mInstance(instance), mRoot(SceneNode(*this, "")) {
	mStartTime = mClock.now();
	mLastFrame = mStartTime;

	vk::Format renderFormat = mInstance.Window().SurfaceFormat().format;
	vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e8;
	
	Subpass shadowSubpass = {};
	shadowSubpass.mShaderPass = "forward/depth";
	shadowSubpass.mAttachments["stm_shadow_atlas"] = { AttachmentType::eDepthStencil, vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore };

	// TODO: some way to enable/disable passes per-frame i.e. don't run the shadow rendernode if there's no lights
	AssignRenderNode("Shadows", { shadowSubpass });
	SetAttachmentInfo("stm_shadow_atlas", { 4096, 4096 }, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);

	Subpass depthPrepass = {};
	depthPrepass.mShaderPass = "forward/depth";
	depthPrepass.mAttachments["stm_main_depth"] = { AttachmentType::eDepthStencil, vk::Format::eD32Sfloat, sampleCount, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore };

	Subpass opaqueSubpass = {};
	opaqueSubpass.mShaderPass = "forward/opaque";
	opaqueSubpass.mAttachments["stm_main_render"] = { AttachmentType::eColor, renderFormat, sampleCount, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore };
	opaqueSubpass.mAttachments["stm_main_depth"] = { AttachmentType::eDepthStencil, vk::Format::eD32Sfloat, sampleCount, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore };
	opaqueSubpass.mAttachmentDependencies["stm_shadow_atlas"] = { vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eShaderRead };
	
	Subpass transparentSubpass = {};
	transparentSubpass.mShaderPass = "forward/transparent";
	transparentSubpass.mAttachments["stm_main_render"] = { AttachmentType::eColor, renderFormat, sampleCount, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore };
	transparentSubpass.mAttachments["stm_main_resolve"] = { AttachmentType::eResolve, renderFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore };
	transparentSubpass.mAttachments["stm_main_depth"] = opaqueSubpass.mAttachments["stm_main_depth"];

	AssignRenderNode("Main", { depthPrepass, opaqueSubpass, transparentSubpass });
	SetAttachmentInfo("stm_main_render", mInstance.Window().SwapchainExtent(), vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);
	SetAttachmentInfo("stm_main_depth", mInstance.Window().SwapchainExtent(), vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);
	SetAttachmentInfo("stm_main_resolve", mInstance.Window().SwapchainExtent(), vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);

	// init environment data
	
	vk::SamplerCreateInfo samplerInfo = {};
	samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
	samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToBorder;
	samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToBorder;
	samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToBorder;
	samplerInfo.compareEnable = VK_TRUE;
	samplerInfo.compareOp = vk::CompareOp::eLess;
	samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
	mShadowSampler = make_shared<Sampler>("ShadowSampler", mInstance.Device(), samplerInfo);
	mEnvironmentTexture = mInstance.Device().FindLoadedAsset<Texture>("stm_1x1_white_opaque");


	// init skybox

	float r = .5f;
	Vector3f verts[8] {
		Vector3f(-r, -r, -r),
		Vector3f(r, -r, -r),
		Vector3f(-r, -r,  r),
		Vector3f(r, -r,  r),
		Vector3f(-r,  r, -r),
		Vector3f(r,  r, -r),
		Vector3f(-r,  r,  r),
		Vector3f(r,  r,  r),
	};
	uint16_t indices[36] {
		2,7,6,2,3,7,
		0,1,2,2,1,3,
		1,5,7,7,3,1,
		4,5,1,4,1,0,
		6,4,2,4,0,2,
		4,7,5,4,6,7
	};
	auto skyVertexBuffer = make_shared<Buffer>("SkyCube/Vertices", mInstance.Device(), verts, sizeof(Vector3f)*8, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst);
	auto skyIndexBuffer = make_shared<Buffer>("SkyCube/Indices" , mInstance.Device(), indices, sizeof(uint16_t)*36, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst);
	
	MeshRenderer* skybox = CreateObject<MeshRenderer>("Skybox");
	skybox->Mesh(shared_ptr<Mesh>(new Mesh("SkyCube", 
		{ { { VertexAttributeType::ePosition, 0 }, VertexAttributeData(Buffer::ArrayView<Vector3f>(skyVertexBuffer), 0, vk::VertexInputRate::eVertex) } },
		Buffer::ArrayView<uint16_t>(skyIndexBuffer) )));
	skybox->Mesh()->AddSubmesh(Mesh::Submesh(8, 0, 36, 0));
	skybox->Material(make_shared<Material>("Skybox", mInstance.Device().LoadAsset<Shader>("Assets/Shaders/skybox.stmb")));
	skybox->LocalScale(1e5f);
	mSkybox = skybox;
}
Scene::~Scene() {
	for (Plugin* p : mPlugins) delete p;
	while (!mObjects.empty()) {
		Object* o = mObjects.front();
		RemoveObject(o);
		delete o;
	}
	if (mBvh) delete mBvh;
	for (auto&[name,ctx] : mGuiContexts) delete ctx;
}

Scene::Plugin* Scene::LoadPlugin(const fs::path& filename) {
	try {
		
#ifdef WIN32
		char* msgBuf;
		auto throw_if_null = [&](auto ptr){
			if (ptr == NULL) {
				FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |  FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&msgBuf, 0, NULL );
				throw runtime_error(msgBuf);
			}
		};

		HMODULE m = LoadLibraryW(filename.c_str());
		throw_if_null(m);
		FARPROC funcPtr = GetProcAddress(m, "stm::CreatePlugin");
		throw_if_null(funcPtr);
#endif
#ifdef __linux
		throw; // TODO loadplugin linux
#endif
		Plugin* plugin = ((Plugin*(*)(Scene*))funcPtr)(this);
		mPlugins.push_back(plugin);
		return plugin;
	} catch(exception e) {}
	return nullptr;
}

void Scene::Update(CommandBuffer& commandBuffer) {
	ProfilerRegion ps("Scene::Update");
	
	auto t1 = mClock.now();
	mDeltaTime = (t1 - mLastFrame).count() * 1e-9f;
	mTotalTime = (t1 - mStartTime).count() * 1e-9f;
	mLastFrame = t1;
	
	// count fps
	mFrameTimeAccum += mDeltaTime;
	mFpsAccum++;
	if (mFrameTimeAccum > 1.f) {
		mFps = mFpsAccum / mFrameTimeAccum;
		mFrameTimeAccum -= 1.f;
		mFpsAccum = 0;
	}

	// TODO: Query input, populate mInputPointers

	mInputStatesPrevious = mInputStates;
	mInputStates.clear();

	
	{
		ProfilerRegion ps("Fixed Update");
		mFixedAccumulator += mDeltaTime;
		float physicsTime = 0;
		t1 = mClock.now();
		while (mFixedAccumulator > mFixedTimeStep && physicsTime < mPhysicsTimeLimitPerFrame) {
			ranges::for_each(mPlugins, [&](Plugin* p){ p->OnFixedUpdate(commandBuffer); });
			for (const auto& o : mObjects) if (o->Enabled()) o->OnFixedUpdate(commandBuffer);
			mFixedAccumulator -= mFixedTimeStep;
			physicsTime = (mClock.now() - t1).count() * 1e-9f;
		}
	}

	{
		ProfilerRegion ps("OnUpdate(), OnLateUpdate()");
		ranges::for_each(mPlugins, [&](Plugin* p){ p->OnUpdate(commandBuffer); });
		for (auto& o : mObjects) if (o->Enabled()) o->OnUpdate(commandBuffer);
		ranges::for_each(mPlugins, [&](Plugin* p){ p->OnLateUpdate(commandBuffer); });
		for (auto& o : mObjects) if (o->Enabled()) o->OnLateUpdate(commandBuffer);
	}

	{
		ProfilerRegion ps("Scene Lighting");
		mLighting.mLightBuffer = commandBuffer.GetBuffer("Light Buffer", sizeof(LightData), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		if (mLights.empty()) {
			mLighting.mShadowAtlas = commandBuffer.GetTexture("Shadow Atlas", { 1, 1, 1 }, vk::Format::eD32Sfloat, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);
		} else {
			ProfilerRegion ps("Gather Lights and Shadows");
			mLighting.mShadowAtlas = commandBuffer.GetTexture("Shadow Atlas", { 4096, 4096, 1 }, vk::Format::eD32Sfloat, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);

			AlignedBox3f shadowBounds;
			shadowBounds.mMin = 1e10f;
			shadowBounds.mMax = -1e10f;
			for (uint32_t i = 0; i < mRenderers.size(); i++) {
				auto bounds = mRenderers[i]->Bounds();
				if (bounds && mRenderers[i]->Visible("forward/depth")) {
					bounds->mMin -= 1e-2f;
					bounds->mMax += 1e-2f;
					shadowBounds.Encapsulate(*bounds);
				}
			}
			uint32_t si = 0;
			uint32_t shadowCount = 0;

			Vector3f sceneCenter = shadowBounds.Center();
			Vector3f sceneExtent = shadowBounds.HalfSize();
			float shadowExtentMax = fmaxf(fmaxf(sceneExtent.x, sceneExtent.y), sceneExtent.z) * 1.73205080757f; // sqrt(3)*x

			mLighting.mLightCount = 0;
			LightData* lights = (LightData*)mLighting.mLightBuffer->Mapped();

			for (Light* l : mLights) {
				if (!l->Enabled()) continue;
				float cosInner = cos(l->InnerSpotAngle());
				float cosOuter = cos(l->OuterSpotAngle());
				lights[mLighting.mLightCount].ToLight = l->InverseTransform();
				lights[mLighting.mLightCount].Emission = l->Color() * l->Intensity();
				lights[mLighting.mLightCount].Type_ShadowIndex = (uint32_t)l->Type();
				lights[mLighting.mLightCount].SpotAngleScale = 1.f / fmaxf(.001f, cosInner - cosOuter);
				lights[mLighting.mLightCount].SpotAngleOffset = -cosOuter * lights[mLighting.mLightCount].SpotAngleScale;

				mLighting.mLightCount++;
				if (mLighting.mLightCount >= gMaxLightCount) break;
			}
		}
		commandBuffer.TransitionBarrier(*mLighting.mShadowAtlas, vk::ImageLayout::eShaderReadOnlyOptimal);
	}

	if (mInstance.Window().Swapchain()) MainRenderExtent(mInstance.Window().SwapchainExtent());
}

void Scene::Render(CommandBuffer& commandBuffer, Camera& camera) {
	vector<Renderer*> renderers;
	{
		ProfilerRegion ps("Culling/Sorting");

		vector<Object*> objects = BVH()->Intersect(camera.Frustum());

		renderers.reserve(objects.size());
		for (Object* o : objects)
			if (Renderer* r = dynamic_cast<Renderer*>(o))
				if (r->Visible(commandBuffer.CurrentShaderPass()))
					renderers.push_back(r);
		
		// add any renderers that don't have bounds (thus are omitted by the BVH)
		for (Renderer* r : mRenderers)
			if (!r->Bounds() && r->Visible(commandBuffer.CurrentShaderPass()) && (camera.DrawSkybox() || r != mSkybox))
				renderers.push_back(r);

		ranges::sort(renderers, [&](Renderer* a, Renderer* b) {
			uint32_t qa = a->RenderQueue(commandBuffer.CurrentShaderPass());
			uint32_t qb = b->RenderQueue(commandBuffer.CurrentShaderPass());
			if (qa == qb) {
				MeshRenderer* ma = dynamic_cast<MeshRenderer*>(a);
				MeshRenderer* mb = dynamic_cast<MeshRenderer*>(b);
				if (ma && mb)
					if (ma->Material() == mb->Material())
						return ma->Mesh() < mb->Mesh();
					else
						return ma->Material() < mb->Material();
			}
			return qa < qb;
		});
	}

	camera.AspectRatio((float)commandBuffer.CurrentFramebuffer()->Extent().width / (float)commandBuffer.CurrentFramebuffer()->Extent().height);

	auto cameraBuffer = commandBuffer.GetBuffer("Camera Buffer", sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	camera.WriteUniformBuffer(cameraBuffer->Mapped());
	camera.SetViewportScissor(commandBuffer);

	ranges::for_each(mPlugins, [&](Plugin* p){ p->OnRenderCamera(commandBuffer, camera); });

	// draw loop

	shared_ptr<Buffer> instanceBuffer;
	uint32_t instanceCount = 0;
	Renderer* firstInstance = nullptr;
	for (Renderer* renderer : renderers) {
		if (firstInstance) {
			if (firstInstance->TryCombineInstances(commandBuffer, renderer, instanceBuffer, instanceCount))
				continue; // instanced, skip
			// draw firstInstance
			if (instanceCount > 1)
				firstInstance->OnDrawInstanced(commandBuffer, camera, instanceBuffer, instanceCount);
			else
				firstInstance->OnDraw(commandBuffer, camera);
		}
		instanceCount = 1;
		firstInstance = renderer;
	}
	if (firstInstance)
		if (instanceCount > 1)
			firstInstance->OnDrawInstanced(commandBuffer, camera, instanceBuffer, instanceCount);
		else
			firstInstance->OnDraw(commandBuffer, camera);
	
	if (mGuiContexts.count(&camera))
		mGuiContexts.at(&camera)->OnDraw(commandBuffer, camera);
}
