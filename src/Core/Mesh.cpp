#include "Mesh.hpp"
#include "CommandBuffer.hpp"

using namespace stm;

VertexLayoutDescription::VertexLayoutDescription(const Shader& vertexShader, const VertexArrayObject& vertexData, vk::PrimitiveTopology topology, vk::IndexType indexType)
  : mTopology(topology), mIndexType(indexType) {
  struct stride_view_hash {
    inline size_t operator()(const Buffer::StrideView& v) const {
      return hash_args(v.buffer().get(), v.offset(), v.size_bytes(), v.stride());
    }
  };
  unordered_map<Buffer::StrideView, uint32_t, stride_view_hash> uniqueBuffers;
  for (const auto&[id, v] : vertexShader.stage_inputs()) {
    optional<VertexArrayObject::Attribute> attrib = vertexData.find(v.mAttributeType, v.mTypeIndex);
    if (!attrib) throw logic_error("VertexArrayObject does not contain required shader input " + to_string(v.mAttributeType) + "." + to_string(v.mTypeIndex));

    auto& dstAttribs = mAttributes[v.mAttributeType];
    if (dstAttribs.size() <= v.mTypeIndex)
      dstAttribs.resize(v.mTypeIndex + 1);

    dstAttribs[v.mTypeIndex].first = attrib->first;
    if (auto it = uniqueBuffers.find(attrib->second); it != uniqueBuffers.end())
      dstAttribs[v.mTypeIndex].second = it->second;
    else {
      dstAttribs[v.mTypeIndex].second =  (uint32_t)uniqueBuffers.size();
      uniqueBuffers.emplace(attrib->second, dstAttribs[v.mTypeIndex].second);
    }
  }
}

void VertexArrayObject::bind(CommandBuffer& commandBuffer) const {
  auto pipeline = dynamic_pointer_cast<GraphicsPipeline>(commandBuffer.bound_pipeline());
  if (!pipeline) throw logic_error("cannot draw a mesh without a bound graphics pipeline\n");

  for (const auto&[type, attributes] : dynamic_cast<GraphicsPipeline*>(commandBuffer.bound_pipeline().get())->vertex_layout().mAttributes)
    for (uint32_t i = 0; i < attributes.size(); i++)
      commandBuffer.bind_vertex_buffer(attributes[i].second, mAttributes.at(type)[i].second);
}
void Mesh::bind(CommandBuffer& commandBuffer) const {
  mVertices->bind(commandBuffer);
  commandBuffer.bind_index_buffer(mIndices);
}