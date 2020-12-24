#pragma once

#include <Asset/Texture.hpp>

namespace dcmvs {

using namespace stm;

enum ImageStackType {
	eNone,
	eDicom,
	eRaw,
	eStandard,
};

class ImageLoader {
public:
	PLUGIN_EXPORT static ImageStackType FolderStackType(const fs::path& folder);
	// Load a stack of RAW images
	// Load a stack of normal images (png, jpg, tiff, etc..)
	// Items are sorted in order of name
	PLUGIN_EXPORT static shared_ptr<Texture> LoadStandardStack(const fs::path& folder, Device& device, float3* size, bool reverse = false, uint32_t channelCount = 0, bool unorm = true);
	// Load a stack of RAW images
	// PLUGIN_EXPORT static Texture* LoadRawStack(const string& folder, Device& device, float3* size);
	PLUGIN_EXPORT static shared_ptr<Texture> LoadDicomStack(const fs::path& folder, Device& device, float3* size);
	// Load a stack of raw, uncompressed images
	// Items are sorted in order of name
	PLUGIN_EXPORT static shared_ptr<Texture> LoadRawStack(const fs::path& folder, Device& device, float3* size);
};

}