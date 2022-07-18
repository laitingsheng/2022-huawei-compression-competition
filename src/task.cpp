#include <cstdint>

#include <stdexcept>

#include "core.hpp"

int main(int argc, char * argv[])
{
	if (argc < 4)
		throw std::runtime_error("expect at least 3 arguments");

	if (char mode = argv[1][0]; mode == 'c')
		core::compress(argv[2], argv[3]);
	else if (mode == 'd')
		core::decompress(argv[2], argv[3]);
	else
	[[unlikely]]
		throw std::runtime_error("invalid arguments supplied");

	return 0;
}
