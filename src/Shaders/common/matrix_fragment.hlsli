#ifndef MATRIX_FRAGMENT_H
#define MATRIX_FRAGMENT_H

#define layout_row_major 0
#define layout_col_major 1

// 16x16 matrix fragment
template<typename T, uint layout>
struct MatrixFragment {
	T m[16*16];

	inline void load(StructuredBuffer<T> buf, const uint offset, const uint width) {

	}
	inline void load(RWStructuredBuffer<T> buf, const uint offset, const uint width) {

	}


	inline void store(RWStructuredBuffer<T> buf, const uint offset, const uint width, const bool mem_layout) {

	}

	inline void load(const T buf[], const uint offset, const uint width) {

	}

	inline void store(T buf[], const uint offset, const uint width, const bool mem_layout) {

	}

	template<uint a_layout, uint b_layout>
	inline void mma(const MatrixFragment<T,a_layout> a, const MatrixFragment<T,b_layout> b) {

	}

	inline void fill(const float value) {

	}
};


#endif