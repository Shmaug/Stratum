#ifndef DIST2_H
#define DIST2_H

#ifdef __HLSL__

inline uint upper_bound(StructuredBuffer<float> data, uint first, uint last, float value) {
	uint it;
	int count = last - first;
	while (count > 0) {
		uint it = first;
		int step = count / 2;
		it += step;
		if (value >= data[it]) {
			first = ++it;
			count -= step + 1;
		} else
			count = step;
	}
	return first;
}

float dist1d_pdf(StructuredBuffer<float> data, const uint pdf, int id) {
    return data[pdf + id];
}
int dist1d_sample(StructuredBuffer<float> data, const uint cdf, const uint n, float rnd_param) {
    const uint ptr = upper_bound(data, cdf, cdf + n + 1, rnd_param) - cdf;
    return clamp(int(ptr - 1), 0, n - 1);
}

inline float dist2d_pdf(StructuredBuffer<float> data, const uint pdf_marginals, const uint pdf_rows, const uint w, const uint h, const float2 uv) {
	const int x = clamp(uv.x * w, 0, w - 1);
	const int y = clamp(uv.y * h, 0, h - 1);
	// What's the PDF for sampling row y?
	const float pdf_y = data[pdf_marginals + y];
	// What's the PDF for sampling row x?
	const float pdf_x = data[pdf_rows + y * w + x];
	return pdf_y * pdf_x * w * h;
}
inline float2 dist2d_sample(StructuredBuffer<float> data, const uint cdf_marginals, const uint cdf_rows, const uint w, const uint h, const float2 rnd) {
	const uint y_ptr = upper_bound(data, cdf_marginals, cdf_marginals + h + 1, rnd.y) - cdf_marginals;
	const int y_offset = clamp(int(y_ptr - 1), 0, h - 1);
	// Uniformly remap u1 to find the continuous offset
	float dy = rnd.y - data[cdf_marginals + y_offset];
	if ((data[cdf_marginals + y_offset + 1] - data[cdf_marginals + y_offset]) > 0)
		dy /= (data[cdf_marginals + y_offset + 1] - data[cdf_marginals + y_offset]);

	// Sample a column at the row y_offset
	const int row_offset = y_offset * (w + 1);
	const uint x_ptr = upper_bound(data, cdf_rows + row_offset, cdf_rows + row_offset + w + 1, rnd.x) - cdf_rows;
	const int x_offset = clamp(int(x_ptr) - row_offset - 1, 0, w - 1);
	// Uniformly remap u2
	float dx = rnd.x - data[cdf_rows + row_offset + x_offset];
	if (data[cdf_rows + row_offset + x_offset + 1] - data[cdf_rows + row_offset + x_offset] > 0)
		dx /= (data[cdf_rows + row_offset + x_offset + 1] - data[cdf_rows + row_offset + x_offset]);

	return float2((x_offset + dx) / w, (y_offset + dy) / h);
}

#endif

#ifdef __cplusplus

// expects pmf to be dist.size() and cdf to be dist.size()+1
inline void build_distribution(const vector<float>& dist, span<float> pmf, span<float> cdf) {
    cdf[0] = 0;
    for (uint32_t i = 0; i < dist.size(); i++)
        cdf[i + 1] = cdf[i] + pmf[i];

    const float total = cdf.back();
    if (total > 0) {
		const float inv_total = 1 / total;
        for (uint32_t i = 0; i < pmf.size(); i++) {
            pmf[i] *= inv_total;
            cdf[i] *= inv_total;
        }
    } else {
		const float inv_total = 1 / (float)pmf.size();
        for (uint32_t i = 0; i < pmf.size(); i++) {
            pmf[i] = inv_total;
            cdf[i] = i * inv_total;
        }
        cdf.back() = 1;
    }
}

inline void build_distributions(const span<float4>& img, const vk::Extent2D& extent, span<float> pdf_marginals, span<float> pdf_rows, span<float> cdf_marginals, span<float> cdf_rows) {
	const float invHeight = 1 / (float)extent.height;
	auto f = [&](uint32_t x, uint32_t y) {
		return luminance(img[y*extent.width + x].head<3>()) * sin(M_PI * (y + 0.5f)*invHeight);
	};

	// Construct a 1D distribution for each row
	for (int y = 0; y < extent.height; y++) {
		cdf_rows[y * (extent.width + 1)] = 0;
		for (int x = 0; x < extent.width; x++) {
			cdf_rows[y * (extent.width + 1) + (x + 1)] = cdf_rows[y * (extent.width + 1) + x] + f(x,y);
		}
		float integral = cdf_rows[y * (extent.width + 1) + extent.width];
		if (integral > 0) {
			// Normalize
			for (int x = 0; x < extent.width; x++) {
				cdf_rows[y * (extent.width + 1) + x] /= integral;
			}
			// Note that after normalization, the last entry of each row for
			// cdf_rows is still the "integral".
			// We need this later for constructing the marginal distribution.

			// Setup the pmf/pdf
			for (int x = 0; x < extent.width; x++) {
				pdf_rows[y * extent.width + x] = f(x,y) / integral;
			}
		} else {
			// We shouldn't sample this row, but just in case we
			// set up a uniform distribution.
			for (int x = 0; x < extent.width; x++) {
				pdf_rows[y * extent.width + x] = float(1) / float(extent.width);
				cdf_rows[y * (extent.width + 1) + x] = float(x) / float(extent.width);
			}
			cdf_rows[y * (extent.width + 1) + extent.width] = 1;
		}
	}

	// Now construct the marginal CDF for each column.
	cdf_marginals[0] = 0;
	for (int y = 0; y < extent.height; y++) {
		float weight = cdf_rows[y * (extent.width + 1) + extent.width];
		cdf_marginals[y + 1] = cdf_marginals[y] + weight;
	}
	float total_values = cdf_marginals.back();
	if (total_values > 0) {
		// Normalize
		for (int y = 0; y < extent.height; y++) {
			cdf_marginals[y] /= total_values;
		}
		cdf_marginals[extent.height] = 1;
		// Setup pdf cols
		for (int y = 0; y < extent.height; y++) {
			float weight = cdf_rows[y * (extent.width + 1) + extent.width];
			pdf_marginals[y] = weight / total_values;
		}
	} else {
		// The whole thing is black...why are we even here?
		// Still set up a uniform distribution.
		for (int y = 0; y < extent.height; y++) {
			pdf_marginals[y] = float(1) / float(extent.height);
			cdf_marginals[y] = float(y) / float(extent.height);
		}
		cdf_marginals[extent.height] = 1;
	}
	// We finally normalize the last entry of each cdf row to 1
	for (int y = 0; y < extent.height; y++) {
		cdf_rows[y * (extent.width + 1) + extent.width] = 1;
	}
}

#endif

#endif