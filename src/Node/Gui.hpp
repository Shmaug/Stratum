#pragma once

#include "NodeGraph.hpp"
#include <Core/PipelineState.hpp>

#include <imgui.h>

namespace stm {

class Gui {
private:
	Node& mNode;
	shared_ptr<PipelineState> mPipeline;
	Geometry mGeometry;
	Buffer::View<ImDrawIdx>  mIndices;
	ImGuiContext* mContext;
	const ImDrawData* mDrawData;
	
public:
  NodeEvent<CommandBuffer&> OnGui;
	
	STRATUM_API Gui(Node& node);
	STRATUM_API ~Gui();

	inline Node& node() const { return mNode; }
	
	inline void set_context() const {
		ImGui::SetCurrentContext(mContext);
		ImGui::SetAllocatorFunctions(
			[](size_t sz, void* user_data) { return ::operator new(sz); },
			[](void* ptr, void* user_data) { ::operator delete(ptr); });
	}

	STRATUM_API void create_textures(CommandBuffer& commandBuffer);

	STRATUM_API void new_frame(const Window& window, float deltaTime) const;
	STRATUM_API void render_gui(CommandBuffer& commandBuffer);
	STRATUM_API void draw(CommandBuffer& commandBuffer) const;
};

} 