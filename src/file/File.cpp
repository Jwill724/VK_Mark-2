#include "pch.h"

#include "File.h"

std::vector<char> File::readFile(const std::string& fileName) {
	fmt::print("Reading file: {}\n", fileName);

	std::ifstream file(fileName, std::ios::ate | std::ios::binary);
	ASSERT(file.is_open() && "Failed to open file!");

	size_t fileSize = static_cast<size_t>(file.tellg());
	ASSERT(fileSize > 0 && "File size is zero!");

	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	ASSERT(file.good() && "File read encountered an error.");

	file.close();
	return buffer;
}