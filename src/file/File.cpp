#include "File.h"

std::vector <char> File::readFile(const std::string& fileName) {
	// ate: starts reading from end of file
	// binary: read as binary file
	// starting from the end we get a position to return size of buffer
	std::ifstream file(fileName, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("Failed to open file!");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();

	return buffer;
}