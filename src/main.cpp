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
	catch (const std::exception& error)
	{
		std::println(stderr, "[Quokka] Error: {}", error.what());

		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
