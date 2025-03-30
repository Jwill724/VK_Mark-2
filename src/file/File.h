#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <fstream>

namespace File {
	std::vector <char> readFile(const std::string& fileName);
}