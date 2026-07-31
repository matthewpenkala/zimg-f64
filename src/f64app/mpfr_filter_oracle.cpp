#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <mpfr.h>

#include "resize/filter.h"

namespace {

constexpr mpfr_prec_t PRECISION = 256;
constexpr mpfr_rnd_t ROUNDING = MPFR_RNDN;

class Mpfr {
	mpfr_t value_;

public:
	Mpfr() { mpfr_init2(value_, PRECISION); }
	Mpfr(const Mpfr &other)
	{
		mpfr_init2(value_, PRECISION);
		mpfr_set(value_, other.value_, ROUNDING);
	}
	Mpfr &operator=(const Mpfr &other)
	{
		if (this != &other)
			mpfr_set(value_, other.value_, ROUNDING);
		return *this;
	}
	~Mpfr() { mpfr_clear(value_); }

	mpfr_ptr get() { return value_; }
	mpfr_srcptr get() const { return value_; }
};

void set_ratio(Mpfr &value, long numerator, unsigned long denominator)
{
	mpfr_set_si(value.get(), numerator, ROUNDING);
	mpfr_div_ui(value.get(), value.get(), denominator, ROUNDING);
}

void spline36(const Mpfr &input, Mpfr &result)
{
	Mpfr x;
	mpfr_abs(x.get(), input.get(), ROUNDING);

	long offset;
	long n0;
	unsigned long d0;
	long n1;
	unsigned long d1;
	long n2;
	unsigned long d2;
	long n3;
	unsigned long d3;

	if (mpfr_cmp_ui(x.get(), 1) < 0) {
		offset = 0;
		n0 = 1;   d0 = 1;
		n1 = -3;  d1 = 209;
		n2 = -453; d2 = 209;
		n3 = 13;  d3 = 11;
	} else if (mpfr_cmp_ui(x.get(), 2) < 0) {
		offset = 1;
		n0 = 0;    d0 = 1;
		n1 = -156; d1 = 209;
		n2 = 270;  d2 = 209;
		n3 = -6;   d3 = 11;
	} else if (mpfr_cmp_ui(x.get(), 3) < 0) {
		offset = 2;
		n0 = 0;   d0 = 1;
		n1 = 26;  d1 = 209;
		n2 = -45; d2 = 209;
		n3 = 1;   d3 = 11;
	} else {
		mpfr_set_zero(result.get(), 0);
		return;
	}

	mpfr_sub_si(x.get(), x.get(), offset, ROUNDING);

	Mpfr c0;
	Mpfr c1;
	Mpfr c2;
	Mpfr c3;
	set_ratio(c0, n0, d0);
	set_ratio(c1, n1, d1);
	set_ratio(c2, n2, d2);
	set_ratio(c3, n3, d3);

	mpfr_mul(result.get(), c3.get(), x.get(), ROUNDING);
	mpfr_add(result.get(), result.get(), c2.get(), ROUNDING);
	mpfr_mul(result.get(), result.get(), x.get(), ROUNDING);
	mpfr_add(result.get(), result.get(), c1.get(), ROUNDING);
	mpfr_mul(result.get(), result.get(), x.get(), ROUNDING);
	mpfr_add(result.get(), result.get(), c0.get(), ROUNDING);
}

struct OracleRow {
	std::map<unsigned, Mpfr> coefficients;
	unsigned nonzero_left = std::numeric_limits<unsigned>::max();
	unsigned nonzero_right = 0;
};

OracleRow make_oracle_row(unsigned src_dim, unsigned dst_dim, double shift_d,
	                      double width_d, unsigned row)
{
	// zimg's authoritative graph and sample geometry is binary64. Freeze those
	// phase decisions first, then use MPFR for the exact-rational kernel,
	// normalization, folding, and coefficient accumulation at that geometry.
	double scale_d = static_cast<double>(dst_dim) / width_d;
	double step_d = std::min(scale_d, 1.0);
	double support_d = 3.0 / step_d;
	unsigned long filter_size =
		std::max(static_cast<unsigned long>(std::ceil(support_d)) * 2UL, 1UL);
	double pos_d = (static_cast<double>(row) + 0.5) / scale_d + shift_d;
	double begin_d = pos_d - static_cast<double>(filter_size) / 2.0;
	begin_d = begin_d < 0.0 ?
		std::floor(begin_d + 0.5) :
		std::floor(begin_d + std::nextafter(0.5, 0.0));
	begin_d += 0.5;

	Mpfr step;
	mpfr_set_d(step.get(), step_d, ROUNDING);
	Mpfr pos;
	mpfr_set_d(pos.get(), pos_d, ROUNDING);
	Mpfr begin;
	mpfr_set_d(begin.get(), begin_d, ROUNDING);

	std::vector<Mpfr> weights(filter_size);
	Mpfr total;
	mpfr_set_zero(total.get(), 0);
	for (unsigned long j = 0; j < filter_size; ++j) {
		Mpfr distance;
		mpfr_set(distance.get(), begin.get(), ROUNDING);
		mpfr_add_ui(distance.get(), distance.get(), j, ROUNDING);
		mpfr_sub(distance.get(), distance.get(), pos.get(), ROUNDING);
		mpfr_mul(distance.get(), distance.get(), step.get(), ROUNDING);
		spline36(distance, weights[j]);
		mpfr_add(total.get(), total.get(), weights[j].get(), ROUNDING);
	}

	OracleRow result;
	for (unsigned long j = 0; j < filter_size; ++j) {
		Mpfr xpos;
		mpfr_set(xpos.get(), begin.get(), ROUNDING);
		mpfr_add_ui(xpos.get(), xpos.get(), j, ROUNDING);

		Mpfr real;
		if (mpfr_sgn(xpos.get()) < 0) {
			mpfr_neg(real.get(), xpos.get(), ROUNDING);
		} else if (mpfr_cmp_ui(xpos.get(), src_dim) >= 0) {
			mpfr_set_ui(real.get(), 2U * src_dim, ROUNDING);
			mpfr_sub(real.get(), real.get(), xpos.get(), ROUNDING);
		} else {
			mpfr_set(real.get(), xpos.get(), ROUNDING);
		}

		if (mpfr_sgn(real.get()) < 0)
			mpfr_set_zero(real.get(), 0);
		if (mpfr_cmp_ui(real.get(), src_dim) >= 0)
			mpfr_set_ui(real.get(), src_dim - 1U, ROUNDING);

		mpfr_floor(real.get(), real.get());
		unsigned index = static_cast<unsigned>(mpfr_get_ui(real.get(), ROUNDING));

		Mpfr normalized;
		mpfr_div(normalized.get(), weights[j].get(), total.get(), ROUNDING);
		if (!mpfr_zero_p(normalized.get())) {
			result.nonzero_left = std::min(result.nonzero_left, index);
			result.nonzero_right = std::max(result.nonzero_right, index + 1U);
		}
		auto [it, inserted] = result.coefficients.try_emplace(index);
		if (inserted)
			mpfr_set_zero(it->second.get(), 0);
		mpfr_add(it->second.get(), it->second.get(), normalized.get(), ROUNDING);
	}
	return result;
}

uint64_t ordered_double(double value)
{
	uint64_t bits;
	static_assert(sizeof(bits) == sizeof(value));
	std::memcpy(&bits, &value, sizeof(bits));
	return (bits & (UINT64_C(1) << 63)) ? ~bits : (bits | (UINT64_C(1) << 63));
}

uint64_t ulp_distance(double a, double b)
{
	uint64_t aa = ordered_double(a);
	uint64_t bb = ordered_double(b);
	return aa > bb ? aa - bb : bb - aa;
}

struct Results {
	uint64_t rows = 0;
	uint64_t coefficients = 0;
	uint64_t geometry_mismatches = 0;
	double max_abs_error = 0.0;
	uint64_t max_ulp_error = 0;
	double max_ulp_actual = 0.0;
	double max_ulp_oracle = 0.0;
	unsigned max_ulp_src = 0;
	unsigned max_ulp_dst = 0;
	unsigned max_ulp_row = 0;
	unsigned max_ulp_index = 0;
	double max_row_sum_error = 0.0;
};

void check_context(Results &results, unsigned src_dim, unsigned dst_dim,
	               double shift, double width)
{
	zimg::resize::Spline36Filter spline36_filter;
	zimg::resize::FilterContextF64 actual =
		zimg::resize::compute_filter_f64(spline36_filter, src_dim, dst_dim, shift, width);

	for (unsigned row = 0; row < dst_dim; ++row) {
		OracleRow oracle = make_oracle_row(src_dim, dst_dim, shift, width, row);
		unsigned actual_left = actual.left[row];
		unsigned actual_right = actual_left + actual.filter_width;
		if (oracle.nonzero_left < actual_left || oracle.nonzero_right > actual_right) {
			++results.geometry_mismatches;
			std::cerr << "geometry mismatch src=" << src_dim << " dst=" << dst_dim
			          << " shift=" << shift << " width=" << width << " row=" << row
			          << " oracle=[" << oracle.nonzero_left << ',' << oracle.nonzero_right
			          << ") actual=[" << actual_left << ',' << actual_right << ")\n";
		}

		double row_sum = 0.0;
		for (unsigned j = 0; j < actual.filter_width; ++j) {
			unsigned index = actual_left + j;
			double actual_value = actual.data[static_cast<size_t>(row) * actual.stride + j];
			double oracle_value = 0.0;
			auto it = oracle.coefficients.find(index);
			if (it != oracle.coefficients.end())
				oracle_value = mpfr_get_d(it->second.get(), ROUNDING);

			results.max_abs_error = std::max(results.max_abs_error, std::abs(actual_value - oracle_value));
			// ULP distance is not meaningful around exact kernel zeros and
			// cancellation; the absolute-error gate covers that region.
			if (std::abs(oracle_value) >= 1.0e-1) {
				uint64_t distance = ulp_distance(actual_value, oracle_value);
				if (distance > results.max_ulp_error) {
					results.max_ulp_error = distance;
					results.max_ulp_actual = actual_value;
					results.max_ulp_oracle = oracle_value;
					results.max_ulp_src = src_dim;
					results.max_ulp_dst = dst_dim;
					results.max_ulp_row = row;
					results.max_ulp_index = index;
				}
			}
			row_sum += actual_value;
			++results.coefficients;
		}
		results.max_row_sum_error = std::max(results.max_row_sum_error, std::abs(row_sum - 1.0));
		++results.rows;
	}
}

} // namespace

int main()
{
	try {
		Results results;

		// Every row covers every production phase and all folded borders.
		check_context(results, 3840, 1920, 0.0, 3840.0);
		check_context(results, 2160, 1080, 0.0, 2160.0);
		check_context(results, 1920, 960, -0.25, 1920.0);
		check_context(results, 2160, 540, 0.0, 2160.0);

		// Deterministic small-size, active-width, and dyadic-shift adversaries.
		uint32_t state = UINT32_C(0x5A17C9E3);
		for (unsigned n = 0; n < 160; ++n) {
			state = state * UINT32_C(1664525) + UINT32_C(1013904223);
			unsigned src = 1U + state % 48U;
			state = state * UINT32_C(1664525) + UINT32_C(1013904223);
			unsigned dst = 1U + state % 48U;
			state = state * UINT32_C(1664525) + UINT32_C(1013904223);
			double shift = static_cast<double>(static_cast<int>(state % 33U) - 16) / 8.0;
			state = state * UINT32_C(1664525) + UINT32_C(1013904223);
			double width = static_cast<double>(1U + state % 96U) / 2.0;
			check_context(results, src, dst, shift, width);
		}

		std::cout << std::setprecision(17)
		          << "{\"oracle\":\"MPFR-256/GMP exact-rational-Spline36\","
		          << "\"rows\":" << results.rows << ','
		          << "\"coefficients\":" << results.coefficients << ','
		          << "\"geometry_mismatches\":" << results.geometry_mismatches << ','
		          << "\"max_abs_coefficient_error\":" << results.max_abs_error << ','
		          << "\"max_ulp_coefficient_error\":" << results.max_ulp_error << ','
		          << "\"max_ulp_actual\":" << results.max_ulp_actual << ','
		          << "\"max_ulp_oracle\":" << results.max_ulp_oracle << ','
		          << "\"max_ulp_context\":\"" << results.max_ulp_src << 'x' << results.max_ulp_dst
		          << " row " << results.max_ulp_row << " input " << results.max_ulp_index << "\","
		          << "\"max_row_sum_error\":" << results.max_row_sum_error << "}\n";

		if (results.geometry_mismatches ||
		    results.max_abs_error > 1.0e-14 ||
		    results.max_ulp_error > 64 ||
		    results.max_row_sum_error > 2.0e-14) {
			std::cerr << "MPFR coefficient oracle gate failed\n";
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "mpfr_filter_oracle: " << e.what() << '\n';
		return 1;
	}
}
