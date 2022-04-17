#include "AccelerationStructure.hpp"

namespace stm {

AccelerationStructure::AccelerationStructure(CommandBuffer& commandBuffer, const string& name, vk::AccelerationStructureTypeKHR type, const vk::ArrayProxyNoTemporaries<const vk::AccelerationStructureGeometryKHR>& geometries,  const vk::ArrayProxyNoTemporaries<const vk::AccelerationStructureBuildRangeInfoKHR>& buildRanges) : DeviceResource(commandBuffer.mDevice, name) {
	vk::AccelerationStructureBuildGeometryInfoKHR buildGeometry(type,vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace, vk::BuildAccelerationStructureModeKHR::eBuild);
	buildGeometry.setGeometries(geometries);

	vk::AccelerationStructureBuildSizesInfoKHR buildSizes;
	if (buildRanges.size() > 0 && buildRanges.front().primitiveCount > 0) {
		vector<uint32_t> counts((uint32_t)geometries.size());
		for (uint32_t i = 0; i < geometries.size(); i++)
			counts[i] = (buildRanges.data() + i)->primitiveCount;
		buildSizes = commandBuffer.mDevice->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometry, counts);
	} else
		buildSizes.accelerationStructureSize = buildSizes.buildScratchSize = 4;

	mBuffer = make_shared<Buffer>(commandBuffer.mDevice, name, buildSizes.accelerationStructureSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress);
	Buffer::View<byte> scratchBuf = make_shared<Buffer>(commandBuffer.mDevice, name + "/ScratchBuffer", buildSizes.buildScratchSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress|vk::BufferUsageFlagBits::eStorageBuffer);

	mAccelerationStructure = commandBuffer.mDevice->createAccelerationStructureKHR(vk::AccelerationStructureCreateInfoKHR({}, **mBuffer.buffer(), mBuffer.offset(), mBuffer.size_bytes(), type));

	buildGeometry.dstAccelerationStructure = mAccelerationStructure;
	buildGeometry.scratchData = scratchBuf.device_address();
	commandBuffer->buildAccelerationStructuresKHR(buildGeometry, buildRanges.data());
	commandBuffer.hold_resource(scratchBuf);
}
AccelerationStructure::~AccelerationStructure() {
	if (mAccelerationStructure)
		mDevice->destroyAccelerationStructureKHR(mAccelerationStructure);
}

}