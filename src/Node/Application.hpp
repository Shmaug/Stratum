#pragma once

#include <Common/dynamic_library.hpp>
#include <Core/Window.hpp>
#include "NodeGraph.hpp"

int main(int argc, char** argv);

namespace stm {

struct Camera;

class Application {
public:
	NodeEvent<> PreFrame;
	NodeEvent<CommandBuffer&, float> OnUpdate;
	NodeEvent<CommandBuffer&> OnRenderWindow;
	NodeEvent<> PostFrame;

	STRATUM_API Application(Node& node, Window& window);

	STRATUM_API void load_shaders();
	STRATUM_API void run();

	inline Node& node() const { return mNode; }
	inline Window& window() const { return mWindow; }

	component_ptr<Camera> mMainCamera;

private:
	Node& mNode;
	Window& mWindow;
};

}