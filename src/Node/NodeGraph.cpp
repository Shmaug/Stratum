#include "NodeGraph.hpp"

using namespace stm;

Node& NodeGraph::emplace(const string& name) {
	Node* ptr = new Node(*this, name);
	mNodes.emplace(ptr, ptr);
	return *ptr;
}

Node::~Node() {
	for (auto edge_it = mNodeGraph.mEdges.find(this); edge_it != mNodeGraph.mEdges.end(); ) {
		if (mParent)
			edge_it->second->set_parent(*mParent);
		else
			edge_it->second->clear_parent();

		edge_it = mNodeGraph.mEdges.find(this);
	}
	clear_parent();

	for (type_index t : mComponents)
		mNodeGraph.mComponentMap.at(t).erase(this);
}

void Node::clear_parent() {
	if (mParent) {
		mParent->OnChildRemoving(*this);
		auto [first, last] = mNodeGraph.mEdges.equal_range(mParent);
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
		auto [first, last] = mNodeGraph.mEdges.equal_range(mParent);
		mNodeGraph.mEdges.erase(ranges::find(first, last, this, &unordered_multimap<const Node*, Node*>::value_type::second));
	}
	mNodeGraph.mEdges.emplace(&parent, this);
	mParent = &parent;
	parent.OnChildAdded(*this);
	OnParentChanged();
}