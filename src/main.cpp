#include "Core/Window.hpp"
#include "Scene/RenderGraph.hpp"

using namespace stm;

int main(int argc, char** argv) {
	#if defined(WIN32) && defined(_DEBUG)
	_CrtMemState s0;
	_CrtMemCheckpoint(&s0);
	#endif

	unique_ptr<Instance> instance = make_unique<Instance>(argc, argv);

	vk::Format renderFormat = instance->Window().SurfaceFormat().format;
	
	unique_ptr sceneRoot = make_unique<SceneNode>(*instance, "Root");
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
	SetAttachmentInfo("stm_main_render", instance->Window().SwapchainExtent(), vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);
	SetAttachmentInfo("stm_main_depth", instance->Window().SwapchainExtent(), vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);
	SetAttachmentInfo("stm_main_resolve", instance->Window().SwapchainExtent(), vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);

	// environment data
	
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


	// skybox

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

	Window& window = instance->Window();

	RenderGraph& rg = sceneRoot->CreateComponent<RenderGraph>("window_rendergraph");

	auto frameSemaphore = make_shared<Semaphore>(instance->Device(), "Frame Semaphore");
	
	float frameTimeAccum = 0;
	uint32_t fpsAccum = 0;
	float fps = 0;

	while (instance->AdvanceFrame()) {
		// count fps
		frameTimeAccum += deltaTime;
		fpsAccum++;
		if (frameTimeAccum > 1.f) {
			fps = fpsAccum / frameTimeAccum;
			frameTimeAccum -= 1.f;
			fpsAccum = 0;
		}
	
		auto commandBuffer = instance->Device().GetCommandBuffer("Frame CommandBuffer");
		
		//sceneRoot->Update(*commandBuffer);

		if (window.Swapchain()) {
			rg.MainRenderExtent(instance->Window().SwapchainExtent());
			auto fb = rg.Render(*commandBuffer);

			// copy to window
			shared_ptr<Texture> srcImage = fb->GetAttachment("stm_main_resolve");
			if (!srcImage) srcImage = fb->GetAttachment("stm_main_render");

			srcImage->TransitionBarrier(*commandBuffer, vk::ImageLayout::eTransferSrcOptimal);
			commandBuffer->TransitionBarrier(window.BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal);
			vk::ImageCopy rgn = {};
			rgn.dstSubresource = rgn.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
			rgn.extent = vk::Extent3D(window.SwapchainExtent(), 1);
			(*commandBuffer)->copyImage(**srcImage, vk::ImageLayout::eTransferSrcOptimal, window.BackBuffer(), vk::ImageLayout::eTransferDstOptimal, { rgn });
			commandBuffer->TransitionBarrier(window.BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR);
			commandBuffer->SignalOnComplete(vk::PipelineStageFlagBits::eTransfer, frameSemaphore);
		}
		
		instance->Device().Execute(move(commandBuffer));
		instance->PresentFrame({ **frameSemaphore, *window.ImageAvailableSemaphore() });
	}
	instance->Device().Flush();
	frameSemaphore.reset();
	
	instance->Device().UnloadAssets();

	for (Plugin* p : mPlugins) delete p;
	sceneRoot.reset();
	
	instance.reset();

	#if defined(WIN32) && defined(_DEBUG)
	_CrtMemState s1;
	_CrtMemCheckpoint(&s1);
	_CrtMemState ds;
	_CrtMemDifference(&ds, &s0, &s1);
	if (ds.lTotalCount) _CrtMemDumpStatistics(&ds);
	#endif
	return EXIT_SUCCESS;
}