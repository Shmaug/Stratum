#pragma once

#include "..\Stratum.hpp"

namespace stm {

template<typename... Args>
class Delegate {
private:
	list<function<void(Args...)>> mCallbacks;
public:
	inline bool empty() { mCallbacks.empty(); }
	inline void clear() { mCallbacks.clear(); }
	template<invocable<Args...> F>
	inline void operator+=(F&& f) { mCallbacks.emplace_back(f); }
	template<invocable<Args...> F>
	inline void operator-=(F&& f) { mCallbacks.erase(f); }
	inline void operator()(Args&&... args) const {
		for (const auto& f : mCallbacks)
			invoke(f, forward<Args>(args)...);
	}
};

class Scene {
public:
	class Node {
	public:
		Scene& mScene;
		string mName;
		
		Delegate<> OnChangeParent;
		Delegate<Node*> OnAddChild;
		Delegate<Node*> OnRemoveChild;

		inline Node(Scene& scene, const string& name) : mName(name), mScene(scene) {}
		virtual ~Node() = default;
		inline const string& Name() const { return mName; }
	};
	
private:
	set<unique_ptr<Node>> mNodes;
	unordered_map<type_index, vector<Node*>> mByType;
	unordered_map<Node*, Node*> mParentEdges; // child -> parent
	unordered_multimap<Node*, Node*> mChildrenEdges; // parent -> child

public:
	template<derived_from<Node> T>
	inline auto find_type() const { return ranges::subrange(mByType.find(typeid(T)), mByType.end()); }
	inline Node* parent(Node& node) const {
		auto it = mParentEdges.find(&node);
		return it == mParentEdges.end() ? nullptr : it->second;
	}
	inline auto children(Node& node) const { return ranges::subrange(mChildrenEdges.find(&node), mChildrenEdges.end()) | views::values; }
	
	inline vector<Node*> descendants(Node& node) const {
		vector<Node*> r;
		queue<Node*> todo;
		todo.push(&node);
		while (!todo.empty()) {
			size_t n = mChildrenEdges.count(todo.front());
			if (n == 0) continue;
			auto c = children(node);
			todo.pop();
			r.resize(r.size() + n);
			ranges::copy(c, r.end()-n);
			ranges::for_each(c, [&](Node* x){ todo.push(x); });
		}
		return r;
	}

	class ancestor_iterator {
	private:
		Node* mNode;
	public:
		using value_type = Node;
		using pointer = value_type*;
		using reference = value_type&;

		inline ancestor_iterator() : mNode(nullptr) {}
		inline ancestor_iterator(Node* node) : mNode(node) {}
		ancestor_iterator(const ancestor_iterator&) = default;

		bool operator==(const ancestor_iterator& rhs) const = default;
		bool operator!=(const ancestor_iterator& rhs) const = default;
		inline operator bool() const { return mNode != nullptr; }
		inline reference operator*() const { return *mNode; }
		inline pointer operator->() const { return mNode; }

		inline ancestor_iterator& operator++() {
			mNode = mNode->mScene.parent(*mNode);
			return *this;
		}
		inline ancestor_iterator operator++(int) {
			ancestor_iterator tmp(*this);
			operator++();
			return tmp;
		}
	};
	inline auto ancestors(Node& node) const { return ranges::subrange(ancestor_iterator(&node), ancestor_iterator()); }

	template<derived_from<Node> T, typename... Args> requires(constructible_from<T,Args...>)
	inline void emplace(Args&&... args) {
		mByType[type_index(typeid(T))].emplace_back(
			mNodes.emplace(make_unique<T>( new T(args...) )).first->get() );
	}
	
	template<derived_from<Node> T>
	inline bool erase(Node& node) {
		auto it = ranges::find(mNodes, &node, &unique_ptr<Node>::get);
		if (it == mNodes.end()) return false;

		// remove children
		for (auto child_it = mChildrenEdges.find(&node); child_it != mChildrenEdges.end(); child_it++) {
			mParentEdges.erase(child_it->second);
			mChildrenEdges.erase(child_it);
			node->OnRemoveChild(*child_it);
			(*child_it)->OnChangeParent();
		}
		// remove from parent
		auto parent_it = mParentEdges.find(&node);
		if (parent_it != mParentEdges.end()) {
			mParentEdges.erase(parent_it);
			mChildrenEdges.erase(ranges::find(mChildrenEdges.find(*parent_it), mChildrenEdges.end(), &node));
			(*parent_it)->OnRemoveChild(node);
			node->OnChangeParent();
		}

		// remove from mByType by indexing the type directly if possible, otherwise
		auto t = mByType.find(type_index(typeid(T)));
		if (t == mByType.end())
			for (auto[t, nodes] : mByType) nodes.erase(ranges::find(nodes, &node));
		else {
			auto tt = t->second.find(&node);
			if (tt == t->second.end())
				for (auto[t, nodes] : mByType) nodes.erase(ranges::find(nodes, &node));
			else
				t->second.erase(tt);
		}
		return mNodes.erase(it);
	}
};

}