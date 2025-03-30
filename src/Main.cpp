#include "Engine.h"
#include <iostream>

int main() {
	std::cout << "Who is the real yn?" << std::endl;
	try {
		Engine::run();
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}