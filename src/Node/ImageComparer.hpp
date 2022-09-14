#pragma once

#include "Gui.hpp"

namespace stm {

class ImageComparer {
public:
	Node& mNode;

	STRATUM_API ImageComparer(Node& node);
	STRATUM_API void inspector_gui();

private:
	shared_ptr<ComputePipelineState> mImageComparePipeline;
	unordered_map<string, Buffer::View<uint32_t>> mMSE;
	uint32_t mMSEMode = 0;
	uint32_t mMSEQuantization = 1024;

	// stores original image object, and a copy
	unordered_map<string, pair<Image::View, Image::View>> mImages;
	unordered_set<string> mComparing;
	string mCurrent;
	float2 mOffset;
	float mZoom;
};

}