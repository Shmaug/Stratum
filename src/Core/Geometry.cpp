#include "Geometry.hpp"
#include "CommandBuffer.hpp"

using namespace stm;

void Geometry::bind(CommandBuffer& commandBuffer) const {
  auto pipeline = dynamic_pointer_cast<GraphicsPipeline>(commandBuffer.bound_pipeline());
  if (!pipeline) throw logic_error("cannot draw a mesh without a bound graphics pipeline\n");
  
  unordered_set<Buffer::View<byte>, buffer_view_hash<byte>> uniqueBuffers;
  const auto& shader_inputs = commandBuffer.bound_pipeline()->stages().front().spirv()->stage_inputs();
  for (const auto&[id, v] : shader_inputs) {
    auto it = mAttributes.find(v.mAttributeType);
    if (it == mAttributes.end() || it->second.size() <= v.mTypeIndex) throw logic_error("Geometry does not contain required shader input " + to_string(v.mAttributeType) + "." + to_string(v.mTypeIndex));
        
    const auto& b = it->second[v.mTypeIndex].buffer();
    commandBuffer.bind_vertex_buffer((uint32_t)uniqueBuffers.size(), b);
    uniqueBuffers.emplace(b);
  }
}

void Geometry::draw(CommandBuffer& commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) const {
  bind(commandBuffer);
  commandBuffer->draw(vertexCount, instanceCount, firstVertex, firstInstance);
  switch (mTopology) {
    default:
    case vk::PrimitiveTopology::ePatchList:
    case vk::PrimitiveTopology::ePointList:
      commandBuffer.mPrimitiveCount += instanceCount * vertexCount;
      break;
    case vk::PrimitiveTopology::eLineList:
    case vk::PrimitiveTopology::eLineStrip:
    case vk::PrimitiveTopology::eLineListWithAdjacency:
    case vk::PrimitiveTopology::eLineStripWithAdjacency:
      commandBuffer.mPrimitiveCount += instanceCount * vertexCount/2;
      break;
    case vk::PrimitiveTopology::eTriangleList:
    case vk::PrimitiveTopology::eTriangleStrip:
    case vk::PrimitiveTopology::eTriangleFan:
    case vk::PrimitiveTopology::eTriangleListWithAdjacency:
    case vk::PrimitiveTopology::eTriangleStripWithAdjacency:
      commandBuffer.mPrimitiveCount += instanceCount * vertexCount/3;
      break;
  }
}
void Geometry::drawIndexed(CommandBuffer& commandBuffer, const Buffer::StrideView& indices, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance) const {
  bind(commandBuffer);
  commandBuffer.bind_index_buffer(indices);
  commandBuffer->drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
  switch (mTopology) {
    default:
    case vk::PrimitiveTopology::ePatchList:
    case vk::PrimitiveTopology::ePointList:
      commandBuffer.mPrimitiveCount += instanceCount * indexCount;
      break;
    case vk::PrimitiveTopology::eLineList:
    case vk::PrimitiveTopology::eLineStrip:
    case vk::PrimitiveTopology::eLineListWithAdjacency:
    case vk::PrimitiveTopology::eLineStripWithAdjacency:
      commandBuffer.mPrimitiveCount += instanceCount * indexCount/2;
      break;
    case vk::PrimitiveTopology::eTriangleList:
    case vk::PrimitiveTopology::eTriangleStrip:
    case vk::PrimitiveTopology::eTriangleFan:
    case vk::PrimitiveTopology::eTriangleListWithAdjacency:
    case vk::PrimitiveTopology::eTriangleStripWithAdjacency:
      commandBuffer.mPrimitiveCount += instanceCount * indexCount/3;
      break;
  }
}