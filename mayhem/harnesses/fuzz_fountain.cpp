// The fountain-chunk -> file reassembly path: fountain_decoder_sink::decode_frame (FountainMetadata
// header + wirehair blocks via fountain_decoder_stream), then the reassembled file's zstd
// skippable-frame filename header and zstd decompression -- what the decoders do with the byte stream
// that comes out of a frame (cimbard_fountain_decode / decompress_on_store).
// Input: [mode] then the chunk stream, consumed in Config::fountain_chunk_size() pieces.
#include "fuzz_common.h"
#include "fountain/fountain_decoder_sink.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	fuzzcfc::init_once();
	if (size < 1)
		return -1;
	cimbar::Config::update(fuzzcfc::pick_mode(data[0]));
	data += 1;
	size -= 1;

	unsigned chunk = cimbar::Config::fountain_chunk_size();
	if (size < chunk)
		return -1;

	fuzzcfc::Recovered rec;
	fountain_decoder_sink sink(chunk, fuzzcfc::store_in_memory(rec));
	for (size_t off = 0; off + chunk <= size; off += chunk)
		sink.decode_frame(reinterpret_cast<const char*>(data + off), chunk);
	(void)sink.get_progress();
	return 0;
}
