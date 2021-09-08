#pragma once
#include <Core/PipelineState.hpp>
#include "NodeGraph.hpp"

#include <imgui.h>

namespace stm {

class Gui {
	
public:	
	STRATUM_API Gui(Node& node);
	STRATUM_API ~Gui();

	inline Node& node() const { return mNode; }
	
	inline void set_context() const {
		ImGui::SetCurrentContext(mContext);
		ImGui::SetAllocatorFunctions(
			[](size_t sz, void* user_data) { return ::operator new(sz); },
			[](void* ptr, void* user_data) { ::operator delete(ptr); });
	}
	
private:
	Node& mNode;
	component_ptr<PipelineState> mPipeline;
	Mesh mMesh;
	bool mUploadFonts = true;
	ImGuiContext* mContext;
	const ImDrawData* mDrawData;
};

} 