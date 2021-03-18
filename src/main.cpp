#include "Core/Window.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/Mesh.hpp"
#include "Core/Profiler.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace stm;

inline unordered_map<string, shared_ptr<SpirvModule>> LoadShaders(Device& device, const fs::path& spvm) {
	unordered_map<string, shared_ptr<SpirvModule>> spirvModules;
	unordered_map<string, SpirvModule> tmp;
	byte_stream<ifstream>(spvm, ios::binary) >> tmp;
	ranges::for_each(tmp | views::values, [&](const auto& m){ spirvModules.emplace(m.mEntryPoint, make_shared<SpirvModule>(m)); });
	return spirvModules;
}

inline shared_ptr<GraphicsPipeline> CreatePipeline(const string& name, RenderPass& renderPass, uint32_t subpassIndex, GeometryData& geometry, const vector<shared_ptr<SpirvModule>>& spirv) {
	auto[attributes,bindings] = geometry.CreateInputBindings(*spirv[0]);
	vk::PipelineVertexInputStateCreateInfo vertexInput({}, bindings, attributes);
	return make_shared<GraphicsPipeline>(renderPass, name, subpassIndex, spirv, unordered_map<string,vk::SpecializationInfo>{}, vertexInput, geometry.mPrimitiveTopology);
}

template<ranges::contiguous_range R>
inline Buffer::ArrayView UploadDevice(CommandBuffer& commandBuffer, const R& data, vk::BufferUsageFlagBits usage) {
	auto buf = make_shared<Buffer>(commandBuffer.mDevice, "camera", data.size()*sizeof(ranges::range_value_t<R>), usage | vk::BufferUsageFlagBits::eTransferDst);
	commandBuffer->copyBuffer(*commandBuffer.UploadData(data).buffer(), **buf, { vk::BufferCopy(0,0,buf->size()) });
	return Buffer::ArrayView(buf, sizeof(ranges::range_value_t<R>), 0, data.size());
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
		aiVector3D position;
		aiVector3D normal;
		aiVector3D tangent;
		aiVector3D texcoord;
	};
	vector<vertex_t> vertices;
	vector<uint32_t> indices;
	for (const aiMesh* m : span(scene->mMeshes, scene->mNumMeshes)) {
		size_t vertsPerFace;
		if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_POINT) vertsPerFace = 1;
		else if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_LINE) vertsPerFace = 2;
		else if (m->mPrimitiveTypes & aiPrimitiveType::aiPrimitiveType_TRIANGLE) vertsPerFace = 3;
		else continue;

		mesh->Submeshes().emplace_back(m->mNumFaces, (uint32_t)indices.size(), (uint32_t)vertices.size());

		size_t tmp = vertices.size();
		vertices.resize(vertices.size() + m->mNumVertices);
		for (uint32_t i = 0; i < m->mNumVertices; i++) {
			vertex_t& v = vertices[tmp + i];
			v.position = m->mVertices[i];
			v.normal = m->mNormals[i];
			v.tangent = m->mTangents[i];
			v.texcoord = m->mTextureCoords[0][i];
		}

		tmp = indices.size();
		indices.resize(indices.size() + m->mNumFaces*vertsPerFace);
		auto it = indices.begin() + tmp;
		for (const aiFace& f : views::counted(m->mFaces, m->mNumFaces))
			ranges::copy(f.mIndices, f.mIndices + f.mNumIndices, it++);
	}

	shared_ptr<Buffer> vb = make_shared<Buffer>(device, "Vertices", vertices.size()*sizeof(vertex_t), vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst);
	shared_ptr<Buffer> ib = make_shared<Buffer>(device, "Indices", indices.size()*sizeof(uint32_t), vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst);

	Buffer& stagingBuffer = commandBuffer.HoldResource(make_shared<Buffer>(device, "GeometryUpload", ib->size() + vb->size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
	memcpy(stagingBuffer.data(), vertices.data(), vb->size());
	memcpy(stagingBuffer.data() + vb->size(), indices.data(), ib->size());
	commandBuffer->copyBuffer(*stagingBuffer, **vb, { vk::BufferCopy(0,0,vb->size()) });
	commandBuffer->copyBuffer(*stagingBuffer, **ib, { vk::BufferCopy(vb->size(),0,ib->size()) });

	mesh->Indices() = Buffer::ArrayView(ib, sizeof(uint32_t));
	mesh->Geometry().mBindings.emplace_back(Buffer::ArrayView(vb, sizeof(vertex_t)), vk::VertexInputRate::eVertex);
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
	commandBuffer->copyBufferToImage(*commandBuffer.UploadData(pixels).buffer(), **tex, vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, tex->ArrayLayers()), {}, tex->Extent()) });
	tex->GenerateMipMaps(commandBuffer);
	return tex;
}

int main(int argc, char** argv) {
	unique_ptr<Instance> instance = make_unique<Instance>(argc, argv);
	Window& window = instance->Window();
	
	{
		shared_ptr<Texture> gEnvironmentTexture, gBaseColorTexture, gNormalTexture, gMetallicRoughnessTexture;
		shared_ptr<Texture> gShadowAtlas = make_shared<Texture>(instance->Device(), "ShadowAtlas", vk::Extent3D(1024,1024,1), vk::Format::eR32Sfloat);
		shared_ptr<Mesh> testMesh;
		
		{
			auto commandBuffer = instance->Device().GetCommandBuffer("Init");
			
			testMesh = LoadScene(*commandBuffer, "Assets/Models/suzanne.obj");
			//gEnvironmentTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/_backgrounds/path.hdr");
			gBaseColorTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/copper/Metal08_col.jpg");
			gNormalTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/copper/Metal08_nrm.jpg");
			//shared_ptr<Texture> gMetallicTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/copper/Metal08_met.jpg");
			//shared_ptr<Texture> gRoughnessTexture = LoadTexture(*commandBuffer, "E:/3d/blender/Textures/copper/Metal08_rgh.jpg");
			gMetallicRoughnessTexture = make_shared<Texture>(instance->Device(), "Metal08_met_rgh", gBaseColorTexture->Extent(), vk::Format::eR8G8Unorm);
			
			instance->Device().Execute(commandBuffer);
			vk::createResultValue(commandBuffer->CompletionFence().wait(), "Init");
		}

		auto colorAttachment = vk::AttachmentDescription({}, vk::Format::eR8G8B8A8Unorm, vk::SampleCountFlagBits::e4);
		colorAttachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
		colorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
		auto depthAttachment = vk::AttachmentDescription({}, vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e4);
		depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		auto resolveAttachment = vk::AttachmentDescription({}, vk::Format::eR8G8B8A8Unorm, vk::SampleCountFlagBits::e1);
		resolveAttachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
		resolveAttachment.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
		RenderPass::SubpassDescription subpass {
			"main_render", vk::PipelineBindPoint::eGraphics, {
				{ "primary_color", { colorAttachment, RenderPass::AttachmentType::eColor, vk::PipelineColorBlendAttachmentState() } },
				{ "primary_depth", { depthAttachment, RenderPass::AttachmentType::eDepthStencil, vk::PipelineColorBlendAttachmentState() } },
				{ "primary_resolve", { resolveAttachment, RenderPass::AttachmentType::eResolve, vk::PipelineColorBlendAttachmentState() } } }, {} };
		shared_ptr<RenderPass> renderPass = make_shared<RenderPass>(window.mInstance.Device(), "main", ranges::single_view(subpass));

		auto spirvModules = LoadShaders(instance->Device(), "Assets/core_shaders.spvm");
		spirvModules.at("fs_pbr")->mDescriptorBindings.at("gSampler").mImmutableSamplers = { vk::SamplerCreateInfo({},
			vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
			0, true, 4, true, vk::CompareOp::eLess, 0, VK_LOD_CLAMP_NONE ) };
		spirvModules.at("fs_pbr")->mDescriptorBindings.at("gShadowSampler").mImmutableSamplers = { vk::SamplerCreateInfo({},
			vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
			vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder,
			0, true, 4, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE, vk::BorderColor::eFloatOpaqueWhite ) };

		auto pbr = CreatePipeline("pbr", *renderPass, 0, testMesh->Geometry(), { spirvModules.at("vs_pbr"), spirvModules.at("fs_pbr") });
		
		shared_ptr<Framebuffer> framebuffer;
		
		shader_interop::CameraData gCamera;
		shader_interop::EnvironmentData gEnvironment;
		shader_interop::MaterialData gMaterial;
		vector<shader_interop::LightData> gLights(1);
		vector<shader_interop::InstanceData> gInstances(1);

		Map<Vector4f>(gCamera.Position.data()) = Vector4f(0,0,-3,0);
		Map<Matrix4f>(gCamera.View.data()) = Matrix4f::Identity();
		Map<Vector3f>(gEnvironment.Ambient.data()) = Vector3f::Constant(0.25f);
		gEnvironment.LightCount = 1;
		gMaterial.gAlphaCutoff = 0.5f;
		Map<Vector4f>(gMaterial.gBaseColor.data()) = Vector4f::Ones();
		gMaterial.gBumpStrength = 1.f;
		Map<Vector3f>(gMaterial.gEmission.data()) = Vector3f::Zero();
		gMaterial.gMetallic = 1.f;
		gMaterial.gRoughness = 1.f;
		Map<Vector4f>(gMaterial.gTextureST.data()) = Vector4f(1,1,0,0);
		gLights[0].Type = LIGHT_DIRECTIONAL;
		Map<Vector3f>(gLights[0].Emission.data()) = Vector3f::Ones();
		Map<Vector3f>(gLights[0].Position.data()) = Vector3f(0.2f,0.8f,-1);
		Map<Vector3f>(gLights[0].Direction.data()) = Map<Vector3f>(gLights[0].Position.data()).normalized();
		Map<Vector3f>(gLights[0].Right.data()) = Vector3f(0,1,0).cross(Map<Vector3f>(gLights[0].Direction.data())).normalized();
		Map<Matrix4f>(gInstances[0].Transform.data()) = Matrix4f::Identity();
		Map<Matrix4f>(gInstances[0].InverseTransform.data()) = Map<Matrix4f>(gInstances[0].Transform.data()).inverse();

		auto frameTimeAccum = chrono::nanoseconds::zero();
		uint32_t fpsAccum = 0;
		while (true) {
			Profiler::BeginSample("Frame" + to_string(instance->Device().FrameCount()));

			shared_ptr<Semaphore> frameSemaphore = make_shared<Semaphore>(instance->Device(), "FrameSemaphore");
			{
				ProfilerRegion ps("PollEvents + AcquireNextImage");
				if (!instance->PollEvents()) break; // Window was closed
				window.AcquireNextImage();
			}

			if (window.Swapchain() && (!framebuffer || window.SwapchainExtent() != framebuffer->Extent())) {
				auto primaryColor = make_shared<Texture>(instance->Device(), "primary_color", vk::Extent3D(window.SwapchainExtent(),1), colorAttachment, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);
				auto primaryDepth = make_shared<Texture>(instance->Device(), "primary_depth", vk::Extent3D(window.SwapchainExtent(),1), depthAttachment, vk::ImageUsageFlagBits::eDepthStencilAttachment);
				auto primaryResolve = make_shared<Texture>(instance->Device(), "primary_resolve", vk::Extent3D(window.SwapchainExtent(),1), resolveAttachment, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
				framebuffer = make_shared<Framebuffer>("framebuffer", *renderPass, vector<TextureView> { TextureView(primaryColor), TextureView(primaryDepth), TextureView(primaryResolve) });
				Map<Matrix4f>(gCamera.Projection.data()) = PerspectiveFov(radians(42.18f), (float)framebuffer->Extent().width/(float)framebuffer->Extent().height, .01f, 128.f).matrix();
				Map<Matrix4f>(gCamera.InvProjection.data()) = Map<Matrix4f>(gCamera.Projection.data()).inverse();
				Map<Matrix4f>(gCamera.ViewProjection.data()) = Map<Matrix4f>(gCamera.View.data()) * Map<Matrix4f>(gCamera.Projection.data());
			}

			auto commandBuffer = instance->Device().GetCommandBuffer("Frame");

			if (framebuffer) {
				auto perCamera = make_shared<DescriptorSet>(instance->Device(), "PerCamera", pbr->DescriptorSetLayouts()[0]);
				perCamera->set(pbr->location("gCamera"), 0, DescriptorSet::Entry(vk::DescriptorType::eUniformBuffer, UploadDevice(*commandBuffer, span(&gCamera,1), vk::BufferUsageFlagBits::eUniformBuffer)));
				perCamera->set(pbr->location("gEnvironment"), 0, DescriptorSet::Entry(vk::DescriptorType::eUniformBuffer, UploadDevice(*commandBuffer, span(&gEnvironment,1), vk::BufferUsageFlagBits::eUniformBuffer)));
				perCamera->set(pbr->location("gLights"), 0, DescriptorSet::Entry(vk::DescriptorType::eStorageBuffer, UploadDevice(*commandBuffer, gLights, vk::BufferUsageFlagBits::eStorageBuffer)));
				perCamera->set(pbr->location("gShadowAtlas"), 0, DescriptorSet::Entry(vk::DescriptorType::eSampledImage, gShadowAtlas, vk::ImageLayout::eShaderReadOnlyOptimal));
				//perCamera->set(pbr->location("gEnvironmentTexture"), 0, DescriptorSet::Entry(vk::DescriptorType::eSampledImage, gEnvironmentTexture, vk::ImageLayout::eShaderReadOnlyOptimal));

				auto perMaterial = make_shared<DescriptorSet>(instance->Device(), "PerMaterial", pbr->DescriptorSetLayouts()[1]);
				perMaterial->set(pbr->location("gMaterial"), 0, DescriptorSet::Entry(vk::DescriptorType::eUniformBuffer, UploadDevice(*commandBuffer, span(&gMaterial,1), vk::BufferUsageFlagBits::eUniformBuffer)));
				perMaterial->set(pbr->location("gBaseColorTexture"), 0, DescriptorSet::Entry(vk::DescriptorType::eSampledImage, gBaseColorTexture, vk::ImageLayout::eShaderReadOnlyOptimal));
				perMaterial->set(pbr->location("gNormalTexture"), 0, DescriptorSet::Entry(vk::DescriptorType::eSampledImage, gNormalTexture, vk::ImageLayout::eShaderReadOnlyOptimal));
				perMaterial->set(pbr->location("gMetallicRoughnessTexture"), 0, DescriptorSet::Entry(vk::DescriptorType::eSampledImage, gMetallicRoughnessTexture, vk::ImageLayout::eShaderReadOnlyOptimal));

				auto perObject = make_shared<DescriptorSet>(instance->Device(), "PerObject", pbr->DescriptorSetLayouts()[2]);
				perObject->set(pbr->location("gInstances"), 0, DescriptorSet::Entry(vk::DescriptorType::eStorageBuffer, UploadDevice(*commandBuffer, gInstances, vk::BufferUsageFlagBits::eStorageBuffer)));

				commandBuffer->TransitionImageDescriptors(*perCamera, *perMaterial, *perObject);

				// render
				commandBuffer->BeginRenderPass(renderPass, framebuffer);
				commandBuffer->BindPipeline(pbr);
				(*commandBuffer)->setViewport(0, { vk::Viewport(0, (float)framebuffer->Extent().height, (float)framebuffer->Extent().width, -(float)framebuffer->Extent().height, 0, 1) });
				(*commandBuffer)->setScissor(0, { vk::Rect2D(vk::Offset2D(0,0), framebuffer->Extent()) });
				(*commandBuffer)->setLineWidth(1);
				commandBuffer->BindDescriptorSets({ perCamera, perMaterial, perObject }, 0);
				testMesh->Draw(*commandBuffer);
				commandBuffer->EndRenderPass();
			
				// copy to window
				if (window.Swapchain()) {
					commandBuffer->WaitOn(vk::PipelineStageFlagBits::eTransfer, window.ImageAvailableSemaphore());
					commandBuffer->SignalOnComplete(vk::PipelineStageFlagBits::eColorAttachmentOutput, frameSemaphore);
					
					commandBuffer->TransitionBarrier(window.BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal);
					
					vk::ImageCopy rgn = {};
					rgn.dstSubresource = rgn.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
					rgn.extent = vk::Extent3D(window.SwapchainExtent(), 1);
					(*commandBuffer)->copyImage(*framebuffer->at("primary_resolve").texture(), vk::ImageLayout::eTransferSrcOptimal, window.BackBuffer(), vk::ImageLayout::eTransferDstOptimal, { rgn });

					commandBuffer->TransitionBarrier(window.BackBuffer(), { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR);
				}
			}

			instance->Device().Execute(commandBuffer);
			
			if (window.Swapchain()) {
				ProfilerRegion ps("Present");
				window.Present({ **frameSemaphore });
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
		}
		
		instance->Device().Flush();
	}
	
	instance.reset();
	
	Profiler::ClearHistory();

	return EXIT_SUCCESS;
}