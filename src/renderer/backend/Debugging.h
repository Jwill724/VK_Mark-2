#pragma once

class Debugging final
{
public:
	static bool IsValidationLayerEnabled()       { return m_validationLayerEnabled; }
	static bool IsGpuAssistedValidationEnabled() { return m_gpuAssistedValidationEnabled; }
	static bool IsConsoleLogTrackingEnabled()    { return m_consoleLogResourceTrackingEnabled;}
	static bool IsGpuTrackingEnabled()           { return m_gpuTimingEnabled; }

private:
	
#ifdef NDEBUG
	inline static bool m_validationLayerEnabled               = false;
	inline static bool m_gpuAssistedValidationEnabled         = false;
	inline static bool m_consoleLogResourceTrackingEnabled    = false;
#else
	inline static bool m_validationLayerEnabled               = true;  // vulkan validation layers for tracking api errors
	inline static bool m_gpuAssistedValidationEnabled         = false; // deeper validation layer that detects memory and sync during rendering
	inline static bool m_consoleLogResourceTrackingEnabled    = true;  // bindless descriptor print info, buffer/m_image creation and destructions
#endif

	inline static bool m_gpuTimingEnabled                     = true;  // tracks and measures gpu pass exeuction for performance analysis (tracy profiler)
};
