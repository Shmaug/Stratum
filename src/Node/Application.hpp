#pragma once

#include <Common/dynamic_library.hpp>
#include <Core/Window.hpp>

#include "DynamicRenderPass.hpp"
#include "Scene.hpp"

int main(int argc, char** argv);

namespace stm {

class Application {
public:
	NodeEvent<> PreFrame;
	NodeEvent<CommandBuffer&, float> OnUpdate;

	STRATUM_API Application(Node& node, Window& window);

	STRATUM_API void load_shaders();

	inline Node& node() const { return mNode; }
	inline Window& window() const { return mWindow; }
	inline const auto& main_pass() const { return mMainPass; }
	inline void main_camera(const component_ptr<Camera>& c) { mMainCamera = c; }
	inline const component_ptr<Camera>& main_camera() const { return mMainCamera; }

	inline vk::ImageUsageFlags& back_buffer_usage() { return mBackBufferUsage; }
	inline const vk::ImageUsageFlags& back_buffer_usage() const { return mBackBufferUsage; }
	inline vk::ImageUsageFlags& depth_buffer_usage() { return mDepthBufferUsage; }
	inline const vk::ImageUsageFlags& depth_buffer_usage() const { return mDepthBufferUsage; }

private:
	Node& mNode;
	Window& mWindow;
	component_ptr<DynamicRenderPass> mWindowRenderNode;
	shared_ptr<DynamicRenderPass::Subpass> mMainPass;
	component_ptr<Camera> mMainCamera;
	vk::ImageUsageFlags mBackBufferUsage = vk::ImageUsageFlagBits::eColorAttachment|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst;
	vk::ImageUsageFlags mDepthBufferUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst;

	friend int ::main(int argc, char** argv);
	STRATUM_API void loop();
};

}