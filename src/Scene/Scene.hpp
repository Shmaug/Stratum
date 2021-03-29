#pragma once

#include <functional>

#include "..\Stratum.hpp"

namespace stm {

class Scene {
public:

	friend class WeakNodePtr; // or just make index getters/etc public

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
	deque<unique_ptr<Node>> mTopologicalNodes; // weak topological ordering, also maybe use vector?
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
	};
	static_assert(input_iterator<parent_iterator>);

	using child_iterator = unordered_multimap<Node*, Node*>::iterator;
	static_assert(input_iterator<child_iterator>);

	class descendent_iterator {
	private:
		using internal_itr_t = unordered_multimap<Node*, Node*>::iterator;
		stack<pair<internal_itr_t, internal_itr_t>> mItrStack;
	public:
		using iterator_category = input_iterator_tag;
		using value_type        = Node;
		using pointer           = value_type*;
		using reference         = value_type&;

		inline descendent_iterator() : mItrStack() {}
		inline descendent_iterator(Node* node) {
			Scene& scene = node->mScene;
			while (scene.mChildrenEdges.contains(node)) {
				mItrStack.push(scene.mChildrenEdges.equal_range(node));
				node = mItrStack.top().first->second;
			}
		}
		descendent_iterator(const descendent_iterator&) = default;
			
		inline reference operator*() const { return *mItrStack.top().first->second; }
		inline pointer operator->() { return mItrStack.top().first->second; }

		inline descendent_iterator& operator++() {
			const auto cur = mItrStack.top().first;
			const auto next = std::next(cur);
			Scene& scene = cur->second->mScene;
			if(scene.mChildrenEdges.contains(cur->second)) {
				Node* node = cur->second;
				while (scene.mChildrenEdges.contains(node)) {
					mItrStack.push(scene.mChildrenEdges.equal_range(node));
					node = mItrStack.top().first->second;
				}
			}
			else if (next == mItrStack.top().second) {
				mItrStack.pop();
			}
			else {
				mItrStack.top().first++;
			}
			
			return *this;
		}
		inline descendent_iterator operator++(int) {
			descendent_iterator tmp(*this);
			operator++();
			return tmp;
		}

		inline operator bool() const { return !mItrStack.empty(); }

		bool operator==(const descendent_iterator& rhs) const = default;
		bool operator!=(const descendent_iterator& rhs) const = default;
	};
	static_assert(input_iterator<descendent_iterator>);

	inline parent_iterator parents_begin(Node* node) {
		return parent_iterator(node);
	}

	inline parent_iterator parents_end(Node* node = nullptr) {
		return parent_iterator();
	}

	using parents_subrange = ranges::subrange<parent_iterator>;
	inline parents_subrange parents(Node* node) {
		return ranges::subrange(parents_begin(node), parents_end(node));
	}

	inline child_iterator children_begin(Node* node) {
		auto [begin, end] = mChildrenEdges.equal_range(node);
		return begin;
	}

	inline child_iterator children_end(Node* node) {
		auto [begin, end] = mChildrenEdges.equal_range(node);
		return end;
	}

	using children_subrange = ranges::subrange<child_iterator>;
	inline children_subrange children(Node* node) {
		return ranges::subrange(children_begin(node), children_end(node));
	}

	inline descendent_iterator descendents_begin(Node* node) {
		return descendent_iterator(node);
	}

	inline descendent_iterator descendents_end(Node* node = nullptr) {
		return descendent_iterator();
	}

	using descendents_subrange = ranges::subrange<descendent_iterator>;
	inline descendents_subrange descendents(Node* node) {
		return ranges::subrange(descendents_begin(node), descendents_end(node));
	}

	/*
	 * This provides a weakly ordered iterator which guarantees that every parent will be evaluated before its children,
	 * but does not guarantee that it will iterate in bfs/dfs order (unless sort() has been called)
	 */
	inline auto nodes() {
		return views::transform(mTopologicalNodes, [](auto& uptr) { return uptr.get(); });
	}

	using type_iterator = unordered_multimap<type_index, Node*>::iterator;
	template<typename NodeType> requires(derived_from(NodeType, Node))
	inline type_iterator types_begin() {
		auto [begin, end] = mNodesByType.equal_range(typeid(NodeType));
		return begin;
	}

	template<typename NodeType> requires(derived_from(NodeType, Node))
	inline type_iterator types_end() {
		auto [begin, end] = mNodesByType.equal_range(typeid(NodeType));
		return end;
	}

	using type_subrange = ranges::subrange<type_iterator>;
	template<typename NodeType> requires(derived_from(NodeType, Node))
	inline void types()
	{
		return ranges::subrange(types_begin<NodeType>(), types_end<NodeType>());
	}

	inline Node* root() { return mTopologicalNodes.front().get(); }
	inline const Node* root() const { return mTopologicalNodes.front().get(); }

	template<typename NodeType> requires(derived_from(NodeType, Node))
	inline void insert(Node* parent, NodeType&& n) {
		unique_ptr<NodeType> tempPtr = make_unique<NodeType>(forward(n));
		mTopologicalNodes.push_back(move(tempPtr));
		Node* nodePtr = mTopologicalNodes.back().get();
		mNodesByType.insert(make_pair(typeid(NodeType), nodePtr));
		mParentEdges.insert(make_pair(nodePtr, parent));
		mChildrenEdges.insert(make_pair(parent, nodePtr));
	}

	template<typename NodeType, typename... Params>
	inline void emplace(Node* parent, Params&&... params) {
		unique_ptr<NodeType> tempPtr = make_unique<NodeType>(forward(params...));
		mTopologicalNodes.push_back(move(tempPtr));
		Node* nodePtr = mTopologicalNodes.back().get();
		mNodesByType.insert(make_pair(typeid(NodeType), nodePtr));
		mParentEdges.insert(make_pair(nodePtr, parent));
		mChildrenEdges.insert(make_pair(parent, nodePtr));
	}

	inline void insert(Node* parent, Scene scene) {
		Node* oldRoot = scene.root();
		const auto moveView = views::transform(scene.mTopologicalNodes, [](unique_ptr<Node>& p) { return move(p); });
		mTopologicalNodes.insert(end(mTopologicalNodes), begin(moveView), end(moveView));
		mNodesByType.insert(begin(scene.mNodesByType), end(scene.mNodesByType));

		mParentEdges.insert(make_pair(oldRoot, parent));
		mChildrenEdges.insert(make_pair(parent, oldRoot));
		mParentEdges.insert(begin(scene.mParentEdges), end(scene.mParentEdges));
		mChildrenEdges.insert(begin(scene.mChildrenEdges), end(scene.mChildrenEdges));
	}

	/*
	 * Expensive so it's better to remove in batches
	 */
	inline void remove(const vector<Node*>& nodes) {
		unordered_set<Node*> parents(begin(nodes), end(nodes));
		deque<unique_ptr<Node>> cleaned;

		size_t startIdx;
		for(startIdx = 0; !parents.contains(mTopologicalNodes[startIdx].get()); startIdx++);

		const auto removeBegin = next(begin(mTopologicalNodes), startIdx), removeEnd = end(mTopologicalNodes);
		for (auto removeItr = removeBegin; removeItr != removeEnd; removeItr++) {
			if (!parents.contains(removeItr->get())) {
				Node* parent = mParentEdges.at(removeItr->get());
				if(!parents.contains(parent)) {
					cleaned.push_back(move(*removeItr));
				}
				else {
					parents.insert(removeItr->get());
				}
			}
		}

		mTopologicalNodes.erase(removeBegin, removeEnd);
		mTopologicalNodes.insert(end(mTopologicalNodes), make_move_iterator(begin(cleaned)), make_move_iterator(end(cleaned)));
	}

	/* 
	 * sketch, assumes all nodes can be accessed by a root descendent iterator (there are no free nodes and no extra roots)
	 * releases all the pointers and then re-claims them when traversing
	 */
	inline void traversal_sort() {
		Node* r = root();
		for (unique_ptr<Node>& p : mTopologicalNodes) p.release();

		auto overwrite_inserter = inserter(mTopologicalNodes, begin(mTopologicalNodes));
		ranges::copy(views::transform(descendents(r), [](Node& n) { return unique_ptr<Node>(&n); }), overwrite_inserter);
	}

	template<typename F> requires(invocable<F,Node&>)
	inline void for_each(F&& fn) const {
		ranges::for_each(descendents(root()), forward(fn));
	}
};

// assumes all parents can be casted to TransformNode, doesn't cache intermediate parent transforms
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
		for(Node& parent : mScene.parents(this)) {
			TransformNode& t = static_cast<TransformNode&>(parent);
			if(t->mGlobalValid) {
				mGlobal = t->mGlobal * mGlobal;
				break;
			} else {
				mGlobal = t-> mLocal * mGlobal;
			}
		}
		mGlobalValid = true;
	}
};

// disgusting
class WeakNodePtr
{
public:
	explicit WeakNodePtr(Scene::Node* ptr) : mPtr(ptr) {
		if (!sIndexCache.contains(mPtr)) {
			sIndexCache.insert(make_pair(mPtr, 0));
		}
	}

	inline operator bool() const {
		if (!sIndexCache.contains(mPtr)) return false;

		const size_t nodeIndex = sIndexCache.at(mPtr);
		const Scene& scene = mPtr->mScene;
		if (scene.mTopologicalNodes[nodeIndex].get() != mPtr) {
			const auto itr = find_if(begin(scene.mTopologicalNodes), end(scene.mTopologicalNodes), 
				[this] (const auto& a) { return a.get() == mPtr; }
			);

			if (itr == end(scene.mTopologicalNodes)) {
				sIndexCache.erase(mPtr);
				return false;
			}
			
			sIndexCache[mPtr] = distance(begin(scene.mTopologicalNodes), itr);
		}
		return true;
	}

	inline size_t Index() const {
		return sIndexCache.at(mPtr);
	}

private:
	Scene::Node* mPtr;

	static unordered_map<Scene::Node*, size_t> sIndexCache;
};

using DelegateHandle = size_t;

template<typename FuncType>
class Delegate;

template<typename Ret, typename... Params>
class Delegate<Ret(Params...)> {
public:
	inline Delegate(Scene::Node* owner) : mOwner(owner) {}

	template<typename FuncType>
	inline DelegateHandle Add(FuncType&& fn) { 
		const DelegateHandle handle = mCallbacks.size();
		mCallbacks.push_back(forward(fn)); 
		return handle;
	}
	inline void Remove(const DelegateHandle handle) {
		const auto removeItr = next(cbegin(mCallbacks), handle);
		mCallbacks.remove(removeItr);
	}

	inline Ret Broadcast(Params&&... params) const {
		mCallbacks.front()(forward(params...));
	}
	inline void BroadcastAll(Params&&... params) const {
		for(const auto& fn : mCallbacks) {
			fn(forward(params...));
		}
	}

	inline Scene::Node* Owner() const { return mOwner; }

private:
	vector<function<Ret(Params...)>> mCallbacks;
	Scene::Node* mOwner;
};

class DelegateSubscriber {
public:
	inline DelegateSubscriber() = default;

	template<typename FuncType>
	void Subscribe(Delegate<FuncType>& delegate, FuncType&& fn)
	{
		const DelegateHandle handle = delegate.Add(forward(fn));

		DelegateRemover weakHandle = DelegateRemover{
			.ptr = WeakNodePtr(delegate.Owner()),
			.removeSelf = [handle, &delegate] () { delegate.Remove(handle); } 
		};

		mSubscriptions.push_back(std::move(weakHandle));
	}

	virtual ~DelegateSubscriber() {
		for(const DelegateRemover& handle : mSubscriptions) {
			if (handle.ptr) handle.removeSelf();
		}
	}

private:
	
	struct DelegateRemover
	{
		WeakNodePtr ptr;
		function<void()> removeSelf;
	};

	vector<DelegateRemover> mSubscriptions;
	const Scene& mScene;
};

}