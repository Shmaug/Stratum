#pragma once

#include "..\Stratum.hpp"

namespace stm {

class Scene {
public:

	class Node {
	private:
		string mName;
	public:
		Scene& mScene;
		inline Node(const string& name, Scene& scene) : mName(name), mScene(scene) {}
		virtual ~Node() = default;
		inline const string& Name() const { return mName; }
	};
	
private:
	vector<unique_ptr<Node>> mTopologicalNodes;
	unordered_multimap<type_index, Node*> mNodesByType;
	unordered_map<Node*, Node*> mParentEdges; // node -> node.parent
	unordered_multimap<Node*, Node*> mChildrenEdges; // node -> node.child

public:

	class parent_iterator {
	private:
		Node* mNode;

	public:
    using iterator_category = input_iterator_tag;
    using value_type        = Node;
    using pointer           = value_type*;
    using reference         = value_type&;

		inline parent_iterator() : mNode(nullptr) {}
		inline parent_iterator(Node* node) : mNode(node) {}
		parent_iterator(const parent_iterator&) = default;
		
    inline reference operator*() const { return *mNode; }
    inline pointer operator->() { return mNode; }

    inline parent_iterator& operator++() {
			if (mNode->mScene.mParentEdges.count(mNode))
				mNode = mNode->mScene.mParentEdges.at(mNode);
			else
				mNode = nullptr;
			return *this;
		}
    inline parent_iterator operator++(int) {
			parent_iterator tmp(*this);
			operator++();
			return tmp;
		}

		inline operator bool() const { return mNode != nullptr; }

    bool operator==(const parent_iterator& rhs) const = default;
    bool operator!=(const parent_iterator& rhs) const = default;
    inline bool operator>(const parent_iterator& rhs) const {
			if (mNode == rhs.mNode) return false;
			return find_if(*this, parent_iterator(), rhs);
		};
		inline bool operator<(const parent_iterator& rhs) const {
			return rhs.operator>(*this);
		};
	};
	static_assert(input_iterator<parent_iterator>);

	class child_iterator {
	private:
		unordered_multimap<Node*, Node*>::iterator mIterator;

	public:
    using iterator_category = input_iterator_tag;
    using value_type        = Node;
    using pointer           = value_type*;
    using reference         = value_type&;

		inline child_iterator() : mIterator({}) {}
		inline child_iterator(Node* node) : mIterator(node->mScene.mChildrenEdges.find(node)) {}
		child_iterator(const child_iterator&) = default;
		
    inline reference operator*() const { return *mIterator->second; }
    inline pointer operator->() { return mIterator->second; }

    inline child_iterator& operator++() {
			return *this;
		}
    inline child_iterator operator++(int) {
			child_iterator tmp(*this);
			operator++();
			return tmp;
		}

		inline operator bool() const { return mIterator->second != nullptr; }

    bool operator==(const child_iterator& rhs) const = default;
    bool operator!=(const child_iterator& rhs) const = default;
    inline bool operator>(const child_iterator& rhs) const {
			if (mNode == rhs.mNode) return false;
			return find_if(*this, child_iterator(), rhs);
		};
		inline bool operator<(const child_iterator& rhs) const {
			return rhs.operator>(*this);
		};
	};
	static_assert(input_iterator<child_iterator>);


	template<typename F> requires(invocable<F,Node&>)
	inline void for_each_descendant(Node& n, F&& fn) const {
		queue<Node*> nodes(mChildrenEdges.at(n));
		while (!nodes.empty()) {
			Node* n = nodes.front();
			nodes.pop();
			fn(forward<Node&>(*n));
			if (mChildrenEdges.count(n)) {
				nodes.resize(nodes.size() + n->mChildren.size());
				ranges::copy_backward(views::transform(n->mChildren, &get<unique_ptr<Node>>), nodes.end());
			}
		}
	}

	template<class T, typename... Args> requires(derived_from<T,Node> && constructible_from<T,Args...>)
	inline T& emplace(Args&&... args) {
		return *static_cast<T*>(mNodes[type_index(typeid(T))].emplace_back(make_unique<T>(forward<Args>(args)...)).get());
	}
	inline Node& emplace(unique_ptr<Node>&& n) {
		if (n->mParent == this) return *n;
		if (n->mParent) n = move(n->mParent->RemoveChild(*n));
		auto& ptr = mNodes.emplace_back(move(n));
		ptr->mParent = this;
		return *ptr;
	}
	template<class T> requires(derived_from<T,Node>)
	inline unique_ptr<T> erase(T* n) {
		auto p = mNodes.find(type_index(typeid(T)));
		if (p == mNodes.end()) return nullptr;
		p->second.erase(n);
	}
	inline bool erase(Node* n) {
		for (auto&[t,l] : mNodes)
			if (l.erase(n))
				return true;
		return false;
	}

};

template<typename transform_t>
class TransformNode : public Scene::Node {
protected:
	transform_t mLocal;
	transform_t mGlobal;
	bool mGlobalValid = false;

public:
	inline TransformNode(Scene& scene, const string& name) : Scene::Node(scene,name) {}

	inline void Local(const transform_t& m) { mLocal = m; mGlobalValid = false; }
	inline const transform_t& Local() const { return mLocal; }
	inline const transform_t& Global() { ValidateGlobal(); return mGlobal; }

	inline virtual void ValidateGlobal() {
		if (mGlobalValid) return;
		mGlobal = mLocal;
		auto* n = this;
		while (n->Parent()) {
			if (auto* t = n->Parent()->get_component<TransformNode<transform_t>>()) {
				if (t->mGlobalValid) {
					mGlobal = t->mGlobal * mGlobal;
					break;
				} else {
					mGlobal = t->mLocal * mGlobal;
				}
			}
			n = n->Parent();
		}
		mGlobalValid = true;
	}
};

}