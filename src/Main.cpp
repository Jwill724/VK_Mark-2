#include "Engine.h"
#include "fmt/core.h"

int main() {
	fmt::print("Dead Horse! Only in Atlanta\n");

	try {
		Engine::run();
	}
	catch (const std::exception& e) {
		fmt::print("Exception: {}\n", e.what());
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}