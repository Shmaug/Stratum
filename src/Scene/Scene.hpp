#pragma once

#include "../Stratum.hpp"

namespace stm {

class Scene {
public:
	class Node {
	public:
		template<typename... Args>
		class Event {
		public:
			using function_t = function<void(Args...)>;
			
			Event(Event&&) = default;
			Event(const Event&) = default;
			inline Event(Node* node) : mNode(*node) {}

			inline bool empty() { mCallbacks.empty(); }
			inline void clear() { mCallbacks.clear(); }
			
			template<derived_from<Node> T, typename F>
			inline void emplace(T& node, F&& nodeFn) {
				mCallbacks.emplace(static_cast<Node*>(&node), bind_front(nodeFn, &node));
			}
			template<derived_from<Node> T>
			inline void erase(T& node) {
				mCallbacks.erase(static_cast<Node*>(&node));
			}
			
			inline void operator()(Args&&... args) {
				for (auto it = mCallbacks.begin(); it != mCallbacks.end();)
					if (mNode.mScene.contains(it->first)) {
						invoke(it->second, forward<Args>(args)...);
						it++;
					} else
						it = mCallbacks.erase(it);
			}
		
		private:
			unordered_multimap<Node*, function_t> mCallbacks;
			Node& mNode;
		};
		
	private:
		string mName;
		Scene& mScene;
	public:
		Event<Node&> OnChildAdded;
		Event<Node&> OnChildRemoved;
		Event<>      OnParentChanged;

		Node(Node&&) = default;
		inline Node(Scene& scene, const string& name) : mName(name), mScene(scene), OnChildAdded(this), OnChildRemoved(this), OnParentChanged(this) {}
		virtual ~Node() = default;
		inline const string& name() const { return mName; }
		inline Scene& scene() const { return mScene; }

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

		inline void clear_parent() {
			auto parent_it = mScene.mParentEdges.find(this);
			if (parent_it != mScene.mParentEdges.end()) {
				Node& parent = *parent_it->second;
				mScene.mParentEdges.erase(parent_it);
				for (auto child_it = mScene.mChildrenEdges.find(parent_it->second); child_it != mScene.mChildrenEdges.end(); child_it++)
					if (child_it->second == this) {
						mScene.mChildrenEdges.erase(child_it);
						break;
					}
				parent.OnChildRemoved(*this);
				OnParentChanged();
			}
		}
		inline void set_parent(Node& parent) {
			clear_parent();
			mScene.mParentEdges.emplace(this, &parent);
			mScene.mChildrenEdges.emplace(&parent, this);
			parent.OnChildAdded(*this);
			OnParentChanged();
		}
	};

	inline bool contains(Node* ptr) {
		return ranges::find(mNodes, ptr, &unique_ptr<Node>::get) != mNodes.end();
	}

	template<derived_from<Node> T>
	inline auto find_type() const { return ranges::subrange(mTypes.find(typeid(T)), mTypes.end()); }
	template<>
	inline auto find_type<Node>() const { return ranges::subrange(mTypes.begin(), mTypes.end()); }
	
	template<derived_from<Node> T, typename... Args> requires(constructible_from<T,Args...>)
	inline T& emplace(Args&&... args) {
		return *static_cast<T*>(mTypes[type_index(typeid(T))].emplace_back( mNodes.emplace(make_unique<T>(forward<Args>(args)...)).first->get() ));
	}
	
	template<derived_from<Node> T>
	inline bool erase(Node& node) {
		auto it = ranges::find(mNodes, &node, &unique_ptr<Node>::get);
		if (it == mNodes.end()) return false;

		for (auto child_it = mChildrenEdges.find(&node); child_it != mChildrenEdges.end(); child_it++)
			child_it->second->clear_parent();
		node.clear_parent();

		// remove from mTypes by indexing the type directly if possible
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

private:
	friend class Node;
	set<unique_ptr<Node>> mNodes;
	unordered_map<type_index, vector<Node*>> mTypes;
	unordered_map<const Node*, Node*> mParentEdges; // child -> parent
	unordered_multimap<const Node*, Node*> mChildrenEdges; // parent -> child
};

}