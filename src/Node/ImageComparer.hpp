#pragma once

#include "Gui.hpp"

namespace stm {

class ImageComparer {
public:
	Node& mNode;

	STRATUM_API ImageComparer(Node& node);
	STRATUM_API void inspector_gui();

private:
	unordered_map<string, pair<Image::View, Image::View>> mImages;
	unordered_set<string> mComparing;
	string mCurrent;
	float2 mOffset;
	float mZoom;
};

}