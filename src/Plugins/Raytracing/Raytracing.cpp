#include <Core/EnginePlugin.hpp>

#include <Scene/Camera.hpp>
#include <Scene/MeshRenderer.hpp>
#include <Scene/GUI.hpp>
#include <Scene/Scene.hpp>
#include <Util/Tokenizer.hpp>

#include <stack>

using namespace std;

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <ThirdParty/stb_image_write.h>
#include <ThirdParty/json11.h>

#ifdef GetObject
#undef GetObject
#endif

#include "Shaders/rtcommon.h"

#define PASS_RAYTRACE (1u << 23u)

inline RTMaterial DefaultMaterial() {
	RTMaterial m = {};
	m.BaseColor = 1.f;
	m.Roughness = 0.5f;
	m.Emission = 0;
	m.Metallic = 0;
	m.Absorption = 0;
	m.Scattering = 0;
	m.Transmission = 0;
	m.TransmissionRoughness = 0.1f;
	m.TextureST = float4(1,1,0,0);
	m.IndexOfRefraction = 1.5f;
	m.BaseColorTexture = 0;
	m.RoughnessTexture = 0;
	m.NormalTexture = 0;
	return m;
}
inline RTMaterial ConvertMaterial(Material* material, stm_ptr<Texture>* textures, uint32_t& textureIndex) {
	RTMaterial m = DefaultMaterial();

	float4 color = 1;
	float4 absorption = 0;
	float4 scatter = 0;
	if (material->GetParameter<float4>("BaseColor", color))
		m.BaseColor = color.rgb;
	else material->GetParameter<float3>("BaseColor", m.BaseColor);
	material->GetParameter<float4>("Emission", m.Emission);
	material->GetParameter<float4>("Absorption", absorption);
	material->GetParameter<float4>("Scattering", scatter);
	material->GetParameter<float>("Roughness", m.Roughness);
	material->GetParameter<float>("Metallic", m.Metallic);
	material->GetParameter<float>("Transmission", m.Transmission);
	if (!material->GetParameter<float>("TransmissionRoughness", m.TransmissionRoughness)) m.TransmissionRoughness = m.Roughness;
	material->GetParameter<float>("IndexOfRefraction", m.IndexOfRefraction);
	material->GetParameter<float4>("TextureST", m.TextureST);

	m.Absorption = absorption.rgb * absorption.w;
	m.Scattering = scatter.rgb * scatter.w;

	stm_ptr<Texture> tex = nullptr;
	if (material->GetParameter<stm_ptr<Texture>>("BaseColorTexture", tex)) {
		for (uint32_t i = 0; i < textureIndex; i++) if (textures[i] == tex) { m.BaseColorTexture = i; goto rgh; }
		m.BaseColorTexture = textureIndex;
		textures[textureIndex++] = tex;
	}
	rgh:
	if (material->GetParameter<stm_ptr<Texture>>("RoughnessTexture", tex)) {
		for (uint32_t i = 0; i < textureIndex; i++) if (textures[i] == tex) { m.RoughnessTexture = i; goto nrm; }
		m.RoughnessTexture = textureIndex;
		textures[textureIndex++] = tex;
	}
	nrm:
	if (material->GetParameter<stm_ptr<Texture>>("NormalTexture", tex)) {
		for (uint32_t i = 0; i < textureIndex; i++) if (textures[i] == tex) { m.NormalTexture = i; goto ret; }
		m.NormalTexture = textureIndex;
		textures[textureIndex++] = tex;
	}
	ret:
	return m;
}

class Raytracing : public EnginePlugin {
private:
	Scene* mScene = nullptr;
	MouseKeyboardInput* mInput = nullptr;

	Material* mEditMaterial = nullptr;

	string mOutputFile = "output.png";

	bool mShowGUI = false;

	float mSampleRate = 0.25f;
	float mGamma = 2.2f;
	float mHistoryTrust = 1.f;
	uint32_t mMaxSurfaceBounces = 6;
	uint32_t mMaxVolumeBounces = 1;

	struct FrameData {
		stm_ptr<Texture> mRadiance;
		stm_ptr<Texture> mNormals;
		stm_ptr<Texture> mPositions;

		stm_ptr<Buffer> mVertices;
		stm_ptr<Buffer> mTriangles;
		stm_ptr<Buffer> mPrimitiveMaterials;

		stm_ptr<Buffer> mBvh;
		stm_ptr<Buffer> mLights;
		stm_ptr<Buffer> mMaterials;

		float2 mResolution;
		uint32_t mLightCount;

		float3 mCameraPosition;
		quaternion mCameraRotation;
		float mCameraFoV;

		uint32_t mRandomSeed;
		uint64_t mLastBuild;

		stm_ptr<Texture> mTextures[TEXTURE_COUNT];
	};
	FrameData* mFrameData = nullptr;

	void SaveOutput() {
		vkDeviceWaitIdle(*mScene->Instance()->Device());

		stm_ptr<CommandBuffer> commandBuffer = mScene->Instance()->Device()->GetCommandBuffer();

		FrameData& fd = mFrameData[mScene->Instance()->Window()->BackBufferIndex()];

		// Copy to host-visible memory
		commandBuffer->TransitionBarrier(fd.mRadiance, vk::ImageLayout::eTransferSrcOptimal);		
		vk::BufferImageCopy rgn = {};
		rgn.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		rgn.imageSubresource.layerCount = 1;
		rgn.imageExtent = fd.mRadiance->Extent();
		Buffer copyBuffer("Image Copy", mScene->Instance()->Device(), fd.mRadiance->Extent().width * fd.mRadiance->Extent().height * sizeof(float4), vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
		((vk::CommandBuffer)*commandBuffer).CopyImageToBuffer(*fd.mRadiance, vk::ImageLayout::eTransferSrcOptimal, copyBuffer, 1, &rgn);
		mScene->Instance()->Device()->Execute(commandBuffer);
		commandBuffer->Wait();

		printf_color(COLOR_CYAN_BOLD, "Writing PNG...");

		uint32_t pixelCount = fd.mRadiance->Extent().width * fd.mRadiance->Extent().height;

		float4* light = (float4*)copyBuffer.MappedData();
		uint8_t* rgba8 = new uint8_t[pixelCount * 64];

		for (uint32_t i = 0; i < pixelCount; i++) {
			float3 s = light[i].rgb;
			s = clamp(pow(s, 1.f / mGamma) * 255.f, 0.f, 255.f);
			rgba8[4 * i + 0] = (uint8_t)s.r;
			rgba8[4 * i + 1] = (uint8_t)s.g;
			rgba8[4 * i + 2] = (uint8_t)s.b;
			rgba8[4 * i + 3] = 0xFF;

			if ((i % 100) == 0) {
				printf_color(COLOR_CYAN_BOLD, "\rWriting PNG: %.1f%%", 100.f * ((float)i + 0.5f) / (float)pixelCount);
			}
		}

		stbi_write_png(mOutputFile.c_str(), fd.mRadiance->Extent().width, fd.mRadiance->Extent().height, 4, rgba8, fd.mRadiance->Extent().width * 4);
		delete[] rgba8;

		printf_color(COLOR_CYAN_BOLD, "\rWriting PNG: Done          \n");
	}
	
	void Build(stm_ptr<CommandBuffer> commandBuffer, FrameData& fd) {
		PROFILER_BEGIN("Create BVH");
		ObjectBvh2* sceneBvh = mScene->BVH();

		vector<RTMaterial> materials;
		vector<stm_ptr<Texture>> textures;

		vector<uint32_t> primitiveMaterials;

		unordered_map<stm_ptr<Buffer>, vector<vk::BufferCopy>> vertexCopies;

		uint32_t primitiveCount = 0;
		uint32_t vertexCount = 0;
		uint32_t indexCount = 0;
		
		fd.mLightCount = 0;
		fd.mTextures[0] = mScene->EnvironmentTexture();
		uint32_t textureIndex = 1;
		
		uint32_t vertexStride = sizeof(StdVertex);

		Shader* bvhShader = commandBuffer->Device()->AssetManager()->LoadShader("Shaders/build_bvh.stmb");

		vector<Object*> objects = mScene->Objects();
		for (Object* o : objects) {
			MeshRenderer* mr = dynamic_cast<MeshRenderer*>(o);
			if (!mr || !mr->Visible()) continue;
			Mesh* m = mr->Mesh();
			if (!m || m->Topology() != vk::PrimitiveTopology::eTriangleList) continue;

			vk::BufferCopy rgn = {};
			rgn.srcOffset = m->BaseVertex() * sizeof(StdVertex);
			rgn.dstOffset = vertexCount * sizeof(StdVertex);
			rgn.size = m->VertexCount() * sizeof(StdVertex);
			vertexCopies[m->VertexBuffer().get()].push_back(rgn);

			uint32_t meshPrimCount = m->IndexCount() / 3;
			uint32_t materialIndex = (uint32_t)materials.size();

			RTMaterial mat = ConvertMaterial(mr->Material(), fd.mTextures, textureIndex);

			if (mat.Emission.r > 0 || mat.Emission.g > 0 || mat.Emission.b > 0)
				fd.mLightCount += meshPrimCount;
			
			for (uint32_t i = 0; i < meshPrimCount; i++)
				primitiveMaterials.push_back(materialIndex);

			materials.push_back(mat);

			primitiveCount += meshPrimCount;
			vertexCount += m->VertexCount();
			indexCount += m->IndexCount();
		}

		#pragma region Upload data
		vertexCount = max(1u, vertexCount);
		indexCount = max(1u, indexCount);
		if (materials.empty()) materials.push_back({});
		if (primitiveMaterials.empty()) primitiveMaterials.push_back(0);

		if (!fd.mVertices || fd.mVertices->Size() < sizeof(StdVertex) * vertexCount)
			fd.mVertices = new Buffer("Vertices", mScene->Instance()->Device(), sizeof(StdVertex) * vertexCount, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
		
		if (!fd.mTriangles || fd.mTriangles->Size() < sizeof(uint32_t) * indexCount)
			fd.mTriangles = new Buffer("Triangles", mScene->Instance()->Device(), sizeof(uint32_t) * indexCount, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);

		if (!fd.mLights || fd.mLights->Size() < sizeof(RTLight) * max(1u, fd.mLightCount))
			fd.mLights = new Buffer("Lights", mScene->Instance()->Device(), sizeof(RTLight) * max(1u, fd.mLightCount), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);

		if (!fd.mMaterials || fd.mMaterials->Size() < sizeof(RTMaterial) * materials.size())
			fd.mMaterials = new Buffer("Materials", mScene->Instance()->Device(), sizeof(RTMaterial) * materials.size(), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);

		if (!fd.mPrimitiveMaterials || fd.mPrimitiveMaterials->Size() < sizeof(uint32_t) * primitiveMaterials.size())
			fd.mPrimitiveMaterials = new Buffer("PrimitiveMaterials", mScene->Instance()->Device(), sizeof(uint32_t) * primitiveMaterials.size(), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
		
		if (!fd.mBvh && fd.mBvh->Size() < sizeof(BvhNode) * primitiveCount * 2)
			fd.mBvh = new Buffer("Bvh", mScene->Instance()->Device(), sizeof(BvhNode) * primitiveCount * 2, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);

		fd.mMaterials->Upload(materials.data(), sizeof(RTMaterial)* materials.size());
		fd.mPrimitiveMaterials->Upload(primitiveMaterials.data(), sizeof(uint32_t) * primitiveMaterials.size());
		#pragma endregion
		
		#pragma region Copy vertices
		if (vertexCopies.size()) {
			for (auto p : vertexCopies)
				((vk::CommandBuffer)*commandBuffer).CopyBuffer(*p.first, *fd.mVertices, p.second.size(), p.second.data());

			vk::BufferMemoryBarrier barrier = {};
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
			barrier.buffer = *fd.mVertices;
			barrier.size = fd.mVertices->Size();
			vkCmdPipelineBarrier(*commandBuffer,
				vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
				0,
				0, nullptr,
				1, &barrier,
				0, nullptr);
		}

		vertexCount = 0;
		ComputeShader* copyKernel = bvhShader->GetCompute("CopyVertices", {});
		((vk::CommandBuffer)*commandBuffer).BindPipeline(vk::PipelineBindPoint::eCompute, copyKernel->mPipeline);
		stm_ptr<DescriptorSet> vds = commandBuffer->GetDescriptorSet("VertexCopy", copyKernel->mDescriptorSetLayouts[0]);
		vds->CreateStorageBufferDescriptor(fd.mVertices, 0, fd.mVertices->Size(), copyKernel->mDescriptorBindings.at("Vertices").second.binding);
		vds->FlushWrites();
		((vk::CommandBuffer)*commandBuffer).BindDescriptorSets(vk::PipelineBindPoint::eCompute, copyKernel->mPipelineLayout, 0, 1, *vds, 0, nullptr);
		for (Object* o : objects) {
			MeshRenderer* mr = dynamic_cast<MeshRenderer*>(o);
			if (!mr || !mr->Visible()) continue;
			Mesh* m = mr->Mesh();
			if (!m || m->Topology() != vk::PrimitiveTopology::eTriangleList) continue;

			float4x4 t = o->ObjectToWorld();
			uint32_t vc = m->VertexCount();

			commandBuffer->PushConstant(copyKernel, "Transform", &t);
			commandBuffer->PushConstant(copyKernel, "VertexStride", &vertexStride);
			commandBuffer->PushConstant(copyKernel, "PrimitiveCount", &vc);
			commandBuffer->PushConstant(copyKernel, "SrcOffset", &vertexCount);
			((vk::CommandBuffer)*commandBuffer).Dispatch((m->VertexCount() + 63) / 64, 1, 1);

			vertexCount += m->VertexCount();
			
			vk::BufferMemoryBarrier vbarrier = {};
			vbarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
			vbarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
			vbarrier.buffer = *fd.mVertices;
			vbarrier.size = fd.mVertices->Size();
			vkCmdPipelineBarrier(*commandBuffer,
				vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
				0,
				0, nullptr,
				1, &vbarrier,
				0, nullptr);
		}
		#pragma endregion
		#pragma region Copy indices
		indexCount = 0;
		vertexCount = 0;
		copyKernel = bvhShader->GetCompute("CopyIndices", {});
		((vk::CommandBuffer)*commandBuffer).BindPipeline(vk::PipelineBindPoint::eCompute, copyKernel->mPipeline);
		for (Object* o : objects) {
			MeshRenderer* mr = dynamic_cast<MeshRenderer*>(o);
			if (!mr || !mr->Visible()) continue;
			Mesh* m = mr->Mesh();
			if (!m || m->Topology() != vk::PrimitiveTopology::eTriangleList) continue;
			
			uint32_t indexStride = m->IndexType() == vk::IndexType::eUint16 ? sizeof(uint16_t) : sizeof(uint32_t);
			uint32_t ic = m->IndexCount();
			uint32_t sbi = m->BaseIndex();

			commandBuffer->PushConstant(copyKernel, "PrimitiveCount", &ic);
			commandBuffer->PushConstant(copyKernel, "SrcIndexStride", &indexStride);
			commandBuffer->PushConstant(copyKernel, "IndexOffset", &vertexCount);
			commandBuffer->PushConstant(copyKernel, "SrcOffset", &sbi);
			commandBuffer->PushConstant(copyKernel, "DstOffset", &indexCount);

			stm_ptr<DescriptorSet> ds = commandBuffer->GetDescriptorSet("IndexCopy", copyKernel->mDescriptorSetLayouts[0]);
			ds->CreateStorageBufferDescriptor(m->IndexBuffer().get(), 0, m->IndexBuffer()->Size(), copyKernel->mDescriptorBindings.at("Triangles").second.binding);
			ds->CreateStorageBufferDescriptor(fd.mTriangles, 0, fd.mTriangles->Size(), copyKernel->mDescriptorBindings.at("RWTriangles").second.binding);
			ds->FlushWrites();
			((vk::CommandBuffer)*commandBuffer).BindDescriptorSets(vk::PipelineBindPoint::eCompute, copyKernel->mPipelineLayout, 0, 1, *ds, 0, nullptr);
			((vk::CommandBuffer)*commandBuffer).Dispatch((m->IndexCount() + 63) / 64, 1, 1);

			indexCount += m->IndexCount();
			vertexCount += m->VertexCount();
				
			vk::BufferMemoryBarrier ibarrier = {};
			ibarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
			ibarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
			ibarrier.buffer = *fd.mTriangles;
			ibarrier.size = fd.mTriangles->Size();
			vkCmdPipelineBarrier(*commandBuffer,
				vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
				0,
				0, nullptr,
				1, &ibarrier,
				0, nullptr);
		}
		vk::BufferMemoryBarrier ibarrier = {};
		ibarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
		ibarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
		ibarrier.buffer = *fd.mTriangles;
		ibarrier.size = fd.mTriangles->Size();
		vkCmdPipelineBarrier(*commandBuffer,
			vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
			0,
			0, nullptr,
			1, &ibarrier,
			0, nullptr);
		#pragma endregion

		#pragma region Build bvh
		ComputeShader* buildKernel = bvhShader->GetCompute("BuildBvh", {});
		((vk::CommandBuffer)*commandBuffer).BindPipeline(vk::PipelineBindPoint::eCompute, buildKernel->mPipeline);

		uint32_t leafSize = 4;
		commandBuffer->PushConstant(buildKernel, "LeafSize", &leafSize);
		commandBuffer->PushConstant(buildKernel, "VertexStride", &vertexStride);
		commandBuffer->PushConstant(buildKernel, "PrimitiveCount", &primitiveCount);

		stm_ptr<DescriptorSet> ds = commandBuffer->GetDescriptorSet("BVH", buildKernel->mDescriptorSetLayouts[0]);
		ds->CreateStorageBufferDescriptor(fd.mBvh, 0, fd.mBvh->Size(), buildKernel->mDescriptorBindings.at("SceneBvh").second.binding);
		ds->CreateStorageBufferDescriptor(fd.mVertices, 0, fd.mVertices->Size(), buildKernel->mDescriptorBindings.at("Vertices").second.binding);
		ds->CreateStorageBufferDescriptor(fd.mTriangles, 0, fd.mTriangles->Size(), buildKernel->mDescriptorBindings.at("Triangles").second.binding);
		ds->CreateStorageBufferDescriptor(fd.mPrimitiveMaterials, 0, fd.mPrimitiveMaterials->Size(), buildKernel->mDescriptorBindings.at("PrimitiveMaterials").second.binding);
		ds->CreateStorageBufferDescriptor(fd.mLights, 0, fd.mLights->Size(), buildKernel->mDescriptorBindings.at("Lights").second.binding);
		ds->CreateStorageBufferDescriptor(fd.mMaterials, 0, fd.mMaterials->Size(), buildKernel->mDescriptorBindings.at("Materials").second.binding);
		ds->FlushWrites();
		((vk::CommandBuffer)*commandBuffer).BindDescriptorSets(vk::PipelineBindPoint::eCompute, buildKernel->mPipelineLayout, 0, 1, *ds, 0, nullptr);

		((vk::CommandBuffer)*commandBuffer).Dispatch(1, 1, 1);
		#pragma endregion

		fd.mLastBuild = commandBuffer->Device()->FrameCount();
		PROFILER_END;
	}
	void DirtyFrames() {
		for (uint32_t i = 0; i < mScene->Instance()->Window()->BackBufferCount(); i++) mFrameData[i].mLastBuild = 0;
	}

	void DrawGui(stm_ptr<CommandBuffer> commandBuffer, GuiContext* gui, Camera* camera) {
		static const float sliderSize = 8;

		GUI::BeginScreenLayoutAuto(LayoutAxis::eVertical, float2(10, camera->FramebufferExtent().height - 300), 175, 10);
		if (GUI::LayoutTextButton(font, "Save", 16, 20)) SaveOutput();

		GUI::LayoutLabel(font, "Sample Rate: " + to_string(mSampleRate), 16, 20, 2, TextAnchor::eMin);
		GUI::LayoutSlider(mSampleRate, .05f, 1, sliderSize, 10, 1);
		
		GUI::LayoutLabel(font, "Gamma: " + to_string(mGamma), 16, 20, 2, TextAnchor::eMin);
		GUI::LayoutSlider(mGamma, 0, 3, sliderSize, 10, 1);

		float ambient = mScene->Environment()->AmbientLight().r;
		GUI::LayoutLabel(font, "Ambient: " + to_string(ambient), 16, 20, 2, TextAnchor::eMin);
		if (GUI::LayoutSlider(ambient, 0, 0.2f, sliderSize, 10, 1)) { mScene->Environment()->AmbientLight(ambient); DirtyFrames(); }

		GUI::LayoutLabel(font, "History Trust: " + to_string(mHistoryTrust), 16, 20, 2, TextAnchor::eMin);
		GUI::LayoutSlider(mHistoryTrust, 0, 1, sliderSize, 10, 1);

		if (mEditMaterial) {
			GUI::LayoutColors theme = GUI::mLayoutTheme;
			GUI::mLayoutTheme.mBackground *= 1.5f;

			GUI::BeginScrollSubLayout(110, 470, 4);
			GUI::LayoutLabel(font24, mEditMaterial->mName, 20, 22);

			float4 baseColor = 0.f;
			if (!mEditMaterial->GetParameter<float4>("BaseColor", baseColor))
				mEditMaterial->GetParameter<float3>("BaseColor", baseColor.rgb);
			GUI::LayoutLabel(font, "BaseColor", 16, 20, 2, TextAnchor::eMin);
			GUI::mLayoutTheme.mControlBackground = float4(baseColor.rgb, 1);
			if (GUI::LayoutSlider(baseColor.r, 0, 1, sliderSize, 10, 1)) { mEditMaterial->SetParameter("BaseColor", baseColor); DirtyFrames(); }
			if (GUI::LayoutSlider(baseColor.g, 0, 1, sliderSize, 10, 1)) { mEditMaterial->SetParameter("BaseColor", baseColor); DirtyFrames(); }
			if (GUI::LayoutSlider(baseColor.b, 0, 1, sliderSize, 10, 1)) { mEditMaterial->SetParameter("BaseColor", baseColor); DirtyFrames(); }
			GUI::mLayoutTheme = theme;
			
			float4 absorption = 0.f;
			mEditMaterial->GetParameter<float4>("Absorption", absorption);
			GUI::LayoutLabel(font, "Absorption", 16, 20, 2, TextAnchor::eMin);
			GUI::mLayoutTheme.mControlBackground = float4(absorption.rgb, 1);
			if (GUI::LayoutSlider(absorption.r, 0, 1, sliderSize, 10, 1)) { mEditMaterial->SetParameter("Absorption", absorption); DirtyFrames(); }
			if (GUI::LayoutSlider(absorption.g, 0, 1, sliderSize, 10, 1)) { mEditMaterial->SetParameter("Absorption", absorption); DirtyFrames(); }
			if (GUI::LayoutSlider(absorption.b, 0, 1, sliderSize, 10, 1)) { mEditMaterial->SetParameter("Absorption", absorption); DirtyFrames(); }
			if (GUI::LayoutSlider(absorption.w, 0, 3, sliderSize, 10, 1)) { mEditMaterial->SetParameter("Absorption", absorption); DirtyFrames(); }
			GUI::mLayoutTheme = theme;

			float4 scatter = 0.f;
			mEditMaterial->GetParameter<float4>("Scattering", scatter);
			GUI::LayoutLabel(font, "Scattering", 16, 20, 2, TextAnchor::eMin);
			GUI::mLayoutTheme.mControlBackground = float4(scatter.rgb, 1);
			if (GUI::LayoutSlider(scatter.r, 0, 1, sliderSize, 10, 1)) { mEditMaterial->SetParameter("Scattering", scatter); DirtyFrames(); }
			if (GUI::LayoutSlider(scatter.g, 0, 1, sliderSize, 10, 1)) { mEditMaterial->SetParameter("Scattering", scatter); DirtyFrames(); }
			if (GUI::LayoutSlider(scatter.b, 0, 1, sliderSize, 10, 1)) { mEditMaterial->SetParameter("Scattering", scatter); DirtyFrames(); }
			if (GUI::LayoutSlider(scatter.w, 0, 3, sliderSize, 10, 1)) { mEditMaterial->SetParameter("Scattering", scatter); DirtyFrames(); }
			GUI::mLayoutTheme = theme;

			float4 emission = 0.f;
			mEditMaterial->GetParameter<float4>("Emission", emission);
			GUI::LayoutLabel(font, "Emission", 16, 20, 2, TextAnchor::eMin);
			GUI::mLayoutTheme.mControlBackground = float4(emission.rgb, 1);
			if (GUI::LayoutSlider(emission.r, 0, 1, sliderSize, 10, 1)) { mEditMaterial->SetParameter("Emission", emission); DirtyFrames(); }
			if (GUI::LayoutSlider(emission.g, 0, 1, sliderSize, 10, 1)) { mEditMaterial->SetParameter("Emission", emission); DirtyFrames(); }
			if (GUI::LayoutSlider(emission.b, 0, 1, sliderSize, 10, 1)) { mEditMaterial->SetParameter("Emission", emission); DirtyFrames(); }
			if (GUI::LayoutSlider(emission.w, 0, 20, 4, 10, 1)) { mEditMaterial->SetParameter("Emission", emission); DirtyFrames(); }
			GUI::mLayoutTheme = theme;
			

			float metallic = 0.f;
			mEditMaterial->GetParameter<float>("Metallic", metallic);
			GUI::LayoutLabel(font, "Metallic: " + to_string(metallic), 16, 20, 2, TextAnchor::eMin);
			if (GUI::LayoutSlider(metallic, 0, 1, sliderSize, 10)) { mEditMaterial->SetParameter("Metallic", metallic); DirtyFrames(); }

			float roughness = 0.5f;
			mEditMaterial->GetParameter<float>("Roughness", roughness);
			GUI::LayoutLabel(font, "Roughness", 16, 20, 2, TextAnchor::eMin);
			if (GUI::LayoutSlider(roughness, 0, 1, sliderSize, 10)) { mEditMaterial->SetParameter("Roughness", roughness); DirtyFrames(); }

			float transmission = 0.f;
			mEditMaterial->GetParameter<float>("Transmission", transmission);
			GUI::LayoutLabel(font, "Transmission", 16, 20, 2, TextAnchor::eMin);
			if (GUI::LayoutSlider(transmission, 0, 1, sliderSize, 10)) { mEditMaterial->SetParameter("Transmission", transmission); DirtyFrames(); }

			float transmissionRoughness = 0.f;
			mEditMaterial->GetParameter<float>("TransmissionRoughness", transmissionRoughness);
			GUI::LayoutLabel(font, "TransmissionRoughness", 16, 20, 2, TextAnchor::eMin);
			if (GUI::LayoutSlider(transmissionRoughness, 0, 1, sliderSize, 10)) { mEditMaterial->SetParameter("TransmissionRoughness", transmissionRoughness); DirtyFrames(); }

			float ior = 1.5f;
			mEditMaterial->GetParameter<float>("IndexOfRefraction", ior);
			GUI::LayoutLabel(font, "IOR: " + to_string(ior), 16, 20, 2, TextAnchor::eMin);
			if (GUI::LayoutSlider(ior, 1, 2.5f, sliderSize, 10)) { mEditMaterial->SetParameter("IndexOfRefraction", ior); DirtyFrames(); }
			GUI::EndLayout();
		}

		GUI::EndLayout();
	}

public:
	inline int Priority() override { return 10000; }

	PLUGIN_API Raytracing() : { mEnabled = true; }
	PLUGIN_API ~Raytracing() {
		safe_delete_array(mFrameData);
	}

	PLUGIN_API bool OnSceneInit(Scene* scene) override {
		mScene = scene;
		mInput = mScene->InputManager()->GetFirst<MouseKeyboardInput>();

		mFrameData = new FrameData[mScene->Instance()->Device()->MaxFramesInFlight()];
		memset(mFrameData, 0, mScene->Instance()->Device()->MaxFramesInFlight()*sizeof(FrameData));

		mScene->DrawSkybox(false);
		mScene->AmbientLight(0.01f);
		//mScene->EnvironmentTexture(mScene->AssetManager()->LoadTexture("Assets/Textures/white.png"));
		mScene->EnvironmentTexture(mScene->AssetManager()->LoadTexture("D:/Projects/blender/Textures/_backgrounds/studio_small_02_4k.hdr", false));
		//mScene->EnvironmentTexture(mScene->AssetManager()->LoadTexture("D:/Projects/blender/Textures/_backgrounds/paul_lobe_haus_8k.hdr", false));
		
		stm_ptr<Mesh> planeMesh = Mesh::CreatePlaneY("Light", mScene->Instance()->Device(), 1, 1);
		Mesh* sphereMesh = mScene->AssetManager()->LoadMesh("Assets/sphere.obj");
		Mesh* rockMesh = mScene->AssetManager()->LoadMesh("Assets/rock.obj", 1.5f);
		Mesh* suzanneMesh = mScene->AssetManager()->LoadMesh("Assets/suzanne.obj", 1.0f);
		
	{
		stm_ptr<Material> groundMaterial = new Material("Ground", mScene->AssetManager()->LoadShader("Shaders/pbr.\stmbb"));
		groundMaterial->PassMask((PassType)(PASS_RAYTRACE));
		groundMaterial->SetParameter("BaseColor", float4(0.8f));
		groundMaterial->SetParameter("Metallic", 0.0f);
		groundMaterial->SetParameter("Roughness", 0.1f);
		groundMaterial->SetParameter("TextureST", float4(16, 16, 1, 1));
		groundMaterial->SetParameter("BaseColorTexture", mScene->AssetManager()->LoadTexture("D:/Projects/blender/Textures/Marble01/Marble01_col.jpg", false));
		groundMaterial->SetParameter("RoughnessTexture", mScene->AssetManager()->LoadTexture("D:/Projects/blender/Textures/Marble01/Marble01_rgh.jpg", false));
		groundMaterial->SetParameter("NormalTexture", mScene->AssetManager()->LoadTexture("D:/Projects/blender/Textures/Marble01/Marble01_nrm.jpg", false));
		MeshRenderer* ground = mScene->CreateObject<MeshRenderer>("Ground");
		ground->LocalScale(5.f);
		ground->Material(groundMaterial);
		ground->Mesh(planeMesh);
	}
	{
		stm_ptr<Material> glassMaterial = new Material("GlassBall", mScene->AssetManager()->LoadShader("Shaders/pbr.stmb"));
		glassMaterial->PassMask((PassType)(PASS_RAYTRACE));
		glassMaterial->SetParameter("BaseColor", float4(1.f));
		glassMaterial->SetParameter("Metallic", 0.f);
		glassMaterial->SetParameter("Roughness", 0.f);
		glassMaterial->SetParameter("TransmissionRoughness", 0.f);
		glassMaterial->SetParameter("IndexOfRefraction", 1.5f);
		glassMaterial->SetParameter("Transmission", 1.0f);
		MeshRenderer* ball = mScene->CreateObject<MeshRenderer>("GlassBall");
		ball->Material(glassMaterial);
		ball->Mesh(sphereMesh);
		ball->LocalScale(.15f);
		ball->LocalPosition(-0.5f, .15f, -0.3f);
		ball->LocalRotation(quaternion(float3(0, 5*M_PI/8, 0)));
	}
	{
		float3 rayleigh(0.65f, .57f, .475f);
		rayleigh = 1.f / (rayleigh*rayleigh*rayleigh*rayleigh)/20.f;
		
		stm_ptr<Material> crystalMaterial = make_shared<Material>("Crystal", mScene->AssetManager()->LoadShader("Shaders/pbr.stmb"));
		crystalMaterial->PassMask((PassType)(PASS_RAYTRACE));
		crystalMaterial->SetParameter("BaseColor", float4(1.f));
		crystalMaterial->SetParameter("Absorption", float4(0.3f, 0.6f, 1.0f, 1.0f));
		crystalMaterial->SetParameter("Scattering", float4(rayleigh, 1));
		crystalMaterial->SetParameter("Metallic", 0.f);
		crystalMaterial->SetParameter("Roughness", 0.f);
		crystalMaterial->SetParameter("TransmissionRoughness", 0.f);
		crystalMaterial->SetParameter("IndexOfRefraction", 1.45f);
		crystalMaterial->SetParameter("Transmission", 1.0f);
		stm_ptr<MeshRenderer> crystal = make_shared<MeshRenderer>("Crystal");
		crystal->Material(crystalMaterial);
		crystal->Mesh(rockMesh);
		crystal->LocalRotation(quaternion(float3(0, 5*M_PI/8, 0)));
		mObjects.push_back(crystal.get());
		mScene->AddObject(crystal);
	}
	{
		stm_ptr<Material> bronzeMaterial = make_shared<Material>("Bronze", mScene->AssetManager()->LoadShader("Shaders/pbr.stmb"));
		bronzeMaterial->PassMask((PassType)(PASS_RAYTRACE));
		bronzeMaterial->SetParameter("BaseColor", float4(1.f));
		bronzeMaterial->SetParameter("Metallic", 1.0f);
		bronzeMaterial->SetParameter("Roughness", 1.0f);
		bronzeMaterial->SetParameter("IndexOfRefraction", 1.180f);
		bronzeMaterial->SetParameter("BaseColorTexture", mScene->AssetManager()->LoadTexture("D:/Projects/blender/Textures/Metal008/Metal008_2K_Color.jpg", false));
		bronzeMaterial->SetParameter("RoughnessTexture", mScene->AssetManager()->LoadTexture("D:/Projects/blender/Textures/Metal008/Metal008_2K_Roughness.jpg", false));
		bronzeMaterial->SetParameter("NormalTexture", mScene->AssetManager()->LoadTexture("D:/Projects/blender/Textures/Metal008/Metal008_2K_Normal.jpg", false));
		stm_ptr<MeshRenderer> ball = make_shared<MeshRenderer>("Ball");
		ball->Material(bronzeMaterial);
		ball->Mesh(sphereMesh);
		ball->LocalScale(.2f);
		ball->LocalPosition(0.1f, .2f, 0.6f);
		mObjects.push_back(ball.get());
		mScene->AddObject(ball);
	}
	{
		stm_ptr<Material> diffuseMaterial = make_shared<Material>("Diffuse", mScene->AssetManager()->LoadShader("Shaders/pbr.stmb"));
		diffuseMaterial->PassMask((PassType)(PASS_RAYTRACE));
		diffuseMaterial->SetParameter("BaseColor", float4(0.9f, 0.8f, 0.3f, 1.f));
		diffuseMaterial->SetParameter("Metallic", 0.0f);
		diffuseMaterial->SetParameter("Roughness", 0.5f);
		diffuseMaterial->SetParameter("IndexOfRefraction", 1.45f);
		stm_ptr<MeshRenderer> suzanne = make_shared<MeshRenderer>("Suzanne");
		suzanne->Material(diffuseMaterial);
		suzanne->Mesh(suzanneMesh);
		suzanne->LocalScale(.1f);
		suzanne->LocalPosition(-0.75, 0.49478f * 0.1f, -0.75f);
		suzanne->LocalRotation(quaternion(float3(0.627540f, -M_PI / 4, 0)));
		mObjects.push_back(suzanne.get());
		mScene->AddObject(suzanne);
	}
		/*
	{
		stm_ptr<Material> lightMaterial = make_shared<Material>("Light", mScene->AssetManager()->LoadShader("Shaders/pbr.stmb"));
		lightMaterial->PassMask((PassType)(PASS_RAYTRACE));
		lightMaterial->SetParameter("BaseColor", float4(0.f));
		lightMaterial->SetParameter("Metallic", 0.f);
		lightMaterial->SetParameter("Roughness", 0.f);
		lightMaterial->SetParameter("Emission", float4(1,1,1,6));
		stm_ptr<MeshRenderer> light = make_shared<MeshRenderer>("Light");
		light->Material(lightMaterial);
		light->Mesh(planeMesh);
		light->LocalScale(.25f);
		light->LocalPosition(float3(1.0f, 0.5f, 0));
		light->LocalRotation(quaternion(float3(M_PI/2, -M_PI/2, 0)));
		mObjects.push_back(light.get());
		mScene->AddObject(light);
	}
	{
		stm_ptr<Material> lightMaterial = make_shared<Material>("Light", mScene->AssetManager()->LoadShader("Shaders/pbr.stmb"));
		lightMaterial->PassMask((PassType)(PASS_RAYTRACE));
		lightMaterial->SetParameter("BaseColor", float4(0.f));
		lightMaterial->SetParameter("Metallic", 0.f);
		lightMaterial->SetParameter("Roughness", 0.f);
		lightMaterial->SetParameter("Emission", float4(1,1,1,4));
		stm_ptr<MeshRenderer> light = make_shared<MeshRenderer>("Light");
		light->Material(lightMaterial);
		light->Mesh(planeMesh);
		light->LocalScale(.25f);
		light->LocalPosition(float3(1.0f, 0.75f, -0.5f));
		light->LocalRotation(quaternion(float3(5*M_PI/8, -3*M_PI/8, 0)));
		mObjects.push_back(light.get());
		mScene->AddObject(light);
	}
		*/

		return true;
	}

	PLUGIN_API void OnUpdate(stm_ptr<CommandBuffer> commandBuffer) override {
		if (mInput->KeyDownFirst(KEY_F11)) mShowGUI = !mShowGUI;
		if (mInput->GetPointerLast(0)->mGuiHitT < 0 && mInput->KeyDownFirst(MOUSE_LEFT)) {
			float2 uv = (mInput->CursorPos() + .5f) / float2(mInput->WindowWidth(), mInput->WindowHeight());
			Object* o = mScene->Raycast(mScene->Cameras()[0]->ScreenToWorldRay(uv), nullptr, false, PASS_RAYTRACE);
			if (MeshRenderer* r = dynamic_cast<MeshRenderer*>(o))
				mEditMaterial = r->Material();
			else
				mEditMaterial = nullptr;
		}
	}
	
	PLUGIN_API void OnLateUpdate(stm_ptr<CommandBuffer> commandBuffer) override {		
		FrameData& fd = mFrameData[commandBuffer->Device()->FrameContextIndex()];
		FrameData& pfd = mFrameData[(commandBuffer->Device()->FrameContextIndex() + (commandBuffer->Device()->MaxFramesInFlight() - 1)) % commandBuffer->Device()->MaxFramesInFlight()];

		if (fd.mLastBuild <= mScene->LastBvhBuild()) Build(commandBuffer, fd);

		fd.mResolution = uint2((float)camera->FramebufferWidth() * mSampleRate, (float)camera->FramebufferHeight() * mSampleRate);	
		
		vk::PipelineStageFlags dstStage, srcStage;

		vk::ImageMemoryBarrier barriers[3];
		if (fd.mRadiance && (fd.mRadiance->Width() != fd.mResolution.x || fd.mRadiance->Height() != fd.mResolution.y)) {
			vkDeviceWaitIdle(*mScene->Instance()->Device());
			fd.mRadiance.reset();
			fd.mPositions.reset();
			fd.mNormals.reset();
		}
		if (!fd.mRadiance) {
			fd.mRadiance = new Texture("Radiance", mScene->Instance()->Device(), fd.mResolution.x, fd.mResolution.y, 1,
				vk::Format::eR32G32B32A32Sfloat, vk::SampleCountFlagBits::e1,
				vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);
			fd.mNormals = new Texture("Normals", mScene->Instance()->Device(), fd.mResolution.x, fd.mResolution.y, 1,
				vk::Format::eR32G32B32A32Sfloat, vk::SampleCountFlagBits::e1,
				vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage);
			fd.mPositions = new Texture("Positions", mScene->Instance()->Device(), fd.mResolution.x, fd.mResolution.y, 1,
				vk::Format::eR32G32B32A32Sfloat, vk::SampleCountFlagBits::e1,
				vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage);
		
			barriers[0] = fd.mRadiance->TransitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, srcStage, dstStage);
			barriers[1] = fd.mNormals->TransitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, srcStage, dstStage);
			barriers[2] = fd.mPositions->TransitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, srcStage, dstStage);
			vkCmdPipelineBarrier(*commandBuffer,
				srcStage, vk::PipelineStageFlagBits::eComputeShader,
				0,
				0, nullptr,
				0, nullptr,
				3, barriers);
		}

		#pragma region Raytrace
		if (pfd.mRadiance) { // Need history to evaluate full trace
			ComputeShader* trace = mScene->AssetManager()->LoadShader("Shaders/raytrace.stmb")->GetCompute("Raytrace", {});
			((vk::CommandBuffer)*commandBuffer).BindPipeline(vk::PipelineBindPoint::eCompute, trace->mPipeline);

			fd.mCameraPosition = camera->WorldPosition();
			fd.mCameraRotation = camera->WorldRotation();
			fd.mCameraFoV = camera->FieldOfView();
			fd.mRandomSeed = rand();

			uint32_t vs = sizeof(StdVertex);
			float3 ambient = mScene->Environment()->AmbientLight();
			quaternion invPrevCameraRotation = inverse(pfd.mCameraRotation);
			float2 rf = float2(fd.mResolution);
			float2 prf = float2(pfd.mResolution);

			commandBuffer->PushConstant(trace, "CameraRotation", &fd.mCameraRotation);
			commandBuffer->PushConstant(trace, "CameraPosition", &fd.mCameraPosition);
			commandBuffer->PushConstant(trace, "FieldOfView", &fd.mCameraFoV);
			commandBuffer->PushConstant(trace, "Resolution", &rf);
			commandBuffer->PushConstant(trace, "HistoryTrust", &mHistoryTrust);
			commandBuffer->PushConstant(trace, "InvCameraRotationHistory", &invPrevCameraRotation);
			commandBuffer->PushConstant(trace, "CameraRotationHistory", &pfd.mCameraRotation);
			commandBuffer->PushConstant(trace, "CameraPositionHistory", &pfd.mCameraPosition);
			commandBuffer->PushConstant(trace, "FieldOfViewHistory", &pfd.mCameraFoV);
			commandBuffer->PushConstant(trace, "ResolutionHistory", &prf);
			commandBuffer->PushConstant(trace, "AmbientLight", &ambient);
			commandBuffer->PushConstant(trace, "LightCount", &fd.mLightCount);
			commandBuffer->PushConstant(trace, "MaxVolumeBounces", &mMaxVolumeBounces);
			commandBuffer->PushConstant(trace, "MaxSurfaceBounces", &mMaxSurfaceBounces);
			commandBuffer->PushConstant(trace, "VertexStride", &vs);
			commandBuffer->PushConstant(trace, "RandomSeed", &fd.mRandomSeed);

			stm_ptr<DescriptorSet> ds = commandBuffer->Device()->GetTempDescriptorSet("RT", trace->mDescriptorSetLayouts[0]);
			vk::DeviceSize bufSize = AlignUp(sizeof(CameraBuffer), commandBuffer->Device()->Limits().minUniformBufferOffsetAlignment);
			ds->CreateStorageTextureDescriptor(fd.mRadiance, trace->mDescriptorBindings.at("Radiance").second.binding);
			ds->CreateStorageTextureDescriptor(fd.mNormals, trace->mDescriptorBindings.at("Normals").second.binding);
			ds->CreateStorageTextureDescriptor(fd.mPositions, trace->mDescriptorBindings.at("Positions").second.binding);
			ds->CreateStorageTextureDescriptor(pfd.mRadiance, trace->mDescriptorBindings.at("RadianceHistory").second.binding);
			ds->CreateStorageTextureDescriptor(pfd.mNormals, trace->mDescriptorBindings.at("NormalsHistory").second.binding);
			ds->CreateStorageTextureDescriptor(pfd.mPositions, trace->mDescriptorBindings.at("PositionsHistory").second.binding);
			ds->CreateStorageBufferDescriptor(fd.mBvh, 0, fd.mBvh->Size(), trace->mDescriptorBindings.at("SceneBvh").second.binding);
			ds->CreateStorageBufferDescriptor(fd.mVertices, 0, fd.mVertices->Size(), trace->mDescriptorBindings.at("Vertices").second.binding);
			ds->CreateStorageBufferDescriptor(fd.mTriangles, 0, fd.mTriangles->Size(), trace->mDescriptorBindings.at("Triangles").second.binding);
			ds->CreateStorageBufferDescriptor(fd.mLights, 0, fd.mLights->Size(), trace->mDescriptorBindings.at("Lights").second.binding);
			ds->CreateStorageBufferDescriptor(fd.mMaterials, 0, fd.mMaterials->Size(), trace->mDescriptorBindings.at("Materials").second.binding);
			ds->CreateStorageBufferDescriptor(fd.mPrimitiveMaterials, 0, fd.mPrimitiveMaterials->Size(), trace->mDescriptorBindings.at("PrimitiveMaterials").second.binding);
			for (uint32_t i = 0; i < TEXTURE_COUNT; i++)
				if (fd.mTextures[i])
					ds->CreateSampledTextureDescriptor(fd.mTextures[i], i, trace->mDescriptorBindings.at("Textures").second.binding, vk::ImageLayout::eShaderReadOnlyOptimal);
			ds->FlushWrites();
			((vk::CommandBuffer)*commandBuffer).BindDescriptorSets(vk::PipelineBindPoint::eCompute, trace->mPipelineLayout, 0, 1, *ds, 0, nullptr);
			((vk::CommandBuffer)*commandBuffer).Dispatch((fd.mRadiance->Width() + 7) / 8, (fd.mRadiance->Height() + 7) / 8, 1);
		}
		#pragma endregion

		barriers[0] = fd.mRadiance->TransitionImageLayout(vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral, srcStage, dstStage);
		vkCmdPipelineBarrier(*commandBuffer,
			vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
			0,
			0, nullptr,
			0, nullptr,
			1, barriers);
	}

	PLUGIN_API void PreRender(stm_ptr<CommandBuffer> commandBuffer, Camera* camera) override {
		if (commandBuffer->CurrentSubpass() != "raytrace") return;

		GraphicsShader* shader = mScene->AssetManager()->LoadShader("Shaders/rtblit.stmb")->GetGraphics(PASS_MAIN, {});
		if (!shader) return;

		vk::PipelineLayout layout = commandBuffer->BindShader(shader, pass, nullptr);
		if (!layout) return;

		float4 st(1, 1, 0, 0);
		float4 tst(1, -1, 0, 1);
		float exposure = .4f;

		FrameData& fd = mFrameData[commandBuffer->Device()->FrameContextIndex()];

		commandBuffer->PushConstant(shader, "ScaleTranslate", &st);
		commandBuffer->PushConstant(shader, "TextureST", &tst);
		commandBuffer->PushConstant(shader, "Exposure", &exposure);
		commandBuffer->PushConstant(shader, "Gamma", &mGamma);
		stm_ptr<DescriptorSet> ds = commandBuffer->Device()->GetTempDescriptorSet("Blit", shader->mDescriptorSetLayouts[0]);
		ds->CreateSampledTextureDescriptor(fd.mRadiance, shader->mDescriptorBindings.at("Radiance").second.binding, vk::ImageLayout::eGeneral);
		ds->FlushWrites();
		((vk::CommandBuffer)*commandBuffer).BindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, 1, *ds, 0, nullptr);
		((vk::CommandBuffer)*commandBuffer).Draw(6, 1, 0, 0);
	}
};

ENGINE_PLUGIN(Raytracing)