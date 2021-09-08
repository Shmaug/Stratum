#include "NodeGraph.hpp"

using namespace stm;

Node& NodeGraph::emplace(const string& name) {
  Node* ptr = new Node(*this, name);
  mNodes.emplace(ptr, ptr);
  return *ptr;
}
void NodeGraph::erase_recurse(Node& node) {
  list<Node*> nodes;
  stack<Node*> todo;
  todo.push(&node);
  while (!todo.empty()) {
    nodes.push_back(todo.top());
    auto[first,last] = mEdges.equal_range(todo.top());
    todo.pop();
    for (auto[_,n] : ranges::subrange(first,last))
      todo.push(n);
  }
  for (Node* n : nodes|views::reverse)
    erase(*n);
}

Node::~Node() {
  auto[first,last] = mNodeGraph.mEdges.equal_range(this);
  for (const auto&[_,child] : ranges::subrange(first, last))
    if (mParent)
      child->set_parent(*mParent);
    else
      child->clear_parent();
  
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