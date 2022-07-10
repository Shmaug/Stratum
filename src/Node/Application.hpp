#pragma once

#include <Common/dynamic_library.hpp>
#include <Core/Window.hpp>
#include "NodeGraph.hpp"

int main(int argc, char** argv);

namespace stm {

class Application {
public:
	Node::Event<> PreFrame;
	Node::Event<CommandBuffer&, float> OnUpdate;
	Node::Event<CommandBuffer&> OnRenderWindow;
	Node::Event<> PostFrame;

	inline Application(Node& node, Window& window) : mNode(node), mWindow(window) {}

	STRATUM_API void load_shaders();
	STRATUM_API void run();

	inline Node& node() const { return mNode; }
	inline Window& window() const { return mWindow; }

private:
	Node& mNode;
	Window& mWindow;
};

}