#pragma once

#include "../Common/hash.hpp"

namespace stm {

class NodeGraph {
public:
	class Node;

	template<typename... Args>
	class Event {
	private:
		unordered_multimap<const Node*, function<void(Args...)>> mCallbacks;

	public:
		inline bool empty() { return mCallbacks.empty(); }
		inline void clear() { mCallbacks.clear(); }
		
		template<typename F> requires(invocable<F, Args...> || invocable<F, Node*, Args...> || invocable<F, Node&, Args...>)
		inline void emplace(const Node& node, F&& fn) {
			if constexpr (invocable<F, Args...>)
				mCallbacks.emplace(&node, function<void(Args...)>(forward<F>(fn)));
			else if constexpr (invocable<F, Node&, Args...>)
				mCallbacks.emplace(&node, function<void(Args...)>(bind_front(forward<F>(fn), node)));
			else
				mCallbacks.emplace(&node, function<void(Args...)>(bind_front(forward<F>(fn), &node)));
		}
		inline void erase(const Node& node) {
			mCallbacks.erase(&node);
		}

		inline void operator()(NodeGraph& nodeGraph, Args&&... args) {
			for (auto it = mCallbacks.begin(); it != mCallbacks.end();)
				if (nodeGraph.contains(it->first))
					++it;
				else
					it = mCallbacks.erase(it);
			for (const auto&[node, fn] : mCallbacks)
				invoke(fn, forward<Args>(args)...);
		}
	};
	
	class Node {
	public:
		class component_t {
		private:
			void* mPointer = nullptr;
			void(*mDestructor)(const void*) = nullptr;
		public:
			component_t() = default;
			component_t(const component_t&) = delete;
			inline component_t(component_t&& c) : mPointer(c.mPointer), mDestructor(c.mDestructor) {
				c.mPointer = nullptr;
				c.mDestructor = nullptr;
			}
			inline component_t(void* ptr, auto dtor) : mPointer(ptr), mDestructor(dtor) {}
			inline ~component_t() {
				if (mPointer) {
					mDestructor(mPointer);
					delete reinterpret_cast<byte*>(mPointer);
					mPointer = nullptr;
				}
			}

			template<typename T> inline explicit operator T*() const { return reinterpret_cast<T*>(mPointer); }
		};

		Event<Node&> OnChildAdded;
		Event<Node&> OnChildRemoving;
		Event<>      OnParentChanged;

		Node(Node&&) = default;

		inline const string& name() const { return mName; }
		inline NodeGraph& node_graph() const { return mNodeGraph; }

		inline Node* parent() const { return mParent; }
		inline auto children() const {
			auto[first,last] = mNodeGraph.mEdges.equal_range(this);
			return ranges::subrange(first,last) | views::values;
		}

		inline void clear_parent() {
			if (mParent) {
				mParent->OnChildRemoving(mNodeGraph, *this);
				auto[first,last] = mNodeGraph.mEdges.equal_range(mParent);
				mNodeGraph.mEdges.erase(ranges::find(first, last, this, &unordered_multimap<const Node*, Node*>::value_type::second));
				mParent = nullptr;
				OnParentChanged(mNodeGraph);
			}
		}
		inline void set_parent(Node& parent) {
			if (mParent) { 
				if (mParent == &parent) return;
				// remove from parent
				mParent->OnChildRemoving(mNodeGraph, *this);
				auto[first,last] = mNodeGraph.mEdges.equal_range(mParent);
				mNodeGraph.mEdges.erase(ranges::find(first, last, this, &unordered_multimap<const Node*, Node*>::value_type::second));
			}
			mNodeGraph.mEdges.emplace(&parent, this);
			mParent = &parent;
			parent.OnChildAdded(mNodeGraph, *this);
			OnParentChanged(mNodeGraph);
		}
	
		template<typename T, typename... Args> requires(constructible_from<T,Args&&...> || constructible_from<T,Node*,Args&&...> || constructible_from<T,Node&,Args&&...>)
		inline T& emplace(Args&&... args) {
			if (mComponents.count(typeid(T))) throw logic_error("Node already contains component of type T");
			void* ptr = ::operator new(sizeof(T), align_val_t(alignof(T)), nothrow);
			mComponents.emplace(typeid(T), component_t(ptr, [](const void* p){ reinterpret_cast<const T*>(p)->~T(); }));
			mNodeGraph.mComponentMap[typeid(T)].emplace(this);
			if constexpr (constructible_from<T, Args&&...>)
				return *new (ptr) T(forward<Args>(args)...);
			else if constexpr (constructible_from<T, Node*, Args&&...>)
				return *new (ptr) T(this, forward<Args>(args)...);
			else if constexpr (constructible_from<T, Node&, Args&&...>)
				return *new (ptr) T(forward<Node&>(*this), forward<Args>(args)...);
			else
				static_assert("T must be constructible from either:\nArgs...\nNode*, Args...\nNode&, Args...\n");
		}
		
		inline bool erase(type_index type) {
			auto it = mComponents.find(type);
			if (it == mComponents.end()) return false;
			mComponents.erase(it);
			if (mComponents.find(type) == mComponents.end())
				mNodeGraph.mComponentMap.at(type).erase(this);
			return true;
		}
		template<typename T> inline bool erase() {
			return erase(typeid(T));
		}

		template<typename T>
		inline auto components() const {
			auto[first,last] = mComponents.equal_range(typeid(T));
			return views::transform(ranges::subrange(first,last), [](const auto& p) -> T& { return *(T*)p.second; });
		}
	
		template<typename T, typename F> requires(invocable<F,T*> || invocable<F,const T*> || invocable<F,T&> || invocable<F,const T&>)
		inline void for_each_component(F&& fn) const {
			auto[first,last] = mComponents.equal_range(typeid(T));
			for (const auto& p : ranges::subrange(first,last))
				if constexpr (invocable<F, T*> || invocable<F, const T*>)
					fn((T*)p.second);
				else
					fn(*(T*)p.second);
		}
	
		template<typename T, typename F> requires(invocable<F,T*> || invocable<F,const T*> || invocable<F,T&> || invocable<F,const T&>)
		inline void for_each_ancestor(F&& fn) const {
			const Node* n = this;
			while (n) {
				n->for_each_component<T>(forward<F>(fn));
				n = n->mParent;
			}
		}

	private:
		friend class NodeGraph;
		NodeGraph& mNodeGraph;
		string mName;
		Node* mParent = nullptr;
		unordered_map<type_index, component_t> mComponents;

		inline Node(NodeGraph& NodeGraph, const string& name) : mNodeGraph(NodeGraph), mName(name) {}
	};

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
	
		for (auto&[tid,c] : node.mComponents) {
			c.~component_t();
			auto it = mComponentMap.find(tid);
			it->second.erase(&node);
			if (it->second.empty())
				mComponentMap.erase(it);
		}

		while (true) {
			auto edge_it = mEdges.find(&node);
			if (edge_it == mEdges.end()) break;
			edge_it->second->clear_parent();
		}
		node.clear_parent();
		
		mNodes.erase(node_it);
	}

	template<typename T> inline auto find_nodes() const {
		ranges::subrange<unordered_set<Node*>::const_iterator> r;
		auto it = mComponentMap.find(typeid(T));
		if (it != mComponentMap.end()) r = ranges::subrange(it->second);
		return views::transform(r, [](Node* n) -> Node& { return *n; });
	}

	template<typename T, typename F> requires(invocable<F,T*> || invocable<F,const T*> || invocable<F,T&> || invocable<F,const T&>)
	inline void for_each_component(F&& fn) const {
		auto it = mComponentMap.find(typeid(T));
		if (it == mComponentMap.end()) return;
		for (Node* n : it->second)
			n->for_each_component<T>(forward<F>(fn));
	}

private:
	friend class Node;
	
	unordered_map<const Node*, unique_ptr<Node>> mNodes;
	unordered_multimap<const Node*, Node*> mEdges;

	unordered_map<type_index, unordered_set<Node*>> mComponentMap;
};

}