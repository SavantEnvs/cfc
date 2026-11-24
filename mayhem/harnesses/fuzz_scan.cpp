// cimbard_scan_extract_decode() (libcimbar's receive API, cimbar_recv_js.h): a raw camera frame goes
// through colour conversion, Extractor (Scanner anchors, Corners, Deskewer) and Decoder::decode_fountain
// into the caller's chunk buffer.
// Input: [mode][format][w lo][w hi][h lo][h hi] then either
//   w,h != 0: the raw frame's pixels, or
//   w == 0 or h == 0: an encoded image (PNG, JPEG, ...) for cv::imdecode -- a captured camera frame --
//   converted to the requested pixel format before it is handed to the scanner.
// format is one of 3 (RGB), 4 (RGBA), 12 (NV12), 420 (YUV420p).
#include "fuzz_common.h"
#include "cimbar_js/cimbar_recv_js.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <cstring>
#include <vector>

namespace {
	// decode an encoded image and lay it out in `format`, as a camera would deliver it
	bool frame_from_encoded(const uint8_t* data, size_t size, int format, std::vector<unsigned char>& out, unsigned& w, unsigned& h)
	{
		if (size == 0)
			return false;
		cv::Mat bgr = cv::imdecode(cv::Mat(1, (int)size, CV_8UC1, (void*)data), cv::IMREAD_COLOR);
		if (bgr.empty() or bgr.cols > 2048 or bgr.rows > 2048)
			return false;
		w = bgr.cols;
		h = bgr.rows;
		cv::Mat conv;
		if (format == 3)
			cv::cvtColor(bgr, conv, cv::COLOR_BGR2RGB);
		else if (format == 4)
			cv::cvtColor(bgr, conv, cv::COLOR_BGR2RGBA);
		else
		{
			// NV12 / YUV420p need even dimensions (OpenCV's cvtColor precondition for these codes)
			if ((w % 2) or (h % 2))
				return false;
			cv::cvtColor(bgr, conv, cv::COLOR_BGR2YUV_I420); // Y plane, then U plane, then V plane
			if (format == 12)
			{
				// NV12: Y plane, then interleaved U/V
				cv::Mat nv12 = conv.clone();
				size_t ysz = (size_t)w * h, csz = ysz / 4;
				const unsigned char* u = conv.data + ysz;
				const unsigned char* v = u + csz;
				unsigned char* uv = nv12.data + ysz;
				for (size_t i = 0; i < csz; ++i)
				{
					uv[2 * i] = u[i];
					uv[2 * i + 1] = v[i];
				}
				conv = nv12;
			}
		}
		out.assign(conv.data, conv.data + conv.total() * conv.elemSize());
		return true;
	}
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	fuzzcfc::init_once();
	static const int kFormats[] = {3, 4, 12, 420};
	if (size < 6)
		return -1;
	int mode = fuzzcfc::pick_mode(data[0]);
	int format = kFormats[data[1] % 4];
	unsigned w = data[2] | (data[3] << 8);
	unsigned h = data[4] | (data[5] << 8);
	data += 6;
	size -= 6;
	std::vector<unsigned char> decoded;
	if (w == 0 or h == 0)
	{
		if (!frame_from_encoded(data, size, format, decoded, w, h))
			return -1;
		data = decoded.data();
		size = decoded.size();
	}
	if (w > 2048 or h > 2048)
		return -1;

	// cimbar_recv_js.h: "imgsize=width*height*channels for rgba. Other formats are weirder." --
	// the API wraps the caller's buffer without a length, so it must hold the frame it describes.
	size_t need = (format == 12 or format == 420) ? (size_t)w * (h * 3 / 2) : (size_t)w * h * format;
	if (size < need)
		return -1;
	// NV12 / YUV420p frames have even dimensions (OpenCV's cvtColor precondition for these codes).
	if ((format == 12 or format == 420) and ((w % 2) or (h % 2)))
		return -1;

	cimbard_configure_decode(mode);
	std::vector<unsigned char> buf(cimbard_get_bufsize());
	cimbard_scan_extract_decode(data, w, h, format, buf.data(), buf.size());
	return 0;
}
