#pragma once

#include <vulkan/vulkan.hpp>

enum class AnimationExtrapolateMode {
	eConstant,
	eLinear,
	eCycle,
	eCycleOffset,
	eBounce,
};
enum class AnimationTangentMode {
	eManual,
	eFlat,
	eLinear,
	eSmooth,
	eStep,
};

enum class ClearFlagBits {
	eNone = 0,
	eDepth = 1,
	eColor  = 2,
	eColorDepth = eColor | eDepth,
	eSkybox = 4 | eColorDepth
};
using ClearFlags = vk::Flags<ClearFlagBits>;

enum class CommandBufferState {
	eRecording,
	ePending,
	eDone
};

enum class ConsoleColorBits {
	eWhite    = 0,
	eRed   		= 1,
	eBlue  		= 2,
	eGreen 		= 4,
	eYellow   = eRed | eGreen,
	eCyan			= eGreen | eBlue,
	eMagenta  = eRed | eBlue,
	eBold = 8
};
using ConsoleColor = vk::Flags<ConsoleColorBits>;

enum class LayoutAxis : uint32_t {
	eHorizontal = 0,
	eVertical = 1
};

enum class StereoEye : uint32_t {
	eNone = 0,
	eLeft = 0,
	eRight = 1
};

enum class StereoMode : uint32_t {
	eNone = 0,
	eVertical = 1,
	eHorizontal = 2
};

enum class TextAnchor {
	eMin, eMid, eMax
};

enum class TextureLoadFlags {
	eSrgb,
	eSigned
};