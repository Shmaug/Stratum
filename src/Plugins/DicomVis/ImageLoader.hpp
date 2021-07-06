#pragma once

#include "../../Core/CommandBuffer.hpp"

namespace dcmvs {

using namespace stm;

PLUGIN_API Texture::View load_dicom(const fs::path& folder, CommandBuffer& commandBuffer, Array3f* voxelSize);
PLUGIN_API Texture::View load_stbi(const fs::path& folder, CommandBuffer& commandBuffer, bool reverse, uint32_t channelCount, bool isInteger, bool isSigned);
PLUGIN_API Texture::View load_raw(const fs::path& folder, CommandBuffer& commandBuffer, vk::Extent2D sliceExtent, vk::Format format);
}