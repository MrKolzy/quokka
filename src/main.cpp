#include "quokka/engine.hpp"

// ***

#include <cstdlib> // EXIT_FAILURE, EXIT_SUCCESS
#include <exception>
#include <iostream> // std::cerr

int main()
{
	try
	{
		quokka::Engine quokka {};
		quokka.run();
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';

		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
