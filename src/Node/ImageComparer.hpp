#pragma once

#include "Gui.hpp"

namespace stm {

class ImageComparer {
public:
	Node& mNode;

	STRATUM_API ImageComparer(Node& node);
	STRATUM_API void inspector_gui();

private:
	unordered_map<string, Image::View> mImages;
	unordered_set<string> mComparing;
};

}