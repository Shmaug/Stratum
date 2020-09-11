#include <Scene/Scene.hpp>
#include <Scene/Light.hpp>
#include <Core/PluginManager.hpp>
#include <Scene/Bone.hpp>
#include <Scene/Renderers/MeshRenderer.hpp>
#include <Util/Profiler.hpp>

#include <assimp/scene.h>
#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

using namespace std;

#define INSTANCE_BATCH_SIZE 1024
#define MAX_GPU_LIGHTS 64

Scene::Scene(::Instance* instance) : mInstance(instance) {
	mEnvironmentTexture = mInstance->Device()->AssetManager()->WhiteTexture();

	mStartTime = mClock.now();
	mLastFrame = mStartTime;

	vk::SamplerCreateInfo samplerInfo = {};
	samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
	samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToBorder;
	samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToBorder;
	samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToBorder;
	samplerInfo.compareEnable = VK_TRUE;
	samplerInfo.compareOp = vk::CompareOp::eLess;
	samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
	mShadowSampler = new Sampler("ShadowSampler", mInstance->Device(), samplerInfo);


	vk::Format renderFormat = mInstance->Window()->Format().format;
	vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e8;
	
	Subpass shadowSubpass = {};
	shadowSubpass.mShaderPass = "forward/depth";
	shadowSubpass.mAttachments["stm_shadow_atlas"] = { AttachmentType::eDepthStencil, vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore };


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

	AssignRenderNode("Shadows", { shadowSubpass });
	AssignRenderNode("Main", { depthPrepass, opaqueSubpass, transparentSubpass });

	SetAttachmentInfo("stm_shadow_atlas", { 4096, 4096 }, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);
	SetAttachmentInfo("stm_main_render", mInstance->Window()->SwapchainExtent(), vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);
	SetAttachmentInfo("stm_main_depth", mInstance->Window()->SwapchainExtent(), vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);
	SetAttachmentInfo("stm_main_resolve", mInstance->Window()->SwapchainExtent(), vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);

	// init skybox

	float r = .5f;
	float3 verts[8] {
		float3(-r, -r, -r),
		float3(r, -r, -r),
		float3(-r, -r,  r),
		float3(r, -r,  r),
		float3(-r,  r, -r),
		float3(r,  r, -r),
		float3(-r,  r,  r),
		float3(r,  r,  r),
	};
	uint16_t indices[36] {
		2,7,6,2,3,7,
		0,1,2,2,1,3,
		1,5,7,7,3,1,
		4,5,1,4,1,0,
		6,4,2,4,0,2,
		4,7,5,4,6,7
	};
	stm_ptr<Buffer> skyVertexBuffer = new Buffer("SkyCube/Vertices", mInstance->Device(), verts, sizeof(float3)*8, vk::BufferUsageFlagBits::eVertexBuffer);
	stm_ptr<Buffer> skyIndexBuffer  = new Buffer("SkyCube/Indices" , mInstance->Device(), indices, sizeof(uint16_t)*36, vk::BufferUsageFlagBits::eIndexBuffer);
	
	stm_ptr<Mesh> skyCube = new Mesh("SkyCube");
	skyCube->SetAttribute(VertexAttributeType::ePosition, 0, BufferView(skyVertexBuffer), 0, (uint32_t)sizeof(float3));
	skyCube->SetIndexBuffer(BufferView(skyIndexBuffer), vk::IndexType::eUint16);
	skyCube->AddSubmesh(Mesh::Submesh(8, 0, 36, 0));

	mSkybox = CreateObject<MeshRenderer>("Skybox");
	mSkybox->Mesh(skyCube);
	mSkybox->Material(new Material("Skybox", mInstance->Device()->AssetManager()->Load<Pipeline>("Shaders/skybox.stmb", "Skybox")));
	mSkybox->LocalScale(1e5f);

	for (EnginePlugin* plugin : mInstance->PluginManager()->Plugins()) plugin->OnSceneInit(this);
}
Scene::~Scene() {
	while (!mObjects.empty()) DestroyObject(mObjects.front());
	mInstance->PluginManager()->UnloadPlugins();

	safe_delete(mBvh);
	for (auto&[name,ctx] : mGuiContexts) safe_delete(ctx);
}

void Scene::SetAttachmentInfo(const RenderTargetIdentifier& name, const vk::Extent2D& extent, vk::ImageUsageFlags usage) {
	// TODO: support specification of finalLayout
	mAttachmentInfo[name] = { extent, usage };
	mRenderGraphDirty = true;
}

void Scene::MainRenderExtent(const vk::Extent2D& extent) {
	SetAttachmentInfo("stm_main_render", { extent }, GetAttachmentInfo("stm_main_render").second);
	SetAttachmentInfo("stm_main_depth", { extent }, GetAttachmentInfo("stm_main_depth").second);
	SetAttachmentInfo("stm_main_resolve", { extent }, GetAttachmentInfo("stm_main_resolve").second);
}

void Scene::AssignRenderNode(const string& name, const deque<Subpass>& subpasses) {
	RenderGraphNode& node = mRenderNodes[name];
	node.mSubpasses = subpasses;
	mRenderGraphDirty = true;
}
void Scene::DeleteRenderNode(const std::string& name) {
	if (mRenderNodes.count(name) == 0) return;
	RenderGraphNode& node = mRenderNodes.at(name);
	mRenderNodes.erase(name);
	mRenderGraphDirty = true;
}

void Scene::BuildRenderGraph(stm_ptr<CommandBuffer> commandBuffer) {
	mRenderGraph.clear();

	// Create new attachment buffers

	unordered_map<RenderTargetIdentifier, stm_ptr<Texture>> newAttachments;
	for (auto&[name, node] : mRenderNodes) {
		if (!node.mRenderPass) {
			// resolve non-subpass dependencies
			node.mNonSubpassDependencies.clear();
			vector<Subpass> subpasses;
			for (uint32_t i = 0; i < node.mSubpasses.size(); i++) {
				for (auto& dependency : node.mSubpasses[i].mAttachmentDependencies) {
					bool subpassDependency = false;
					for (uint32_t j = 0; j < node.mSubpasses.size(); j++) {
						if (i == j || node.mSubpasses[j].mAttachments.count(dependency.first) == 0) continue;
						AttachmentType type = node.mSubpasses[j].mAttachments.at(dependency.first).mType;
						if (type == AttachmentType::eColor || type == AttachmentType::eDepthStencil || type == AttachmentType::eResolve || type == AttachmentType::ePreserve) {
								subpassDependency = true;
								break;
							}
						}
					if (!subpassDependency)
						node.mNonSubpassDependencies.insert(dependency.first);
				}
				subpasses.push_back(node.mSubpasses[i]);
			}
			node.mRenderPass = new RenderPass(name, mInstance->Device(), subpasses);
		}

		// populate newAttachments
		for (uint32_t i = 0; i < node.mRenderPass->AttachmentCount(); i++) {
			const RenderTargetIdentifier& attachment = node.mRenderPass->AttachmentName(i);
			
			vk::Extent2D extent = { 1024, 1024 };
			vk::ImageUsageFlags usage = HasDepthComponent(node.mRenderPass->Attachment(i).format) ? vk::ImageUsageFlagBits::eDepthStencilAttachment : vk::ImageUsageFlagBits::eColorAttachment;
			if (mAttachmentInfo.count(attachment)) {
				auto[ext, usg] = mAttachmentInfo.at(attachment);
				extent = ext;
				usage = usg;
			}

			// attempt to re-use attachment...
			if (mAttachments.count(attachment)) {
				stm_ptr<Texture> a = mAttachments.at(attachment);
				if (mAttachmentInfo.count(attachment) && a->Extent().width == extent.width && a->Extent().height == extent.height && a->Usage() == usage) {
					newAttachments.emplace(attachment, mAttachments.at(attachment));
					continue;
				} else {
					// mismatch in framebuffer dimensions... need to make a new framebuffer
					commandBuffer->TrackResource(node.mFramebuffer);
					node.mFramebuffer = nullptr;
				}
			}
			// ...or just create a new one if not
			newAttachments.emplace(attachment, new Texture(attachment, mInstance->Device(), nullptr, 0, vk::Extent3D(extent, 1), node.mRenderPass->Attachment(i).format, 1, node.mRenderPass->Attachment(i).samples, usage));
		}

		// release previous framebuffer if invalid
		if (node.mFramebuffer && node.mFramebuffer->AttachmentCount() != node.mRenderPass->AttachmentCount()) {
			commandBuffer->TrackResource(node.mFramebuffer);
			node.mFramebuffer = nullptr;
		}

		// create framebuffer if needed
		if (!node.mFramebuffer) {
			vector<stm_ptr<Texture>> attachments(node.mRenderPass->AttachmentCount());
			for (uint32_t i = 0; i < node.mRenderPass->AttachmentCount(); i++)
				attachments[i] = newAttachments.at(node.mRenderPass->AttachmentName(i));
			node.mFramebuffer = new Framebuffer(name, node.mRenderPass, attachments);
		}
	}

	// Delete unused attachments
	for (auto& it = mAttachments.begin(); it != mAttachments.end();) {
		if (newAttachments.count(it->first) && newAttachments.at(it->first) == it->second) { it++; continue; }
		commandBuffer->TrackResource(it->second);
		it = mAttachments.erase(it);
	}
	for (auto& it = mAttachmentInfo.begin(); it != mAttachmentInfo.end();) {
		if (newAttachments.count(it->first)) { it++; continue; }
		it = mAttachmentInfo.erase(it);
	}

	mAttachments = newAttachments;

	// Create new render graph
	for (auto&[name,node] : mRenderNodes) mRenderGraph.push_back(&node);
	sort(mRenderGraph.begin(), mRenderGraph.end(), [&](const RenderGraphNode* a, const RenderGraphNode* b) {
		// b < a if a depends on b
		for (const RenderTargetIdentifier& dep : a->mNonSubpassDependencies)
			for (const Subpass& subpass : b->mSubpasses) {
				for (const auto&[name,attachment] : subpass.mAttachments)
					if (name == dep && (attachment.mType == AttachmentType::eColor || attachment.mType == AttachmentType::eDepthStencil || attachment.mType == AttachmentType::eResolve))
						return false;
			}
		return true;
	});

	mRenderGraphDirty = false;
}

void Scene::AddObjectInternal(Object* object) {
	safe_delete(mBvh);
	object->mScene = this;
	mObjects.push_back(object);

	if (Renderer* r = dynamic_cast<Renderer*>(object)) mRenderers.push_back(r);
	if (Camera* c = dynamic_cast<Camera*>(object)) mCameras.push_back(c);
	if (Light* l = dynamic_cast<Light*>(object)) mLights.push_back(l);
}
void Scene::DestroyObject(Object* object, bool freeptr) {
	safe_delete(mBvh);

	for (auto it = mRenderers.begin(); it != mRenderers.end(); it++)
		if (*it == object) { mRenderers.erase(it); break; }
	for (auto it = mCameras.begin(); it != mCameras.end(); it++)
		if (*it == object) { mCameras.erase(it); break; }
	for (auto it = mLights.begin(); it != mLights.end(); it++)
		if (*it == object) { mLights.erase(it); break; }
	for (auto it = mObjects.begin(); it != mObjects.end(); it++)
		if (*it == object) { mObjects.erase(it); break; }
			
	object->mScene = nullptr;
	if (freeptr) delete object;
}

void Scene::Update(stm_ptr<CommandBuffer> commandBuffer) {
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

	PROFILER_BEGIN("FixedUpdate");
	float physicsTime = 0;
	mFixedAccumulator += mDeltaTime;
	t1 = mClock.now();
	while (mFixedAccumulator > mFixedTimeStep && physicsTime < mPhysicsTimeLimitPerFrame) {
		for (EnginePlugin* p : mInstance->PluginManager()->Plugins()) p->OnFixedUpdate(commandBuffer);
		for (const auto& o : mObjects) if (o->EnabledHierarchy()) o->OnFixedUpdate(commandBuffer);

		mFixedAccumulator -= mFixedTimeStep;
		physicsTime = (mClock.now() - t1).count() * 1e-9f;
	}
	PROFILER_END;

	PROFILER_BEGIN("Update");
	for (auto o : mObjects) if (o->EnabledHierarchy()) o->OnUpdate(commandBuffer);
	for (EnginePlugin* p : mInstance->PluginManager()->Plugins()) p->OnUpdate(commandBuffer);
	for (auto o : mObjects) if (o->EnabledHierarchy()) o->OnLateUpdate(commandBuffer);
	for (EnginePlugin* p : mInstance->PluginManager()->Plugins()) p->OnLateUpdate(commandBuffer);
	mSkybox->Material()->OnLateUpdate(commandBuffer);
	commandBuffer->TransitionBarrier(mEnvironmentTexture, vk::ImageLayout::eShaderReadOnlyOptimal);
	PROFILER_END;
	
	
	PROFILER_BEGIN("Lighting Update");
	mLightBuffer = commandBuffer->GetBuffer("Light Buffer", MAX_GPU_LIGHTS * sizeof(GPULight), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
	mShadowBuffer = commandBuffer->GetBuffer("Shadow Buffer", MAX_GPU_LIGHTS * sizeof(ShadowData), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
	mShadowAtlas = commandBuffer->GetTexture("Shadow Atlas", { 4096, 4096, 1 }, vk::Format::eD32Sfloat, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);
	
	AABB shadowBounds;
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
	
	float3 sceneCenter = shadowBounds.Center();
	float3 sceneExtent = shadowBounds.HalfSize();
	float shadowExtentMax = max(max(sceneExtent.x, sceneExtent.y), sceneExtent.z) * 1.73205080757f; // sqrt(3)*x
	
	mLightCount = 0;
	GPULight* lights = (GPULight*)mLightBuffer->MappedData();
	ShadowData* shadows = (ShadowData*)mShadowBuffer->MappedData();

	for (Light* l : mLights) {
		if (!l->EnabledHierarchy()) continue;

		float cosInner = cosf(l->InnerSpotAngle());
		float cosOuter = cosf(l->OuterSpotAngle());

		lights[mLightCount].Color = l->Color() * l->Intensity();
		lights[mLightCount].Type = l->Type();
		lights[mLightCount].WorldPosition = l->WorldPosition();
		lights[mLightCount].InvSqrRange = 1.f / (l->Range() * l->Range());
		lights[mLightCount].Direction = -(l->WorldRotation() * float3(0, 0, 1));
		lights[mLightCount].SpotAngleScale = 1.f / fmaxf(.001f, cosInner - cosOuter);
		lights[mLightCount].SpotAngleOffset = -cosOuter * lights[mLightCount].SpotAngleScale;
		lights[mLightCount].CascadeSplits = 0;
		lights[mLightCount].ShadowIndex = -1;

		mLightCount++;
		if (mLightCount >= MAX_GPU_LIGHTS) break;
	}
	PROFILER_END;

	commandBuffer->TransitionBarrier(mShadowAtlas, vk::ImageLayout::eShaderReadOnlyOptimal);
}

void Scene::Render(stm_ptr<CommandBuffer> commandBuffer) {
	if (mRenderGraphDirty) BuildRenderGraph(commandBuffer);

	BEGIN_CMD_REGION(commandBuffer, "Render Scene");

	PROFILER_BEGIN("OnGui");
	for (Camera* camera : mCameras) {
		for (EnginePlugin* p : mInstance->PluginManager()->Plugins()) p->OnPreRender(commandBuffer);

		if (!mGuiContexts.count(camera)) mGuiContexts.emplace(camera, new GuiContext(mInstance->Device(), mInstance->InputManager()));
		GuiContext* gui = mGuiContexts.at(camera);

		for (const auto& o : mObjects) if (o->EnabledHierarchy()) o->OnGui(commandBuffer, camera, gui);
		for (EnginePlugin* p : mInstance->PluginManager()->Plugins()) p->OnGui(commandBuffer, camera, gui);
		mGuiContexts.emplace(camera, gui);

		gui->OnPreRender(commandBuffer);
	}
	PROFILER_END;
	
	set<Camera*> passCameras;
	for (RenderGraphNode* rp : mRenderGraph) {
		PROFILER_BEGIN(rp->mRenderPass->mName);
		passCameras.clear();
		commandBuffer->BeginRenderPass(rp->mRenderPass, rp->mFramebuffer);
		
		PROFILER_BEGIN(rp->mRenderPass->GetSubpass(0).mShaderPass);
		BEGIN_CMD_REGION(commandBuffer, rp->mRenderPass->GetSubpass(0).mShaderPass);
		for (Camera* camera : mCameras)
			if (camera->RendersToSubpass(commandBuffer->CurrentRenderPass(), commandBuffer->CurrentSubpassIndex())) {
				RenderCamera(commandBuffer, camera);
				passCameras.insert(camera);
			}
		END_CMD_REGION(commandBuffer);
		PROFILER_END;
		
		for (uint32_t i = 1; i < rp->mRenderPass->SubpassCount(); i++) {
			PROFILER_BEGIN(rp->mRenderPass->GetSubpass(i).mShaderPass);
			BEGIN_CMD_REGION(commandBuffer, rp->mRenderPass->GetSubpass(i).mShaderPass);
			commandBuffer->NextSubpass();
			for (Camera* camera : mCameras)
				if (camera->RendersToSubpass(commandBuffer->CurrentRenderPass(), commandBuffer->CurrentSubpassIndex())) {
					RenderCamera(commandBuffer, camera);
					passCameras.insert(camera);
				}
			END_CMD_REGION(commandBuffer);
			PROFILER_END;
		}
		
		commandBuffer->EndRenderPass();

		for (EnginePlugin* p : mInstance->PluginManager()->Plugins())
			p->OnPostProcess(commandBuffer, rp->mFramebuffer, passCameras);
	
		PROFILER_END;
	}

	for (auto& kp : mGuiContexts) kp.second->Reset();
	END_CMD_REGION(commandBuffer);
}
void Scene::RenderCamera(stm_ptr<CommandBuffer> commandBuffer, Camera* camera) {
	PROFILER_BEGIN("Culling/Sorting");
	vector<Object*> objects = BVH()->FrustumCheck(camera->Frustum(), camera->LayerMask());

	vector<Renderer*> renderers;
	renderers.reserve(objects.size());
	for (Object* o : objects)
		if (Renderer* r = dynamic_cast<Renderer*>(o))
			if (r->Visible(commandBuffer->CurrentShaderPass()))
				renderers.push_back(r);
	
	// add any renderers that don't have bounds (thus are omitted by the BVH)
	for (Renderer* r : mRenderers)
		if (!r->Bounds() && r->Visible(commandBuffer->CurrentShaderPass()) && (r != mSkybox || camera->DrawSkybox()))
			renderers.push_back(r);

	sort(renderers.begin(), renderers.end(), [&](Renderer* a, Renderer* b) {
		uint32_t qa = a->RenderQueue(commandBuffer->CurrentShaderPass());
		uint32_t qb = b->RenderQueue(commandBuffer->CurrentShaderPass());
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
	PROFILER_END;

	camera->AspectRatio((float)commandBuffer->CurrentFramebuffer()->Extent().width / (float)commandBuffer->CurrentFramebuffer()->Extent().height);

	stm_ptr<Buffer> cameraBuffer = commandBuffer->GetBuffer("Camera Buffer", sizeof(CameraBuffer), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
	camera->WriteUniformBuffer(cameraBuffer->MappedData());
	camera->SetViewportScissor(commandBuffer);

	stm_ptr<DescriptorSet> perCamera = commandBuffer->GetDescriptorSet("Per Camera", mInstance->Device()->DefaultDescriptorSetLayout(PER_CAMERA));
	perCamera->CreateUniformBufferDescriptor(cameraBuffer, CAMERA_BUFFER_BINDING);
	perCamera->CreateStorageBufferDescriptor(mLightBuffer, LIGHT_BUFFER_BINDING);
	perCamera->CreateStorageBufferDescriptor(mShadowBuffer, SHADOW_BUFFER_BINDING);
	perCamera->CreateSampledTextureDescriptor(mShadowAtlas, SHADOW_ATLAS_BINDING);
	perCamera->CreateSampledTextureDescriptor(mEnvironmentTexture, ENVIRONMENT_TEXTURE_BINDING);
	perCamera->FlushWrites();
	
	for (EnginePlugin* plugin : mInstance->PluginManager()->Plugins()) plugin->OnRenderCamera(commandBuffer, camera, perCamera);

	stm_ptr<Buffer> instanceBuffer;
	uint32_t instanceCount = 0;
	Renderer* firstInstance = nullptr;
	for (Renderer* renderer : renderers) {
		if (firstInstance) {
			if (firstInstance->TryCombineInstances(commandBuffer, renderer, instanceBuffer, instanceCount)) continue;

			if (instanceCount > 1)
				firstInstance->OnDrawInstanced(commandBuffer, camera, perCamera, instanceBuffer, instanceCount);
			else
				firstInstance->OnDraw(commandBuffer, camera, perCamera);
		}
		instanceCount = 1;
		firstInstance = renderer;
	}
	if (firstInstance)
		if (instanceCount > 1)
			firstInstance->OnDrawInstanced(commandBuffer, camera, perCamera, instanceBuffer, instanceCount);
		else
			firstInstance->OnDraw(commandBuffer, camera, perCamera);
	
	if (mGuiContexts.count(camera))
		mGuiContexts.at(camera)->OnDraw(commandBuffer, camera, perCamera);
}

void Scene::PushSceneConstants(stm_ptr<CommandBuffer> commandBuffer) {
	commandBuffer->PushConstantRef("AmbientLight", mAmbientLight);
	commandBuffer->PushConstantRef("LightCount", (uint32_t)mLightCount);
	commandBuffer->PushConstantRef("ShadowTexelSize", 1.f / float2((float)mShadowAtlas->Extent().width, (float)mShadowAtlas->Extent().height));
	commandBuffer->PushConstantRef("Time", mTotalTime);
}

vector<Object*> Scene::Objects() const {
	vector<Object*> objs(mObjects.size());
	for (uint32_t i = 0; i < mObjects.size(); i++) objs[i] = mObjects[i];
	return objs;
}

ObjectBvh2* Scene::BVH() {
	if (!mBvh) {
		PROFILER_BEGIN("Build BVH");
		mBvh = new ObjectBvh2(Objects());
		mLastBvhBuild = mInstance->Device()->FrameCount();
		PROFILER_END;
	}
	return mBvh;
}