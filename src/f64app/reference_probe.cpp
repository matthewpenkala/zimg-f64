#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include "common/except.h"
#include "f64app/geometry.h"
#include "resize/filter.h"

namespace {

using zimg_f64_reference::AxisGeometry;
using zimg_f64_reference::ChromaLocationH;
using zimg_f64_reference::ChromaLocationW;
using zimg_f64_reference::PlaneGeometry;

ChromaLocationW parse_location_w(const std::string &value)
{
	if (value == "LEFT")
		return ChromaLocationW::LEFT;
	if (value == "CENTER")
		return ChromaLocationW::CENTER;
	throw std::invalid_argument("horizontal chroma location must be LEFT or CENTER");
}

ChromaLocationH parse_location_h(const std::string &value)
{
	if (value == "CENTER")
		return ChromaLocationH::CENTER;
	if (value == "TOP")
		return ChromaLocationH::TOP;
	if (value == "BOTTOM")
		return ChromaLocationH::BOTTOM;
	throw std::invalid_argument("vertical chroma location must be CENTER, TOP, or BOTTOM");
}

void probe_filter()
{
	unsigned src_dim;
	unsigned dst_dim;
	double shift;
	double width;
	unsigned row;

	if (!(std::cin >> src_dim >> dst_dim >> shift >> width >> row))
		throw std::runtime_error("malformed F query");

	static bool cached = false;
	static unsigned cached_src_dim = 0;
	static unsigned cached_dst_dim = 0;
	static double cached_shift = 0.0;
	static double cached_width = 0.0;
	static zimg::resize::FilterContextF64 filter;

	if (!cached ||
	    src_dim != cached_src_dim || dst_dim != cached_dst_dim ||
	    shift != cached_shift || width != cached_width) {
		zimg::resize::Spline36Filter spline36;
		filter = zimg::resize::compute_filter_f64(spline36, src_dim, dst_dim, shift, width);
		cached = true;
		cached_src_dim = src_dim;
		cached_dst_dim = dst_dim;
		cached_shift = shift;
		cached_width = width;
	}
	if (row >= filter.filter_rows)
		throw std::out_of_range("filter row outside destination");

	std::cout << "F " << filter.filter_width << ' ' << filter.stride << ' '
	          << filter.left[row] << ' ' << filter.input_width;
	for (unsigned j = 0; j < filter.filter_width; ++j)
		std::cout << ' ' << filter.data[static_cast<size_t>(row) * filter.stride + j];
	std::cout << '\n';
}

void probe_geometry()
{
	unsigned src_width;
	unsigned src_height;
	double src_active_left;
	double src_active_top;
	double src_active_width;
	double src_active_height;
	unsigned src_subsample_w;
	unsigned src_subsample_h;
	std::string src_location_w;
	std::string src_location_h;

	unsigned dst_width;
	unsigned dst_height;
	double dst_active_left;
	double dst_active_top;
	double dst_active_width;
	double dst_active_height;
	unsigned dst_subsample_w;
	unsigned dst_subsample_h;
	std::string dst_location_w;
	std::string dst_location_h;

	if (!(std::cin >>
	      src_width >> src_height >>
	      src_active_left >> src_active_top >> src_active_width >> src_active_height >>
	      src_subsample_w >> src_subsample_h >> src_location_w >> src_location_h >>
	      dst_width >> dst_height >>
	      dst_active_left >> dst_active_top >> dst_active_width >> dst_active_height >>
	      dst_subsample_w >> dst_subsample_h >> dst_location_w >> dst_location_h)) {
		throw std::runtime_error("malformed G query");
	}

	PlaneGeometry src_luma = zimg_f64_reference::make_luma_geometry(
		src_width, src_height, src_active_left, src_active_top, src_active_width, src_active_height);
	PlaneGeometry dst_luma = zimg_f64_reference::make_luma_geometry(
		dst_width, dst_height, dst_active_left, dst_active_top, dst_active_width, dst_active_height);
	PlaneGeometry src_chroma = zimg_f64_reference::make_progressive_chroma_geometry(
		src_luma, src_subsample_w, src_subsample_h,
		parse_location_w(src_location_w), parse_location_h(src_location_h));
	PlaneGeometry dst_chroma = zimg_f64_reference::make_progressive_chroma_geometry(
		dst_luma, dst_subsample_w, dst_subsample_h,
		parse_location_w(dst_location_w), parse_location_h(dst_location_h));

	AxisGeometry horizontal = zimg_f64_reference::make_axis_geometry(
		src_chroma.width, dst_chroma.width,
		src_chroma.active_left, src_chroma.active_width,
		dst_chroma.active_left, dst_chroma.active_width);
	AxisGeometry vertical = zimg_f64_reference::make_axis_geometry(
		src_chroma.height, dst_chroma.height,
		src_chroma.active_top, src_chroma.active_height,
		dst_chroma.active_top, dst_chroma.active_height);

	std::cout << "G "
	          << src_chroma.width << ' ' << src_chroma.height << ' '
	          << src_chroma.active_left << ' ' << src_chroma.active_top << ' '
	          << src_chroma.active_width << ' ' << src_chroma.active_height << ' '
	          << dst_chroma.width << ' ' << dst_chroma.height << ' '
	          << dst_chroma.active_left << ' ' << dst_chroma.active_top << ' '
	          << dst_chroma.active_width << ' ' << dst_chroma.active_height << ' '
	          << horizontal.shift << ' ' << horizontal.subwidth << ' '
	          << vertical.shift << ' ' << vertical.subwidth << '\n';
}

} // namespace

int main()
{
	try {
		std::ios::sync_with_stdio(false);
		std::cin.tie(nullptr);
		std::cout << std::setprecision(17);

		std::string command;
		while (std::cin >> command) {
			if (command == "F")
				probe_filter();
			else if (command == "G")
				probe_geometry();
			else
				throw std::runtime_error("unknown probe command");
		}
		return 0;
	} catch (const zimg::error::Exception &e) {
		std::cerr << "reference_probe: " << e.what() << '\n';
		return 1;
	} catch (const std::exception &e) {
		std::cerr << "reference_probe: " << e.what() << '\n';
		return 1;
	}
}
