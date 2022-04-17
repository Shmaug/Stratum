#include "Gui.hpp"

namespace stm {

class Inspector {
public:
	STRATUM_API Inspector(Node& node);

	inline Node& node() const { return mNode; }
    
	template<typename T>
	inline void register_inspector_gui_fn(void(*fn_ptr)(T*)) {
		mInspectorGuiFns[typeid(T)] = reinterpret_cast<void(*)(void*)>(fn_ptr);
	}
	inline void unregister_inspector_gui_fn(type_index t) {
		mInspectorGuiFns.erase(t);
	}

private:
    Node& mNode;
	unordered_map<type_index, void(*)(void*)> mInspectorGuiFns;
};

}