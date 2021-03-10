#pragma once

#include "../Core/InputState.hpp"
#include "SceneNode.hpp"

namespace stm {

class Input : public SceneNode::Component {
public:
  inline Input(SceneNode& node, const string& name) : SceneNode::Component(node, name) {}

  inline void Update(CommandBuffer& commandBuffer) {
    mInputStatesPrevious = mInputStates;
    mInputStates.clear();
  }

private:
	unordered_map<string, InputState> mInputStates;
	unordered_map<string, InputState> mInputStatesPrevious;
};

}
