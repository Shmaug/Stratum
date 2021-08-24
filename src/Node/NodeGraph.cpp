#include "NodeGraph.hpp"

using namespace stm;

Node& NodeGraph::emplace(const string& name) {
  auto node = make_unique<Node>(*this, name);
  Node* ptr = node.get();
  mNodes.emplace(ptr, move(node));
  return *ptr;
}
void NodeGraph::erase(Node& node) {
  while (true) {
    auto[first,last] = mEdges.equal_range(&node);
    if (first == last) break;
    first->second->clear_parent();
  }
  node.clear_parent();
  mNodes.erase(&node);
}
void NodeGraph::erase_recurse(Node& node) {
  stack<Node*> nodes;
  stack<Node*> todo;
  todo.push(&node);
  while (!todo.empty()) {
    nodes.push(todo.top());
    auto[first,last] = mEdges.equal_range(todo.top());
    todo.pop();
    for (auto[_,n] : ranges::subrange(first,last))
      todo.push(n);
  }
  while (!nodes.empty()) {
    erase(*nodes.top());
    nodes.pop();
  }
}

Node::~Node() {
  clear_parent();
  for (auto edge_it = mNodeGraph.mEdges.find(this); edge_it != mNodeGraph.mEdges.end(); ) {
    edge_it->second->clear_parent();
    edge_it = mNodeGraph.mEdges.find(this);
  }
  for (type_index t : mComponents)
    mNodeGraph.mComponentMap.at(t).erase(this);
}

void Node::clear_parent() {
  if (mParent) {
    mParent->OnChildRemoving(*this);
    auto[first,last] = mNodeGraph.mEdges.equal_range(mParent);
    mNodeGraph.mEdges.erase(ranges::find(first, last, this, &unordered_multimap<const Node*, Node*>::value_type::second));
    mParent = nullptr;
    OnParentChanged();
  }
}
void Node::set_parent(Node& parent) {
  if (mParent) { 
    if (mParent == &parent) return;
    // remove from parent
    mParent->OnChildRemoving(*this);
    auto[first,last] = mNodeGraph.mEdges.equal_range(mParent);
    mNodeGraph.mEdges.erase(ranges::find(first, last, this, &unordered_multimap<const Node*, Node*>::value_type::second));
  }
  mNodeGraph.mEdges.emplace(&parent, this);
  mParent = &parent;
  parent.OnChildAdded(*this);
  OnParentChanged();
}