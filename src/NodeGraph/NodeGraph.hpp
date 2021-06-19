#pragma once

#include "../Common/hash.hpp"

namespace stm {

class NodeGraph {
public:
	class Node;

	template<typename... Args>
	class Event {
	private:
		unordered_multimap<const Node*, pair<function<void(Args...)>, NodeGraph*>> mCallbacks;

	public:
		inline bool empty() { return mCallbacks.empty(); }
		inline void clear() { mCallbacks.clear(); }

		inline void validate() {
			for (auto it = mCallbacks.begin(); it != mCallbacks.end();)
				if (it->second.second->contains(it->first))
					++it;
				else
					it = mCallbacks.erase(it);
		}

		template<typename F> requires(invocable<F, Args...> || invocable<F, Node*, Args...> || invocable<F, Node&, Args...>)
		inline void emplace(const Node& node, F&& fn) {
			validate();
			if constexpr (invocable<F, Args...>)
				mCallbacks.emplace(&node, make_pair<function<void(Args...)>, NodeGraph*>( forward<F>(fn), &node.node_graph() ));
			else if constexpr (invocable<F, Node&, Args...>)
				mCallbacks.emplace(&node, make_pair<function<void(Args...)>, NodeGraph*>( bind_front(forward<F>(fn), node), &node.node_graph() ));
			else
				mCallbacks.emplace(&node, make_pair<function<void(Args...)>, NodeGraph*>( bind_front(forward<F>(fn), &node), &node.node_graph() ));
		}
		inline void erase(const Node& node) {
			mCallbacks.erase(&node);
			validate();
		}

		inline void operator()(Args&&... args) const {
			for (auto it = mCallbacks.begin(); it != mCallbacks.end(); it++)
				if (it->second.second->contains(it->first))
					invoke(it->second.first, forward<Args>(args)...);
		}
		inline void operator()(Args&&... args) {
			for (auto it = mCallbacks.begin(); it != mCallbacks.end();)
				if (it->second.second->contains(it->first)) {
					invoke(it->second.first, forward<Args>(args)...);
					++it;
				} else
					it = mCallbacks.erase(it);
		}
	};
	
	class Node {
	private:
		using component_t = pair<void*, function<void(const void*)>>;

		friend class NodeGraph;
		NodeGraph& mNodeGraph;
		string mName;
		const Node* mParent = nullptr;
		unordered_map<type_index, component_t> mComponents;

		inline Node(NodeGraph& NodeGraph, const string& name) : mNodeGraph(NodeGraph), mName(name) {}

		inline auto erase_unchecked(unordered_map<type_index, component_t>::const_iterator it) {
			it->second.second(it->second.first);
			mNodeGraph.mComponentMap.at(it->first).erase(this);
			return mComponents.erase(it);
		}

	public:
		Event<Node&> OnChildAdded;
		Event<Node&> OnChildRemoving;
		Event<>      OnParentChanged;

		Node(Node&&) = default;
		inline ~Node() {
			clear_parent();
			while (true) {
				auto edge_it = mNodeGraph.mEdges.find(this);
				if (edge_it == mNodeGraph.mEdges.end()) break;
				edge_it->second->clear_parent();
			}
			for (auto it = mComponents.begin(); it != mComponents.end();)
				it = erase_unchecked(it);
		}

		inline const string& name() const { return mName; }
		inline NodeGraph& node_graph() const { return mNodeGraph; }

		inline const Node* parent() const { return mParent; }
		inline auto children() const {
			auto[first,last] = mNodeGraph.mEdges.equal_range(this);
			return ranges::transform_view(ranges::subrange(first,last) | views::values, [](Node* n) -> Node& { return *n; });
		}

		inline void clear_parent() {
			if (mParent) {
				mParent->OnChildRemoving(*this);
				auto[first,last] = mNodeGraph.mEdges.equal_range(mParent);
				mNodeGraph.mEdges.erase(ranges::find(first, last, this, &unordered_multimap<const Node*, Node*>::value_type::second));
				mParent = nullptr;
				OnParentChanged();
			}
		}
		inline void set_parent(const Node& parent) {
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
	
		template<typename T, typename... Args>
		inline T& make_component(Args&&... args) {
			if (mComponents.count(typeid(T))) throw logic_error("Node already contains component of type T");
			mNodeGraph.mComponentMap[typeid(T)].emplace(this);
			T* ptr;
			if constexpr(constructible_from<T, Args&&...>)
				ptr = new T(forward<Args>(args)...);
			else if constexpr(constructible_from<T, Node*, Args&&...>)
				ptr = new T(this, forward<Args>(args)...);
			else if constexpr(constructible_from<T, Node&, Args&&...>)
				ptr = new T(forward<Node&>(*this), forward<Args>(args)...);
			else
				static_assert(false, "T is must be constructible from Args...");
			mComponents.emplace(typeid(T), component_t(ptr, [](const void* p) { delete reinterpret_cast<const T*>(p); } ));
			return *ptr;
		}
		
		// make a new T inside a new node named 'name', parented to this node
		template<typename T, typename... Args>
		inline T& make_child(const string& name, Args&&... args) const {
			Node& n = mNodeGraph.emplace(name);
			n.set_parent(*this);
			return n.make_component<T>(forward<Args>(args)...);
		}

		inline bool erase(type_index type) {
			auto it = mComponents.find(type);
			if (it == mComponents.end()) return false;
			erase_unchecked(it);
			return true;
		}
		template<typename T> inline bool erase() { return erase(typeid(T)); }

		template<typename T>
		inline T& component() const {
			auto it = mComponents.find(typeid(T));
			return *reinterpret_cast<T*>(it->second.first);
		}

		template<typename T, typename F> requires(
			invocable<F,const Node*> || invocable<F,const Node&> ||
			invocable<F,T*> || invocable<F,const T*> ||
			invocable<F,T&> || invocable<F,const T&>)
		inline auto invoke(F&& fn) const {
			if constexpr (invocable<F, const Node*>)
				return fn(this);
			else if constexpr (invocable<F,const Node&>)
				return fn(*this);
			else if constexpr (invocable<F, T*> || invocable<F, const T*>)
				return fn(&component<T>());
			else
				return fn(component<T>());
		}

		template<typename T, typename F> 
		inline void for_each_ancestor(F&& fn) const {
			auto cmap_it = mNodeGraph.mComponentMap.find(typeid(T));
			if (cmap_it == mNodeGraph.mComponentMap.end()) return;
			const Node* n = this;
			while (n) {
				if (cmap_it->second.find(n) != cmap_it->second.end())
					n->invoke<T,F>(forward<F>(fn));
				n = n->mParent;
			}
		}
	
		template<typename T, typename F>
		inline void for_each_child(F&& fn) const {
			auto cmap_it = mNodeGraph.mComponentMap.find(typeid(T));
			if (cmap_it == mNodeGraph.mComponentMap.end()) return;
			queue<const Node*> q;
			q.push(this);
			while (!q.empty()) {
				const Node* n = q.front();
				q.pop();
				if (cmap_it->second.find(n) != cmap_it->second.end())
					n->invoke<T,F>(forward<F>(fn));
				for (Node& c : n->children())
					q.push(&c);
			}
		}
	};

	inline bool empty() const { return mNodes.empty(); }
	inline void clear() { mNodes.clear(); }
	inline bool contains(const Node* ptr) const { return mNodes.count(ptr); }


	inline Node& emplace(const string& name) {
		auto node = make_unique<Node>(move(Node(*this, name)));
		Node* ptr = node.get();
		mNodes.emplace(ptr, move(node));
		return *ptr;
	}
	inline void erase(Node& node) {
		auto node_it = mNodes.find(&node);
		if (node_it == mNodes.end()) return;
		mNodes.erase(node_it);
	}


	template<typename T> inline auto find_nodes() const {
		ranges::subrange<unordered_set<const Node*>::const_iterator> r;
		auto it = mComponentMap.find(typeid(T));
		if (it != mComponentMap.end()) r = ranges::subrange(it->second);
		return views::transform(r, [](const Node* n) -> const Node& { return *n; });
	}

private:
	friend class Node;
	unordered_map<const Node*, unique_ptr<Node>> mNodes;
	unordered_multimap<const Node*, Node*> mEdges;
	unordered_map<type_index, unordered_set<const Node*>> mComponentMap;
};

}