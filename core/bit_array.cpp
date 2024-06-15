#include "bit_array.h"

#include "ensure.h"
#include <string.h> // Needed to include `memset` in linux.
#include <algorithm>
#include <cmath>

BitArray::BitArray(std::uint32_t p_initial_size_in_bit) {
	resize_in_bits(p_initial_size_in_bit);
}

BitArray::BitArray(const std::vector<std::uint8_t> &p_bytes) :
		bytes(p_bytes) {
}

bool BitArray::resize_in_bytes(int p_bytes_count) {
	ENSURE_V_MSG(p_bytes_count >= 0, false, "Bytes count can't be negative");
	bytes.resize(p_bytes_count);
	return true;
}

int BitArray::size_in_bytes() const {
	return int(bytes.size());
}

bool BitArray::resize_in_bits(int p_bits_count) {
	ENSURE_V_MSG(p_bits_count >= 0, false, "Bits count can't be negative");
	const int min_size = int(std::ceil((static_cast<float>(p_bits_count)) / 8.0f));
	bytes.resize(min_size);
	return true;
}

int BitArray::size_in_bits() const {
	return int(bytes.size() * 8);
}

bool BitArray::store_bits(int p_bit_offset, std::uint64_t p_value, int p_bits) {
	ENSURE_V_MSG(p_bit_offset >= 0, false, "Offset can't be negative");
	ENSURE_V_MSG(p_bits > 0, false, "The number of bits should be more than 0");
	ENSURE_V_MSG((p_bit_offset + p_bits - 1) < size_in_bits(), false, "The bit array size is `" + std::to_string(size_in_bits()) + "` while you are trying to write `" + std::to_string(p_bits) + "` starting from `" + std::to_string(p_bit_offset) + "`.");

	int bits = p_bits;
	int bit_offset = p_bit_offset;
	uint64_t val = p_value;

	while (bits > 0) {
		const int bits_to_write = std::min(bits, 8 - bit_offset % 8);
		const int bits_to_jump = bit_offset % 8;
		const int bits_to_skip = 8 - (bits_to_write + bits_to_jump);
		const int byte_offset = bit_offset / 8;

		// Clear the bits that we have to write
		//const std::uint8_t byte_clear = ~(((0xFF >> bits_to_jump) << (bits_to_jump + bits_to_skip)) >> bits_to_skip);
		std::uint8_t byte_clear = 0xFF >> bits_to_jump;
		byte_clear = byte_clear << (bits_to_jump + bits_to_skip);
		byte_clear = ~(byte_clear >> bits_to_skip);
		bytes[byte_offset] &= byte_clear;

		// Now we can continue to write bits
		bytes[byte_offset] |= (val & 0xFF) << bits_to_jump;

		bits -= bits_to_write;
		bit_offset += bits_to_write;

		val >>= bits_to_write;
	}

	return true;
}

bool BitArray::read_bits(int p_bit_offset, int p_bits, std::uint64_t &r_out) const {
	ENSURE_V_MSG(p_bits > 0, false, "The number of bits should be more than 0");
	ENSURE_V_MSG((p_bit_offset + p_bits - 1) < size_in_bits(), false, "The bit array size is `" + std::to_string(size_in_bits()) + "` while you are trying to read `" + std::to_string(p_bits) + "` starting from `" + std::to_string(p_bit_offset) + "`.");

	int bits = p_bits;
	int bit_offset = p_bit_offset;
	uint64_t val = 0;

	const std::uint8_t *bytes_ptr = bytes.data();

	int val_bits_to_jump = 0;
	while (bits > 0) {
		const int bits_to_read = std::min(bits, 8 - bit_offset % 8);
		const int bits_to_jump = bit_offset % 8;
		const int bits_to_skip = 8 - (bits_to_read + bits_to_jump);
		const int byte_offset = bit_offset / 8;

		std::uint8_t byte_mask = 0xFF >> bits_to_jump;
		byte_mask = byte_mask << (bits_to_skip + bits_to_jump);
		byte_mask = byte_mask >> bits_to_skip;
		const uint64_t byte_val = static_cast<uint64_t>((bytes_ptr[byte_offset] & byte_mask) >> bits_to_jump);
		val |= byte_val << val_bits_to_jump;

		bits -= bits_to_read;
		bit_offset += bits_to_read;
		val_bits_to_jump += bits_to_read;
	}

	r_out = val;
	return true;
}

void BitArray::zero() {
	if (bytes.size() > 0) {
		memset(bytes.data(), 0, sizeof(std::uint8_t) * bytes.size());
	}
}
