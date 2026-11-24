// The app's own native pipeline: MultiThreadedDecoder::add(cv::Mat) (app/src/cpp/cfc-cpp), which
// processImageJNI calls for every camera frame -- mode autodetect cycle, Scanner + Corners + Deskewer,
// Decoder::decode_fountain into a concurrent_fountain_decoder_sink, and decompress_on_store writing the
// recovered file (named by the frame's zstd filename header) into the decoder's data directory.
// Input: [mode][w lo][w hi][h lo][h hi] then either
//   w,h != 0: 1..3 raw RGB frames of w*h*3 bytes, or
//   w == 0 or h == 0: 1..3 encoded images (PNG, JPEG, ...; captured camera frames), each as
//   [len: u32 little-endian][len bytes] (the last one may omit its length and run to the end),
//   decoded with cv::imdecode and converted to RGB as the app's camera path delivers it.
// mode picks 0 (autodetect), 4, 66, 67 or 68, as the app's mode selector does.
#include "cfc-cpp/MultiThreadedDecoder.h"

#include <opencv2/core/ocl.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

namespace {
	std::string scratch_dir()
	{
		static std::string dir = [] {
			const char* tmp = getenv("TMPDIR");
			std::string tmpl = std::string(tmp && *tmp ? tmp : "/tmp") + "/cfc_fuzz_XXXXXX";
			char* d = mkdtemp(tmpl.data());
			if (!d)
				abort();
			cv::setNumThreads(1);
			cv::ocl::setUseOpenCL(false);
			return std::string(d);
		}();
		return dir;
	}

	clock_t scanned_now()
	{
		// MultiThreadedDecoder's counters are plain statics bumped by the worker thread
		return *const_cast<volatile clock_t*>(&MultiThreadedDecoder::scanned);
	}
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	static const int kModes[] = {0, 4, 66, 67, 68};
	std::string dir = scratch_dir();
	if (size < 5)
		return -1;
	int mode = kModes[data[0] % 5];
	unsigned w = data[1] | (data[2] << 8);
	unsigned h = data[3] | (data[4] << 8);
	data += 5;
	size -= 5;
	std::vector<cv::Mat> mats;
	if (w == 0 or h == 0)
	{
		while (size > 0 and mats.size() < 3)
		{
			size_t len = size;
			if (size >= 4)
			{
				size_t l = data[0] | (data[1] << 8) | (data[2] << 16) | ((size_t)data[3] << 24);
				if (l <= size - 4)
				{
					len = l;
					data += 4;
					size -= 4;
				}
			}
			cv::Mat bgr;
			if (len > 0)
				bgr = cv::imdecode(cv::Mat(1, (int)len, CV_8UC1, (void*)data), cv::IMREAD_COLOR);
			data += len;
			size -= len;
			if (bgr.empty() or bgr.cols > 2048 or bgr.rows > 2048)
				continue;
			cv::Mat rgb;
			cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
			mats.push_back(rgb);
		}
	}
	else
	{
		if (w > 2048 or h > 2048)
			return -1;
		size_t frame = (size_t)w * h * 3;
		size_t frames = std::min<size_t>(size / frame, 3);
		for (size_t i = 0; i < frames; ++i)
			mats.push_back(cv::Mat((int)h, (int)w, CV_8UC3, (void*)(data + i * frame)).clone());
	}
	if (mats.empty())
		return -1;

	// the mode-autodetect cycle keys off a static frame counter: restart it so each input sees the same cycle
	MultiThreadedDecoder::count = 0;
	{
		MultiThreadedDecoder mtd(dir, mode);
		for (cv::Mat& mat : mats)
		{
			clock_t before = scanned_now();
			if (!mtd.add(mat))
				continue;
			// one frame at a time: wait until a worker has picked it up (stop() below, or the next
			// add(), would otherwise race the pool and drop it); stop() then joins the running job.
			while (scanned_now() == before)
				std::this_thread::yield();
		}
		mtd.stop();
		(void)mtd.get_done();
		(void)mtd.get_progress();
	}

	// files recovered from this input must not leak into the next one
	std::error_code ec;
	for (auto& e : std::filesystem::directory_iterator(dir, ec))
		std::filesystem::remove_all(e.path(), ec);
	return 0;
}
