#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

struct Vertex
{
	glm::vec3 position{0.0f};

	int16_t  normalX = 0;     // oct snorm16
	int16_t  normalY = 0;

	int16_t  tangentX = 0;    // oct snorm16, W in sign of tangentY
	int16_t  tangentY = 0;

	uint16_t uvX = 0;         // half-float bits
	uint16_t uvY = 0;

	uint32_t colorRGBA8 = 0xFFFFFFFFu;
};

inline int16_t ToSnorm16(float value)
{
	float clamped = glm::clamp(value, -1.0f, 1.0f);
	int32_t scaled = static_cast<int32_t>(std::round(clamped * 32767.0f));
	scaled = std::min<int32_t>(32767, std::max<int32_t>(-32767, scaled));
	return static_cast<int16_t>(scaled);
};

inline int8_t ToSnorm8(float value)
{
	float clamped = glm::clamp(value, -1.0f, 1.0f);
	int32_t scaled  = static_cast<int32_t>(std::round(clamped * 127.0f));
	scaled = std::min(127, std::max(-127, scaled));
	return static_cast<int8_t>(scaled);
}

inline uint32_t ToUnorm8(float value)
{
	float clamped = glm::clamp(value, 0.0f, 1.0f);
	return static_cast<uint32_t>(std::round(clamped * 255.0f));
}


// Store UV as FP16 bits
inline uint16_t FloatToHalfBits(float value)
{
	// minimal float->half conversion (IEEE 754), no dependencies
	union { uint32_t u; float f; } in{};
	in.f = value;

	uint32_t sign = (in.u >> 31) & 1u;
	int32_t exp = static_cast<int32_t>((in.u >> 23) & 0xFFu) - 127;
	uint32_t mantissa = in.u & 0x7FFFFFu;

	if (exp > 15) return static_cast<uint16_t>((sign << 15) | (0x1Fu << 10)); // inf

	if (exp < -14)
	{
		if (exp < -24) return static_cast<uint16_t>(sign << 15); // 0

		mantissa |= 0x800000u;
		uint32_t shift = static_cast<uint32_t>(-exp - 14);
		uint32_t halfMantissa = mantissa >> (shift + 13);
		return static_cast<uint16_t>((sign << 15) | halfMantissa);
	}

	uint16_t halfExp = static_cast<uint16_t>(exp + 15);
	uint16_t halfMantissa = static_cast<uint16_t>(mantissa >> 13);
	return static_cast<uint16_t>((sign << 15) | (halfExp << 10) | halfMantissa);
};

inline glm::vec2 EncodeOctahedral(const glm::vec3& n)
{
	glm::vec3 normal = glm::normalize(n);

	// Oct encode
	glm::vec3 oct = normal / (abs(normal.x) + abs(normal.y) + abs(normal.z));
	glm::vec2 enc = glm::vec2(oct.x, oct.y);

	if (oct.z < 0.0f)
	{
		glm::vec2 signNotZero = glm::vec2(
			(enc.x >= 0.0f) ? 1.0f : -1.0f,
			(enc.y >= 0.0f) ? 1.0f : -1.0f
		);

		enc = (glm::vec2(1.0f) - glm::abs(glm::vec2(enc.y, enc.x))) * signNotZero;
	}

	return enc;
}

inline void EncodeOctahedral_Normal(Vertex& vertex, const glm::vec3& n)
{
	glm::vec2 enc  = EncodeOctahedral(n);
	vertex.normalX = ToSnorm16(enc.x);
	vertex.normalY = ToSnorm16(enc.y);
}

inline void EncodeOctahedral_Tangent(Vertex& vertex, const glm::vec4& t)
{
	glm::vec2 enc = EncodeOctahedral(glm::vec3(t.x, t.y, t.z));

	int16_t tx = static_cast<int16_t>(glm::clamp(
		static_cast<int>(std::round(enc.x * 32767.0f)), -32767, 32767));

	const float yBiased = enc.y * 0.5f + 0.5f;
	int16_t ty = static_cast<int16_t>(glm::clamp(
		static_cast<int>(std::round(yBiased * 32767.0f)), 1, 32767));

	if (t.w < 0.0f) ty = -ty;

	vertex.tangentX = tx;
	vertex.tangentY = ty;
}

inline void EncodeRGBA8(Vertex& vertex, const glm::vec4& v)
{
	glm::vec4 c = glm::clamp(v, 0.0f, 1.0f);

	uint32_t r = ToUnorm8(c.r);
	uint32_t g = ToUnorm8(c.g);
	uint32_t b = ToUnorm8(c.b);
	uint32_t a = ToUnorm8(c.a);

	vertex.colorRGBA8 = (r) | (g << 8) | (b << 16) | (a << 24);
}

//// Decode FP16 bits (uint16_t) back to float
//inline float HalfBitsToFloat(uint16_t h)
//{
//	union { uint32_t u; float f; } out{};
//	uint32_t sign = (h >> 15) & 0x1u;
//	uint32_t exp  = (h >> 10) & 0x1Fu;
//	uint32_t mant = h & 0x3FFu;
//
//	if (exp == 0)
//	{
//		if (mant == 0) // zero
//			out.u = sign << 31;
//		else // subnormal
//		{
//			exp = 0x7F - 14; // adjust bias
//			while ((mant & 0x200) == 0) { mant <<= 1; exp--; }
//			mant &= 0x3FFu;
//			out.u = (sign << 31) | (exp << 23) | (mant << 13);
//		}
//	}
//	else if (exp == 31) // inf / nan
//	{
//		out.u = (sign << 31) | 0x7F800000u | (mant << 13);
//	}
//	else // normal
//	{
//		exp = exp + (0x7F - 15);
//		out.u = (sign << 31) | (exp << 23) | (mant << 13);
//	}
//	return out.f;
//}
