#pragma once

#include <vector>
#include <cstdint>

namespace NS {
class SceneSynchronizerDebugger;
}

class BitArray {
	NS::SceneSynchronizerDebugger *debugger = nullptr;
	std::vector<std::uint8_t> bytes;

public:
	BitArray() = default; // Use this with care: Not initializing the debugger will cause a crash
	BitArray(const BitArray &p_other) = default;
	BitArray(NS::SceneSynchronizerDebugger &p_debugger);
	BitArray(NS::SceneSynchronizerDebugger &p_debugger, std::uint32_t p_initial_size_in_bit);
	BitArray(NS::SceneSynchronizerDebugger &p_debugger, const std::vector<std::uint8_t> &p_bytes);

	void operator=(const BitArray &p_other);

	void set_debugger(NS::SceneSynchronizerDebugger &p_debugger) {
		debugger = &p_debugger;
	}

	NS::SceneSynchronizerDebugger &get_debugger() const {
		return *debugger;
	}

	const std::vector<std::uint8_t> &get_bytes() const {
		return bytes;
	}

	std::vector<std::uint8_t> &get_bytes_mut() {
		return bytes;
	}

	bool resize_in_bytes(int p_bytes_count);
	int size_in_bytes() const;

	bool resize_in_bits(int p_bits_count);
	int size_in_bits() const;

	bool store_bits(int p_bit_offset, std::uint64_t p_value, int p_bits);
	bool read_bits(int p_bit_offset, int p_bits, std::uint64_t &r_out) const;

	// Puts all the bytes to 0.
	void zero();
};
