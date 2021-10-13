/*
Copyright (c) 2016 Wojciech Jarosz. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

You are under no obligation whatsoever to provide any bug fixes, patches, or
upgrades to the features, functionality or performance of the source code
("Enhancements") to anyone; however, if you choose to make your Enhancements
available either publicly, or directly to the authors of this software, without
imposing a separate written license agreement for such Enhancements, then you
hereby grant the following license: a non-exclusive, royalty-free perpetual
license to install, use, modify, prepare derivative works, incorporate into
other computer software, distribute, and sublicense such enhancements or
derivative works thereof, in binary and source code form.
*/

/*!
    force-random-dither.cpp -- Generate a dither matrix using the force-random-dither method from:

	W. Purgathofer, R. F. Tobler and M. Geiler.
	"Forced random dithering: improved threshold matrices for ordered dithering"
	Image Processing, 1994. Proceedings. ICIP-94., IEEE International Conference,
	Austin, TX, 1994, pp. 1032-1035 vol.2.
	doi: 10.1109/ICIP.1994.413512

    \author Wojciech Jarosz
*/

#include "common.hpp"
#include <random>

namespace stm {

template<ranges::contiguous_range R> requires(is_arithmetic_v<ranges::range_value_t<R>>)
inline void generate_blue_noise(R& M, size_t Sm) {
	const size_t Smk = Sm*Sm;
	
	using T = ranges::range_value_t<R>;

	vector<T> forceField(Smk);
	ranges::fill(M, (T)0);
	ranges::fill(forceField, (T)0);

	// initialize free locations
	vector<size_t> freeLocations(Smk);
	for (size_t i = 0; i < Smk; ++i)
		freeLocations[i] = i;

	random_device rd;
	mt19937 gen{rd()};
	printf("Generating noise...");
	for (T ditherValue = 0; ditherValue < Smk; ++ditherValue) {
		ranges::shuffle(freeLocations, gen);
		
		T minimum = T(1e20);
		Array2i minimumLocation = Array2i::Zero();
		size_t halfP = min(max((size_t)1, (size_t)sqrt(freeLocations.size()*3/4)), freeLocations.size());
		for (size_t i = 0; i < halfP; ++i) {
			const size_t& location = freeLocations[i];
			const T ff = forceField[location];
			if (ff < minimum) {
				minimum = ff;
				minimumLocation[0] = location%Sm;
				minimumLocation[1] = location/Sm;
			}
		}
		
		Array2i cell;
		for (cell.y() = 0; cell.y() < Sm; cell.y()++)
			for (cell.x() = 0; cell.x() < Sm; cell.x()++) {
				Array2i x0 = cell.min(minimumLocation);
				Array2i x1 = cell.max(minimumLocation);
				Array2i delta = (x1 - x0).min(x0 + Array2i(Sm,Sm) - x1);
				T r2 = delta[0]*delta[0] + delta[1]*delta[1];
				T r = sqrt(r2);				
				forceField[cell.x() + cell.y()*Sm] += exp(-sqrt(2*r));
			}

		size_t idx = minimumLocation.x() + minimumLocation.y()*Sm;
		freeLocations.erase(remove(freeLocations.begin(), freeLocations.end(), idx), freeLocations.end());
		if constexpr (floating_point<T>)
			M[idx] = (float)ditherValue/(float)Smk;
		else
			M[idx] = ditherValue;
		
		printf("\rGenerating noise (%.2f %%)", 100*(float)ditherValue/(float)Smk);
	}
	printf("Generated noise        \n");
}

}