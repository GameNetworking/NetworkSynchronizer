#include "bit_array.h"

#include "core/math/math_funcs.h"
#include "core/string/ustring.h"

BitArray::BitArray(uint32_t p_initial_size_in_bit) {
	resize_in_bits(p_initial_size_in_bit);
}

BitArray::BitArray(const Vector<uint8_t> &p_bytes) :
		bytes(p_bytes) {
}

bool BitArray::resize_in_bytes(int p_bytes_count) {
	ERR_FAIL_COND_V_MSG(p_bytes_count < 0, false, "Bytes count can't be negative");
	bytes.resize(p_bytes_count);
	return true;
}

int BitArray::size_in_bytes() const {
	return bytes.size();
}

bool BitArray::resize_in_bits(int p_bits_count) {
	ERR_FAIL_COND_V_MSG(p_bits_count < 0, false, "Bits count can't be negative");
	const int min_size = Math::ceil((static_cast<float>(p_bits_count)) / 8.0);
	bytes.resize(min_size);
	return true;
}

int BitArray::size_in_bits() const {
	return bytes.size() * 8;
}

bool BitArray::store_bits(int p_bit_offset, uint64_t p_value, int p_bits) {
	ERR_FAIL_COND_V_MSG(p_bit_offset < 0, false, "Offset can't be negative");
	ERR_FAIL_COND_V_MSG(p_bits <= 0, false, "The number of bits should be more than 0");
	ERR_FAIL_INDEX_V_MSG(p_bit_offset + p_bits - 1, size_in_bits(), false, "The bit array size is `" + itos(size_in_bits()) + "` while you are trying to write `" + itos(p_bits) + "` starting from `" + itos(p_bit_offset) + "`.");

	int bits = p_bits;
	int bit_offset = p_bit_offset;
	uint64_t val = p_value;

	while (bits > 0) {
		const int bits_to_write = MIN(bits, 8 - bit_offset % 8);
		const int bits_to_jump = bit_offset % 8;
		const int bits_to_skip = 8 - (bits_to_write + bits_to_jump);
		const int byte_offset = bit_offset / 8;

		// Clear the bits that we have to write
		//const uint8_t byte_clear = ~(((0xFF >> bits_to_jump) << (bits_to_jump + bits_to_skip)) >> bits_to_skip);
		uint8_t byte_clear = 0xFF >> bits_to_jump;
		byte_clear = byte_clear << (bits_to_jump + bits_to_skip);
		byte_clear = ~(byte_clear >> bits_to_skip);
		bytes.write[byte_offset] &= byte_clear;

		// Now we can continue to write bits
		bytes.write[byte_offset] |= (val & 0xFF) << bits_to_jump;

		bits -= bits_to_write;
		bit_offset += bits_to_write;

		val >>= bits_to_write;
	}

	return true;
}

bool BitArray::read_bits(int p_bit_offset, int p_bits, std::uint64_t &r_out) const {
	ERR_FAIL_COND_V_MSG(p_bits <= 0, false, "The number of bits should be more than 0");
	ERR_FAIL_INDEX_V_MSG(p_bit_offset + p_bits - 1, size_in_bits(), false, "The bit array size is `" + itos(size_in_bits()) + "` while you are trying to read `" + itos(p_bits) + "` starting from `" + itos(p_bit_offset) + "`.");

	int bits = p_bits;
	int bit_offset = p_bit_offset;
	uint64_t val = 0;

	const uint8_t *bytes_ptr = bytes.ptr();

	int val_bits_to_jump = 0;
	while (bits > 0) {
		const int bits_to_read = MIN(bits, 8 - bit_offset % 8);
		const int bits_to_jump = bit_offset % 8;
		const int bits_to_skip = 8 - (bits_to_read + bits_to_jump);
		const int byte_offset = bit_offset / 8;

		uint8_t byte_mask = 0xFF >> bits_to_jump;
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
		memset(bytes.ptrw(), 0, sizeof(uint8_t) * bytes.size());
	}
}
