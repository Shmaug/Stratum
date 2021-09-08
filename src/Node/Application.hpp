#include <Common/dynamic_library.hpp>
#include <Core/Window.hpp>
#include "DynamicRenderPass.hpp"

int main(int argc, char** argv);

namespace stm {

class Application {
public:
	NodeEvent<> PreFrame;
	NodeEvent<CommandBuffer&, float> OnUpdate;

	STRATUM_API Application(Node& node, Window& window);

	inline Node& node() const { return mNode; }
	inline Window& window() const { return mWindow; }
	inline const auto& render_pass() const { return mMainPass; }

private:
	Node& mNode;
	Window& mWindow;
	component_ptr<DynamicRenderPass> mWindowRenderNode;
	shared_ptr<DynamicRenderPass::Subpass> mMainPass;

	friend int ::main(int argc, char** argv);
	STRATUM_API void loop();
};

}