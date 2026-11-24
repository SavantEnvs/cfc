// Shared setup for the cfc / libcimbar harnesses.
#pragma once
#include "cimb_translator/Config.h"
#include "compression/zstd_decompressor.h"
#include "compression/zstd_header_check.h"
#include "fountain/FountainInit.h"
#include "util/File.h"

#include <opencv2/core.hpp>
#include <opencv2/core/ocl.hpp>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace fuzzcfc {
	// The modes cimbar::Config::update() knows: 4 / 8 = legacy 4C/8C ("coupled"), 66 = Bu, 67 = Bm, 68 = B.
	static const int kModes[] = {4, 8, 66, 67, 68};

	inline int pick_mode(uint8_t b) { return kModes[b % (sizeof(kModes) / sizeof(kModes[0]))]; }

	inline void init_once()
	{
		static bool done = [] {
			// OpenCV is a system (uninstrumented) library: keep it single-threaded and off OpenCL
			// so every input is decoded the same way on every host.
			cv::setNumThreads(1);
			cv::ocl::setUseOpenCL(false);
			FountainInit::init();
			return true;
		}();
		(void)done;
	}

	// Same work as fountain_decoder_sink.h's decompress_on_store() (what the cimbar CLI and the app's
	// MultiThreadedDecoder use): parse the zstd skippable-frame filename header, basename it, and
	// zstd-decompress the reassembled file -- into memory instead of a file on disk.
	struct Recovered {
		std::vector<std::string> names;
		std::vector<std::string> contents;
	};

	inline auto store_in_memory(Recovered& out)
	{
		return [&out](const std::string& fallback, const std::vector<uint8_t>& data) {
			std::string fn = cimbar::zstd_header_check::get_filename(data.data(), data.size());
			if (!fn.empty())
				fn = File::basename(fn);
			if (fn.empty())
				fn = fallback;
			cimbar::zstd_decompressor<std::stringstream> dec;
			dec.write(reinterpret_cast<const char*>(data.data()), data.size());
			out.names.push_back(fn);
			out.contents.push_back(dec.str());
			return fn;
		};
	}
}
