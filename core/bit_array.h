#pragma once

#include "core/templates/vector.h"

class BitArray {
	Vector<uint8_t> bytes;

public:
	BitArray() = default;
	BitArray(uint32_t p_initial_size_in_bit);
	BitArray(const Vector<uint8_t> &p_bytes);

	const Vector<uint8_t> &get_bytes() const {
		return bytes;
	}

	Vector<uint8_t> &get_bytes_mut() {
		return bytes;
	}

	bool resize_in_bytes(int p_bytes_count);
	int size_in_bytes() const;

	bool resize_in_bits(int p_bits_count);
	int size_in_bits() const;

	bool store_bits(int p_bit_offset, uint64_t p_value, int p_bits);
	bool read_bits(int p_bit_offset, int p_bits, std::uint64_t &r_out) const;

	// Puts all the bytes to 0.
	void zero();
};
