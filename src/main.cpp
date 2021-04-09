#include "Core/Window.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/Mesh.hpp"
#include "Core/Profiler.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace stm;
using namespace stm::hlsl;
#pragma pack(push)
#pragma pack(1)
#include "Shaders/pbr.hlsl"
#pragma pack(pop)

unordered_map<std::string, std::shared_ptr<stm::SpirvModule>> gSpirvModules;

inline unordered_map<string, shared_ptr<SpirvModule>> LoadShaders(Device& device, const fs::path& spvm) {
	unordered_map<string, shared_ptr<SpirvModule>> spirvModules;
	unordered_map<string, SpirvModule> tmp;
	byte_stream<ifstream>(spvm, ios::binary) >> tmp;
	ranges::for_each(tmp | views::values, [&](const auto& m){ spirvModules.emplace(m.mEntryPoint, make_shared<SpirvModule>(m)); });
	return spirvModules;
}

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
		commandBuffer.BindDescriptorSet(0, make_shared<DescriptorSet>(pipeline->DescriptorSetLayouts()[0], "mipmap tmp" + to_string(i), unordered_map<uint32_t, DescriptorSet::Entry> {
			{ pipeline->binding("gInput2").mBinding, DescriptorSet::TextureEntry{nullptr,TextureView(texture,i,1), vk::ImageLayout::eGeneral } },
			{ pipeline->binding("gOutput2").mBinding, DescriptorSet::TextureEntry{nullptr,TextureView(texture,i+1,1), vk::ImageLayout::eGeneral } } }));
		commandBuffer.DispatchTiled(extent);
	}
}

inline shared_ptr<Texture> LoadTexture(CommandBuffer& commandBuffer, const string& name, const Texture::PixelData& pixelData, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	const auto&[pixels,extent,format] = pixelData;
	shared_ptr<Texture> tex = make_shared<Texture>(commandBuffer.mDevice, name, extent, format, 1, 0, vk::SampleCountFlagBits::e1, usage | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
	tex->TransitionBarrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	auto staging = commandBuffer.CreateStagingBuffer(pixels);
	commandBuffer->copyBufferToImage(*staging.buffer(), **tex, vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy(staging.offset(), 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, tex->ArrayLayers()), {}, tex->Extent()) });
	GenerateMipMaps(commandBuffer, tex);
	return tex;
}
inline shared_ptr<Texture> LoadTexture(CommandBuffer& commandBuffer, const fs::path& filename, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	return LoadTexture(commandBuffer, filename.stem().string(), Texture::LoadPixels(filename), usage);
}
inline shared_ptr<Mesh> LoadScene(CommandBuffer& commandBuffer, const fs::path& filename) {
	Device& device = commandBuffer.mDevice;
	shared_ptr<Mesh> mesh = make_shared<Mesh>(filename.stem().string());

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(filename.string().c_str(), 
		aiPostProcessSteps::aiProcess_CalcTangentSpace |
		aiPostProcessSteps::aiProcess_JoinIdenticalVertices |
		aiPostProcessSteps::aiProcess_MakeLeftHanded |
		aiPostProcessSteps::aiProcess_Triangulate |
		aiPostProcessSteps::aiProcess_SortByPType);

	vector<float3> vertices;
	vector<float3> normals;
	vector<float3> tangents;
	vector<float3> texcoords;

	vector<uint32_t> indices;
	for (const aiMesh* m : span(scene->mMeshes, scene->mNumMeshes)) {
		size_t vertsPerFace;
		if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_POINT) vertsPerFace = 1;
		else if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_LINE) vertsPerFace = 2;
		else if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_TRIANGLE) vertsPerFace = 3;
		else continue;

		mesh->Submeshes().emplace_back(m->mNumFaces, (uint32_t)indices.size(), (uint32_t)vertices.size());

		size_t tmp = vertices.size();
		vertices.resize(tmp + m->mNumVertices);
		normals.resize(tmp + m->mNumVertices);
		tangents.resize(tmp + m->mNumVertices);
		texcoords.resize(tmp + m->mNumVertices);
		ranges::copy(span((float3*)m->mVertices, m->mNumVertices), vertices.begin() + tmp);
		ranges::copy(span((float3*)m->mNormals, m->mNumVertices), normals.begin() + tmp);
		ranges::copy(span((float3*)m->mTangents, m->mNumVertices), tangents.begin() + tmp);
		ranges::copy(span((float3*)m->mTextureCoords[0], m->mNumVertices), texcoords.begin() + tmp);

		tmp = indices.size();
		indices.resize(indices.size() + m->mNumFaces*vertsPerFace);
		auto it = indices.begin() + tmp;
		for (const aiFace& f : span(m->mFaces, m->mNumFaces)) {
			ranges::copy(f.mIndices, f.mIndices + f.mNumIndices, it);
			it += f.mNumIndices;
		}
	}

	mesh->Indices() = commandBuffer.CopyToDevice("Indices", indices, vk::BufferUsageFlagBits::eIndexBuffer);

	mesh->Geometry().mBindings.resize(4);
	mesh->Geometry().mBindings[0] = { commandBuffer.CopyToDevice("Vertices", vertices, vk::BufferUsageFlagBits::eVertexBuffer), vk::VertexInputRate::eVertex };
	mesh->Geometry().mBindings[1] = { commandBuffer.CopyToDevice("Normals", normals, vk::BufferUsageFlagBits::eVertexBuffer), vk::VertexInputRate::eVertex };
	mesh->Geometry().mBindings[2] = { commandBuffer.CopyToDevice("Tangents", tangents, vk::BufferUsageFlagBits::eVertexBuffer), vk::VertexInputRate::eVertex };
	mesh->Geometry().mBindings[3] = { commandBuffer.CopyToDevice("Texcoords", texcoords, vk::BufferUsageFlagBits::eVertexBuffer), vk::VertexInputRate::eVertex };
	mesh->Geometry()[VertexAttributeType::ePosition][0] = GeometryData::Attribute(0, vk::Format::eR32G32B32Sfloat, 0);
	mesh->Geometry()[VertexAttributeType::eNormal][0] = GeometryData::Attribute(1, vk::Format::eR32G32B32Sfloat, 0);
	mesh->Geometry()[VertexAttributeType::eTangent][0] = GeometryData::Attribute(2, vk::Format::eR32G32B32Sfloat, 0);
	mesh->Geometry()[VertexAttributeType::eTexcoord][0] = GeometryData::Attribute(3, vk::Format::eR32G32B32Sfloat, 0);
	return mesh;
}

inline shared_ptr<Texture> CombineChannels(CommandBuffer& commandBuffer, const string& name, const Texture::PixelData& img0, const Texture::PixelData& img1, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	const auto&[pixels0,extent0,format0] = img0;
	const auto&[pixels1,extent1,format1] = img1;
	
	if (extent0 != extent1) throw invalid_argument("Image extents must match");

	vk::Format dstFormat = vk::Format::eR8G8Unorm;

	auto pipeline = make_shared<ComputePipeline>(commandBuffer.mDevice, "interleave", gSpirvModules.at("interleave"));
	auto staging0 = commandBuffer.CreateStagingBuffer(pixels0, vk::BufferUsageFlagBits::eUniformTexelBuffer);
	auto staging1 = commandBuffer.CreateStagingBuffer(pixels1, vk::BufferUsageFlagBits::eUniformTexelBuffer);
	auto combined = Buffer::RangeView(make_shared<Buffer>(commandBuffer.mDevice, name, staging0.size()+staging1.size(), vk::BufferUsageFlagBits::eStorageTexelBuffer|vk::BufferUsageFlagBits::eTransferSrc),staging0.stride()+staging1.stride());
	auto dst = make_shared<Texture>(commandBuffer.mDevice, name, extent0, dstFormat, 1, 0, vk::SampleCountFlagBits::e1, usage | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);

	commandBuffer.HoldResource(combined.get());
	commandBuffer.HoldResource(dst);
	
	commandBuffer.BindPipeline(pipeline);
	commandBuffer.BindDescriptorSet(0, make_shared<DescriptorSet>(commandBuffer.BoundPipeline()->DescriptorSetLayouts()[0], "tmp", unordered_map<uint32_t, DescriptorSet::Entry> {
		{ pipeline->binding("gOutputRG").mBinding, TexelBufferView(combined, dstFormat) },
		{ pipeline->binding("gInputR").mBinding, TexelBufferView(staging0, format0) },
		{ pipeline->binding("gInputG").mBinding, TexelBufferView(staging1, format1) } }));
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
		gSpirvModules = LoadShaders(instance->Device(), "Assets/core_shaders.spvm");

		// Create render passes

		vk::PipelineColorBlendAttachmentState blendOpaque;
		blendOpaque.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		
		auto shadowAtlasAttachment = vk::AttachmentDescription({}, vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
		RenderPass::SubpassDescription render_shadows { "render_shadows", vk::PipelineBindPoint::eGraphics,
			{ // attachments
				{ "shadow_atlas", { shadowAtlasAttachment, RenderPass::AttachmentType::eDepthStencil, blendOpaque } }
			}
		};
		shared_ptr<RenderPass> shadowPass = make_shared<RenderPass>(instance->Device(), "shadow", vector<RenderPass::SubpassDescription>{ render_shadows });

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
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal);
		RenderPass::SubpassDescription main_render { "main_render", vk::PipelineBindPoint::eGraphics,
			{ // attachments
				{ "primary_color", { colorAttachment, RenderPass::AttachmentType::eColor, blendOpaque } },
				{ "primary_resolve", { resolveAttachment, RenderPass::AttachmentType::eResolve, blendOpaque } },
				{ "primary_depth", { depthAttachment, RenderPass::AttachmentType::eDepthStencil, blendOpaque } },
			}
		};
		shared_ptr<RenderPass> mainPass = make_shared<RenderPass>(instance->Device(), "main", vector<RenderPass::SubpassDescription>{ main_render });


		// Create resources

		shared_ptr<Texture> gBaseColorTexture, gNormalTexture, gMetallicRoughnessTexture;
		shared_ptr<Mesh> testMesh;
		{
			auto commandBuffer = instance->Device().GetCommandBuffer("Init");
			gBaseColorTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/copper/Metal08_col.jpg");
			gNormalTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/copper/Metal08_nrm.jpg");
			gMetallicRoughnessTexture = CombineChannels(*commandBuffer, "Metal08_met_rgh",
				Texture::LoadPixels("E:/3d/blender/Textures/copper/Metal08_met.jpg"),
				Texture::LoadPixels("E:/3d/blender/Textures/copper/Metal08_rgh.jpg"));
			testMesh = LoadScene(*commandBuffer, "Assets/Models/suzanne.obj");
			instance->Device().Execute(commandBuffer);
		}
		unordered_map<string,shared_ptr<Sampler>> immutableSamplers;
		immutableSamplers["gSampler"] = make_shared<Sampler>(instance->Device(), "gSampler", vk::SamplerCreateInfo({},
			vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
			0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));
		immutableSamplers["gShadowSampler"] = make_shared<Sampler>(instance->Device(), "gShadowSampler", vk::SamplerCreateInfo({},
			vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder,
			0, true, 2, true, vk::CompareOp::eLessOrEqual, 0, VK_LOD_CLAMP_NONE, vk::BorderColor::eFloatOpaqueWhite));

		auto pbr_shadowpass = make_shared<GraphicsPipeline>("pbr_depth", *shadowPass, 0, testMesh->Geometry(), gSpirvModules.at("vs_pbr"), nullptr, Pipeline::SpecializationMap(), immutableSamplers);
		auto pbr = make_shared<GraphicsPipeline>("pbr", *mainPass, 0, testMesh->Geometry(), gSpirvModules.at("vs_pbr"), gSpirvModules.at("fs_pbr"), Pipeline::SpecializationMap(), immutableSamplers);
		auto axis = make_shared<GraphicsPipeline>("axis", *mainPass, 0, GeometryData(vk::PrimitiveTopology::eLineList), gSpirvModules.at("vs_axis"), gSpirvModules.at("fs_color"));
		
		auto gShadowAtlas = make_shared<Texture>(instance->Device(), "shadow_atlas", vk::Extent3D(2048,2048,1), shadowAtlasAttachment, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);
		auto shadowFramebuffer = make_shared<Framebuffer>("shadow_framebuffer", *shadowPass, vk::ArrayProxy<const TextureView> { TextureView(gShadowAtlas) });

		TransformData gCameraToWorld = make_transform(Vector3f(0,0,-4));
		vector<TransformData> gInstanceTransforms { TRANSFORM_I };
		vector<LightData> gLights(1);
		gLights[0].Flags = LIGHT_ATTEN_DIRECTIONAL | LIGHT_USE_SHADOWMAP;
		gLights[0].LightToWorld = make_transform(float3(1,1,-1), Quaternionf::FromTwoVectors(Vector3f(0,0,1), Vector3f(-1,-1,1)));
		gLights[0].ShadowProjection = make_orthographic(3, 3, -512, 128);
		gLights[0].ShadowBias = .00001f;
		gLights[0].Emission = Vector3f::Constant(3);
		gLights[0].ShadowST = Vector4f(1,1,0,0);
		MaterialData gMaterial;
		gMaterial.gTextureST = Vector4f(1,1,0,0);
		gMaterial.gBaseColor = Vector4f::Ones();
		gMaterial.gEmission = Vector3f::Zero();
		gMaterial.gBumpStrength = 1;
		gMaterial.gMetallic = 0;
		gMaterial.gRoughness = 1;
		
		shared_ptr<Framebuffer> framebuffer;
		auto t0 = chrono::high_resolution_clock::now();
		auto frameTimeAccum = chrono::nanoseconds::zero();
		uint32_t fpsAccum = 0;
		size_t frameCount = 0;
		while (true) {
			Profiler::BeginSample("Frame" + to_string(frameCount));

			auto commandBuffer = instance->Device().GetCommandBuffer("Frame");
			shared_ptr<Semaphore> renderSemaphore = make_shared<Semaphore>(instance->Device(), "RenderSemaphore");

			{
				ProfilerRegion ps("PollEvents + AcquireNextImage");
				if (!instance->PollEvents()) break; // Window was closed
				window.AcquireNextImage(*commandBuffer);
			}

			// update

			auto t1 = chrono::high_resolution_clock::now();
			float deltaTime = chrono::duration_cast<chrono::duration<float>>(t1 - t0).count();
			t0 = t1;
			
			static float fovy = 60;
			static Vector2f euler = Vector2f::Zero();
			fovy = clamp(fovy - window.ScrollDelta(), 20.f, 90.f);

			if (window.KeyDown(MOUSE_RIGHT)) {
				euler += window.CursorDelta().reverse() * .005f;
				euler.x() = clamp(euler.x(), -(float)M_PI/2, (float)M_PI/2);
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
			

			// render

			if (window.Swapchain() && (!framebuffer || window.SwapchainExtent() != framebuffer->Extent())) {
				auto primaryColor = make_shared<Texture>(instance->Device(), "primary_color", vk::Extent3D(window.SwapchainExtent(),1), colorAttachment, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);
				auto primaryDepth = make_shared<Texture>(instance->Device(), "primary_depth", vk::Extent3D(window.SwapchainExtent(),1), depthAttachment, vk::ImageUsageFlagBits::eDepthStencilAttachment);
				auto primaryResolve = make_shared<Texture>(instance->Device(), "primary_resolve", vk::Extent3D(window.SwapchainExtent(),1), resolveAttachment, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
				framebuffer = make_shared<Framebuffer>("framebuffer", *mainPass, vk::ArrayProxy<const TextureView> { TextureView(primaryColor), TextureView(primaryResolve), TextureView(primaryDepth) });
			}
			
			if (framebuffer) {
				auto perCamera = make_shared<DescriptorSet>(pbr->DescriptorSetLayouts()[0], "PerCamera");
				perCamera->insert(pbr->binding("gLights").mBinding, commandBuffer->CopyToDevice("gLights", gLights, vk::BufferUsageFlagBits::eStorageBuffer));
				perCamera->insert(pbr->binding("gShadowAtlas").mBinding, gShadowAtlas, vk::ImageLayout::eShaderReadOnlyOptimal);
				auto perMaterial = make_shared<DescriptorSet>(pbr->DescriptorSetLayouts()[1], "PerMaterial");
				perMaterial->insert(pbr->binding("gMaterial").mBinding, commandBuffer->CopyToDevice("gMaterial", span(&gMaterial,1), vk::BufferUsageFlagBits::eUniformBuffer));
				perMaterial->insert(pbr->binding("gBaseColorTexture").mBinding, TextureView(gBaseColorTexture), vk::ImageLayout::eShaderReadOnlyOptimal);
				perMaterial->insert(pbr->binding("gNormalTexture").mBinding, TextureView(gNormalTexture), vk::ImageLayout::eShaderReadOnlyOptimal);
				perMaterial->insert(pbr->binding("gMetallicRoughnessTexture").mBinding, TextureView(gMetallicRoughnessTexture), vk::ImageLayout::eShaderReadOnlyOptimal);
				auto perObject = make_shared<DescriptorSet>(pbr->DescriptorSetLayouts()[2], "PerObject");
				perObject->insert(pbr->binding("gInstanceTransforms").mBinding, commandBuffer->CopyToDevice("gInstanceTransforms", gInstanceTransforms, vk::BufferUsageFlagBits::eStorageBuffer));

				commandBuffer->TransitionImageDescriptors(*perCamera, *perMaterial, *perObject);

				// render shadows
				
				commandBuffer->BeginRenderPass(shadowPass, shadowFramebuffer, vector<vk::ClearValue>{ vk::ClearDepthStencilValue{1.f,0} });
				(*commandBuffer)->setViewport(0, { vk::Viewport(0, (float)gShadowAtlas->Extent().height, (float)gShadowAtlas->Extent().width, -(float)gShadowAtlas->Extent().height, 0, 1) });
				(*commandBuffer)->setScissor(0, { vk::Rect2D(vk::Offset2D(0,0), vk::Extent2D(gShadowAtlas->Extent().width, gShadowAtlas->Extent().height)) });
				commandBuffer->BindPipeline(pbr_shadowpass);
				commandBuffer->BindDescriptorSet(2, perObject);
				commandBuffer->PushConstant("WorldToCamera", inverse(gLights[0].LightToWorld));
				commandBuffer->PushConstant("Projection", gLights[0].ShadowProjection);
				testMesh->Draw(*commandBuffer);
				commandBuffer->EndRenderPass();

				// render image

				ProjectionData projection = make_perspective(radians(fovy), (float)window.SwapchainExtent().height/(float)window.SwapchainExtent().width, 0, 64);

				commandBuffer->BeginRenderPass(mainPass, framebuffer, vector<vk::ClearValue>{ vk::ClearColorValue(std::array<float,4>{0.2f, 0.2f, 0.2f, 1}), vk::ClearColorValue(std::array<float,4>{0,0,0,0}), vk::ClearDepthStencilValue{1.f,0} });
				(*commandBuffer)->setViewport(0, { vk::Viewport(0, (float)framebuffer->Extent().height, (float)framebuffer->Extent().width, -(float)framebuffer->Extent().height, 0, 1) });
				(*commandBuffer)->setScissor(0, { vk::Rect2D(vk::Offset2D(0,0), framebuffer->Extent()) });

				commandBuffer->BindPipeline(pbr);
				commandBuffer->BindDescriptorSets(0, vector { perCamera, perMaterial, perObject });
				commandBuffer->PushConstant("WorldToCamera", inverse(gCameraToWorld));
				commandBuffer->PushConstant("Projection", projection);
				commandBuffer->PushConstant("LightCount", (uint32_t)gLights.size());
				testMesh->Draw(*commandBuffer);
				
				commandBuffer->BindPipeline(axis);
				(*commandBuffer)->setLineWidth(1);
				commandBuffer->PushConstant("WorldToCamera", inverse(gCameraToWorld));
				commandBuffer->PushConstant("Projection", projection);
				(*commandBuffer)->draw(2, 3, 0, 0);

				commandBuffer->EndRenderPass();
				
				// copy to window
				commandBuffer->TransitionBarrier(window.BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal);
				
				vk::ImageCopy rgn = {};
				rgn.dstSubresource = rgn.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
				rgn.extent = vk::Extent3D(window.SwapchainExtent(), 1);
				framebuffer->at("primary_resolve").texture().TransitionBarrier(*commandBuffer, vk::ImageLayout::eTransferSrcOptimal);
				(*commandBuffer)->copyImage(*framebuffer->at("primary_resolve").texture(), vk::ImageLayout::eTransferSrcOptimal, window.BackBuffer(), vk::ImageLayout::eTransferDstOptimal, { rgn });

				commandBuffer->TransitionBarrier(window.BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR);
				commandBuffer->WaitOn(vk::PipelineStageFlagBits::eTransfer, window.ImageAvailableSemaphore());
				commandBuffer->SignalOnComplete(vk::PipelineStageFlagBits::eTransfer, renderSemaphore);
			}

			instance->Device().Execute(commandBuffer);
			
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
			frameCount++;
		}
		
		instance->Device().Flush();
		gSpirvModules.clear();
	}
	instance.reset();
	Profiler::ClearHistory();
	return EXIT_SUCCESS;
}