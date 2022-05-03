//#pragma compile dxc -spirv -T cs_6_7 -HV 2021 -E kernel_forward
//#pragma compile dxc -spirv -T cs_6_7 -HV 2021 -E kernel_backward

#include "common/matrix_fragment.hlsli"

enum class Activation {
	eNone,
	eReLU,
	eExponential,
	eSine,
	eSigmoid,
	eSquareplus,
	eSoftplus,
};

#define WIDTH 1024
#define SKEW 8
#define INPUT_SKEW 8
#define OUT_T half
#define OUTPUT_LAYOUT layout_row_major
#define ACTIVATION Activation::eNone
#define INFERENCE false

#define N_ITERS (WIDTH >= 256 ? 2 : 8)
#define N_BLOCK_ROWS (INPUT_WIDTH/16)
#define N_BLOCKS (WIDTH / 16)

// Shared memory contains the intermediate activations of blockDim.y*16 elements.
// In some cases, it also contains the weight matrix for the first and last layer.
groupshared half shmem[(16 + 16 * N_ITERS) * (WIDTH + SKEW)];

void copy_int4(RWStructuredBuffer<half> dst, const uint dst_offset, const uint act_shmem) {

}
void copy_int4(const uint act_shmem, StructuredBuffer<half> src, const uint src_offset) {

}

template<typename OUT_T, bool BACKWARD>
void threadblock_layer(Activation activation,
	const uint act_shmem,
	StructuredBuffer<half> weights_this_layer_buf, const uint weights_this_layer_offset,
	RWStructuredBuffer<OUT_T> out_intermediate_threadblock_this_layer_buf, const uint out_intermediate_threadblock_this_layer_offset,
	StructuredBuffer<OUT_T> activation_aux_buf, const uint activation_aux_offset = -1) {
	// act_shmem contains the intermediate activations (shared memory) of the thread block's chunk of the batch.
	//           Can be forward activations or backward activations, depending on caller.
	// weights_this_layer points to the weight matrix of the current layer.
	// out_intermediate_threadblock_this_layer points to the location where intermediate activations produced by the thread block should be written to.
	//                  Can be nullptr if nothing should be written.
	// activation_aux points to additional arguments that the activation function may depend on. Points to the hidden forward activations when computing backward activations.

	// If we're performing the backward pass, weights must be loaded in transposed form, which
	// is achieved by interpreting the memory in row_major instead of col_major order.
	static const bool weights_layout_t = BACKWARD ? layout_row_major : layout_col_major;

	// Fragments
	MatrixFragment<half, layout_row_major> act_frag;
	MatrixFragment<half, weights_layout_t> weights_frag[N_BLOCKS];
	MatrixFragment<OUT_T, layout_row_major> result_frag[N_ITERS];

	// Indices
	const uint li = threadIdx.x; // index in warp ("lane index")
	const uint wi = threadIdx.y; // index in block ("warp index")

	const uint lane_offset = (8 * li) % WIDTH;
	const uint row = (8 * li + wi * 8 * 32) / WIDTH;

	const uint weights_col = 16 * wi;

	GroupMemoryBarrierWithGroupSync();

	// Load N_BLOCKS chunks of weights from global memory into registers.
	#pragma unroll
	for (uint i = 0; i < N_BLOCKS; ++i) {
		if (BACKWARD) {
			// If we're performing the backward pass, additional index swizzling is needed to
			// load the weights in transposed form.
			weights_frag[i].load(weights_this_layer_buf, weights_this_layer_offset + 16 * i * WIDTH + weights_col, WIDTH);
		} else {
			weights_frag[i].load(weights_this_layer_buf, weights_this_layer_offset + 16 * i + weights_col * WIDTH, WIDTH);
		}
	}

	#pragma unroll
	for (int l = 0; l < N_ITERS; ++l) {
		result_frag[l].fill(0.0f);

		#pragma unroll
		for (uint i = 0; i < N_BLOCKS; ++i) {
			// Load a chunk of intermediate activations from shared memory and multiply with chunk of weights
			act_frag.load(shmem, act_shmem + 16 * i + (16 * l) * (WIDTH + SKEW), WIDTH + SKEW);
			result_frag[l].mma(act_frag, weights_frag[i]);
		}

		// Activation
		if (BACKWARD) {
			// Load the temporary forward matrix for the relu transfer
			act_frag.load(activation_aux_buf, activation_aux_offset + weights_col + l * 16 * WIDTH, WIDTH);
			warp_activation_backward<half>(activation, result_frag[l], act_frag, result_frag[l]);
		} else {
			warp_activation<half>(activation, result_frag[l], result_frag[l]);
		}
	}

	GroupMemoryBarrierWithGroupSync();

	#pragma unroll
	for (int l = 0; l < N_ITERS; ++l) {
		store_matrix_fragment(act_shmem, weights_col, l * 16 * (WIDTH + SKEW), result_frag[l], WIDTH + SKEW, layout_row_major);
	}

	if (out_intermediate_threadblock_this_layer_offset != -1) {
		GroupMemoryBarrierWithGroupSync();

		#pragma unroll
		for (int l = 0; l < N_ITERS; ++l)
			copy_int4(out_intermediate_threadblock_this_layer_buf, out_intermediate_threadblock_this_layer_offset + lane_offset + (row + 16 * l) * WIDTH, act_shmem + lane_offset + (row + 16 * l) * (WIDTH + SKEW));
	}
}

template<typename OUT_T, uint INPUT_LAYOUT>
void threadblock_input_layer_forward_dynamic(Activation activation,
	const uint act_shmem,
	StructuredBuffer<half> input_threadblock_buf, const uint input_threadblock_offset,
	StructuredBuffer<half> weights_this_layer_buf, const uint weights_this_layer_offset,
	RWStructuredBuffer<OUT_T> out_intermediate_threadblock_this_layer_buf, const uint out_intermediate_threadblock_this_layer_offset,
	const uint in_width, const uint batch_size) {
	// act_shmem contains the intermediate activations (shared memory) of the thread block's chunk of the batch
	// input_threadblock points to the thread block's chunk of the input batch in global memory
	// weights_this_layer points to the weight matrix of the current layer
	// out_intermediate_threadblock_this_layer points to the location where intermediate activations produced by the thread block should be written to.
	//                  Can be nullptr if nothing should be written.
	// in_width is the dynamic width of the input layer

	// Fragments
	MatrixFragment<half, INPUT_LAYOUT> act_frag;
	MatrixFragment<half, layout_col_major> weights_frag;
	MatrixFragment<OUT_T, layout_row_major> result_frag[N_ITERS];

	// Indices
	const uint li = threadIdx.x; // index in warp ("lane index")
	const uint wi = threadIdx.y; // index in block ("warp index")

	const uint lane_offset = (8 * li) % WIDTH;
	const uint row = (8 * li + wi * 8 * 32) / WIDTH;

	const uint weights_col = 16 * wi;

	const uint weights_shmem = act_shmem + 16 * (in_width + INPUT_SKEW);

	// Load input weight matrix (fits completely into shared memory)
	// Each thread can load 8 fp16 elements (16 bytes) at once; we have N_BLOCKS warps
	const uint n_elems_per_load = N_BLOCKS * 32 * 8;
	const uint thread_elem_idx = (li + wi * 32) * 8;

	const uint n_elems_b = WIDTH * in_width;

	#pragma unroll
	for (uint idx = thread_elem_idx; idx < n_elems_b; idx += n_elems_per_load) {
		const uint idx_skewed = idx + idx / in_width * INPUT_SKEW;
		copy_int4(weights_shmem + idx_skewed, weights_this_layer_buf, weights_this_layer_offset + idx);
	}

	const uint n_tensor_ops = in_width / 16;

	if (INPUT_LAYOUT == layout_col_major) {
		GroupMemoryBarrierWithGroupSync();
	}

	#pragma unroll
	for (int l = 0; l < N_ITERS; ++l) {
		if (INPUT_LAYOUT == layout_row_major) {
			// Load chunk of inputs into shmem.
			// This is faster than loading it from gmem directly, even though it is only used once.
			// (Possibly due to latency hiding through staging.)
			const uint n_elems_a = 16 * in_width;

			#pragma unroll
			for (uint idx = thread_elem_idx; idx < n_elems_a; idx += n_elems_per_load) {
				const uint idx_skewed = idx + idx / in_width * INPUT_SKEW;
				copy_int4(act_shmem + idx_skewed, input_threadblock_buf, input_threadblock_offset + l * n_elems_a + idx);
			}

			GroupMemoryBarrierWithGroupSync();
		}

		fill_matrix_fragment(result_frag[l], 0.0f);
		#pragma unroll
		for (uint i = 0; i < n_tensor_ops; ++i) {
			// Load chunk of inputs and weights from shared memory and multiply them
			if (INPUT_LAYOUT == layout_row_major) {
				act_frag.load(act_shmem, 16 * i, in_width + INPUT_SKEW);
			} else {
				act_frag.load(input_threadblock_buf, input_threadblock_offset + 16 * i * batch_size + 16 * l, batch_size);
			}
			weights_frag.load(shmem, weights_shmem + 16 * i + weights_col * (in_width + INPUT_SKEW), in_width + INPUT_SKEW);
			result_frag[l].mma(act_frag, weights_frag);
		}

		if (INPUT_LAYOUT == layout_row_major) {
			GroupMemoryBarrierWithGroupSync();
		}

		warp_activation<half>(activation, result_frag[l], result_frag[l]);
	}

	if (INPUT_LAYOUT == layout_col_major) {
		GroupMemoryBarrierWithGroupSync();
	}

	#pragma unroll
	for (int l = 0; l < N_ITERS; ++l) {
		result_frag[l].store(shmem, act_shmem + weights_col + (16 * l) * (WIDTH + SKEW), WIDTH + SKEW, layout_row_major);
	}

	if (out_intermediate_threadblock_this_layer_offset != -1) {
		GroupMemoryBarrierWithGroupSync();

		#pragma unroll
		for (int i = 0; i < N_ITERS; ++i) {
			copy_int4(out_intermediate_threadblock_this_layer_buf, out_intermediate_threadblock_this_layer_offset + lane_offset + (row + 16 * i) * WIDTH, act_shmem + lane_offset + (row + 16 * i) * (WIDTH + SKEW));
		}
	}
}

template<typename OUT_T>
void threadblock_last_layer_forward(Activation activation,
	const uint act_shmem,
	StructuredBuffer<half> weights_this_layer_buf, const uint weights_this_layer_offset,
	RWStructuredBuffer<OUT_T> output_buf, const uint output_offset,
	const uint output_stride, const uint output_layout) {
	// act_shmem contains the intermediate activations (shared memory) of the thread block's chunk of the batch
	// weights_this_layer points to the weight matrix of the current layer
	// out points to the location where the result produced by the thread block should be written to.
	//   Can be nullptr if nothing should be written.

	// Fragments
	MatrixFragment<half, layout_row_major> act_frag;
	MatrixFragment<half, layout_col_major> weights_frag[N_BLOCKS];
	MatrixFragment<OUT_T, layout_row_major> result_frag;

	// Indices
	const uint li = threadIdx.x; // index in warp ("lane index")
	const uint wi = threadIdx.y; // index in block ("warp index")

	const uint weights_shmem = act_shmem + N_ITERS * 16 * (WIDTH + SKEW);

	const uint weights_row = (8 * li) % WIDTH;
	const uint weights_col = (8 * li + 8 * 32 * wi) / WIDTH;

	// Load weight matrix into shared memory for the last multiplication.
	// Loading into shared memory as opposed to directly into registers is faster
	// because unlike in the previous layers, each warp uses the same entries of the weight matrix.
	copy_int4(weights_shmem + weights_row + weights_col * (WIDTH + SKEW), weights_this_layer_buf, weights_this_layer_offset + weights_row + weights_col * WIDTH);

	GroupMemoryBarrierWithGroupSync();

	#pragma unroll
	for (uint i = 0; i < N_BLOCKS; ++i)
		weights_frag[i].load(shmem, weights_shmem + 16 * i, WIDTH + SKEW);

	// Perform last layer by parallelizing over iters
	for (uint idx = wi; idx < N_ITERS; idx += N_BLOCKS) {
		result_frag.fill(0.0f);
		#pragma unroll
		for (uint i = 0; i < N_BLOCKS; ++i) {
			// Load a chunk of intermediate activations from shared memory and multiply with chunk of the weight matrix
			act_frag.load(shmem, act_shmem + 16 * i + (16 * idx) * (WIDTH + SKEW), WIDTH + SKEW);
			result_frag.mma(act_frag, weights_frag[i]);
		}

		warp_activation<half>(activation, result_frag, result_frag);

		if (output_layout == layout_row_major) {
			result_frag.store(output_buf, output_offset + idx * 16 * output_stride, output_stride, output_layout);
		} else {
			result_frag.store(output_buf, output_offset + idx * 16, output_stride, output_layout);
		}
	}
}

template<typename buf_t>
void threadblock_load_input_static(const uint act_shmem, buf_t input_threadblock_buf, const uint input_threadblock_offset) {
	// Indices
	const uint li = threadIdx.x; // index in warp ("lane index")
	const uint wi = threadIdx.y; // index in block ("warp index")

	const uint lane_offset = (8 * li) % WIDTH;
	const uint row = (8 * li + wi * 8 * 32) / WIDTH;

	#pragma unroll
	for (int i = 0; i < N_ITERS; ++i)
		copy_int4(act_shmem + lane_offset + (row + 16 * i) * (WIDTH + SKEW), input_threadblock_buf, input_threadblock_offset + lane_offset + (row + 16 * i) * WIDTH);
}
void threadblock_write_output_static(const uint act_shmem, RWStructuredBuffer<half> output_threadblock_buf, const uint output_threadblock_offset) {
	// output_threadblock will be filled by the thread block's act_shmem

	// Indices
	const uint li = threadIdx.x; // index in warp ("lane index")
	const uint wi = threadIdx.y; // index in block ("warp index")

	const uint lane_offset = (8 * li) % WIDTH;
	const uint row = (8 * li + wi * 8 * 32) / WIDTH;

	GroupMemoryBarrierWithGroupSync();

	#pragma unroll
	for (int i = 0; i < N_ITERS; ++i)
		copy_int4(output_threadblock_buf, output_threadblock_offset + lane_offset + (row + 16 * i) * WIDTH, act_shmem + lane_offset + (row + 16 * i) * (WIDTH + SKEW));
}

const Activation output_activation;
StructuredBuffer<half> input;
StructuredBuffer<half> weights;
RWStructuredBuffer<OUT_T> out_intermediate;
RWStructuredBuffer<OUT_T> output;
const uint output_stride;
const uint batch_size;
const uint in_width;
const uint out_width;
const uint n_hidden_matmuls;
const uint input_layout;
const uint output_layout;

[numthreads(32,N_BLOCK_ROWS,1)]
void kernel_forward() {
	// `input` points to the input matrix. Can be any width.
	// `weights` points to the weight matrices (contiguous in memory).
	// `out_intermediate` points to the memory where intermediate activations should be written. When performing inference, a value of nullptr is expected (intermediate results are not written).
	// `out` points to the memory where the network output should be written. (Output width is assumed to be 16 neurons.)

	// Commented out due to isolated strange side-effects on Windows
	// if (INFERENCE) {
	// 	assert(out_intermediate == nullptr);
	// } else {
	// 	assert(out_intermediate);
	// }


	// Each block computes exactly one 16-element chunk of the batch.
	const uint elem_idx = 16 * blockIdx.x * N_ITERS;

	const uint act_shmem = 0;

	// First layer
	if (input_layout == layout_col_major || in_width != WIDTH) {
		if (input_layout == layout_row_major) {
			threadblock_input_layer_forward_dynamic<OUT_T, layout_row_major>(ACTIVATION, act_shmem, input, elem_idx * in_width, weights, 0, out_intermediate, !INFERENCE ? (elem_idx * WIDTH) : -1, in_width, batch_size);
		} else {
			threadblock_input_layer_forward_dynamic<OUT_T, layout_col_major>(ACTIVATION, act_shmem, input, elem_idx, weights, 0, out_intermediate, !INFERENCE ? (elem_idx * WIDTH) : -1, in_width, batch_size);
		}
	} else {
		// If the input has the same width & layout as the hidden layers, we can simply use the network's regular layer routine (with static size)
		// instead of using the slower dynamic input layer routine.
		threadblock_load_input_static(act_shmem, input, elem_idx * WIDTH);
		threadblock_layer<OUT_T, false>(ACTIVATION, act_shmem, weights, 0, out_intermediate, !INFERENCE ? (elem_idx * WIDTH) : -1);
	}

	const uint first_weights_stride = WIDTH * in_width;
	const uint weights_stride = WIDTH * WIDTH;
	const uint layer_stride = WIDTH * batch_size;

	// Hidden layers
	for (uint k = 0; k < n_hidden_matmuls; ++k) {
		threadblock_layer<OUT_T, false>(ACTIVATION, act_shmem, weights, first_weights_stride + weights_stride * k, out_intermediate, !INFERENCE ? (layer_stride * (k + 1) + elem_idx * WIDTH) : -1);
	}

	if (out_width > 16) {
		// In the forward pass, intermediate activations are already written out.
		if (INFERENCE) {
			threadblock_write_output_static(act_shmem, out_intermediate, elem_idx * WIDTH);
		}
	} else if (output) {
		// Last layer
		if (output_layout == layout_row_major) {
			threadblock_last_layer_forward<OUT_T>(output_activation, act_shmem, weights, first_weights_stride + weights_stride * n_hidden_matmuls, output, elem_idx * output_stride, output_stride, output_layout);
		} else {
			threadblock_last_layer_forward<OUT_T>(output_activation, act_shmem, weights, first_weights_stride + weights_stride * n_hidden_matmuls, output, elem_idx                , output_stride, output_layout);
		}
	}
}

StructuredBuffer<half> dL_doutput;
StructuredBuffer<half> forward;
StructuredBuffer<half> weights_first_layer;
RWStructuredBuffer<OUT_T> dL_dinput;

[numthreads(32,N_BLOCK_ROWS,1)]
void kernel_backward() {
	// `dL_doutput` points to the input matrix of the backward pass, i.e. the loss gradients. Assumed to be 16 neurons wide.
	// `weights` points to the weight matrices (contiguous in memory).
	// `out_intermediate` points to the memory where backpropagated activation gradients should be written.
	// `forward` points to the memory where the intermediate activations of the forward pass are located. (needed for activation backprop)

	// Indices
	const uint li = threadIdx.x; // index in warp ("lane index")
	const uint wi = threadIdx.y; // index in block ("warp index")
	const uint bi = blockIdx.x;  // block index

	const uint lane_offset = (8 * li) % WIDTH;
	const uint row = (8 * li + wi * 8 * 32) / WIDTH;

	// Multipying one 16-row chunk of intermediate activations with the weight matrix requires all warps of the block.
	// Thus, each block computes exactly one 16-row chunk of the next layer's intermediate activations.
	const uint elem_idx_base = 16 * bi * N_ITERS;
	const uint elem_idx = elem_idx_base;

	const uint weights_stride = WIDTH * WIDTH;
	const uint layer_stride = WIDTH * batch_size;

	// Backprop through last layer
	if (out_width <= 16) {
		// Fragments in registers
		MatrixFragment<half, OUTPUT_LAYOUT> act_frag;
		MatrixFragment<half, layout_row_major> weights_frag;
		MatrixFragment<half> result_frag[N_ITERS];

		// Load the relevant chunk of the last layer's weight matrix from global memory into registers
		const uint weights_col = 16 * wi;

		weights_frag.load(weights, weights_stride * n_hidden_matmuls + weights_col, WIDTH);

		#pragma unroll
		for (int l = 0; l < N_ITERS; ++l) {
			fill_matrix_fragment(result_frag[l], 0.0f);

			// Load a chunk of output gradients from shared memory and multiply with previously loaded weights
			if (OUTPUT_LAYOUT == layout_row_major) {
				act_frag.load(dL_doutput, (elem_idx + 16 * l) * output_stride, output_stride);
			} else {
				act_frag.load(dL_doutput, (elem_idx + 16 * l), output_stride);
			}

			// NOTE: activation transfer of the _output_ activation is expected to be done _prior_ to calling this kernel
			//       in a separate pass, because the tranfered activation gradient is also needed to compute the weight
			//       gradient of the last weight matrix (see backward()).
			result_frag[l].mma(act_frag, weights_frag);

			// Load the temporary forward matrix for the relu transfer
			MatrixFragment<half, layout_row_major> forward_frag;
			forward_frag.load(forward, layer_stride * n_hidden_matmuls + weights_col + (elem_idx + l * 16) * WIDTH, WIDTH);

			warp_activation_backward<half>(ACTIVATION, result_frag[l], forward_frag, result_frag[l]);
		}

		GroupMemoryBarrierWithGroupSync();

		#pragma unroll
		for (int l = 0; l < N_ITERS; ++l) {
			result_frag[l].store(shmem, weights_col, (16 * l) * (WIDTH + SKEW), WIDTH + SKEW, layout_row_major);
		}

		GroupMemoryBarrierWithGroupSync();

		#pragma unroll
		for (int i = 0; i < N_ITERS; ++i) {
			copy_int4(out_intermediate, 0 + lane_offset + (row + elem_idx + i * 16) * WIDTH, lane_offset + (row + 16 * i) * (WIDTH + SKEW));
		}
	} else {
		// If the output width is larger than 16, we will have used CUTLASS for backpropping through the last layer.
		// Load the resulting gradients.
		threadblock_load_input_static(0, out_intermediate, elem_idx * WIDTH);
	}

	// Backprop through hidden layers
	for (uint k = 0; k < n_hidden_matmuls; ++k) {
		threadblock_layer<half, true>(ACTIVATION, 0, weights, weights_stride * (n_hidden_matmuls - k - 1), out_intermediate, layer_stride * (k + 1) + elem_idx_base * WIDTH, forward, layer_stride * (n_hidden_matmuls - k - 1) + elem_idx_base * WIDTH);
	}

	// Compute loss gradients w.r.t. input if desired.
	// THIS CODE ASSUMES THAT THE INPUT WIDTH IS THE SAME AS THE NETWORK WIDTH
	// AND THAT THE INPUT LAYOUT IS THE SAME AS THE HIDDEN LAYOUT.
	// DON'T PASS A NON-NULL dL_dinput IF THIS REQUIREMENT IS NOT MET.
	if (dL_dinput != -1) {
		threadblock_layer<half, true>(Activation::eNone, 0, weights_first_layer, 0, dL_dinput, elem_idx_base * WIDTH);
	}
}
