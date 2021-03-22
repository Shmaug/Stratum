#include "Core/Window.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/Mesh.hpp"
#include "Core/Profiler.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace stm;
using namespace stm::shader_interop;

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

	struct vertex_t {
		float3 position;
		float3 normal;
		float3 tangent;
		float3 texcoord;
	};
	vector<vertex_t> vertices;
	vector<uint32_t> indices;
	for (const aiMesh* m : span(scene->mMeshes, scene->mNumMeshes)) {
		size_t vertsPerFace;
		//if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_POINT) vertsPerFace = 1;
		//else if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_LINE) vertsPerFace = 2;
		//else
		if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_TRIANGLE) vertsPerFace = 3;
		else continue;

		mesh->Submeshes().emplace_back(m->mNumFaces, (uint32_t)indices.size(), (uint32_t)vertices.size());

		size_t tmp = vertices.size();
		vertices.resize(vertices.size() + m->mNumVertices);
		for (uint32_t i = 0; i < m->mNumVertices; i++) {
			vertex_t& v = vertices[tmp + i];
			memcpy(&v.position, m->mVertices + i, sizeof(v.position));
			memcpy(&v.normal, m->mNormals + i, sizeof(v.normal));
			memcpy(&v.tangent, m->mTangents + i, sizeof(v.tangent));
			memcpy(&v.texcoord, m->mTextureCoords[0] + i, sizeof(v.texcoord));
		}

		tmp = indices.size();
		indices.resize(indices.size() + m->mNumFaces*vertsPerFace);
		auto it = indices.begin() + tmp;
		for (const aiFace& f : span(m->mFaces, m->mNumFaces)) {
			ranges::copy(f.mIndices, f.mIndices + f.mNumIndices, it);
			it += f.mNumIndices;
		}
	}

	mesh->Indices() = commandBuffer.CopyToDevice("Indices", indices, vk::BufferUsageFlagBits::eIndexBuffer);
	mesh->Geometry().mBindings.emplace_back(commandBuffer.CopyToDevice("Vertices", vertices, vk::BufferUsageFlagBits::eVertexBuffer), vk::VertexInputRate::eVertex);
	mesh->Geometry()[VertexAttributeType::ePosition][0] = GeometryData::Attribute(0, vk::Format::eR32G32B32Sfloat, offsetof(vertex_t, position));
	mesh->Geometry()[VertexAttributeType::eNormal][0] = GeometryData::Attribute(0, vk::Format::eR32G32B32Sfloat, offsetof(vertex_t, normal));
	mesh->Geometry()[VertexAttributeType::eTangent][0] = GeometryData::Attribute(0, vk::Format::eR32G32B32Sfloat, offsetof(vertex_t, tangent));
	mesh->Geometry()[VertexAttributeType::eTexcoord][0] = GeometryData::Attribute(0, vk::Format::eR32G32B32Sfloat, offsetof(vertex_t, texcoord));
	return mesh;
}

inline shared_ptr<Texture> LoadTexture(CommandBuffer& commandBuffer, const fs::path& filename, vk::ImageUsageFlagBits usage = vk::ImageUsageFlagBits::eSampled) {
	auto[pixels,extent,format] = Texture::LoadPixels(filename);
	shared_ptr<Texture> tex = make_shared<Texture>(commandBuffer.mDevice, filename.stem().string(), extent, format, vk::SampleCountFlagBits::e1, usage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
	tex->TransitionBarrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	auto staging = commandBuffer.CreateStagingBuffer(pixels);
	commandBuffer->copyBufferToImage(*staging.buffer(), **tex, vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy(staging.offset(), 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, tex->ArrayLayers()), {}, tex->Extent()) });
	tex->GenerateMipMaps(commandBuffer);
	return tex;
}

int main(int argc, char** argv) {
	unique_ptr<Instance> instance = make_unique<Instance>(argc, argv);

	{
		Window& window = instance->Window();

		shared_ptr<Texture> gEnvironmentTexture, gBaseColorTexture, gNormalTexture, gMetallicRoughnessTexture;
		//shared_ptr<Texture> gShadowAtlas = make_shared<Texture>(instance->Device(), "ShadowAtlas", vk::Extent3D(1024,1024,1), vk::Format::eR32Sfloat);
		shared_ptr<Mesh> testMesh;
		
		{
			auto commandBuffer = instance->Device().GetCommandBuffer("Init");
			
			testMesh = LoadScene(*commandBuffer, "Assets/Models/suzanne.obj");
			//gEnvironmentTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/_backgrounds/path.hdr");
			gBaseColorTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/copper/Metal08_col.jpg");
			gNormalTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/copper/Metal08_nrm.jpg");
			//shared_ptr<Texture> gMetallicTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/copper/Metal08_met.jpg");
			//shared_ptr<Texture> gRoughnessTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/copper/Metal08_rgh.jpg");
			//gMetallicRoughnessTexture = make_shared<Texture>(instance->Device(), "Metal08_met_rgh", gBaseColorTexture->Extent(), vk::Format::eR8G8Unorm);
			
			instance->Device().Execute(commandBuffer);
		}

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
		RenderPass::SubpassDescription subpass {
			"main_render", vk::PipelineBindPoint::eGraphics, {
				{ "primary_color", { colorAttachment, RenderPass::AttachmentType::eColor, blendOpaque } },
				{ "primary_depth", { depthAttachment, RenderPass::AttachmentType::eDepthStencil, blendOpaque } },
				{ "primary_resolve", { resolveAttachment, RenderPass::AttachmentType::eResolve, blendOpaque } } }, {} };
		shared_ptr<RenderPass> renderPass = make_shared<RenderPass>(instance->Device(), "main", ranges::single_view(subpass));

		auto spirvModules = LoadShaders(instance->Device(), "Assets/core_shaders.spvm");
		//spirvModules.at("fs_pbr")->mDescriptorBindings.at("gSampler").mImmutableSamplers = {  };
		//spirvModules.at("fs_pbr")->mDescriptorBindings.at("gShadowSampler").mImmutableSamplers = { vk::SamplerCreateInfo({},
		//	vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		//	vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder,
		//	0, true, 4, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE, vk::BorderColor::eFloatOpaqueWhite ) };

		auto pbr = make_shared<GraphicsPipeline>("pbr", *renderPass, 0, testMesh->Geometry(), spirvModules.at("vs_pbr"), spirvModules.at("fs_pbr"));
		
		shared_ptr<Framebuffer> framebuffer;
		
		auto gSampler = make_shared<Sampler>(instance->Device(), "Sampler", vk::SamplerCreateInfo({},
			vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
			0, true, 4, true, vk::CompareOp::eLess, 0, VK_LOD_CLAMP_NONE));

		CameraData gCamera;
		EnvironmentData gEnvironment;
		MaterialData gMaterial;
		vector<LightData> gLights(1);
		vector<InstanceData> gInstances(1);

		Map4(gCamera.Position) = Vector4f(0,0,-3,0);
		Map4x4(gCamera.View) = Matrix4f::Identity();
		Map3(gEnvironment.Ambient) = Vector3f::Constant(0.25f);
		gEnvironment.LightCount = 1;
		Map4(gMaterial.gTextureST) = Vector4f(1,1,0,0);
		Map4(gMaterial.gBaseColor) = Vector4f::Ones();
		Map3(gMaterial.gEmission) = Vector3f::Zero();
		gMaterial.gBumpStrength = 1.f;
		gMaterial.gMetallic = 1.f;
		gMaterial.gRoughness = 1.f;
		gMaterial.gAlphaCutoff = 0.5f;
		gLights[0].Type = LIGHT_DIRECTIONAL;
		Map3(gLights[0].Emission) = Vector3f::Ones();
		Map3(gLights[0].Position) = Vector3f(0.2f,0.8f,-1);
		Map3(gLights[0].Direction) = Map3(gLights[0].Position).normalized();
		Map3(gLights[0].Right) = Vector3f(0,1,0).cross(Map3(gLights[0].Direction)).normalized();
		Map4x4(gInstances[0].Transform) = Matrix4f::Identity();
		Map4x4(gInstances[0].InverseTransform) = Map4x4(gInstances[0].Transform).inverse();

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

			if (window.Swapchain() && (!framebuffer || window.SwapchainExtent() != framebuffer->Extent())) {
				auto primaryColor = make_shared<Texture>(instance->Device(), "primary_color", vk::Extent3D(window.SwapchainExtent(),1), colorAttachment, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);
				auto primaryDepth = make_shared<Texture>(instance->Device(), "primary_depth", vk::Extent3D(window.SwapchainExtent(),1), depthAttachment, vk::ImageUsageFlagBits::eDepthStencilAttachment);
				auto primaryResolve = make_shared<Texture>(instance->Device(), "primary_resolve", vk::Extent3D(window.SwapchainExtent(),1), resolveAttachment, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
				framebuffer = make_shared<Framebuffer>("framebuffer", *renderPass, vector<TextureView> { TextureView(primaryColor), TextureView(primaryDepth), TextureView(primaryResolve) });
				Map4x4(gCamera.Projection) = PerspectiveFov(radians(42.18f), (float)framebuffer->Extent().width/(float)framebuffer->Extent().height, .01f, 128.f).matrix().transpose();
				Map4x4(gCamera.InvProjection) = Map4x4(gCamera.Projection).inverse();
				Map4x4(gCamera.ViewProjection) = Map4x4(gCamera.View) * Map4x4(gCamera.Projection);
			}
			
			if (framebuffer) {
				// render
				auto perCamera = make_shared<DescriptorSet>(instance->Device(), "PerCamera", pbr->DescriptorSetLayouts()[0]);
				auto perMaterial = make_shared<DescriptorSet>(instance->Device(), "PerMaterial", pbr->DescriptorSetLayouts()[1]);
				auto perObject = make_shared<DescriptorSet>(instance->Device(), "PerObject", pbr->DescriptorSetLayouts()[2]);
				perCamera->insert(pbr->binding("gCamera").mBinding, commandBuffer->CopyToDevice("gCamera", span(&gCamera,1), vk::BufferUsageFlagBits::eUniformBuffer));
				perMaterial->insert(pbr->binding("gMaterial").mBinding, commandBuffer->CopyToDevice("gMaterial", span(&gMaterial,1), vk::BufferUsageFlagBits::eUniformBuffer));
				perMaterial->insert(pbr->binding("gBaseColorTexture").mBinding, TextureView(gBaseColorTexture), vk::ImageLayout::eShaderReadOnlyOptimal);
				perMaterial->insert(pbr->binding("gNormalTexture").mBinding, TextureView(gNormalTexture), vk::ImageLayout::eShaderReadOnlyOptimal);
				perMaterial->insert(pbr->binding("gSampler").mBinding, gSampler);
				perObject->insert(pbr->binding("gInstances").mBinding, commandBuffer->CopyToDevice("gInstances", gInstances, vk::BufferUsageFlagBits::eStorageBuffer));

				commandBuffer->TransitionImageDescriptors(*perCamera, *perMaterial, *perObject);
				
				commandBuffer->BeginRenderPass(renderPass, framebuffer, vector<vk::ClearValue>{ vk::ClearColorValue(std::array<float,4>{0.25f, 0.3f, 0.9f, 1}), vk::ClearDepthStencilValue{1.f,0}, vk::ClearColorValue(std::array<float,4>{0,0,0,0}) });
				commandBuffer->BindPipeline(pbr);
				(*commandBuffer)->setViewport(0, { vk::Viewport(0, (float)framebuffer->Extent().height, (float)framebuffer->Extent().width, -(float)framebuffer->Extent().height, 0, 1) });
				(*commandBuffer)->setScissor(0, { vk::Rect2D(vk::Offset2D(0,0), framebuffer->Extent()) });
				(*commandBuffer)->setLineWidth(1);
				commandBuffer->BindDescriptorSets({ perCamera, perMaterial, perObject }, 0);
				testMesh->Draw(*commandBuffer);
				commandBuffer->EndRenderPass();

				// copy to window
				commandBuffer->WaitOn(vk::PipelineStageFlagBits::eTransfer, window.ImageAvailableSemaphore());
				commandBuffer->TransitionBarrier(window.BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal);
				
				vk::ImageCopy rgn = {};
				rgn.dstSubresource = rgn.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
				rgn.extent = vk::Extent3D(window.SwapchainExtent(), 1);
				framebuffer->at("primary_resolve").texture().TransitionBarrier(*commandBuffer, vk::ImageLayout::eTransferSrcOptimal);
				(*commandBuffer)->copyImage(*framebuffer->at("primary_resolve").texture(), vk::ImageLayout::eTransferSrcOptimal, window.BackBuffer(), vk::ImageLayout::eTransferDstOptimal, { rgn });

				commandBuffer->TransitionBarrier(window.BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR);
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
				printf("%.2f fps (%fms)          \r", (float)fpsAccum/totalTime, totalTime/fpsAccum);
				frameTimeAccum -= 1s;
				fpsAccum = 0;
			}
			frameCount++;
		}
		
		instance->Device().Flush();
	}
	
	instance.reset();
	
	Profiler::ClearHistory();

	return EXIT_SUCCESS;
}