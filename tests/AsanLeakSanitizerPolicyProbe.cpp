#include <cstddef>

int main() {
	volatile std::byte* const intentional_leak = new std::byte[1];
	intentional_leak[0]						   = std::byte{0};
	return 0;
}
