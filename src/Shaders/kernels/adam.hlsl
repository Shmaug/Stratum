inline float weight_decay(const float relative_weight_decay, const float absolute_weight_decay, const float weight) {
	// Relative weight decay is closely related to l2 regularization, whereas absolute weight decay corresponds to l1 regularization
	return (1 - relative_weight_decay) * weight - (weight < 0 ? -abs(absolute_weight_decay) : abs(absolute_weight_decay));
}

const uint32_t n_elements;
const uint32_t n_matrix_weights;
const float relative_weight_decay;
const float absolute_weight_decay;
const float loss_scale;
float learning_rate;
const float non_matrix_learning_rate_factor;
const bool optimize_matrix_params;
const bool optimize_non_matrix_params;
const float beta1;
const float beta2;
const float epsilon;
const float lower_lr_bound;
const float upper_lr_bound;
const float l2_reg;

#define T half

RWStructuredBuffer<float> weights_full_precision;
RWStructuredBuffer<float> first_moments;
RWStructuredBuffer<float> second_moments;
RWStructuredBuffer<uint32_t> param_steps;

#pragma compile dxc -spirv -T cs_6_7 -HV 2021 -E step

RWStructuredBuffer<T> weights;
StructuredBuffer<T> gradients;

[numthreads(128,1,1)]
void step(uint3 threadIdx : SV_DispatchThreadID) {
	const uint i = threadIdx.x;
	if (i >= n_elements) return;

	float gradient = (float)gradients[i] / loss_scale;
	if (i >= n_matrix_weights) {
		if (!optimize_non_matrix_params || gradient == 0) {
			return;
		}
	} else {
		if (!optimize_matrix_params) {
			return;
		}
	}

	const float weight_fp = weights_full_precision[i];

	if (i < n_matrix_weights) {
		// No L2 reg for non-matrix params
		gradient += l2_reg * weight_fp;
	}

	const float gradient_sq = gradient * gradient;

	float first_moment = first_moments[i] = beta1 * first_moments[i] + (1 - beta1) * gradient;
	const float second_moment = second_moments[i] = beta2 * second_moments[i] + (1 - beta2) * gradient_sq;

	float learning_rate = learning_rate;

	if (i >= n_matrix_weights) {
		// Potentially different learning rate for non-matrix params
		learning_rate *= non_matrix_learning_rate_factor;
	}

	// Debiasing. Since some parameters might see fewer steps than others, they each need their own step counter.
	const uint current_step = ++param_steps[i];
	learning_rate *= sqrt(1 - pow(beta2, (float)current_step)) / (1 - pow(beta1, (float)current_step));

	// Follow AdaBound paradigm
	const float effective_learning_rate = min(max(learning_rate / (sqrt(second_moment) + epsilon), lower_lr_bound), upper_lr_bound);

	const float decayed_weight = weight_decay(relative_weight_decay * learning_rate, absolute_weight_decay * learning_rate, weight_fp);
	const float new_weight = decayed_weight - effective_learning_rate * first_moment;

	weights_full_precision[i] = new_weight;
	weights[i] = (T)new_weight;
}
