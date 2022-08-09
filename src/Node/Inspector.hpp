#pragma once

#include "Gui.hpp"

namespace stm {

class Inspector {
public:
	STRATUM_API Inspector(Node& node);

	inline Node& node() const { return mNode; }
	inline Node* selected() const { return mSelected; }
	inline void select(Node* n) { mSelected = n; }

	template<typename T>
	inline void register_inspector_gui_fn(void(*fn_ptr)(Inspector&,T*)) {
		mInspectorGuiFns[typeid(T)] = reinterpret_cast<void(*)(Inspector&,void*)>(fn_ptr);
	}
	inline void unregister_inspector_gui_fn(type_index t) {
		mInspectorGuiFns.erase(t);
	}

	inline void component_ptr_field(const auto& p) {
		if (ImGui::Button(p.node().name().c_str()))
			select(&p.node());
	}

private:
    Node& mNode;
	Node* mSelected;
	unordered_map<type_index, void(*)(Inspector&,void*)> mInspectorGuiFns;

	void node_graph_gui_fn(Node& n);
};

}