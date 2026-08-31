#include "BCNCompression.h"

#include "bc7enc/bc7enc.h"
#include "bc7enc/rgbcx.h"

#include <algorithm>
#include <cstring>

namespace
{
	void FetchBlock(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h,
		uint32_t bx, uint32_t by, uint8_t(&block)[64])
	{
		for (uint32_t y = 0; y < 4; ++y)
			for (uint32_t x = 0; x < 4; ++x)
			{
				const uint32_t sx = std::min(bx * 4u + x, w - 1u);
				const uint32_t sy = std::min(by * 4u + y, h - 1u);
				std::memcpy(&block[(y * 4u + x) * 4u],
					&rgba[(static_cast<size_t>(sy) * w + sx) * 4u], 4u);
			}
	}
}

void InitBC7Encoder()
{
	bc7enc_compress_block_init();
	rgbcx::init();
}

void CompressBC7(
	const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, std::vector<uint8_t>& out)
{
	if (!w || !h) { out.clear(); return; }

	const uint32_t bw = (w + 3u) / 4u;
	const uint32_t bh = (h + 3u) / 4u;
	out.resize(static_cast<size_t>(bw) * bh * 16u);

	bc7enc_compress_block_params params;
	bc7enc_compress_block_params_init(&params);

	for (uint32_t by = 0; by < bh; ++by)
		for (uint32_t bx = 0; bx < bw; ++bx)
		{
			uint8_t block[64];
			FetchBlock(rgba, w, h, bx, by, block);
			bc7enc_compress_block(
				&out[(static_cast<size_t>(by) * bw + bx) * 16u], block, &params);
		}
}

void CompressBC5(
	const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, std::vector<uint8_t>& out)
{
	if (!w || !h) { out.clear(); return; }

	const uint32_t bw = (w + 3u) / 4u;
	const uint32_t bh = (h + 3u) / 4u;
	out.resize(static_cast<size_t>(bw) * bh * 16u);

	for (uint32_t by = 0; by < bh; ++by)
		for (uint32_t bx = 0; bx < bw; ++bx)
		{
			uint8_t block[64];
			FetchBlock(rgba, w, h, bx, by, block);
			rgbcx::encode_bc5(
				&out[(static_cast<size_t>(by) * bw + bx) * 16u], block, 0, 1, 4);
		}
}
