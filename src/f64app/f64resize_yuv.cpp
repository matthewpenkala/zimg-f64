/*
 * Format-specific binary64 reference resizer for deterministic validation.
 *
 * Canonical semantics are pinned to:
 *   https://github.com/sekrit-twc/zimg
 *   commit 1ad1895d5ff0bbe69c61243f9996aede713d1b5f
 *
 * The Spline36 kernel and filter matrix are built by the pinned upstream
 * implementation. The extension in resize/filter.{h,cpp} retains the
 * normalized matrix in binary64. This executable preserves that precision in
 * coefficients, accumulators, and the inter-axis image. It performs no RGB or
 * transfer conversion, no range remapping, no intermediate clipping, and no
 * intermediate quantization. Only the final 10-bit code-value store rounds
 * (nearest-even) and clamps to the representable [0, 1023] container.
 */

#include <algorithm>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#ifdef _WIN32
  #include <fcntl.h>
  #include <io.h>
#endif

#include "zimg/resize/filter.h"
#include "geometry.h"

namespace {

using zimg_f64_reference::AxisGeometry;
using zimg_f64_reference::ChromaLocationH;
using zimg_f64_reference::ChromaLocationW;
using zimg_f64_reference::PlaneGeometry;
using zimg_f64_reference::make_axis_geometry;
using zimg_f64_reference::make_luma_geometry;
using zimg_f64_reference::make_progressive_chroma_geometry;

constexpr const char *ZIMG_COMMIT = "1ad1895d5ff0bbe69c61243f9996aede713d1b5f";
constexpr unsigned SRC_W = 3840;
constexpr unsigned SRC_H = 2160;
constexpr unsigned DST_W = 1920;
constexpr unsigned DST_H = 1080;
constexpr unsigned SRC_SUB_W = 1;
constexpr unsigned SRC_SUB_H = 0;
constexpr unsigned DST_SUB_W = 1;
constexpr unsigned DST_SUB_H = 1;
constexpr unsigned CODE_MAX = 1023;

struct Options {
	std::string input_path;
	std::string output_path;
	std::string prequant_path;
	unsigned threads = std::max(1U, std::thread::hardware_concurrency());
	uint64_t max_frames = 0;
	bool audit_only = false;
	bool precision_f32 = false;
};

struct PlanePlan {
	PlaneGeometry src;
	PlaneGeometry dst;
	AxisGeometry horizontal;
	AxisGeometry vertical;
	bool horizontal_first;
};

struct PlaneStats {
	uint16_t input_min = std::numeric_limits<uint16_t>::max();
	uint16_t input_max = 0;
	double prequant_min = std::numeric_limits<double>::infinity();
	double prequant_max = -std::numeric_limits<double>::infinity();
	uint64_t clipped_low = 0;
	uint64_t clipped_high = 0;
	uint64_t samples = 0;

	void merge(const PlaneStats &other)
	{
		input_min = std::min(input_min, other.input_min);
		input_max = std::max(input_max, other.input_max);
		prequant_min = std::min(prequant_min, other.prequant_min);
		prequant_max = std::max(prequant_max, other.prequant_max);
		clipped_low += other.clipped_low;
		clipped_high += other.clipped_high;
		samples += other.samples;
	}
};

template <class Context>
struct FilterPair {
	Context horizontal;
	Context vertical;
};

bool resize_h_first(double xscale, double yscale)
{
	// resize/resize.cpp::resize_h_first.
	double h_first_cost = std::max(xscale, 1.0) * 2.0 + xscale * std::max(yscale, 1.0);
	double v_first_cost = std::max(yscale, 1.0) + yscale * std::max(xscale, 1.0) * 2.0;
	return h_first_cost < v_first_cost;
}

PlanePlan make_plan(const PlaneGeometry &src, const PlaneGeometry &dst)
{
	AxisGeometry horizontal = make_axis_geometry(src.width, dst.width,
	                                             src.active_left, src.active_width,
	                                             dst.active_left, dst.active_width);
	AxisGeometry vertical = make_axis_geometry(src.height, dst.height,
	                                           src.active_top, src.active_height,
	                                           dst.active_top, dst.active_height);
	double xscale = static_cast<double>(dst.width) / horizontal.subwidth;
	double yscale = static_cast<double>(dst.height) / vertical.subwidth;
	return{ src, dst, horizontal, vertical, resize_h_first(xscale, yscale) };
}

template <class FP>
using ContextFor = std::conditional_t<std::is_same_v<FP, double>,
                                      zimg::resize::FilterContextF64,
                                      zimg::resize::FilterContext>;

template <class FP>
ContextFor<FP> compute_context(const zimg::resize::Filter &filter, const AxisGeometry &axis)
{
	if constexpr (std::is_same_v<FP, double>)
		return zimg::resize::compute_filter_f64(filter, axis.src_dim, axis.dst_dim, axis.shift, axis.subwidth);
	else
		return zimg::resize::compute_filter(filter, axis.src_dim, axis.dst_dim, axis.shift, axis.subwidth);
}

template <class FP>
FilterPair<ContextFor<FP>> make_filters(const PlanePlan &plan)
{
	zimg::resize::Spline36Filter spline36;
	return{
		compute_context<FP>(spline36, plan.horizontal),
		compute_context<FP>(spline36, plan.vertical)
	};
}

template <class Func>
void parallel_chunks(size_t count, unsigned requested_threads, Func func)
{
	if (!count)
		return;

	unsigned nthreads = static_cast<unsigned>(std::min<size_t>(std::max(1U, requested_threads), count));
	std::vector<std::thread> workers;
	workers.reserve(nthreads > 0 ? nthreads - 1 : 0);

	for (unsigned t = 1; t < nthreads; ++t) {
		size_t begin = count * t / nthreads;
		size_t end = count * (t + 1) / nthreads;
		workers.emplace_back([=, &func] { func(t, begin, end); });
	}

	func(0, 0, count / nthreads);
	for (auto &worker : workers)
		worker.join();
}

template <class FP, class Context>
void vertical_u16_to_fp(const std::vector<uint16_t> &src, unsigned src_width,
	                    const Context &filter, std::vector<FP> &dst, unsigned threads)
{
	parallel_chunks(filter.filter_rows, threads, [&](unsigned, size_t begin, size_t end)
	{
		for (size_t y = begin; y < end; ++y) {
			FP *dst_row = dst.data() + y * src_width;
			std::fill_n(dst_row, src_width, static_cast<FP>(0));
			unsigned top = filter.left[y];
			const auto *coeffs = filter.data.data() + y * filter.stride;

			for (unsigned k = 0; k < filter.filter_width; ++k) {
				FP coeff = static_cast<FP>(coeffs[k]);
				const uint16_t *src_row = src.data() + static_cast<size_t>(top + k) * src_width;
				for (unsigned x = 0; x < src_width; ++x)
					dst_row[x] = std::fma(coeff, static_cast<FP>(src_row[x]), dst_row[x]);
			}
		}
	});
}

template <class FP, class Context>
void horizontal_u16_to_fp(const std::vector<uint16_t> &src, unsigned src_width, unsigned src_height,
	                      const Context &filter, std::vector<FP> &dst, unsigned threads)
{
	parallel_chunks(src_height, threads, [&](unsigned, size_t begin, size_t end)
	{
		for (size_t y = begin; y < end; ++y) {
			const uint16_t *src_row = src.data() + y * src_width;
			FP *dst_row = dst.data() + y * filter.filter_rows;

			for (unsigned x = 0; x < filter.filter_rows; ++x) {
				unsigned left = filter.left[x];
				const auto *coeffs = filter.data.data() + static_cast<size_t>(x) * filter.stride;
				FP accum = static_cast<FP>(0);
				for (unsigned k = 0; k < filter.filter_width; ++k)
					accum = std::fma(static_cast<FP>(coeffs[k]), static_cast<FP>(src_row[left + k]), accum);
				dst_row[x] = accum;
			}
		}
	});
}

template <class FP>
uint16_t quantize_final(FP value, PlaneStats &stats)
{
	double value_d = static_cast<double>(value);
	stats.prequant_min = std::min(stats.prequant_min, value_d);
	stats.prequant_max = std::max(stats.prequant_max, value_d);
	++stats.samples;

	long q = std::lrint(value);
	if (q < 0) {
		++stats.clipped_low;
		q = 0;
	} else if (q > static_cast<long>(CODE_MAX)) {
		++stats.clipped_high;
		q = CODE_MAX;
	}
	return static_cast<uint16_t>(q);
}

template <class FP, class Context>
PlaneStats horizontal_fp_to_u16(const std::vector<FP> &src, unsigned src_width, unsigned height,
	                            const Context &filter, std::vector<uint16_t> &dst,
	                            std::vector<double> *prequant, unsigned threads)
{
	unsigned nthreads = static_cast<unsigned>(std::min<size_t>(std::max(1U, threads), height));
	std::vector<PlaneStats> thread_stats(nthreads);

	parallel_chunks(height, threads, [&](unsigned thread_id, size_t begin, size_t end)
	{
		PlaneStats &stats = thread_stats[thread_id];
		for (size_t y = begin; y < end; ++y) {
			const FP *src_row = src.data() + y * src_width;
			uint16_t *dst_row = dst.data() + y * filter.filter_rows;

			for (unsigned x = 0; x < filter.filter_rows; ++x) {
				unsigned left = filter.left[x];
				const auto *coeffs = filter.data.data() + static_cast<size_t>(x) * filter.stride;
				FP accum = static_cast<FP>(0);
				for (unsigned k = 0; k < filter.filter_width; ++k)
					accum = std::fma(static_cast<FP>(coeffs[k]), src_row[left + k], accum);
				if (prequant)
					(*prequant)[y * filter.filter_rows + x] = static_cast<double>(accum);
				dst_row[x] = quantize_final(accum, stats);
			}
		}
	});

	PlaneStats result;
	for (const PlaneStats &stats : thread_stats)
		result.merge(stats);
	return result;
}

template <class FP, class Context>
PlaneStats vertical_fp_to_u16(const std::vector<FP> &src, unsigned src_width,
	                          const Context &filter, std::vector<uint16_t> &dst,
	                          std::vector<double> *prequant, unsigned threads)
{
	unsigned nthreads = static_cast<unsigned>(std::min<size_t>(std::max(1U, threads), filter.filter_rows));
	std::vector<PlaneStats> thread_stats(nthreads);

	parallel_chunks(filter.filter_rows, threads, [&](unsigned thread_id, size_t begin, size_t end)
	{
		PlaneStats &stats = thread_stats[thread_id];
		for (size_t y = begin; y < end; ++y) {
			unsigned top = filter.left[y];
			const auto *coeffs = filter.data.data() + y * filter.stride;
			uint16_t *dst_row = dst.data() + y * src_width;

			for (unsigned x = 0; x < src_width; ++x) {
				FP accum = static_cast<FP>(0);
				for (unsigned k = 0; k < filter.filter_width; ++k) {
					const FP *src_row = src.data() + static_cast<size_t>(top + k) * src_width;
					accum = std::fma(static_cast<FP>(coeffs[k]), src_row[x], accum);
				}
				if (prequant)
					(*prequant)[y * src_width + x] = static_cast<double>(accum);
				dst_row[x] = quantize_final(accum, stats);
			}
		}
	});

	PlaneStats result;
	for (const PlaneStats &stats : thread_stats)
		result.merge(stats);
	return result;
}

template <class FP>
PlaneStats resample_plane(const std::vector<uint16_t> &src, const PlanePlan &plan,
	                      const FilterPair<ContextFor<FP>> &filters,
	                      std::vector<uint16_t> &dst, std::vector<double> *prequant,
	                      unsigned threads)
{
	PlaneStats stats;
	for (uint16_t x : src) {
		stats.input_min = std::min(stats.input_min, x);
		stats.input_max = std::max(stats.input_max, x);
	}

	if (plan.horizontal_first) {
		std::vector<FP> intermediate(static_cast<size_t>(plan.dst.width) * plan.src.height);
		horizontal_u16_to_fp<FP>(src, plan.src.width, plan.src.height,
		                         filters.horizontal, intermediate, threads);
		PlaneStats final_stats = vertical_fp_to_u16<FP>(intermediate, plan.dst.width,
		                                                filters.vertical, dst, prequant, threads);
		final_stats.input_min = stats.input_min;
		final_stats.input_max = stats.input_max;
		return final_stats;
	}

	std::vector<FP> intermediate(static_cast<size_t>(plan.src.width) * plan.dst.height);
	vertical_u16_to_fp<FP>(src, plan.src.width, filters.vertical, intermediate, threads);
	PlaneStats final_stats = horizontal_fp_to_u16<FP>(intermediate, plan.src.width, plan.dst.height,
	                                                  filters.horizontal, dst, prequant, threads);
	final_stats.input_min = stats.input_min;
	final_stats.input_max = stats.input_max;
	return final_stats;
}

bool read_exact(std::istream &stream, void *ptr, size_t bytes, bool allow_clean_eof)
{
	stream.read(static_cast<char *>(ptr), static_cast<std::streamsize>(bytes));
	std::streamsize got = stream.gcount();
	if (got == 0 && allow_clean_eof && stream.eof())
		return false;
	if (got != static_cast<std::streamsize>(bytes))
		throw std::runtime_error("truncated raw frame");
	return true;
}

void write_exact(std::ostream &stream, const void *ptr, size_t bytes)
{
	stream.write(static_cast<const char *>(ptr), static_cast<std::streamsize>(bytes));
	if (!stream)
		throw std::runtime_error("raw output write failed");
}

template <class Context>
double max_row_sum_error(const Context &ctx)
{
	double max_error = 0.0;
	for (unsigned i = 0; i < ctx.filter_rows; ++i) {
		double sum = 0.0;
		for (unsigned k = 0; k < ctx.filter_width; ++k)
			sum += static_cast<double>(ctx.data[static_cast<size_t>(i) * ctx.stride + k]);
		max_error = std::max(max_error, std::abs(sum - 1.0));
	}
	return max_error;
}

double max_f64_f32_coefficient_delta(const zimg::resize::FilterContextF64 &f64,
	                                 const zimg::resize::FilterContext &f32)
{
	if (f64.filter_width != f32.filter_width || f64.filter_rows != f32.filter_rows)
		throw std::runtime_error("f64/f32 context geometry mismatch");

	double max_delta = 0.0;
	for (unsigned i = 0; i < f64.filter_rows; ++i) {
		if (f64.left[i] != f32.left[i])
			throw std::runtime_error("f64/f32 left-offset mismatch");
		for (unsigned k = 0; k < f64.filter_width; ++k) {
			double a = f64.data[static_cast<size_t>(i) * f64.stride + k];
			double b = static_cast<double>(f32.data[static_cast<size_t>(i) * f32.stride + k]);
			max_delta = std::max(max_delta, std::abs(a - b));
		}
	}
	return max_delta;
}

uint64_t multiply_mod_u64(uint64_t a, uint64_t b) noexcept
{
	// Compute the low 64 bits without relying on an overflowing unsigned
	// multiplication. This keeps the diagnostic FNV-1a hash compatible with
	// modulo-2^64 semantics while allowing Clang's strict integer sanitizer
	// to remain enabled without a suppression.
	uint64_t a_lo = static_cast<uint32_t>(a);
	uint64_t a_hi = a >> 32;
	uint64_t b_lo = static_cast<uint32_t>(b);
	uint64_t b_hi = b >> 32;
	uint64_t low_product = a_lo * b_lo;
	uint64_t cross_low = (a_hi * b_lo & UINT64_C(0xFFFFFFFF)) +
	                     (a_lo * b_hi & UINT64_C(0xFFFFFFFF));
	uint64_t high = ((low_product >> 32) + cross_low) & UINT64_C(0xFFFFFFFF);
	return (high << 32) | static_cast<uint32_t>(low_product);
}

uint64_t hash_f64_context(const zimg::resize::FilterContextF64 &ctx)
{
	uint64_t hash = 1469598103934665603ULL;
	auto mix = [&](const void *ptr, size_t bytes)
	{
		const auto *p = static_cast<const unsigned char *>(ptr);
		for (size_t i = 0; i < bytes; ++i) {
			hash ^= p[i];
			hash = multiply_mod_u64(hash, UINT64_C(1099511628211));
		}
	};

	mix(&ctx.filter_width, sizeof(ctx.filter_width));
	mix(&ctx.filter_rows, sizeof(ctx.filter_rows));
	for (unsigned i = 0; i < ctx.filter_rows; ++i) {
		mix(&ctx.left[i], sizeof(ctx.left[i]));
		mix(&ctx.data[static_cast<size_t>(i) * ctx.stride],
		    static_cast<size_t>(ctx.filter_width) * sizeof(double));
	}
	return hash;
}

void audit_axis(const char *name, const AxisGeometry &axis)
{
	zimg::resize::Spline36Filter filter;
	auto f64 = zimg::resize::compute_filter_f64(filter, axis.src_dim, axis.dst_dim, axis.shift, axis.subwidth);
	auto f32 = zimg::resize::compute_filter(filter, axis.src_dim, axis.dst_dim, axis.shift, axis.subwidth);

	std::cerr << std::setprecision(17)
	          << "{\"axis\":\"" << name
	          << "\",\"src\":" << axis.src_dim
	          << ",\"dst\":" << axis.dst_dim
	          << ",\"shift\":" << axis.shift
	          << ",\"active_width\":" << axis.subwidth
	          << ",\"taps\":" << f64.filter_width
	          << ",\"f64_row_sum_max_error\":" << max_row_sum_error(f64)
	          << ",\"f32_row_sum_max_error\":" << max_row_sum_error(f32)
	          << ",\"max_f64_f32_coeff_delta\":" << max_f64_f32_coefficient_delta(f64, f32)
	          << ",\"f64_fnv1a64\":\"0x" << std::hex << hash_f64_context(f64) << std::dec
	          << "\"}\n";
}

void run_audit(const PlanePlan &luma, const PlanePlan &chroma)
{
	if (luma.horizontal.shift != 0.0 || luma.vertical.shift != 0.0)
		throw std::runtime_error("unexpected luma phase");

	// Do not generalize this result to another siting, crop, or active region.
	// For the explicitly assumed full-frame progressive LEFT -> LEFT case:
	//   src active_left = 0 * 1/2 - (-1/2 + 1/2 * 1/2) = +0.25
	//   dst active_left = 0 * 1/2 - (-1/2 + 1/2 * 1/2) = +0.25
	//   active scale = 960 / 1920 = 0.5
	// GraphBuilder then gives shift = src_left - dst_left / active_scale.
	double source_chroma_active_left = chroma.src.active_left;
	double destination_chroma_active_left = chroma.dst.active_left;
	double chroma_active_scale = chroma.dst.active_width / chroma.src.active_width;
	double expected_chroma_shift = source_chroma_active_left -
	                               destination_chroma_active_left / chroma_active_scale;
	if (source_chroma_active_left != 0.25 ||
	    destination_chroma_active_left != 0.25 ||
	    chroma_active_scale != 0.5 ||
	    chroma.horizontal.shift != expected_chroma_shift ||
	    expected_chroma_shift != -0.25 ||
	    chroma.vertical.shift != 0.0)
		throw std::runtime_error("unexpected progressive LEFT chroma phase");
	if (luma.horizontal_first || chroma.horizontal_first)
		throw std::runtime_error("unexpected axis order; pinned zimg semantics require vertical first");

	std::cerr << "{\"implementation\":\"zimg-f64-reference\",\"zimg_commit\":\""
	          << ZIMG_COMMIT << "\",\"axis_order\":\"vertical-then-horizontal\","
	          << "\"input\":\"3840x2160 yuv422p10le progressive left\","
	          << "\"output\":\"1920x1080 yuv420p10le progressive left\","
	          << "\"rounding\":\"nearest-even-final-only\"}\n";
	std::cerr << std::setprecision(17)
	          << "{\"chroma_phase_derivation\":{"
	          << "\"source_assumption\":\"progressive 4:2:2 LEFT\","
	          << "\"destination\":\"progressive 4:2:0 LEFT\","
	          << "\"source_active_left\":" << source_chroma_active_left << ','
	          << "\"destination_active_left\":" << destination_chroma_active_left << ','
	          << "\"horizontal_active_scale\":" << chroma_active_scale << ','
	          << "\"formula\":\"src_left-dst_left/scale\","
	          << "\"horizontal_shift_source_chroma_samples\":" << expected_chroma_shift << ','
	          << "\"vertical_shift_source_chroma_samples\":" << chroma.vertical.shift
	          << "}}\n";
	audit_axis("Y-horizontal", luma.horizontal);
	audit_axis("Y-vertical", luma.vertical);
	audit_axis("C-horizontal", chroma.horizontal);
	audit_axis("C-vertical", chroma.vertical);
}

Options parse_options(int argc, char **argv)
{
	Options options;
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		auto require_value = [&](const char *name) -> std::string
		{
			if (++i >= argc)
				throw std::runtime_error(std::string("missing value for ") + name);
			return argv[i];
		};

		if (arg == "--input")
			options.input_path = require_value("--input");
		else if (arg == "--output")
			options.output_path = require_value("--output");
		else if (arg == "--prequant-output")
			options.prequant_path = require_value("--prequant-output");
		else if (arg == "--threads")
			options.threads = static_cast<unsigned>(std::stoul(require_value("--threads")));
		else if (arg == "--max-frames")
			options.max_frames = std::stoull(require_value("--max-frames"));
		else if (arg == "--audit-only")
			options.audit_only = true;
		else if (arg == "--precision") {
			std::string precision = require_value("--precision");
			if (precision == "f32")
				options.precision_f32 = true;
			else if (precision != "f64")
				throw std::runtime_error("--precision must be f64 or f32");
		} else {
			throw std::runtime_error("unknown option: " + arg);
		}
	}
	if (!options.threads)
		throw std::runtime_error("--threads must be positive");
	return options;
}

template <class FP>
int run_stream(const Options &options, std::istream &input, std::ostream &output,
	           const PlanePlan &luma_plan, const PlanePlan &chroma_plan)
{
	auto luma_filters = make_filters<FP>(luma_plan);
	auto chroma_filters = make_filters<FP>(chroma_plan);

	const size_t src_y_samples = static_cast<size_t>(SRC_W) * SRC_H;
	const size_t src_c_samples = static_cast<size_t>(SRC_W >> SRC_SUB_W) * (SRC_H >> SRC_SUB_H);
	const size_t dst_y_samples = static_cast<size_t>(DST_W) * DST_H;
	const size_t dst_c_samples = static_cast<size_t>(DST_W >> DST_SUB_W) * (DST_H >> DST_SUB_H);

	std::vector<uint16_t> src_y(src_y_samples);
	std::vector<uint16_t> src_u(src_c_samples);
	std::vector<uint16_t> src_v(src_c_samples);
	std::vector<uint16_t> dst_y(dst_y_samples);
	std::vector<uint16_t> dst_u(dst_c_samples);
	std::vector<uint16_t> dst_v(dst_c_samples);
	std::vector<double> prequant_y;
	std::vector<double> prequant_u;
	std::vector<double> prequant_v;
	std::ofstream prequant_file;

	if (!options.prequant_path.empty()) {
		prequant_y.resize(dst_y_samples);
		prequant_u.resize(dst_c_samples);
		prequant_v.resize(dst_c_samples);
		prequant_file.open(options.prequant_path, std::ios::binary | std::ios::trunc);
		if (!prequant_file)
			throw std::runtime_error("could not open prequant output: " + options.prequant_path);
	}

	PlaneStats aggregate_y, aggregate_u, aggregate_v;
	uint64_t frames = 0;
	auto start = std::chrono::steady_clock::now();

	while (!options.max_frames || frames < options.max_frames) {
		if (!read_exact(input, src_y.data(), src_y.size() * sizeof(uint16_t), true))
			break;
		read_exact(input, src_u.data(), src_u.size() * sizeof(uint16_t), false);
		read_exact(input, src_v.data(), src_v.size() * sizeof(uint16_t), false);

		aggregate_y.merge(resample_plane<FP>(src_y, luma_plan, luma_filters, dst_y,
		                                    options.prequant_path.empty() ? nullptr : &prequant_y,
		                                    options.threads));
		aggregate_u.merge(resample_plane<FP>(src_u, chroma_plan, chroma_filters, dst_u,
		                                    options.prequant_path.empty() ? nullptr : &prequant_u,
		                                    options.threads));
		aggregate_v.merge(resample_plane<FP>(src_v, chroma_plan, chroma_filters, dst_v,
		                                    options.prequant_path.empty() ? nullptr : &prequant_v,
		                                    options.threads));

		write_exact(output, dst_y.data(), dst_y.size() * sizeof(uint16_t));
		write_exact(output, dst_u.data(), dst_u.size() * sizeof(uint16_t));
		write_exact(output, dst_v.data(), dst_v.size() * sizeof(uint16_t));
		if (prequant_file) {
			write_exact(prequant_file, prequant_y.data(), prequant_y.size() * sizeof(double));
			write_exact(prequant_file, prequant_u.data(), prequant_u.size() * sizeof(double));
			write_exact(prequant_file, prequant_v.data(), prequant_v.size() * sizeof(double));
		}
		++frames;

		double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		double frames_d = static_cast<double>(frames);
		std::cerr << "\rframes=" << frames << " fps=" << std::fixed << std::setprecision(3)
		          << (frames_d / std::max(elapsed, 1.0e-9)) << std::flush;
	}

	double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	double frames_d = static_cast<double>(frames);
	std::cerr << "\n{\"precision\":\"" << (std::is_same_v<FP, double> ? "f64" : "f32")
	          << "\",\"frames\":" << frames
	          << ",\"seconds\":" << std::setprecision(9) << elapsed
	          << ",\"fps\":" << (frames_d / std::max(elapsed, 1.0e-9))
	          << ",\"planes\":{"
	          << "\"Y\":{\"input_min\":" << aggregate_y.input_min << ",\"input_max\":" << aggregate_y.input_max
	          << ",\"prequant_min\":" << aggregate_y.prequant_min << ",\"prequant_max\":" << aggregate_y.prequant_max
	          << ",\"clip_low\":" << aggregate_y.clipped_low << ",\"clip_high\":" << aggregate_y.clipped_high << "},"
	          << "\"Cb\":{\"input_min\":" << aggregate_u.input_min << ",\"input_max\":" << aggregate_u.input_max
	          << ",\"prequant_min\":" << aggregate_u.prequant_min << ",\"prequant_max\":" << aggregate_u.prequant_max
	          << ",\"clip_low\":" << aggregate_u.clipped_low << ",\"clip_high\":" << aggregate_u.clipped_high << "},"
	          << "\"Cr\":{\"input_min\":" << aggregate_v.input_min << ",\"input_max\":" << aggregate_v.input_max
	          << ",\"prequant_min\":" << aggregate_v.prequant_min << ",\"prequant_max\":" << aggregate_v.prequant_max
	          << ",\"clip_low\":" << aggregate_v.clipped_low << ",\"clip_high\":" << aggregate_v.clipped_high << "}}}\n";
	return frames ? 0 : 2;
}

} // namespace

int main(int argc, char **argv)
{
	try {
		std::fesetround(FE_TONEAREST);
#ifdef _WIN32
		_setmode(_fileno(stdin), _O_BINARY);
		_setmode(_fileno(stdout), _O_BINARY);
#endif
		Options options = parse_options(argc, argv);

		PlaneGeometry src_luma = make_luma_geometry(SRC_W, SRC_H);
		PlaneGeometry dst_luma = make_luma_geometry(DST_W, DST_H);
		PlaneGeometry src_chroma = make_progressive_chroma_geometry(
			src_luma, SRC_SUB_W, SRC_SUB_H, ChromaLocationW::LEFT, ChromaLocationH::CENTER);
		PlaneGeometry dst_chroma = make_progressive_chroma_geometry(
			dst_luma, DST_SUB_W, DST_SUB_H, ChromaLocationW::LEFT, ChromaLocationH::CENTER);
		PlanePlan luma_plan = make_plan(src_luma, dst_luma);
		PlanePlan chroma_plan = make_plan(src_chroma, dst_chroma);

		run_audit(luma_plan, chroma_plan);
		if (options.audit_only)
			return 0;

		std::ifstream input_file;
		std::ofstream output_file;
		std::istream *input = &std::cin;
		std::ostream *output = &std::cout;

		if (!options.input_path.empty()) {
			input_file.open(options.input_path, std::ios::binary);
			if (!input_file)
				throw std::runtime_error("could not open input: " + options.input_path);
			input = &input_file;
		}
		if (!options.output_path.empty()) {
			output_file.open(options.output_path, std::ios::binary | std::ios::trunc);
			if (!output_file)
				throw std::runtime_error("could not open output: " + options.output_path);
			output = &output_file;
		}

		if (options.precision_f32)
			return run_stream<float>(options, *input, *output, luma_plan, chroma_plan);
		return run_stream<double>(options, *input, *output, luma_plan, chroma_plan);
	} catch (const std::exception &e) {
		std::cerr << "\nf64resize_yuv: " << e.what() << '\n';
		return 1;
	}
}
