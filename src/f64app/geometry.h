#ifndef ZIMG_F64_REFERENCE_GEOMETRY_H_
#define ZIMG_F64_REFERENCE_GEOMETRY_H_

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace zimg_f64_reference {

enum class ChromaLocationW : std::uint8_t {
	LEFT,
	CENTER,
};

enum class ChromaLocationH : std::uint8_t {
	CENTER,
	TOP,
	BOTTOM,
};

struct PlaneGeometry {
	unsigned width;
	unsigned height;
	double active_left;
	double active_top;
	double active_width;
	double active_height;
};

struct AxisGeometry {
	unsigned src_dim;
	unsigned dst_dim;
	double shift;
	double subwidth;
};

inline double chroma_offset_w(ChromaLocationW location, double subsampling)
{
	return location == ChromaLocationW::LEFT ? -0.5 + 0.5 * subsampling : 0.0;
}

inline double chroma_offset_h(ChromaLocationH location, double subsampling)
{
	if (location == ChromaLocationH::TOP)
		return -0.5 + 0.5 * subsampling;
	if (location == ChromaLocationH::BOTTOM)
		return 0.5 - 0.5 * subsampling;
	return 0.0;
}

inline PlaneGeometry make_luma_geometry(unsigned width, unsigned height,
	                                    double active_left = 0.0,
	                                    double active_top = 0.0,
	                                    double active_width = -1.0,
	                                    double active_height = -1.0)
{
	if (!width || !height)
		throw std::invalid_argument("plane dimensions must be nonzero");
	if (active_width < 0.0)
		active_width = static_cast<double>(width);
	if (active_height < 0.0)
		active_height = static_cast<double>(height);
	if (!std::isfinite(active_left) || !std::isfinite(active_top) ||
	    !std::isfinite(active_width) || !std::isfinite(active_height) ||
	    !(active_width > 0.0) || !(active_height > 0.0))
		throw std::invalid_argument("active geometry must be finite with positive dimensions");
	return{ width, height, active_left, active_top, active_width, active_height };
}

inline PlaneGeometry make_progressive_chroma_geometry(const PlaneGeometry &luma,
	                                                   unsigned subsample_w,
	                                                   unsigned subsample_h,
	                                                   ChromaLocationW location_w,
	                                                   ChromaLocationH location_h)
{
	if (subsample_w >= 31 || subsample_h >= 31)
		throw std::invalid_argument("subsampling exponent too large");
	double active_subscale_w = 1.0 / static_cast<double>(1U << subsample_w);
	double active_subscale_h = 1.0 / static_cast<double>(1U << subsample_h);
	unsigned chroma_width = luma.width >> subsample_w;
	unsigned chroma_height = luma.height >> subsample_h;
	if (!chroma_width || !chroma_height)
		throw std::invalid_argument("subsampling produces an empty chroma plane");

	// Match pinned zimg GraphBuilder semantics:
	//   chroma_from_luma() scales the active region by the ideal power-of-two
	//   subsampling factor, while apply_pixel_siting() derives its siting offset
	//   from the actual integer plane-size ratio. These differ for dimensions
	//   that are not divisible by the subsampling factor.
	double siting_subscale_w = static_cast<double>(chroma_width) / static_cast<double>(luma.width);
	double siting_subscale_h = static_cast<double>(chroma_height) / static_cast<double>(luma.height);

	PlaneGeometry chroma{
		chroma_width,
		chroma_height,
		luma.active_left * active_subscale_w,
		luma.active_top * active_subscale_h,
		luma.active_width * active_subscale_w,
		luma.active_height * active_subscale_h
	};
	chroma.active_left -= chroma_offset_w(location_w, siting_subscale_w);
	chroma.active_top -= chroma_offset_h(location_h, siting_subscale_h);
	return chroma;
}

inline AxisGeometry make_axis_geometry(unsigned src_dim, unsigned dst_dim,
	                                   double src_active_pos, double src_active_width,
	                                   double dst_active_pos, double dst_active_width)
{
	if (!src_dim || !dst_dim ||
	    !std::isfinite(src_active_pos) || !std::isfinite(dst_active_pos) ||
	    !std::isfinite(src_active_width) || !std::isfinite(dst_active_width) ||
	    !(src_active_width > 0.0) || !(dst_active_width > 0.0))
		throw std::invalid_argument("invalid axis geometry");
	double scale = dst_active_width / src_active_width;
	if (!std::isfinite(scale) || !(scale > 0.0))
		throw std::invalid_argument("invalid axis scale");
	double shift = src_active_pos - dst_active_pos / scale;
	double subwidth = src_active_width * (static_cast<double>(dst_dim) / dst_active_width);
	if (!std::isfinite(shift) || !std::isfinite(subwidth) || !(subwidth > 0.0))
		throw std::invalid_argument("axis geometry is not finite");
	return{ src_dim, dst_dim, shift, subwidth };
}

} // namespace zimg_f64_reference

#endif // ZIMG_F64_REFERENCE_GEOMETRY_H_
