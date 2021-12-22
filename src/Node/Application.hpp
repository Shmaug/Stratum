#pragma once

#include <Common/dynamic_library.hpp>
#include <Core/Window.hpp>
#include "Scene.hpp"

int main(int argc, char** argv);

namespace stm {

class Application {
public:
	NodeEvent<> PreFrame;
	NodeEvent<CommandBuffer&, float> OnUpdate;
	NodeEvent<CommandBuffer&> OnRenderWindow;

	STRATUM_API Application(Node& node, Window& window);

	STRATUM_API void load_shaders();

	inline Node& node() const { return mNode; }
	inline Window& window() const { return mWindow; }
	inline void main_camera(const component_ptr<Camera>& c) { mMainCamera = c; }
	inline const component_ptr<Camera>& main_camera() const { return mMainCamera; }

private:
	Node& mNode;
	Window& mWindow;
	component_ptr<Camera> mMainCamera;

	friend int ::main(int argc, char** argv);
	STRATUM_API void loop();
};

}