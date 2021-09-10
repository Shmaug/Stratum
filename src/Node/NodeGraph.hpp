#pragma once

#include <Common/hash.hpp>

namespace stm {

class Node;
class NodeGraph;
template<typename T> class component_ptr;

enum EventPriority : uint32_t {
	eFirst = 0,
	eDefault = numeric_limits<uint32_t>::max()/2,
	eLast = numeric_limits<uint32_t>::max(),
};

template<typename... Args>
class NodeEvent {
public:
	using function_t = function<void(Args...)>;

	NodeEvent() = default;
	NodeEvent(NodeEvent&&) = default;
	NodeEvent& operator=(NodeEvent&&) = default;
	
	NodeEvent(const NodeEvent&) = delete;
	NodeEvent& operator=(const NodeEvent&) = delete;

	inline void clear() { mListeners.clear(); }
	inline bool empty() const { return mListeners.empty(); }
	inline size_t count(const Node& node) const { return mListeners.count(&node); }
	
	inline void erase(const Node& node) {
		for (auto it = mListeners.begin(); it != mListeners.end();)
			if (get<const Node*>(*it) == &node)
				it = mListeners.erase(it);
			else
				++it;
	}

	inline void operator()(Args... args) const {
		for (const auto&[n, fn, p] : mListeners)
			invoke(fn, forward<Args>(args)...);
	}

private:
	const NodeGraph* mNodeGraph = nullptr;
	vector<tuple<const Node*, function_t, uint32_t>> mListeners;

	friend class Node;
	template<typename T> friend class component_ptr;
	void bind_listener(const Node& node, function_t&& fn, uint32_t priority = EventPriority::eDefault);
};

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

	template<typename... Args, typename F>
	inline void listen(NodeEvent<Args...>& nodeEvent, F&& fn, uint32_t priority = EventPriority::eDefault) const {
		if constexpr (invocable<F> && sizeof...(Args))
			nodeEvent.bind_listener(*mNode, bind(forward<F>(fn)), priority);
		else if constexpr (invocable<F, Args...>)
			nodeEvent.bind_listener(*mNode, forward<F>(fn), priority);

		else if constexpr (invocable<F, T*> && sizeof...(Args))
			nodeEvent.bind_listener(*mNode, bind(forward<F>(fn), mComponent), priority);
		else if constexpr (invocable<F, T*, Args...>)
			nodeEvent.bind_listener(*mNode, bind_front(forward<F>(fn), mComponent), priority);

		else
			static_assert(false, "Could not invoke F with event arguments");
	}

	inline operator component_ptr<const T>() const {
		return component_ptr<const T>(mNode, mComponent);
	}

private:
	Node* mNode = nullptr;
	T* mComponent = nullptr;
};

class NodeGraph {
public:
	inline bool empty() const { return mNodes.empty(); }
	inline bool contains(const Node* ptr) const { return mNodes.count(ptr); }
	inline size_t count(type_index type) const { return mComponentMap.count(type); }
	template<typename T> inline size_t count() const { return count(typeid(T)); }

	STRATUM_API Node& emplace(const string& name);
	inline void erase(Node& node) { mNodes.erase(&node); }
	STRATUM_API void erase_recurse(Node& node);

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
	class component_map {
	private:
		void(*mDestructor)(const void*);
		unordered_map<const Node*, void*> mComponents;
	public:
		template<typename T> inline component_map(T* ptr = nullptr) : mDestructor([](const void* p) { delete reinterpret_cast<const T*>(p); }) {}

		inline auto begin() { return mComponents.begin(); }
		inline auto end() { return mComponents.end(); }
		inline auto begin() const { return mComponents.begin(); }
		inline auto end() const { return mComponents.end(); }
		inline size_t size() const { return mComponents.size(); }
		inline void* find(const Node* node) {
			auto it = mComponents.find(node);
			return (it == mComponents.end()) ? nullptr : it->second;
		}
		inline void emplace(const Node* node, void* ptr) {
			mComponents.emplace(node, ptr);
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

class Node {
public:
	NodeEvent<>      OnParentChanged;
	NodeEvent<Node&> OnChildAdded;
	NodeEvent<Node&> OnChildRemoving;

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

	template<typename T, typename... Args> requires(constructible_from<T, Args...>)
	inline component_ptr<T> make_component(Args&&... args) {
		if (mComponents.count(typeid(T))) throw logic_error("Cannot make multiple components of the same type within the same node");
		T* ptr = new T(forward<Args>(args)...);
		mComponents.emplace(typeid(T));
		auto cmap_it = mNodeGraph.mComponentMap.find(typeid(T));
		if (cmap_it == mNodeGraph.mComponentMap.end()) cmap_it = mNodeGraph.mComponentMap.emplace(typeid(T), NodeGraph::component_map(ptr)).first;
		cmap_it->second.emplace(this, ptr);
		return component_ptr<T>(this, ptr);
	}
	template<typename T, typename... Args> requires(constructible_from<T, Node*, Args...>)
	inline component_ptr<T> make_component(Args&&... args) {
		return make_component<T>(this, forward<Args>(args)...);
	}
	template<typename T, typename... Args> requires(constructible_from<T, Node&, Args...>)
	inline component_ptr<T> make_component(Args&&... args) {
		return make_component<T>(*this, forward<Args>(args)...);
	}

	inline Node& make_child(const string& name) {
		Node& n = mNodeGraph.emplace(name);
		n.set_parent(*this);
		return n;
	}
	
	inline void erase(type_index type) {
		auto cmap_it = mNodeGraph.mComponentMap.find(type);
		if (cmap_it != mNodeGraph.mComponentMap.end()) {
			cmap_it->second.erase(this);
			mComponents.erase(type);
		}
	}
	template<typename T>
	inline void erase() { erase(typeid(T)); }

	template<typename T> inline component_ptr<T> find() const {
		auto it = mNodeGraph.mComponentMap.find(typeid(T));
		if (it == mNodeGraph.mComponentMap.end()) return {};
		void* ptr = it->second.find(this);
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

	template<typename... Args, typename F> requires(invocable<F> || invocable<F,Args...>)
	inline void listen(NodeEvent<Args...>& nodeEvent, F&& fn, uint32_t priority = EventPriority::eDefault) const {
		if constexpr (invocable<F> && sizeof...(Args))
			nodeEvent.bind_listener(*this, bind(forward<F>(fn)), priority);
		else if constexpr(invocable<F, Args...>)
			nodeEvent.bind_listener(*this, forward<F>(fn), priority);
		else
			static_assert(false, "Could not invoke F with event arguments");
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
inline void NodeEvent<Args...>::bind_listener(const Node& listener, function_t&& fn, uint32_t priority) {
	mListeners.emplace_back(&listener, forward<function_t>(fn), priority);
	ranges::stable_sort(mListeners, {}, [](const auto& a) { return get<uint32_t>(a); });
	if (!mNodeGraph) mNodeGraph = &listener.node_graph();
}

}