// Encode -> decode round trip, like `cimbar --encode` then `cimbar`: zstd_compressor (with the
// skippable filename frame), fountain_encoder_stream (wirehair), reed-solomon, CimbWriter/CimbEncoder,
// then Decoder::decode_fountain into a fountain_decoder_sink. A frame the encoder rendered must decode
// back to the same bytes, so a mismatch aborts.
// Input: [mode][compression level][encode id][name len] name bytes, then the file contents.
#include "fuzz_common.h"
#include "encoder/Decoder.h"
#include "encoder/Encoder.h"
#include "fountain/fountain_decoder_sink.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	fuzzcfc::init_once();
	if (size < 4)
		return -1;
	int mode = fuzzcfc::pick_mode(data[0]);
	int level = 1 + data[1] % 22;
	uint8_t encode_id = data[2];
	unsigned nlen = data[3] % 32;
	data += 4;
	size -= 4;
	if (size < nlen)
		return -1;
	std::string name(reinterpret_cast<const char*>(data), nlen);
	data += nlen;
	size -= nlen;
	// each frame costs a full-size render and decode; keep inputs to a few frames' worth
	if (size == 0 or size > 4096)
		return -1;
	std::string contents(reinterpret_cast<const char*>(data), size);

	cimbar::Config::update(mode);
	Encoder enc;
	enc.set_encode_id(encode_id);
	std::stringstream in(contents);
	auto fes = enc.create_fountain_encoder(in, name, level);
	if (!fes)
		return 0;

	fuzzcfc::Recovered rec;
	Decoder dec;
	fountain_decoder_sink sink(cimbar::Config::fountain_chunk_size(), fuzzcfc::store_in_memory(rec));
	for (int frame = 0; frame < 8 and rec.contents.empty(); ++frame)
	{
		auto img = enc.encode_next(*fes);
		if (!img)
			break;
		dec.decode_fountain(*img, sink);
	}
	if (!rec.contents.empty() and rec.contents[0] != contents)
	{
		fprintf(stderr, "round trip mismatch: mode %d, %zu bytes in, %zu bytes out\n", mode, contents.size(), rec.contents[0].size());
		abort();
	}
	return 0;
}
