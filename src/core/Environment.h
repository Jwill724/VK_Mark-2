#pragma once

#include <array>
#include "../renderer/RendererDefinitions.h"

namespace RD = RendererDefinitions;

namespace Environment
{
	inline constexpr std::array _HDRPaths =
	{
		"res/assets/envhdr/belfast_sunset_puresky_2k.hdr",
		"res/assets/envhdr/kloppenheim_06_puresky_2k.hdr",
		"res/assets/envhdr/wasteland_clouds_2k.hdr",
		"res/assets/envhdr/san_giuseppe_bridge_2k.hdr",
		"res/assets/envhdr/rogland_clear_night_2k.hdr"
	};

	inline constexpr std::size_t _HDRPathCount = _HDRPaths.size();
	static_assert(_HDRPathCount <= RD::MAX_ENVIRONMENT_SETS);
}
