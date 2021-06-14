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
	private:
		class component_ptr {
		private:
			void* mPointer;
			void(*mDestructor)(const void*);
		public:
			inline component_ptr() : mPointer(nullptr), mDestructor(nullptr) {}
			component_ptr(const component_ptr&) = delete;
			inline component_ptr(component_ptr&& c) : mPointer(c.mPointer), mDestructor(c.mDestructor) {
				c.mPointer = nullptr;
			}
			inline ~component_ptr() {
				if (mPointer) {
					mDestructor(mPointer);
					::operator delete(mPointer);
					mPointer = nullptr;
				}
			}
			
			template<typename T, typename...Args>
			inline component_ptr(const T*, Args&&...args) {
				mPointer = ::operator new(sizeof(T), align_val_t(alignof(T)), nothrow);
				mDestructor = [](const void* ptr) { reinterpret_cast<const T*>(ptr)->~T(); };
				new (mPointer) T(forward<Args>(args)...);
			}
		
			template<typename T> inline T* get() const {
				return reinterpret_cast<T*>(mPointer);
			}
		};
		
		friend class NodeGraph;
		NodeGraph& mNodeGraph;
		string mName;
		Node* mParent = nullptr;
		unordered_map<type_index, component_ptr> mComponents;

		inline Node(NodeGraph& NodeGraph, const string& name) : mNodeGraph(NodeGraph), mName(name) {}

	public:
		Event<Node&> OnChildAdded;
		Event<Node&> OnChildRemoving;
		Event<>      OnParentChanged;

		Node(Node&&) = default;
		inline ~Node() {
			clear_parent();
			// detach children
			while (true) {
				auto edge_it = mNodeGraph.mEdges.find(this);
				if (edge_it == mNodeGraph.mEdges.end()) break;
				edge_it->second->clear_parent();
			}
			
			for (const auto&[tid,c] : mComponents)
				if (auto it = mNodeGraph.mComponentMap.find(tid); it != mNodeGraph.mComponentMap.end())
					it->second.erase(this);
			mComponents.clear();
		}

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
	
		template<typename T, typename... Args> requires(constructible_from<T, Args&&...>)
		inline T& emplace(Args&&... args) {
			if (mComponents.count(typeid(T))) throw logic_error("Node already contains component of type T");
			mNodeGraph.mComponentMap[typeid(T)].emplace(this);
			component_ptr c(reinterpret_cast<const T*>(nullptr), forward<Args>(args)...);
			return *mComponents.emplace(typeid(T), move(c)).first->second.get<T>();
		}
		template<typename T, typename... Args> requires(constructible_from<T, Node*, Args&&...>)
		inline T& emplace(Args&&... args) {
			if (mComponents.count(typeid(T))) throw logic_error("Node already contains component of type T");
			mNodeGraph.mComponentMap[typeid(T)].emplace(this);
			component_ptr c(reinterpret_cast<const T*>(nullptr), this, forward<Args>(args)...);
			return *mComponents.emplace(typeid(T), move(c)).first->second.get<T>();
		}
		template<typename T, typename... Args> requires(constructible_from<T, Node&, Args&&...>)
		inline T& emplace(Args&&... args) {
			if (mComponents.count(typeid(T))) throw logic_error("Node already contains component of type T");
			mNodeGraph.mComponentMap[typeid(T)].emplace(this);
			component_ptr c(reinterpret_cast<const T*>(nullptr), forward<Node&>(*this), forward<Args>(args)...);
			return *mComponents.emplace(typeid(T), move(c)).first->second.get<T>();
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
			return views::transform(ranges::subrange(first,last), [](const auto& p) -> T& { return *p.second.get<T>(); });
		}
	
		template<typename T, typename F> requires(invocable<F,T*> || invocable<F,const T*> || invocable<F,T&> || invocable<F,const T&>)
		inline void for_each_component(F&& fn) const {
			auto[first,last] = mComponents.equal_range(typeid(T));
			for (const auto& p : ranges::subrange(first,last))
				if constexpr (invocable<F, T*> || invocable<F, const T*>)
					fn((T*)p.second.get<T>());
				else
					fn(*(T*)p.second.get<T>());
		}
	
		template<typename T, typename F> requires(invocable<F,T*> || invocable<F,const T*> || invocable<F,T&> || invocable<F,const T&>)
		inline void for_each_ancestor(F&& fn) const {
			const Node* n = this;
			while (n) {
				n->for_each_component<T>(forward<F>(fn));
				n = n->mParent;
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