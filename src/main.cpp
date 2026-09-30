#include "quokka/engine.hpp"

#include <cstdio> // stderr
#include <cstdlib> // EXIT_FAILURE, EXIT_SUCCESS
#include <exception>
#include <print>

int main()
{
	try
	{
		quokka::Engine engine {};
		engine.run();
	}
	catch (const std::exception& exception)
	{
		std::println(stderr, "[Quokka] Error: {}", exception.what());

		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
