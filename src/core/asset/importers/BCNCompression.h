#pragma once

#include <vector>
#include <cstdint>

void CompressBC7(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, std::vector<uint8_t>& out);
void CompressBC5(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, std::vector<uint8_t>& out);
void InitBC7Encoder();
