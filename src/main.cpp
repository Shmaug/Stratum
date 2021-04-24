#include "Core/Material.hpp"
#include "Core/Mesh.hpp"
#include "Core/Profiler.hpp"
#include "Core/Window.hpp"

#include "Scene/MeshRenderer.hpp"

using namespace stm;
using namespace stm::hlsl;
#pragma pack(push)
#pragma pack(1)
#include "Shaders/pbr.hlsl"
#pragma pack(pop)

unordered_map<string, shared_ptr<SpirvModule>> gSpirvModules;

inline void generate_mip_maps(CommandBuffer& commandBuffer, shared_ptr<Texture> texture) {
	ProfilerRegion pr("generate_mip_maps(" + texture->name() + ")", commandBuffer);
	texture->transition_barrier(commandBuffer, vk::ImageLayout::eGeneral);
	auto pipeline = make_shared<ComputePipeline>(commandBuffer.mDevice, "average2d", gSpirvModules.at("average2d"));
	commandBuffer.bind_pipeline(pipeline);
	vk::Extent3D extent = texture->extent();
	for (uint32_t i = 0; i < texture->mip_levels()-1; i++) {
		extent.width  = max(1u, extent.width/2);
		extent.height = max(1u, extent.height/2);
		extent.depth  = max(1u, extent.depth/2);
		if (i > 0)
			commandBuffer.barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::ImageMemoryBarrier(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead, 
				vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, **texture,
				vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, i, 1, 0, texture->array_layers()))
			);
		commandBuffer.bind_descriptor_set(0, make_shared<DescriptorSet>(pipeline->descriptor_set_layouts()[0], "mipmap tmp" + to_string(i), unordered_map<uint32_t, Descriptor> {
			{ pipeline->binding("gInput2").mBinding, storage_texture_descriptor(Texture::View(texture,i,1)) },
			{ pipeline->binding("gOutput2").mBinding, storage_texture_descriptor(Texture::View(texture,i+1,1)) } }));
		commandBuffer.dispatch_align(extent);
	}
}

inline shared_ptr<Texture> load_texture(CommandBuffer& commandBuffer, const string& name, const Texture::PixelData& pixelData, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	ProfilerRegion pr("load " + name, commandBuffer);
	const auto&[pixels,extent] = pixelData;
	shared_ptr<Texture> tex = make_shared<Texture>(commandBuffer.mDevice, name, extent, pixels.format(), 1, 0, vk::SampleCountFlagBits::e1, usage|vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
	tex->transition_barrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	commandBuffer->copyBufferToImage(*commandBuffer.hold_resource(pixels.buffer_ptr()), **tex, vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy(pixels.offset(), 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, tex->array_layers()), {}, tex->extent()) });
	generate_mip_maps(commandBuffer, tex);
	return tex;
}
inline shared_ptr<Texture> load_texture(CommandBuffer& commandBuffer, const fs::path& filename, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	return load_texture(commandBuffer, filename.stem().string(), Texture::load(commandBuffer.mDevice, filename), usage);
}

inline shared_ptr<Texture> combine_rg(CommandBuffer& commandBuffer, const string& name, const Texture::PixelData& img0, const Texture::PixelData& img1, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	ProfilerRegion pr("combine_rg " + name, commandBuffer);
	
	const auto&[pixels0,extent0] = img0;
	const auto&[pixels1,extent1] = img1;
	
	if (extent0 != extent1) throw invalid_argument("Image extents must match");

	auto pipeline = make_shared<ComputePipeline>(commandBuffer.mDevice, "interleave", gSpirvModules.at("interleave"));
	auto combined = commandBuffer.hold_resource(Buffer::TexelView(
		make_shared<Buffer>(commandBuffer.mDevice, name, pixels0.size_bytes() + pixels1.size_bytes(), vk::BufferUsageFlagBits::eStorageTexelBuffer|vk::BufferUsageFlagBits::eTransferSrc), vk::Format::eR8G8Unorm));
	
	commandBuffer.bind_pipeline(pipeline);
	commandBuffer.bind_descriptor_set(0, make_shared<DescriptorSet>(commandBuffer.bound_pipeline()->descriptor_set_layouts()[0], "tmp", unordered_map<uint32_t, Descriptor> {
		{ pipeline->binding("gOutputRG").mBinding, combined },
		{ pipeline->binding("gInputR").mBinding, pixels0 },
		{ pipeline->binding("gInputG").mBinding, pixels1 } }));
	commandBuffer.push_constant<uint32_t>("Width", extent0.width);
	commandBuffer.dispatch_align(extent0);

	commandBuffer.barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead, combined);

	auto dst = make_shared<Texture>(commandBuffer.mDevice, name, extent0, combined.format(), 1, 0, vk::SampleCountFlagBits::e1, usage | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
	commandBuffer.hold_resource(dst).transition_barrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	commandBuffer->copyBufferToImage(*combined.buffer(), **dst, vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy(combined.offset(), 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), {}, extent0) });
	generate_mip_maps(commandBuffer, dst);
	return dst;
}

int main(int argc, char** argv) {
	unique_ptr<Instance> instance = make_unique<Instance>(argc, argv);
	{
		Window& window = instance->window();
		Device& device = instance->device();
		Scene scene;

		// Create render passes

		vk::PipelineColorBlendAttachmentState blendOpaque;
		blendOpaque.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		
		auto shadowAtlasAttachment = vk::AttachmentDescription({}, vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
		auto depthAttachment = vk::AttachmentDescription({}, vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e4,
			vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal);
		auto colorAttachment = vk::AttachmentDescription({}, vk::Format::eR8G8B8A8Unorm, vk::SampleCountFlagBits::e4,
			vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal);
		auto resolveAttachment = vk::AttachmentDescription({}, vk::Format::eR8G8B8A8Unorm, vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR);
			
		RenderNode& shadowRenderNode = scene.emplace<RenderNode>(scene, "shadowpass");
		shadowRenderNode[""].bind_point(vk::PipelineBindPoint::eGraphics);
		shadowRenderNode[""]["gShadowAtlas"] = { RenderPass::AttachmentType::eDepthStencil, shadowAtlasAttachment, blendOpaque };

		RenderNode& mainRenderNode = scene.emplace<RenderNode>(scene, "main");
		mainRenderNode[""].bind_point(vk::PipelineBindPoint::eGraphics);
		mainRenderNode[""]["primary_color"] =  { RenderPass::AttachmentType::eColor, colorAttachment, blendOpaque };
		mainRenderNode[""]["primary_depth"] =  { RenderPass::AttachmentType::eDepthStencil, depthAttachment, blendOpaque };
		mainRenderNode[""]["primary_resolve"] =  { RenderPass::AttachmentType::eResolve, resolveAttachment, blendOpaque };
		
		// Create resources

		ranges::for_each( byte_stream<ifstream>("Assets/core_shaders.spvm", ios::binary).read<vector<SpirvModule>>(),
			[&](const auto& m) { gSpirvModules.emplace(m.mEntryPoint, make_shared<SpirvModule>(m)).first->second; });

		auto pbrMaterial = make_shared<Material>("pbr", gSpirvModules.at("vs_pbr"), gSpirvModules.at("fs_pbr"));
		auto shadowMaterial = make_shared<Material>("shadow", gSpirvModules.at("vs_pbr"));
		
		{
			auto commandBuffer = device.get_command_buffer("Init");
			pbrMaterial->descriptor("gBaseColorTexture") 				 = sampled_texture_descriptor(Texture::View(load_texture(*commandBuffer, "E:/3d/blender/Textures/Metal008/Metal008_2K_Color.jpg")));
			pbrMaterial->descriptor("gNormalTexture") 					 = sampled_texture_descriptor(Texture::View(load_texture(*commandBuffer, "E:/3d/blender/Textures/Metal008/Metal008_2K_Normal.jpg")));
			pbrMaterial->descriptor("gMetallicRoughnessTexture") = sampled_texture_descriptor(Texture::View(combine_rg(*commandBuffer, "Metal08_met_rgh", Texture::load(device, "E:/3d/blender/Textures/Metal008/Metal008_2K_Metalness.jpg"), Texture::load(device, "E:/3d/blender/Textures/Metal008/Metal008_2K_Roughness.jpg"))));
			MeshRenderer& renderer = scene.emplace<MeshRenderer>(scene, "suzanne", make_shared<Mesh>(*commandBuffer, "Assets/Models/suzanne.obj"));
			device.submit(commandBuffer);

			renderer.material(shadowRenderNode) = shadowMaterial;
			renderer.material(mainRenderNode) = pbrMaterial;

			mainRenderNode.OnRender.emplace(renderer, &MeshRenderer::draw);
			shadowRenderNode.OnRender.emplace(renderer, &MeshRenderer::draw);
		}

		pbrMaterial->immutable_sampler("gSampler", make_shared<Sampler>(device, "gSampler", vk::SamplerCreateInfo({},
			vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
			0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE)));
		pbrMaterial->immutable_sampler("gShadowSampler", make_shared<Sampler>(device, "gShadowSampler", vk::SamplerCreateInfo({},
			vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder,
			0, true, 2, true, vk::CompareOp::eLess, 0, VK_LOD_CLAMP_NONE, vk::BorderColor::eFloatOpaqueWhite)));

		TransformData gCameraToWorld = make_transform(Vector3f(0,0,-4));
		buffer_vector<TransformData> gInstanceTransforms(device, 1);
		buffer_vector<MaterialData> gMaterials(device, 1);
		buffer_vector<LightData> gLights(device, 1);
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
		
		while (true) {
			Profiler::begin_sample("Frame" + to_string(window.present_count()));
			if (!instance->poll_events()) break; // Window was closed

			auto commandBuffer = device.get_command_buffer("Frame");

			auto backBuffer = window.acquire_image(*commandBuffer);
			commandBuffer->wait_semaphore(vk::PipelineStageFlagBits::eColorAttachmentOutput, window.image_available_semaphore());

			static float fovy = 60;
			{
				ProfilerRegion ps("Camera Controls");
				
				static auto t0 = chrono::high_resolution_clock::now();
				auto t1 = chrono::high_resolution_clock::now();
				auto dt = t1 - t0;
				t0 = t1;

				float deltaTime = chrono::duration_cast<chrono::duration<float>>(dt).count();

				// count fps
				static float frameTimeAccum = 0;
				static uint32_t fpsAccum = 0;
				frameTimeAccum += deltaTime;
				fpsAccum++;
				if (frameTimeAccum > 1) {
					float totalTime = chrono::duration<float>(frameTimeAccum).count();
					printf("%.2f fps (%fms)\t\t\r", (float)fpsAccum/totalTime, totalTime/fpsAccum);
					frameTimeAccum -= 1;
					fpsAccum = 0;
				}

				fovy = clamp(fovy - window.scroll_delta(), 20.f, 90.f);

				if (window.is_key_down(MOUSE_RIGHT)) {
					static Vector2f euler = Vector2f::Zero();
					euler += window.cursor_delta().reverse() * .005f;
					euler.x() = clamp(euler.x(), -numbers::pi_v<float>/2, numbers::pi_v<float>/2);
					gCameraToWorld.Rotation = Quaternionf(AngleAxisf(euler.y(), Vector3f(0,1,0))) * Quaternionf(AngleAxisf(euler.x(), Vector3f(1,0,0)));
				}
				Vector3f mv = Vector3f::Zero();
				if (window.is_key_down(KEY_D)) mv += Vector3f( 1,0,0);
				if (window.is_key_down(KEY_A)) mv += Vector3f(-1,0,0);
				if (window.is_key_down(KEY_W)) mv += Vector3f(0,0, 1);
				if (window.is_key_down(KEY_S)) mv += Vector3f(0,0,-1);
				gCameraToWorld.Translation += (gCameraToWorld.Rotation * mv*deltaTime).array();

				if (window.is_key_down(KEY_LEFT))  gInstanceTransforms[0].Rotation *= Quaternionf(AngleAxisf( deltaTime, gCameraToWorld.Rotation * Vector3f(0,1,0)));
				if (window.is_key_down(KEY_RIGHT)) gInstanceTransforms[0].Rotation *= Quaternionf(AngleAxisf(-deltaTime, gCameraToWorld.Rotation * Vector3f(0,1,0)));
				if (window.is_key_down(KEY_UP)) 	 gInstanceTransforms[0].Rotation *= Quaternionf(AngleAxisf( deltaTime, gCameraToWorld.Rotation * Vector3f(1,0,0)));
				if (window.is_key_down(KEY_DOWN))  gInstanceTransforms[0].Rotation *= Quaternionf(AngleAxisf(-deltaTime, gCameraToWorld.Rotation * Vector3f(1,0,0)));
			}

			// render
			if (backBuffer) {
				Texture::View shadowAtlas = make_shared<Texture>(device, "gShadowAtlas", 
					get<vk::AttachmentDescription>(shadowRenderNode[""]["gShadowAtlas"]), 
					vk::Extent3D(2048,2048,1), vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);

				Texture::View primaryColor = make_shared<Texture>(device, "primary_color", 
					get<vk::AttachmentDescription>(mainRenderNode[""]["primary_color"]),
					backBuffer.texture().extent(), vk::ImageUsageFlagBits::eColorAttachment);
				Texture::View primaryDepth = make_shared<Texture>(device, "primary_depth", 
					get<vk::AttachmentDescription>(mainRenderNode[""]["primary_depth"]),
					backBuffer.texture().extent(), vk::ImageUsageFlagBits::eDepthStencilAttachment);

				{
					ProfilerRegion ps("Update Materials", *commandBuffer);
					
					pbrMaterial->push_constant("WorldToCamera", inverse(gCameraToWorld));
					pbrMaterial->push_constant("Projection", make_perspective(radians(fovy), (float)window.swapchain_extent().height/(float)window.swapchain_extent().width, 0, 64));
					pbrMaterial->push_constant("LightCount", (uint32_t)gLights.size());
					pbrMaterial->descriptor("gLights") = commandBuffer->copy_buffer<byte>(gLights, vk::BufferUsageFlagBits::eStorageBuffer);
					pbrMaterial->descriptor("gMaterials") = commandBuffer->copy_buffer<byte>(gMaterials, vk::BufferUsageFlagBits::eStorageBuffer);
					pbrMaterial->descriptor("gInstanceTransforms") = shadowMaterial->descriptor("gInstanceTransforms") = commandBuffer->copy_buffer<byte>(gInstanceTransforms, vk::BufferUsageFlagBits::eStorageBuffer);
					pbrMaterial->descriptor("gShadowAtlas") = sampled_texture_descriptor(Texture::View(shadowAtlas));
					shadowMaterial->push_constant("WorldToCamera", inverse(gLights[0].LightToWorld));
					shadowMaterial->push_constant("Projection", gLights[0].ShadowProjection);

					shadowMaterial->transition_images(*commandBuffer);
					pbrMaterial->transition_images(*commandBuffer);
				}

				shadowRenderNode.render(*commandBuffer, { { "gShadowAtlas", shadowAtlas } });
				mainRenderNode.render(*commandBuffer, {
					{ "primary_color", primaryColor },
					{ "primary_depth", primaryDepth },
					{ "primary_resolve", backBuffer } });
			}

			shared_ptr<Semaphore> renderSemaphore = make_shared<Semaphore>(device, "RenderSemaphore");
			commandBuffer->signal_semaphore(vk::PipelineStageFlagBits::eColorAttachmentOutput, renderSemaphore);
			
			device.submit(commandBuffer);
			if (backBuffer)
				window.present({ **renderSemaphore });
		}
		
		device.flush();
		gSpirvModules.clear();
	}
	instance.reset();
	Profiler::clear();
	return EXIT_SUCCESS;
}