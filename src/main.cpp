#include "Core/Material.hpp"
#include "Core/Mesh.hpp"
#include "Core/Profiler.hpp"
#include "Core/Window.hpp"

using namespace stm;
using namespace stm::hlsl;
#pragma pack(push)
#pragma pack(1)
#include "Shaders/pbr.hlsl"
#pragma pack(pop)

unordered_map<string, shared_ptr<SpirvModule>> gSpirvModules;

inline void GenerateMipMaps(CommandBuffer& commandBuffer, shared_ptr<Texture> texture) {
	texture->TransitionBarrier(commandBuffer, vk::ImageLayout::eGeneral);
	auto pipeline = make_shared<ComputePipeline>(commandBuffer.mDevice, "average2d", gSpirvModules.at("average2d"));
	commandBuffer.BindPipeline(pipeline);
	vk::Extent3D extent = texture->Extent();
	for (uint32_t i = 0; i < texture->MipLevels()-1; i++) {
		extent.width  = max(1u, extent.width/2);
		extent.height = max(1u, extent.height/2);
		extent.depth  = max(1u, extent.depth/2);
		if (i > 0)
			commandBuffer.Barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::ImageMemoryBarrier(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead, 
				vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, **texture,
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, i, 1, 0, texture->ArrayLayers()))
			);
		commandBuffer.BindDescriptorSet(0, make_shared<DescriptorSet>(pipeline->DescriptorSetLayouts()[0], "mipmap tmp" + to_string(i), unordered_map<uint32_t, Descriptor> {
			{ pipeline->binding("gInput2").mBinding, storage_texture_descriptor(Texture::View(texture,i,1)) },
			{ pipeline->binding("gOutput2").mBinding, storage_texture_descriptor(Texture::View(texture,i+1,1)) } }));
		commandBuffer.DispatchTiled(extent);
	}
}

inline shared_ptr<Texture> LoadTexture(CommandBuffer& commandBuffer, const string& name, const Texture::PixelData& pixelData, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	const auto&[pixels,extent,format] = pixelData;
	shared_ptr<Texture> tex = make_shared<Texture>(commandBuffer.mDevice, name, extent, format, 1, 0, vk::SampleCountFlagBits::e1, usage | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
	tex->TransitionBarrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	
	auto& staging = commandBuffer.HoldResource(make_shared<Buffer>(pixels, "copy src", pixels->size(), vk::BufferUsageFlagBits::eTransferSrc));
	commandBuffer->copyBufferToImage(*staging, **tex, vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, tex->ArrayLayers()), {}, tex->Extent()) });
	
	GenerateMipMaps(commandBuffer, tex);
	return tex;
}
inline shared_ptr<Texture> LoadTexture(CommandBuffer& commandBuffer, const fs::path& filename, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	return LoadTexture(commandBuffer, filename.stem().string(), Texture::LoadPixels(commandBuffer.mDevice, filename), usage);
}

inline shared_ptr<Texture> CombineChannels(CommandBuffer& commandBuffer, const string& name, const Texture::PixelData& img0, const Texture::PixelData& img1, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	const auto&[pixels0,extent0,format0] = img0;
	const auto&[pixels1,extent1,format1] = img1;
	
	if (extent0 != extent1) throw invalid_argument("Image extents must match");

	vk::Format dstFormat = vk::Format::eR8G8Unorm;

	auto pipeline = make_shared<ComputePipeline>(commandBuffer.mDevice, "interleave", gSpirvModules.at("interleave"));
	auto staging0 = Buffer::View(make_shared<Buffer>(pixels0, "gInputR", pixels0->size(), vk::BufferUsageFlagBits::eUniformTexelBuffer));
	auto staging1 = Buffer::View(make_shared<Buffer>(pixels1, "gInputG", pixels1->size(), vk::BufferUsageFlagBits::eUniformTexelBuffer));
	auto combined = Buffer::StrideView(make_shared<Buffer>(commandBuffer.mDevice, name, staging0.size()+staging1.size(), vk::BufferUsageFlagBits::eStorageTexelBuffer|vk::BufferUsageFlagBits::eTransferSrc), ElementSize(dstFormat));
	auto dst = make_shared<Texture>(commandBuffer.mDevice, name, extent0, dstFormat, 1, 0, vk::SampleCountFlagBits::e1, usage | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);

	commandBuffer.HoldResource(combined);
	commandBuffer.HoldResource(dst);
	
	commandBuffer.BindPipeline(pipeline);
	commandBuffer.BindDescriptorSet(0, make_shared<DescriptorSet>(commandBuffer.BoundPipeline()->DescriptorSetLayouts()[0], "tmp", unordered_map<uint32_t, Descriptor> {
		{ pipeline->binding("gOutputRG").mBinding, Buffer::TexelView(combined, dstFormat) },
		{ pipeline->binding("gInputR").mBinding, Buffer::TexelView(staging0, format0) },
		{ pipeline->binding("gInputG").mBinding, Buffer::TexelView(staging1, format1) } }));
	commandBuffer.PushConstant<uint32_t>("Width", extent0.width);
	commandBuffer.DispatchTiled(extent0);

	commandBuffer.Barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead, combined);
	dst->TransitionBarrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	commandBuffer->copyBufferToImage(*combined.buffer(), **dst, vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy(combined.offset(), 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), {}, extent0) });
	GenerateMipMaps(commandBuffer, dst);

	return dst;
}

int main(int argc, char** argv) {
	unique_ptr<Instance> instance = make_unique<Instance>(argc, argv);
	{
		Window& window = instance->Window();
		Device& device = instance->Device();

		ranges::for_each( byte_stream<ifstream>("Assets/core_shaders.spvm", ios::binary).read<vector<SpirvModule>>(),
			[&](const auto& m) { gSpirvModules.emplace(m.mEntryPoint, make_shared<SpirvModule>(m)).first->second; });

		// Create render passes

		vk::PipelineColorBlendAttachmentState blendOpaque;
		blendOpaque.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		
		auto shadowAtlasAttachment = vk::AttachmentDescription({}, vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
		auto colorAttachment = vk::AttachmentDescription({}, vk::Format::eR8G8B8A8Unorm, vk::SampleCountFlagBits::e4,
			vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal);
		auto depthAttachment = vk::AttachmentDescription({}, vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e4,
			vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal);
		auto resolveAttachment = vk::AttachmentDescription({}, vk::Format::eR8G8B8A8Unorm, vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR);
		RenderPass::SubpassDescription render_shadows { "render_shadows", vk::PipelineBindPoint::eGraphics,
			{ // attachments
				{ "shadow_atlas", { shadowAtlasAttachment, RenderPass::AttachmentType::eDepthStencil, blendOpaque } }
			}
		};
		RenderPass::SubpassDescription main_render { "main_render", vk::PipelineBindPoint::eGraphics,
			{ // attachments
				{ "primary_color", { colorAttachment, RenderPass::AttachmentType::eColor, blendOpaque } },
				{ "swapchain_image", { resolveAttachment, RenderPass::AttachmentType::eResolve, blendOpaque } },
				{ "primary_depth", { depthAttachment, RenderPass::AttachmentType::eDepthStencil, blendOpaque } },
			}
		};
		shared_ptr<RenderPass> shadowPass = make_shared<RenderPass>(device, "shadow", vector<RenderPass::SubpassDescription>{ render_shadows });
		shared_ptr<RenderPass> mainPass = make_shared<RenderPass>(device, "main", vector<RenderPass::SubpassDescription>{ main_render });


		// Create resources

		auto axisMaterial = make_shared<Material>("axis", gSpirvModules.at("vs_axis"), gSpirvModules.at("fs_color"));
		auto pbrMaterial = make_shared<Material>("pbr", gSpirvModules.at("vs_pbr"), gSpirvModules.at("fs_pbr"));
		auto shadowMaterial = make_shared<Material>("pbr", gSpirvModules.at("vs_pbr"));
		shared_ptr<Mesh> testMesh;

		{
			auto commandBuffer = device.GetCommandBuffer("Init");
			testMesh = make_shared<Mesh>(*commandBuffer, "Assets/Models/suzanne.obj");
			pbrMaterial->Descriptor("gBaseColorTexture") 				 = sampled_texture_descriptor(Texture::View(LoadTexture(*commandBuffer, "E:/3d/blender/Textures/copper/Metal08_col.jpg")));
			pbrMaterial->Descriptor("gNormalTexture") 					 = sampled_texture_descriptor(Texture::View(LoadTexture(*commandBuffer, "E:/3d/blender/Textures/copper/Metal08_nrm.jpg")));
			pbrMaterial->Descriptor("gMetallicRoughnessTexture") = sampled_texture_descriptor(Texture::View(CombineChannels(*commandBuffer, "Metal08_met_rgh", Texture::LoadPixels(device, "E:/3d/blender/Textures/copper/Metal08_met.jpg"), Texture::LoadPixels(device, "E:/3d/blender/Textures/copper/Metal08_rgh.jpg"))));
			device.Execute(commandBuffer);
		}

		pbrMaterial->SetImmutableSampler("gSampler", make_shared<Sampler>(device, "gSampler", vk::SamplerCreateInfo({},
			vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
			0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE)));
		pbrMaterial->SetImmutableSampler("gShadowSampler", make_shared<Sampler>(device, "gShadowSampler", vk::SamplerCreateInfo({},
			vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder,
			0, true, 2, true, vk::CompareOp::eLessOrEqual, 0, VK_LOD_CLAMP_NONE, vk::BorderColor::eFloatOpaqueWhite)));

		auto deviceAlloc = device_allocator<byte>(device, device.MemoryTypeIndex(host_visible_coherent), device.Limits().minStorageBufferOffsetAlignment);

		TransformData gCameraToWorld = make_transform(Vector3f(0,0,-4));
		device_vector<TransformData> gInstanceTransforms(1, deviceAlloc);
		device_vector<MaterialData> gMaterials(1, deviceAlloc);
		device_vector<LightData> gLights(1, deviceAlloc);
		gInstanceTransforms[0] = make_transform(Vector3f::Zero());
		gMaterials[0].gTextureST = Vector4f(1,1,0,0);
		gMaterials[0].gBaseColor = Vector4f::Ones();
		gMaterials[0].gEmission = Vector3f::Zero();
		gMaterials[0].gBumpStrength = 1;
		gMaterials[0].gMetallic = 0;
		gMaterials[0].gRoughness = 1;
		gLights[0].Flags = LIGHT_ATTEN_DIRECTIONAL | LIGHT_USE_SHADOWMAP;
		gLights[0].LightToWorld = make_transform(float3(1,1,-1), Quaternionf::FromTwoVectors(Vector3f(0,0,1), Vector3f(-1,-1,1)));
		gLights[0].ShadowProjection = make_orthographic(3, 3, -512, 128);
		gLights[0].ShadowBias = .00001f;
		gLights[0].Emission = Vector3f::Constant(3);
		gLights[0].ShadowST = Vector4f(1,1,0,0);
		
		auto frameTimeAccum = chrono::nanoseconds::zero();
		uint32_t fpsAccum = 0;
		while (true) {
			Profiler::BeginSample("Frame" + to_string(window.PresentCount()));

			auto commandBuffer = device.GetCommandBuffer("Frame");

			shared_ptr<Semaphore> renderSemaphore = make_shared<Semaphore>(device, "RenderSemaphore");
			commandBuffer->SignalOnComplete(vk::PipelineStageFlagBits::eAllCommands, renderSemaphore);
			
			if (!instance->PollEvents()) break; // Window was closed
			window.AcquireNextImage(*commandBuffer);
			commandBuffer->WaitOn(vk::PipelineStageFlagBits::eAllCommands, window.ImageAvailableSemaphore());

			static auto t0 = chrono::high_resolution_clock::now();
			auto t1 = chrono::high_resolution_clock::now();
			float deltaTime = chrono::duration_cast<chrono::duration<float>>(t1 - t0).count();
			t0 = t1;

			static float fovy = 60;
			{
				ProfilerRegion ps("Camera Controls");

				fovy = clamp(fovy - window.ScrollDelta(), 20.f, 90.f);

				if (window.KeyDown(MOUSE_RIGHT)) {
					static Vector2f euler = Vector2f::Zero();
					euler += window.CursorDelta().reverse() * .005f;
					euler.x() = clamp(euler.x(), -numbers::pi_v<float>/2, numbers::pi_v<float>/2);
					gCameraToWorld.Rotation = Quaternionf(AngleAxisf(euler.y(), Vector3f(0,1,0))) * Quaternionf(AngleAxisf(euler.x(), Vector3f(1,0,0)));
				}
				Vector3f mv = Vector3f::Zero();
				if (window.KeyDown(KEY_D)) mv += Vector3f( 1,0,0);
				if (window.KeyDown(KEY_A)) mv += Vector3f(-1,0,0);
				if (window.KeyDown(KEY_W)) mv += Vector3f(0,0, 1);
				if (window.KeyDown(KEY_S)) mv += Vector3f(0,0,-1);
				gCameraToWorld.Translation += (gCameraToWorld.Rotation * mv*deltaTime).array();

				if (window.KeyDown(KEY_LEFT))  gInstanceTransforms[0].Rotation *= Quaternionf(AngleAxisf( deltaTime, gCameraToWorld.Rotation * Vector3f(0,1,0)));
				if (window.KeyDown(KEY_RIGHT)) gInstanceTransforms[0].Rotation *= Quaternionf(AngleAxisf(-deltaTime, gCameraToWorld.Rotation * Vector3f(0,1,0)));
				if (window.KeyDown(KEY_UP)) 	 gInstanceTransforms[0].Rotation *= Quaternionf(AngleAxisf( deltaTime, gCameraToWorld.Rotation * Vector3f(1,0,0)));
				if (window.KeyDown(KEY_DOWN))  gInstanceTransforms[0].Rotation *= Quaternionf(AngleAxisf(-deltaTime, gCameraToWorld.Rotation * Vector3f(1,0,0)));
			}

			// render
			if (window.Swapchain()) {
				auto gShadowAtlas = make_shared<Texture>(device, "shadow_atlas", vk::Extent3D(2048,2048,1), shadowAtlasAttachment, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);
				auto primaryColor = make_shared<Texture>(device, "primary_color", vk::Extent3D(window.SwapchainExtent(),1), colorAttachment, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);
				auto primaryDepth = make_shared<Texture>(device, "primary_depth", vk::Extent3D(window.SwapchainExtent(),1), depthAttachment, vk::ImageUsageFlagBits::eDepthStencilAttachment);
				auto instanceTransforms = make_buffer(gInstanceTransforms, "gInstanceTransforms", vk::BufferUsageFlagBits::eStorageBuffer);
						

				shadowMaterial->Descriptor("gInstanceTransforms") = instanceTransforms;
				shadowMaterial->PushConstant("WorldToCamera", inverse(gLights[0].LightToWorld));
				shadowMaterial->PushConstant("Projection", gLights[0].ShadowProjection);

				pbrMaterial->Descriptor("gShadowAtlas") = sampled_texture_descriptor(gShadowAtlas);
				pbrMaterial->Descriptor("gLights") = make_buffer(gLights, "gLights", vk::BufferUsageFlagBits::eStorageBuffer);
				pbrMaterial->Descriptor("gMaterials") = make_buffer(gMaterials, "gMaterial", vk::BufferUsageFlagBits::eStorageBuffer);
				pbrMaterial->Descriptor("gInstanceTransforms") = instanceTransforms;
				pbrMaterial->PushConstant("WorldToCamera", inverse(gCameraToWorld));
				pbrMaterial->PushConstant("Projection", make_perspective(radians(fovy), (float)window.SwapchainExtent().height/(float)window.SwapchainExtent().width, 0, 64));
				pbrMaterial->PushConstant("LightCount", (uint32_t)gLights.size());

				axisMaterial->PushConstant("WorldToCamera", pbrMaterial->PushConstant("WorldToCamera"));
				axisMaterial->PushConstant("Projection", pbrMaterial->PushConstant("Projection"));

				{
					ProfilerRegion ps("Render Shadows");
					shadowMaterial->TransitionTextures(*commandBuffer);
					
					auto framebuffer = make_shared<Framebuffer>("shadow_framebuffer", *shadowPass, Texture::View(gShadowAtlas));
					commandBuffer->BeginRenderPass(shadowPass, framebuffer, { vk::ClearDepthStencilValue{1.f,0} });
					(*commandBuffer)->setViewport(0, { vk::Viewport(0, (float)gShadowAtlas->Extent().height, (float)gShadowAtlas->Extent().width, -(float)gShadowAtlas->Extent().height, 0, 1) });
					(*commandBuffer)->setScissor(0, { vk::Rect2D(vk::Offset2D(0,0), vk::Extent2D(gShadowAtlas->Extent().width, gShadowAtlas->Extent().height)) });

					shadowMaterial->Bind(*commandBuffer, testMesh->Geometry());
					testMesh->Draw(*commandBuffer);
					
					commandBuffer->EndRenderPass();
				}
				{
					ProfilerRegion ps("Main render");
					pbrMaterial->TransitionTextures(*commandBuffer);
					axisMaterial->TransitionTextures(*commandBuffer);

					auto framebuffer = make_shared<Framebuffer>("framebuffer", *mainPass, Texture::View(primaryColor), window.BackBuffer(), Texture::View(primaryDepth));
					vector<vk::ClearValue> clearValues(framebuffer->size());
					clearValues[mainPass->AttachmentIndex("primary_color")].setColor(std::array<float,4>{0.2f, 0.2f, 0.2f, 1});
					clearValues[mainPass->AttachmentIndex("swapchain_image")].setColor(std::array<float,4>{0,0,0,0});
					clearValues[mainPass->AttachmentIndex("primary_depth")].setDepthStencil({1,0});

					commandBuffer->BeginRenderPass(mainPass, framebuffer, clearValues);
					(*commandBuffer)->setViewport(0, { vk::Viewport(0, (float)framebuffer->Extent().height, (float)framebuffer->Extent().width, -(float)framebuffer->Extent().height, 0, 1) });
					(*commandBuffer)->setScissor(0, { vk::Rect2D(vk::Offset2D(0,0), framebuffer->Extent()) });

					axisMaterial->Bind(*commandBuffer, vk::PrimitiveTopology::eLineList);
					(*commandBuffer)->setLineWidth(1);
					(*commandBuffer)->draw(2, 3, 0, 0);

					pbrMaterial->Bind(*commandBuffer, testMesh->Geometry());
					testMesh->Draw(*commandBuffer);
					
					commandBuffer->EndRenderPass();
				}
			}

			device.Execute(commandBuffer);
			
			if (window.Swapchain()) {
				ProfilerRegion ps("Present");
				window.Present({ **renderSemaphore });
			}

			// count fps
			frameTimeAccum += Profiler::EndSample().mDuration;
			fpsAccum++;
			if (frameTimeAccum > 1s) {
				float totalTime = chrono::duration<float>(frameTimeAccum).count();
				printf("%.2f fps (%fms)\t\t\r", (float)fpsAccum/totalTime, totalTime/fpsAccum);
				frameTimeAccum -= 1s;
				fpsAccum = 0;
			}
		}
		
		device.Flush();
		gSpirvModules.clear();
	}
	instance.reset();
	Profiler::ClearHistory();
	return EXIT_SUCCESS;
}