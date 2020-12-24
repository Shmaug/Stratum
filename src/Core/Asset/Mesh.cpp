#include "Mesh.hpp"

#include "../CommandBuffer.hpp"


using namespace stm;

Mesh::VertexStageInput Mesh::CreateInput(const SpirvModule& vertexStage) {
	VertexStageInput result;
	for (const auto& [varName, input] : vertexStage.mStageInputs) {
		for (const auto& [attribId, attrib] : mVertexAttributes) {
			if (attribId.mType == input.mType && attribId.mTypeIndex == input.mTypeIndex) {
				// vertex stage accepts attrib, create an attribute description
				vk::VertexInputAttributeDescription a(input.mLocation, (uint32_t)result.mBindings.size(), input.mFormat, (uint32_t)attrib.mElementOffset);
				for (uint32_t j = 0; j < result.mBindings.size(); j++)
					if (result.mBindings[j] == attrib) {
						a.binding = j;
						break;
					}
				result.mAttributes.push_back(a);
				if (a.binding == result.mBindings.size()) result.mBindings.push_back(attrib);
			}
		}
	}
	return result;
}