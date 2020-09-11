#include <Data/Mesh.hpp>
#include <Core/CommandBuffer.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Camera.hpp>
#include <Scene/TriangleBvh2.hpp>

#pragma warning(disable:26451)

using namespace std;

void Mesh::SetAttribute(VertexAttributeType type, uint32_t typeIndex, const BufferView& bufferView, uint32_t elementOffset, uint32_t elementStride, vk::VertexInputRate inputRate) {
	mPipelineInputs.clear();

	VertexAttribute attribute;
	attribute.mBufferView = bufferView;
	attribute.mType = type;
	attribute.mTypeIndex = typeIndex;
	attribute.mElementOffset = elementOffset;
	attribute.mElementStride = elementStride;
	attribute.mInputRate = inputRate;
	mVertexAttributes.push_back(attribute);
}

vk::PipelineVertexInputStateCreateInfo Mesh::PipelineInput(GraphicsPipeline* pipeline) {
	if (mPipelineInputs.count(pipeline)) return mPipelineInputs.at(pipeline).mCreateInfo;

	const SpirvModule* vertexStage = nullptr;
	for (const SpirvModule& m : pipeline->mShaderVariant->mModules)
		if (m.mStage == vk::ShaderStageFlagBits::eVertex)
			vertexStage = &m;

	if (!vertexStage) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "Error: Pipeline %s does not have a vertex stage.", pipeline->mName.c_str());
		throw;
	}

	auto& p = mPipelineInputs[pipeline];
	vector<vk::VertexInputBindingDescription>& bindingDescriptions = p.mBindingDescriptions;
	vector<vk::VertexInputAttributeDescription>& attributeDescriptions = p.mAttributeDescriptions;
	for (const auto& input : vertexStage->mInputs) {
		for (uint32_t i = 0; i < mVertexAttributes.size(); i++) {
			if (mVertexAttributes[i].mType == input.second.mType && mVertexAttributes[i].mTypeIndex == input.second.mTypeIndex) {
				vk::VertexInputAttributeDescription attribute;
				attribute.location = input.second.mLocation;
				attribute.binding = (uint32_t)bindingDescriptions.size();
				attribute.format = input.second.mFormat;
				attribute.offset = mVertexAttributes[i].mElementOffset;
			
				vk::VertexInputBindingDescription desc;
				desc.binding = (uint32_t)bindingDescriptions.size();
				desc.stride = mVertexAttributes[i].mElementStride;
				desc.inputRate = mVertexAttributes[i].mInputRate;
				for (uint32_t j = 0; j < bindingDescriptions.size(); j++)
					if (bindingDescriptions[j] == desc) {
						attribute.binding = j;
						break;
					}
				if (attribute.binding == bindingDescriptions.size())
					bindingDescriptions.push_back(desc);
					
				attributeDescriptions.push_back(attribute);
			}
		}
	}
	p.mCreateInfo = vk::PipelineVertexInputStateCreateInfo({}, bindingDescriptions, attributeDescriptions);
	return p.mCreateInfo;
}

void Mesh::Draw(stm_ptr<CommandBuffer> commandBuffer, Camera* camera, uint32_t instanceCount, uint32_t firstInstance) {
	for (uint32_t i = 0; i < mVertexAttributes.size(); i++)
		if (mVertexAttributes[i].mBufferView.mBuffer)
			commandBuffer->BindVertexBuffer(mVertexAttributes[i].mBufferView, i);
	if (mIndexBuffer.mBuffer)
		commandBuffer->BindIndexBuffer(mIndexBuffer, mIndexType);

	if (camera) {
		if (camera->Scene()) camera->Scene()->PushSceneConstants(commandBuffer);
		camera->SetViewportScissor(commandBuffer, StereoEye::eLeft);
	}

	uint32_t vertexDrawCount = 0;
	for (auto& submesh : mSubmeshes) {
		submesh.Draw(commandBuffer, instanceCount, firstInstance);
		vertexDrawCount += submesh.mIndexCount ? submesh.mIndexCount : submesh.mVertexCount;
	}
	if (camera && camera->StereoMode() != StereoMode::eNone) {
		camera->SetViewportScissor(commandBuffer, StereoEye::eRight);
		for (auto& submesh : mSubmeshes) submesh.Draw(commandBuffer, instanceCount, firstInstance);
		vertexDrawCount *= 2;
	}

	if (mTopology == vk::PrimitiveTopology::eTriangleList)
		commandBuffer->mTriangleCount += instanceCount * (vertexDrawCount / 3);
	else if (mTopology == vk::PrimitiveTopology::eTriangleStrip)
		commandBuffer->mTriangleCount += instanceCount * (vertexDrawCount - 2);
	else if (mTopology == vk::PrimitiveTopology::eTriangleFan)
		commandBuffer->mTriangleCount += instanceCount * (vertexDrawCount - 1);
}
void Mesh::Submesh::Draw(stm_ptr<CommandBuffer> commandBuffer, uint32_t instanceCount, uint32_t firstInstance) {
	if (mIndexCount)
		((vk::CommandBuffer)*commandBuffer).drawIndexed(mIndexCount, instanceCount, mBaseIndex, mBaseVertex, firstInstance);
	else
		((vk::CommandBuffer)*commandBuffer).draw(mVertexCount, instanceCount, mBaseVertex, firstInstance);
}