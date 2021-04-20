#pragma once

#include "../Stratum.hpp"

namespace stm {

template<typename Tkey, typename... Args>
class DelegateMap {
private:
	unordered_multimap<weak_ptr<Tkey>, function<void(Args...)>> mCallbacks;
	
public:
	inline bool empty() { mCallbacks.empty(); }
	inline void clear() { mCallbacks.clear(); }
	
	template<invocable<Args...> F> inline void emplace(weak_ptr<Tkey> key, F&& fn) { mCallbacks[key].emplace_back(fn); }
	template<invocable<Args...> F> inline void operator+=(F&& fn) { mCallbacks[nullptr].emplace_back(fn); }
	template<invocable<Args...> F> inline void operator-=(F&& fn) { mCallbacks.at(nullptr).erase(fn); }
	inline void operator-=(Tkey* key) { mCallbacks.erase(key); }
	
	inline void operator()(Args&&... args) const {
		for (auto it = mCallbacks.begin(); it != mCallbacks.end(); it++)
			if (auto k = it->first.lock()) {
				invoke(it->second, forward<Args>(args)...);
			}
	}
};

class Scene {
public:
	class Node;

private:
	friend class Node;
	set<unique_ptr<Node>> mNodes;
	unordered_map<type_index, vector<Node*>> mTypes;
	unordered_map<const Node*, Node*> mParentEdges; // child -> parent
	unordered_multimap<const Node*, Node*> mChildrenEdges; // parent -> child
	
public:

	template<typename... Args>
	using NodeDelegate = DelegateMap<Node, Args...>;

	class Node {
	public:
		Scene& mScene;
		string mName;
		
		NodeDelegate<>      OnChangeParent;
		NodeDelegate<Node*> OnAddChild;
		NodeDelegate<Node*> OnRemoveChild;

		inline Node(Scene& scene, const string& name) : mName(name), mScene(scene) {}
		virtual ~Node() = default;
		inline const string& name() const { return mName; }

		inline Node* parent() const {
			auto it = mScene.mParentEdges.find(this);
			return it == mScene.mParentEdges.end() ? nullptr : it->second;
		}
		inline auto children() const {
  		return ranges::subrange(mScene.mChildrenEdges.find(this), mScene.mChildrenEdges.end()) | views::values;
		}
		inline vector<Node*> descendants() const {
			// TODO: somehow return a ranges::view
			vector<Node*> r;
			queue<const Node*> todo;
			todo.push(this);
			while (!todo.empty()) {
				size_t n = mScene.mChildrenEdges.count(todo.front());
				if (n == 0) continue;
				auto c = todo.front()->children();
				todo.pop();
				r.resize(r.size() + n);
				ranges::copy(c, r.end()-n);
				ranges::for_each(c, [&](Node* x){ todo.push(x); });
			}
			return r;
		}
	};

	template<derived_from<Node> T>
	inline auto find_type() const { return ranges::subrange(mTypes.find(typeid(T)), mTypes.end()); }
	template<>
	inline auto find_type<Node>() const { return ranges::subrange(mTypes.begin(), mTypes.end()); }
	
	template<derived_from<Node> T, typename... Args> requires(constructible_from<T,Args...>)
	inline Node& emplace(Args&&... args) {
		return mTypes[type_index(typeid(T))].emplace_back(
			mNodes.emplace(make_unique<T>(forward<Args>(args)...)).first->get() );
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

		// remove from mTypes by indexing the type directly if possible, otherwise
		auto t = mTypes.find(type_index(typeid(T)));
		if (t == mTypes.end())
			for (auto[t, nodes] : mTypes) nodes.erase(ranges::find(nodes, &node));
		else {
			auto tt = t->second.find(&node);
			if (tt == t->second.end())
				for (auto[t, nodes] : mTypes) nodes.erase(ranges::find(nodes, &node));
			else
				t->second.erase(tt);
		}
		return mNodes.erase(it);
	}
};

}