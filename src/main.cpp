#include "Core/Window.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/Mesh.hpp"
#include "Core/Profiler.hpp"
#include "Scene/GuiContext.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace stm;
using namespace stm::shader_interop;

unordered_map<std::string, std::shared_ptr<stm::SpirvModule>> gSpirvModules;

inline unordered_map<string, shared_ptr<SpirvModule>> LoadShaders(Device& device, const fs::path& spvm) {
	unordered_map<string, shared_ptr<SpirvModule>> spirvModules;
	unordered_map<string, SpirvModule> tmp;
	byte_stream<ifstream>(spvm, ios::binary) >> tmp;
	ranges::for_each(tmp | views::values, [&](const auto& m){ spirvModules.emplace(m.mEntryPoint, make_shared<SpirvModule>(m)); });
	return spirvModules;
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

inline shared_ptr<Texture> LoadTexture(CommandBuffer& commandBuffer, const string& name, const Texture::PixelData& pixelData, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	const auto&[pixels,extent,format] = pixelData;
	shared_ptr<Texture> tex = make_shared<Texture>(commandBuffer.mDevice, name, extent, format, 1, 0, vk::SampleCountFlagBits::e1, usage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
	tex->TransitionBarrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	auto staging = commandBuffer.CreateStagingBuffer(pixels);
	commandBuffer->copyBufferToImage(*staging.buffer(), **tex, vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy(staging.offset(), 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, tex->ArrayLayers()), {}, tex->Extent()) });
	tex->GenerateMipMaps(commandBuffer);
	return tex;
}
inline shared_ptr<Texture> LoadTexture(CommandBuffer& commandBuffer, const fs::path& filename, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	return LoadTexture(commandBuffer, filename.stem().string(), Texture::LoadPixels(filename), usage);
}

inline shared_ptr<Texture> CombineChannels(CommandBuffer& commandBuffer, const string& name, const Texture::PixelData& img0, const Texture::PixelData& img1, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	const auto&[pixels0,extent0,format0] = img0;
	const auto&[pixels1,extent1,format1] = img1;
	
	if (extent0 != extent1) throw invalid_argument("Image extents must match");

	vk::Format dstFormat = vk::Format::eR8G8Unorm;

	auto pipeline = make_shared<ComputePipeline>(commandBuffer.mDevice, "combine", gSpirvModules.at("combine"));
	auto staging0 = commandBuffer.CreateStagingBuffer(pixels0, vk::BufferUsageFlagBits::eUniformTexelBuffer);
	auto staging1 = commandBuffer.CreateStagingBuffer(pixels1, vk::BufferUsageFlagBits::eUniformTexelBuffer);
	auto combined = Buffer::RangeView(make_shared<Buffer>(commandBuffer.mDevice, name, staging0.size()+staging1.size(), vk::BufferUsageFlagBits::eStorageTexelBuffer|vk::BufferUsageFlagBits::eTransferSrc),staging0.stride()+staging1.stride());
	auto dst      = make_shared<Texture>(commandBuffer.mDevice, name, extent0, dstFormat, 1, 0, vk::SampleCountFlagBits::e1, usage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);

	commandBuffer.HoldResource(combined.get());
	commandBuffer.HoldResource(dst);
	
	commandBuffer.BindPipeline(pipeline);
	commandBuffer.BindDescriptorSet(make_shared<DescriptorSet>(commandBuffer.BoundPipeline()->DescriptorSetLayouts()[0], "tmp", unordered_map<uint32_t, DescriptorSet::Entry> {
		{ pipeline->binding("gOutput").mBinding, TexelBufferView(combined, dstFormat) },
		{ pipeline->binding("gInput0").mBinding, TexelBufferView(staging0, format0) },
		{ pipeline->binding("gInput1").mBinding, TexelBufferView(staging1, format1) } }), 0);
	commandBuffer.PushConstantRef<uint32_t>("Width", extent0.width);
	commandBuffer.DispatchTiled(extent0);

	commandBuffer.Barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead, combined);
	dst->TransitionBarrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	commandBuffer->copyBufferToImage(*combined.buffer(), **dst, vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy(combined.offset(), 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), {}, extent0) });
	dst->GenerateMipMaps(commandBuffer);

	return dst;
}

inline Vector3f ProjectPointToRay(Matrix4f view, Matrix4f projection, Vector2f p) {
	Vector4f ray = projection.inverse() * p.homogeneous().homogeneous();
	return (view.inverse() * Vector4f(ray[0], -ray[1], 1, 0)).head<3>().normalized();
}


int main(int argc, char** argv) {
	unique_ptr<Instance> instance = make_unique<Instance>(argc, argv);

	{
		Window& window = instance->Window();
		gSpirvModules = LoadShaders(instance->Device(), "Assets/core_shaders.spvm");

		unordered_map<string,shared_ptr<Sampler>> immutableSamplers;
		immutableSamplers["gSampler"] = make_shared<Sampler>(instance->Device(), "Sampler", vk::SamplerCreateInfo({},
			vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
			0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));
		immutableSamplers["gShadowSampler"] = make_shared<Sampler>(instance->Device(), "gShadowSampler", vk::SamplerCreateInfo({},
			vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder,
			0, true, 2, true, vk::CompareOp::eLess, 0, VK_LOD_CLAMP_NONE, vk::BorderColor::eFloatOpaqueWhite));

		shared_ptr<Texture> gBaseColorTexture, gNormalTexture, gMetallicRoughnessTexture;
		shared_ptr<Texture> gShadowAtlas = make_shared<Texture>(instance->Device(), "ShadowAtlas", vk::Extent3D(1024,1024,1), vk::Format::eR32Sfloat);
		shared_ptr<Mesh> testMesh;
		
		{
			auto commandBuffer = instance->Device().GetCommandBuffer("Init");
			
			testMesh = LoadScene(*commandBuffer, "Assets/Models/suzanne.obj");
			gBaseColorTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/PavingStones33/PavingStones33_col.jpg");
			gNormalTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/PavingStones33/PavingStones33_nrm.jpg");
			gMetallicRoughnessTexture = CombineChannels(*commandBuffer, "PavingStones33_met_rgh",
				Texture::PixelData(byte_blob(gBaseColorTexture->Extent().width*gBaseColorTexture->Extent().height), gBaseColorTexture->Extent(), vk::Format::eR8Unorm),
				Texture::LoadPixels("E:/3d/blender/Textures/PavingStones33/PavingStones33_rgh.jpg"));

			instance->Device().Execute(commandBuffer);
		}

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
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal);
		vk::PipelineColorBlendAttachmentState blendOpaque;
		blendOpaque.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		RenderPass::SubpassDescription shadows {
			"shadows", vk::PipelineBindPoint::eGraphics, {
				{ "shadow_atlas", { shadowAtlasAttachment, RenderPass::AttachmentType::eDepthStencil, blendOpaque } } }, {} };
		RenderPass::SubpassDescription main_render {
			"main_render", vk::PipelineBindPoint::eGraphics, {
				{ "primary_color", { colorAttachment, RenderPass::AttachmentType::eColor, blendOpaque } },
				{ "primary_depth", { depthAttachment, RenderPass::AttachmentType::eDepthStencil, blendOpaque } },
				{ "primary_resolve", { resolveAttachment, RenderPass::AttachmentType::eResolve, blendOpaque } } },
				{ "shadows"} };
		shared_ptr<RenderPass> renderPass = make_shared<RenderPass>(instance->Device(), "main", vk::ArrayProxy<const RenderPass::SubpassDescription>{ shadows, main_render });

		auto pbr_depth = make_shared<GraphicsPipeline>("pbr", *renderPass, 0, testMesh->Geometry(),
			gSpirvModules.at("vs_pbr"), gSpirvModules.at("fs_pbr"), Pipeline::SpecializationMap{ { "gDepth", byte_blob(vk::Bool32(true)) } }, immutableSamplers);
		auto pbr = make_shared<GraphicsPipeline>("pbr", *renderPass, 1, testMesh->Geometry(),
			gSpirvModules.at("vs_pbr"), gSpirvModules.at("fs_pbr"), Pipeline::SpecializationMap(), immutableSamplers);
		
		CameraData gCamera;
		MaterialData gMaterial;
		vector<LightData> gLights(1);
		vector<InstanceData> gInstances(1);

		Affine3f lightToWorld(AngleAxisf((float)M_PI/3, Vector3f(1,-.2f,-.1f).normalized()));
		gLights[0].Flags = LIGHT_ATTEN_DIRECTIONAL;
		gLights[0].ToLight = (Orthographic<float>(32,32,-512,512) * lightToWorld.inverse()).matrix();
		gLights[0].Position = -(lightToWorld * Vector3f(0,0,1)).normalized();
		gLights[0].Emission = Vector3f::Constant(3);
		gInstances[0].Transform = Matrix4f::Identity();
		gInstances[0].InverseTransform = gInstances[0].Transform.inverse();
		gCamera.Position = Vector3f(0,0,-4);
		gCamera.View = Matrix4f::Identity();
		gCamera.LightCount = (uint32_t)gLights.size();
		
		gMaterial.gTextureST = Vector4f(1,1,0,0);
		gMaterial.gBaseColor = Vector4f::Ones();
		gMaterial.gEmission = Vector3f::Zero();
		gMaterial.gBumpStrength = 1.f;
		gMaterial.gMetallic = 0.5f;
		gMaterial.gRoughness = 1.f;
		gMaterial.gAlphaCutoff = 0.5f;

		shared_ptr<Framebuffer> framebuffer;
		
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
			
			if (window.MouseState().mKeys.count(MOUSE_LEFT)) {
				Vector3f r0 = ProjectPointToRay(gCamera.View, gCamera.Projection, window.WindowToClip(window.LastCursorPos()));
				Vector3f r1 = ProjectPointToRay(gCamera.View, gCamera.Projection, window.WindowToClip(window.CursorPos()));
				float angle = acos(r0.dot(r1)/(r0.norm()*r1.norm()));
				if (angle > 1e-6f) {
					gInstances[0].Transform *= Affine3f(AngleAxisf(angle, r0.cross(r1).normalized())).matrix();
					gInstances[0].InverseTransform = gInstances[0].Transform.inverse();
				}
			}

			if (window.Swapchain() && (!framebuffer || window.SwapchainExtent() != framebuffer->Extent())) {
				auto primaryColor = make_shared<Texture>(instance->Device(), "primary_color", vk::Extent3D(window.SwapchainExtent(),1), colorAttachment, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);
				auto primaryDepth = make_shared<Texture>(instance->Device(), "primary_depth", vk::Extent3D(window.SwapchainExtent(),1), depthAttachment, vk::ImageUsageFlagBits::eDepthStencilAttachment);
				auto primaryResolve = make_shared<Texture>(instance->Device(), "primary_resolve", vk::Extent3D(window.SwapchainExtent(),1), resolveAttachment, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
				framebuffer = make_shared<Framebuffer>("framebuffer", *renderPass, vector<TextureView> { TextureView(primaryColor), TextureView(primaryDepth), TextureView(primaryResolve) });
				gCamera.Projection = PerspectiveFov(radians(42.18f), (float)framebuffer->Extent().width/(float)framebuffer->Extent().height, .01f, 128.f).matrix();
				gCamera.InvProjection = gCamera.Projection.inverse();
				gCamera.ViewProjection = gCamera.Projection * gCamera.View;
			}
			
			if (framebuffer) {
				// render
				auto perCamera = make_shared<DescriptorSet>(pbr->DescriptorSetLayouts()[0], "PerCamera");
				auto perMaterial = make_shared<DescriptorSet>(pbr->DescriptorSetLayouts()[1], "PerMaterial");
				auto perObject = make_shared<DescriptorSet>(pbr->DescriptorSetLayouts()[2], "PerObject");
				perCamera->insert(pbr->binding("gCamera").mBinding, commandBuffer->CopyToDevice("gCamera", span(&gCamera,1), vk::BufferUsageFlagBits::eUniformBuffer));
				perCamera->insert(pbr->binding("gLights").mBinding, commandBuffer->CopyToDevice("gLights", gLights, vk::BufferUsageFlagBits::eStorageBuffer));
				perCamera->insert(pbr->binding("gShadowAtlas").mBinding, gShadowAtlas, vk::ImageLayout::eShaderReadOnlyOptimal);
				perMaterial->insert(pbr->binding("gMaterial").mBinding, commandBuffer->CopyToDevice("gMaterial", span(&gMaterial,1), vk::BufferUsageFlagBits::eUniformBuffer));
				perMaterial->insert(pbr->binding("gBaseColorTexture").mBinding, TextureView(gBaseColorTexture), vk::ImageLayout::eShaderReadOnlyOptimal);
				perMaterial->insert(pbr->binding("gNormalTexture").mBinding, TextureView(gNormalTexture), vk::ImageLayout::eShaderReadOnlyOptimal);
				perMaterial->insert(pbr->binding("gMetallicRoughnessTexture").mBinding, TextureView(gMetallicRoughnessTexture), vk::ImageLayout::eShaderReadOnlyOptimal);
				perObject->insert(pbr->binding("gInstances").mBinding, commandBuffer->CopyToDevice("gInstances", gInstances, vk::BufferUsageFlagBits::eStorageBuffer));

				commandBuffer->TransitionImageDescriptors(*perCamera, *perMaterial, *perObject);
				
				commandBuffer->BeginRenderPass(renderPass, framebuffer, vector<vk::ClearValue>{ vk::ClearColorValue(std::array<float,4>{0.25f, 0.3f, 0.9f, 1}), vk::ClearDepthStencilValue{1.f,0}, vk::ClearColorValue(std::array<float,4>{0,0,0,0}) });
				
				commandBuffer->BindPipeline(pbr_depth);
				(*commandBuffer)->setViewport(0, { vk::Viewport(0, (float)gShadowAtlas->Extent().height, (float)gShadowAtlas->Extent().width, -(float)gShadowAtlas->Extent().height, 0, 1) });
				(*commandBuffer)->setScissor(0, { vk::Rect2D(vk::Offset2D(0,0), vk::Extent2D(gShadowAtlas->Extent().width, gShadowAtlas->Extent().height)) });
				(*commandBuffer)->setLineWidth(1);

				commandBuffer->NextSubpass();
				
				commandBuffer->BindPipeline(pbr);
				(*commandBuffer)->setViewport(0, { vk::Viewport(0, (float)framebuffer->Extent().height, (float)framebuffer->Extent().width, -(float)framebuffer->Extent().height, 0, 1) });
				(*commandBuffer)->setScissor(0, { vk::Rect2D(vk::Offset2D(0,0), framebuffer->Extent()) });
				(*commandBuffer)->setLineWidth(1);
				commandBuffer->BindDescriptorSets({ perCamera, perMaterial, perObject }, 0);
				testMesh->Draw(*commandBuffer);
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