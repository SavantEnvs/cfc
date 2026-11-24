// Decoder::decode / Decoder::decode_fountain on an already-extracted cimbar frame -- what
// `cimbar --no-deskew [--no-fountain] -m <mode>` does with an input image.
// Input: [mode][flags][w lo][w hi][h lo][h hi] then either
//   w,h != 0: w*h*3 bytes of raw RGB pixels, or
//   w == 0 or h == 0: an encoded image (PNG, ...) for cv::imdecode, like the CLI's cv::imread.
// flags: bit0 preprocess, bits1-2 color_correction (0..2), bit3 plain decode (--no-fountain).
#include "fuzz_common.h"
#include "encoder/Decoder.h"
#include "fountain/fountain_decoder_sink.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	fuzzcfc::init_once();
	if (size < 6)
		return -1;
	int mode = fuzzcfc::pick_mode(data[0]);
	uint8_t flags = data[1];
	unsigned w = data[2] | (data[3] << 8);
	unsigned h = data[4] | (data[5] << 8);
	data += 6;
	size -= 6;

	cv::Mat img;
	if (w == 0 or h == 0)
	{
		if (size == 0)
			return -1;
		img = cv::imdecode(cv::Mat(1, (int)size, CV_8UC1, (void*)data), cv::IMREAD_COLOR);
		if (img.empty())
			return -1;
		cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
	}
	else
	{
		// raw RGB frame: the buffer must hold the w*h*3 bytes it claims (cv::Mat wraps, it does not copy)
		if (w > 2048 or h > 2048 or size < (size_t)w * h * 3)
			return -1;
		img = cv::Mat((int)h, (int)w, CV_8UC3, (void*)data).clone();
	}

	cimbar::Config::update(mode);
	bool preprocess = flags & 1;
	int color_correction = (flags >> 1) % 3;
	Decoder dec;
	if (flags & 8)
	{
		std::stringstream ss;
		dec.decode(img, ss, preprocess, color_correction);
	}
	else
	{
		fuzzcfc::Recovered rec;
		fountain_decoder_sink sink(cimbar::Config::fountain_chunk_size(), fuzzcfc::store_in_memory(rec));
		dec.decode_fountain(img, sink, preprocess, color_correction);
	}
	return 0;
}
