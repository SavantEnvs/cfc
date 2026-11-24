// Seed generator for the cfc harnesses (not part of the fuzz build; run by hand to refresh
// mayhem/<target>/testsuite). Uses libcimbar's own encoder, so every seed is a valid frame/stream.
//   gen_seeds <outdir>   -> <outdir>/<target>/<name>
#include "cimb_translator/Config.h"
#include "encoder/Encoder.h"
#include "fountain/FountainInit.h"

#include <opencv2/opencv.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void put(const fs::path& p, const std::string& hdr, const unsigned char* d, size_t n)
{
	fs::create_directories(p.parent_path());
	std::ofstream f(p, std::ios::binary);
	f.write(hdr.data(), hdr.size());
	f.write((const char*)d, n);
}

static std::string payload(size_t n, unsigned seed)
{
	std::string s;
	for (size_t i = 0; i < n; ++i)
		s += (char)((i * 7 + seed * 13 + (i >> 3)) & 0x7f);
	return s;
}

int main(int argc, char** argv)
{
	fs::path out = argv[1];
	FountainInit::init();
	const int modes[] = {4, 8, 66, 67, 68}; // index == byte0 for the harnesses
	for (int mi = 0; mi < 5; ++mi)
	{
		int mode = modes[mi];
		cimbar::Config::update(mode);
		std::string data = payload(mode == 66 ? 600 : 2000, mi);

		// fountain chunks (fuzz_fountain): [mode] + chunks
		{
			Encoder enc;
			std::stringstream ss(data);
			auto fes = enc.create_fountain_encoder(ss, "seed.txt", 3);
			unsigned chunk = cimbar::Config::fountain_chunk_size();
			std::vector<char> buf(chunk * (fes->block_count() + 2));
			fes->read(buf.data(), buf.size());
			put(out / "fuzz_fountain" / ("mode" + std::to_string(mode)), std::string(1, (char)mi), (unsigned char*)buf.data(), buf.size());
		}

		// one rendered frame
		Encoder enc;
		std::stringstream ss(data);
		auto fes = enc.create_fountain_encoder(ss, "seed.txt", 3);
		cv::Mat img = *enc.encode_next(*fes); // RGB
		cv::Mat bgr;
		cv::cvtColor(img, bgr, cv::COLOR_RGB2BGR);
		std::vector<unsigned char> png;
		cv::imencode(".png", bgr, png);

		// fuzz_decode: PNG form (w=h=0), and raw RGB for the small modes
		put(out / "fuzz_decode" / ("png_mode" + std::to_string(mode)), std::string{(char)mi, 0, 0, 0, 0, 0}, png.data(), png.size());
		if (mode == 66 or mode == 67)
		{
			std::string hdr{(char)mi, 0, (char)(img.cols & 0xff), (char)(img.cols >> 8), (char)(img.rows & 0xff), (char)(img.rows >> 8)};
			put(out / "fuzz_decode" / ("raw_mode" + std::to_string(mode)), hdr, img.data, img.total() * 3);
			hdr[1] = 8; // plain decode
			put(out / "fuzz_decode" / ("raw_plain_mode" + std::to_string(mode)), hdr, img.data, img.total() * 3);
		}

		// fuzz_scan / fuzz_mtdecoder: the frame on a white border, slightly rotated, as a captured camera
		// frame -- a JPEG, as a camera writes it, so the seeds stay small (~250-300 KB).
		if (mode == 66 or mode == 68 or mode == 4)
		{
			int pad = 40;
			cv::Mat canvas(img.rows + 2 * pad, img.cols + 2 * pad, CV_8UC3, cv::Scalar(255, 255, 255));
			img.copyTo(canvas(cv::Rect(pad, pad, img.cols, img.rows)));
			cv::Mat rot = cv::getRotationMatrix2D(cv::Point2f(canvas.cols / 2.f, canvas.rows / 2.f), 2.0, 1.0);
			cv::warpAffine(canvas, canvas, rot, canvas.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(255, 255, 255));
			if (mode != 66)
				cv::resize(canvas, canvas, cv::Size(), 0.75, 0.75, cv::INTER_AREA);
			if (canvas.cols % 2 or canvas.rows % 2) // even dimensions, for the NV12 / YUV420p forms
				canvas = canvas(cv::Rect(0, 0, canvas.cols & ~1, canvas.rows & ~1)).clone();
			cv::Mat cbgr;
			cv::cvtColor(canvas, cbgr, cv::COLOR_RGB2BGR);
			std::vector<unsigned char> cjpg;
			cv::imencode(".jpg", cbgr, cjpg, {cv::IMWRITE_JPEG_QUALITY, 70});
			std::string m = std::to_string(mode);
			// fuzz_scan: [mode][format idx: 0 RGB, 1 RGBA, 2 NV12, 3 YUV420p][0 0 0 0] encoded image
			put(out / "fuzz_scan" / ("jpg_rgb_mode" + m), std::string{(char)mi, 0, 0, 0, 0, 0}, cjpg.data(), cjpg.size());
			if (mode == 66)
			{
				put(out / "fuzz_scan" / "jpg_rgba_mode66", std::string{(char)mi, 1, 0, 0, 0, 0}, cjpg.data(), cjpg.size());
				put(out / "fuzz_scan" / "jpg_nv12_mode66", std::string{(char)mi, 2, 0, 0, 0, 0}, cjpg.data(), cjpg.size());
				put(out / "fuzz_scan" / "jpg_yuv420_mode66", std::string{(char)mi, 3, 0, 0, 0, 0}, cjpg.data(), cjpg.size());
			}
			// fuzz_mtdecoder: [mode idx: 0 auto, 1 4, 2 66, 3 67, 4 68][0 0 0 0] encoded image (one frame)
			char mt = mode == 4 ? 1 : mode == 66 ? 2 : 4;
			put(out / "fuzz_mtdecoder" / ("jpg_mode" + m), std::string{mt, 0, 0, 0, 0}, cjpg.data(), cjpg.size());
			if (mode == 4) // autodetect: the app's first frame tries mode 4
				put(out / "fuzz_mtdecoder" / "jpg_auto_mode4", std::string{0, 0, 0, 0, 0}, cjpg.data(), cjpg.size());
		}

		// fuzz_roundtrip: [mode][level][encode id][name len] name contents
		std::string name = "file" + std::to_string(mode) + ".bin";
		std::string hdr{(char)mi, 3, (char)mi, (char)name.size()};
		std::string contents = payload(mode == 66 ? 200 : 700, mi + 5);
		put(out / "fuzz_roundtrip" / ("mode" + std::to_string(mode)), hdr + name, (unsigned char*)contents.data(), contents.size());
	}
	return 0;
}
