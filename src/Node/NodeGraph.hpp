#pragma once

#include <Common/hash.hpp>

namespace stm {

class Node;
class NodeGraph;
template<typename T> class component_ptr;

// stores a component and the node it belongs to
template<typename T>
class component_ptr {
public:
	component_ptr() = default;
	component_ptr(const component_ptr&) = default;
	inline component_ptr(nullptr_t) {}
	inline component_ptr(component_ptr&& c) : mNode(c.mNode), mComponent(c.mComponent) { c.reset(); }
	inline component_ptr(Node* n, T* c) : mNode(n), mComponent(c) {}
	inline component_ptr(const Node* n, T* c) : mNode(const_cast<Node*>(n)), mComponent(c) {}

	component_ptr& operator=(const component_ptr&) = default;
	inline component_ptr& operator=(component_ptr&& c) {
		mNode = c.mNode;
		mComponent = c.mComponent;
		c.mNode = nullptr;
		c.mComponent = nullptr;
		return *this;
	}

	inline Node& node() const { return *mNode; }

	inline operator bool() const { return mComponent != nullptr; }
	inline T& operator*() const { return *mComponent; }
	inline T* operator->() const { return mComponent; }
	inline T* get() const { return mComponent; }
	inline void reset() { mNode = nullptr; mComponent = nullptr; }

	inline operator component_ptr<const T>() const {
		return component_ptr<const T>(mNode, mComponent);
	}

private:
	Node* mNode = nullptr;
	T* mComponent = nullptr;
};

template<>
class component_ptr<void> {
public:
	component_ptr() = default;
	component_ptr(const component_ptr&) = default;
	inline component_ptr(nullptr_t) {}
	inline component_ptr(component_ptr&& c) : mNode(c.mNode), mComponent(c.mComponent) { c.reset(); }
	inline component_ptr(Node* n, void* c) : mNode(n), mComponent(c) {}
	inline component_ptr(const Node* n, void* c) : mNode(const_cast<Node*>(n)), mComponent(c) {}
	template<typename T>
	inline component_ptr(const component_ptr<T>& c) : mNode(&c.node()), mComponent(c.get()) {}

	component_ptr& operator=(const component_ptr&) = default;
	inline component_ptr& operator=(component_ptr&& c) {
		mNode = c.mNode;
		mComponent = c.mComponent;
		c.mNode = nullptr;
		c.mComponent = nullptr;
		return *this;
	}

	inline Node& node() const { return *mNode; }

	inline operator bool() const { return mComponent != nullptr; }
	inline void* operator->() const { return mComponent; }
	inline void* get() const { return mComponent; }
	inline void reset() { mNode = nullptr; mComponent = nullptr; }

private:
	Node* mNode = nullptr;
	void* mComponent = nullptr;
};

// stores nodes, components, and node relationships
class NodeGraph {
public:
	inline bool empty() const { return mNodes.empty(); }
	inline bool contains(const Node* ptr) const { return mNodes.count(ptr); }
	inline size_t count(type_index type) const { return mComponentMap.count(type); }
	template<typename T> inline size_t count() const { return count(typeid(T)); }

	STRATUM_API Node& emplace(const string& name);
	inline void erase(Node& node) { mNodes.erase(&node); }
	inline void erase_recurse(Node& node) {
		list<Node*> nodes;
		queue<Node*> todo;
		todo.push(&node);
		while (!todo.empty()) {
			nodes.push_front(todo.front());
			auto[first,last] = mEdges.equal_range(todo.front());
			todo.pop();
			for (auto[_,n] : ranges::subrange(first,last))
				todo.push(n);
		}
		for (Node* n : nodes)
			erase(*n);
	}

	template<typename T> inline size_t component_count() const {
		auto it = mComponentMap.find(typeid(T));
		if (it == mComponentMap.end()) return 0;
		return it->second.size();
	}
	template<typename T> inline auto find_components() const {
		return views::transform(mComponentMap.at(typeid(T)), [](const auto& p) -> component_ptr<T> {
			return component_ptr<T>(p.first, reinterpret_cast<T*>(p.second));
		});
	}

private:
	friend class Node;

	// stores components of one type, and the destructor for that type
	class component_map {
	private:
		void(*mDestructor)(const void*);
		unordered_map<const Node*, void*> mComponents;
	public:
		inline component_map(auto dtor) : mDestructor(dtor) {}

		inline auto begin() { return mComponents.begin(); }
		inline auto end() { return mComponents.end(); }
		inline auto begin() const { return mComponents.begin(); }
		inline auto end() const { return mComponents.end(); }
		inline size_t size() const { return mComponents.size(); }
		inline void* find(const Node* node) {
			auto it = mComponents.find(node);
			return (it == mComponents.end()) ? nullptr : it->second;
		}
		inline void*& emplace(const Node* node, void* ptr) {
			return mComponents.emplace(node, ptr).first->second;
		}
		inline void erase(const Node* node) {
			auto it = mComponents.find(node);
			if (it != mComponents.end()) {
				mDestructor(it->second);
				mComponents.erase(it);
			}
		}
	};

	unordered_map<const Node*, unique_ptr<Node>> mNodes;
	unordered_map<type_index, component_map> mComponentMap;
	unordered_multimap<const Node*, Node*> mEdges;
};

// Stores a name, parent pointer, and a list of component types
// components and child pointers are stored in NodeGraph
class Node {
public:
	enum EventPriority : uint32_t {
		eFirst       = 0,
		eAlmostFirst = 0x3FFFFFFF,
		eDefault     = 0x7FFFFFFF,
		eAlmostLast  = 0xBFFFFFFD,
		eLast        = 0xFFFFFFFF
	};

	template<typename... Args>
	class Event {
	public:
		using function_t = function<void(Args...)>;

		Event() = default;
		Event(Event&&) = default;
		Event& operator=(Event&&) = default;

		Event(const Event&) = delete;
		Event& operator=(const Event&) = delete;

		inline void clear() { mListeners.clear(); }
		inline bool empty() const { return mListeners.empty(); }
		inline size_t count(const Node& node) const { return mListeners.count(&node); }

		void add_listener(const Node& node, function_t&& fn, uint32_t priority = EventPriority::eDefault);
		inline void erase(const Node& node) {
			for (auto it = mListeners.begin(); it != mListeners.end();)
				if (get<const Node*>(*it) == &node)
					it = mListeners.erase(it);
				else
					++it;
		}

		inline void operator()(Args... args) const {
			vector<tuple<const Node*, function_t, uint32_t>> tmp(mListeners.size());
			ranges::copy(mListeners, tmp.begin());
			for (const auto&[n, fn, p] : tmp)
				if (mNodeGraph->contains(n))
					invoke(fn, forward<Args>(args)...);
		}

	private:
		const NodeGraph* mNodeGraph = nullptr;
		vector<tuple<const Node*, function_t, uint32_t>> mListeners;
	};

	Event<>      OnParentChanged;
	Event<Node&> OnChildAdded;
	Event<Node&> OnChildRemoving;

	Node() = delete;
	Node(const Node&) = delete;

	Node(Node&&) = default;
	STRATUM_API ~Node();

	inline const string& name() const { return mName; }
	inline NodeGraph& node_graph() const { return mNodeGraph; }
	inline Node* parent() const { return mParent; }
	inline auto children() const {
		auto[first,last] = mNodeGraph.mEdges.equal_range(this);
		return ranges::transform_view(ranges::subrange(first,last), [](const auto& n) -> Node& { return *n.second; });
	}
	STRATUM_API void clear_parent();
	STRATUM_API void set_parent(Node& parent);

	inline Node& root() {
		Node* r = this;
		while (r->mParent) r = r->mParent;
		return *r;
	}

	inline Node& make_child(const string& name) {
		Node& n = mNodeGraph.emplace(name);
		n.set_parent(*this);
		return n;
	}

	inline bool descendant_of(const Node& ancestor) const {
		const Node* n = this;
		while (n && n != &ancestor)
			n = n->mParent;
		return n == &ancestor;
	}

	template<typename T, typename... Args> requires(constructible_from<T, Args...>)
	inline component_ptr<T> make_component(Args&&... args) {
		if (mComponents.count(typeid(T))) throw logic_error("Cannot make multiple components of the same type within the same node");
		auto cmap_it = mNodeGraph.mComponentMap.find(typeid(T));
		if (cmap_it == mNodeGraph.mComponentMap.end()) cmap_it = mNodeGraph.mComponentMap.emplace(typeid(T), NodeGraph::component_map([](const void* p) {
			delete reinterpret_cast<const T*>(p);
		})).first;
		mComponents.emplace(typeid(T));
		void*& ptr = cmap_it->second.emplace(this, nullptr);
		ptr = new T(forward<Args>(args)...);
		return component_ptr<T>(this, reinterpret_cast<T*>(ptr));
	}
	template<typename T, typename... Args> requires(constructible_from<T, Node*, Args...>)
	inline component_ptr<T> make_component(Args&&... args) {
		return make_component<T>(this, forward<Args>(args)...);
	}
	template<typename T, typename... Args> requires(constructible_from<T, Node&, Args...>)
	inline component_ptr<T> make_component(Args&&... args) {
		return make_component<T>(*this, forward<Args>(args)...);
	}

	inline void erase_component(type_index type) {
		auto cmap_it = mNodeGraph.mComponentMap.find(type);
		if (cmap_it != mNodeGraph.mComponentMap.end()) {
			cmap_it->second.erase(this);
			mComponents.erase(type);
		}
	}
	template<typename T> inline void erase_component() { erase_component(typeid(T)); }

	inline const unordered_set<type_index>& components() const { return mComponents; }

	inline void* find(type_index type) const {
		auto it = mNodeGraph.mComponentMap.find(type);
		return it == mNodeGraph.mComponentMap.end() ? nullptr : it->second.find(this);
	}
	template<typename T> inline component_ptr<T> find() const {
		void* ptr = find(typeid(T));
		if (ptr == nullptr) return {};
		return component_ptr<T>(this, reinterpret_cast<T*>(ptr));
	}
	template<typename T> inline component_ptr<T> find_in_ancestor() const {
		auto cmap_it = mNodeGraph.mComponentMap.find(typeid(T));
		if (cmap_it == mNodeGraph.mComponentMap.end()) return {};
		const Node* n = this;
		while (n) {
			void* ptr = cmap_it->second.find(n);
			if (ptr != nullptr)
				return component_ptr<T>(n, reinterpret_cast<T*>(ptr));
			n = n->mParent;
		}
		return {};
	}
	template<typename T> inline component_ptr<T> find_in_descendants() const {
		auto cmap_it = mNodeGraph.mComponentMap.find(typeid(T));
		if (cmap_it == mNodeGraph.mComponentMap.end()) return {};
		queue<const Node*> q;
		q.push(this);
		while (!q.empty()) {
			const Node* n = q.front();
			q.pop();
			void* ptr = cmap_it->second.find(n);
			if (ptr != nullptr)
				return component_ptr<T>(n, reinterpret_cast<T*>(ptr));
			for (const Node& c : n->children())
				q.push(&c);
		}
		return {};
	}

	template<typename T, invocable<component_ptr<T>> F>
	inline void for_each_ancestor(F&& fn) const {
		auto cmap_it = mNodeGraph.mComponentMap.find(typeid(T));
		if (cmap_it == mNodeGraph.mComponentMap.end()) return;
		const Node* n = this;
		while (n) {
			void* ptr = cmap_it->second.find(n);
			if (ptr != nullptr) {
				component_ptr p(n, reinterpret_cast<T*>(ptr));
				fn(p);
			}
			n = n->mParent;
		}
	}
	template<typename T, invocable<component_ptr<T>> F>
	inline void for_each_descendant(F&& fn) const {
		auto cmap_it = mNodeGraph.mComponentMap.find(typeid(T));
		if (cmap_it == mNodeGraph.mComponentMap.end()) return;
		queue<const Node*> q;
		q.push(this);
		while (!q.empty()) {
			const Node* n = q.front();
			q.pop();
			void* ptr = cmap_it->second.find(n);
			if (ptr != nullptr) {
				component_ptr p(n, reinterpret_cast<T*>(ptr));
				fn(p);
			}
			for (const Node& c : n->children())
				q.push(&c);
		}
	}

private:
	NodeGraph& mNodeGraph;
	string mName;
	Node* mParent;
	unordered_set<type_index> mComponents;
	friend class NodeGraph;
	inline Node(NodeGraph& nodeGraph, const string& name) : mNodeGraph(nodeGraph), mName(name), mParent(nullptr) {}
};

template<typename... Args>
inline void Node::Event<Args...>::add_listener(const Node& listener, function_t&& fn, uint32_t priority) {
	mListeners.emplace_back(&listener, forward<function_t>(fn), priority);
	ranges::stable_sort(mListeners, {}, [](const auto& a) { return get<uint32_t>(a); });
	if (!mNodeGraph) mNodeGraph = &listener.node_graph();
}

}