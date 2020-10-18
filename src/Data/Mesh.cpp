#include "Mesh.hpp"

#include "../Core/CommandBuffer.hpp"
#include "../Scene/Scene.hpp"
#include "../Scene/Camera.hpp"
#include "../Scene/TriangleBvh2.hpp"

using namespace std;
using namespace stm;

vk::PipelineVertexInputStateCreateInfo Mesh::PipelineInput(GraphicsPipeline* pipeline) {
	if (mPipelineInputs.count(pipeline)) return mPipelineInputs.at(pipeline).mCreateInfo;

	const SpirvModule* vertexStage = nullptr;
	for (const SpirvModule& m : pipeline->mShaderVariant->mModules)
		if (m.mStage == vk::ShaderStageFlagBits::eVertex)
			vertexStage = &m;

	if (!vertexStage) throw invalid_argument("pipeline does not have a vertex stage.");

	auto& p = mPipelineInputs[pipeline];
	for (const auto& [varName, input] : vertexStage->mInputs) {
		for (const auto& [idx,attrib] : mVertexAttributes) {
			if (attrib.mType == input.mType && attrib.mTypeIndex == input.mTypeIndex) {
				vk::VertexInputAttributeDescription attribute(input.mLocation, (uint32_t)p.mBindingDescriptions.size(), input.mFormat, (uint32_t)attrib.mElementOffset);
				vk::VertexInputBindingDescription bufferBinding((uint32_t)p.mBindingDescriptions.size(), (uint32_t)attrib.mBufferView.mElementSize, attrib.mInputRate);
				for (uint32_t j = 0; j < p.mBindingDescriptions.size(); j++)
					if (p.mBindingDescriptions[j] == bufferBinding) {
						attribute.binding = j;
						break;
					}
				if (attribute.binding == p.mBindingDescriptions.size())
					p.mBindingDescriptions.push_back(bufferBinding);
				p.mAttributeDescriptions.push_back(attribute);
			}
		}
	}
	p.mCreateInfo = vk::PipelineVertexInputStateCreateInfo({}, p.mBindingDescriptions, p.mAttributeDescriptions);
	return p.mCreateInfo;
}

void Mesh::Draw(CommandBuffer& commandBuffer, Camera* camera, uint32_t instanceCount, uint32_t firstInstance) {
	GraphicsPipeline::Instance* pipeline = commandBuffer.CurrentPipelineInstance();
	for (uint32_t i = 0; i < ; i++)
		if (VertexAttribute* v = GetAttribute())
			commandBuffer.BindVertexBuffer(*v->mBufferView.mBuffer, i);
	if (mIndexBuffer.mBuffer)
		commandBuffer.BindIndexBuffer(mIndexBuffer);

	if (camera) camera->SetViewportScissor(commandBuffer, StereoEye::eLeft);

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
		commandBuffer.mTriangleCount += instanceCount * (vertexDrawCount / 3);
	else if (mTopology == vk::PrimitiveTopology::eTriangleStrip)
		commandBuffer.mTriangleCount += instanceCount * (vertexDrawCount - 2);
	else if (mTopology == vk::PrimitiveTopology::eTriangleFan)
		commandBuffer.mTriangleCount += instanceCount * (vertexDrawCount - 1);
}
void Mesh::Submesh::Draw(CommandBuffer& commandBuffer, uint32_t instanceCount, uint32_t firstInstance) {
	if (mIndexCount)
		commandBuffer->drawIndexed(mIndexCount, instanceCount, mBaseIndex, mBaseVertex, firstInstance);
	else
		commandBuffer->draw(mVertexCount, instanceCount, mBaseVertex, firstInstance);
}