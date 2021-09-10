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

	inline vk::ImageUsageFlags& back_buffer_usage() { return mBackBufferUsage; }
	inline const vk::ImageUsageFlags& back_buffer_usage() const { return mBackBufferUsage; }
	inline vk::ImageUsageFlags& depth_buffer_usage() { return mDepthBufferUsage; }
	inline const vk::ImageUsageFlags& depth_buffer_usage() const { return mDepthBufferUsage; }

private:
	Node& mNode;
	Window& mWindow;
	component_ptr<DynamicRenderPass> mWindowRenderNode;
	shared_ptr<DynamicRenderPass::Subpass> mMainPass;
	vk::ImageUsageFlags mBackBufferUsage = vk::ImageUsageFlagBits::eColorAttachment;
	vk::ImageUsageFlags mDepthBufferUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment;

	friend int ::main(int argc, char** argv);
	STRATUM_API void loop();
};

}