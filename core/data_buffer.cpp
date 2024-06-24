#include "data_buffer.h"

#include "ensure.h"
#include "fp16.h"
#include "net_math.h"
#include "scene_synchronizer_debugger.h"

// Using explicit declaration because std::numeric_limits<int16_t>::max() etc..
// may return different value depending on the compiler used.
#define NS__INT8_MIN (-127i8 - 1)
#define NS__INT16_MIN (-32767i16 - 1)
#define NS__INT32_MIN (-2147483647i32 - 1)
#define NS__INT64_MIN (-9223372036854775807i64 - 1)
#define NS__INT8_MAX 127i8
#define NS__INT16_MAX 32767i16
#define NS__INT32_MAX 2147483647i32
#define NS__INT64_MAX 9223372036854775807i64
#define NS__UINT8_MAX 0xffui8
#define NS__UINT16_MAX 0xffffui16
#define NS__UINT32_MAX 0xffffffffui32
#define NS__UINT64_MAX 0xffffffffffffffffui64

#ifdef NS_DEBUG_ENABLED
#define DEBUG_DATA_BUFFER

#ifdef DISABLE_DEBUG_DATA_BUFFER
#undef DEBUG_DATA_BUFFER
#endif
#endif

#ifdef DEBUG_DATA_BUFFER
#define DEB_WRITE(dt, compression, input)                                                             \
	if (debug_enabled) {                                                                              \
		SceneSynchronizerDebugger::singleton()->databuffer_write(dt, compression, bit_offset, std::string(input).c_str()); \
	}

#define DEB_READ(dt, compression, input)                                                             \
	if (debug_enabled) {                                                                             \
		SceneSynchronizerDebugger::singleton()->databuffer_read(dt, compression, bit_offset, std::string(input).c_str()); \
	}

// Beware that the following two macros were written to make sure nested function call doesn't add debug calls,
// making the log unreadable.
#define DEB_DISABLE                               \
	const bool was_debug_enabled = debug_enabled; \
	debug_enabled = false;

#define DEB_ENABLE debug_enabled = was_debug_enabled;

#else
#define DEB_WRITE(dt, compression, input) (void)(input);
#define DEB_READ(dt, compression, input) (void)(input);
#define DEB_DISABLE
#define DEB_ENABLE
#endif

// TODO improve the allocation mechanism.

NS_NAMESPACE_BEGIN
DataBuffer::DataBuffer(const DataBuffer &p_other) :
	metadata_size(p_other.metadata_size),
	bit_offset(p_other.bit_offset),
	bit_size(p_other.bit_size),
	is_reading(p_other.is_reading),
	buffer(p_other.buffer) {
}

DataBuffer::DataBuffer(const BitArray &p_buffer) :
	bit_size(p_buffer.size_in_bits()),
	is_reading(true),
	buffer(p_buffer) {
}

// TODO : Implemet this.
//DataBuffer &DataBuffer::operator=(DataBuffer &&p_other) {
//	metadata_size = std::move(p_other.metadata_size);
//	bit_offset = std::move(p_other.bit_offset);
//	bit_size = std::move(p_other.bit_size);
//	is_reading = std::move(p_other.is_reading);
//	buffer = std::move(p_other.buffer);
//}

bool DataBuffer::operator==(const DataBuffer &p_other) const {
	return buffer.get_bytes() == p_other.buffer.get_bytes();
}

void DataBuffer::copy(const DataBuffer &p_other) {
	metadata_size = p_other.metadata_size;
	bit_offset = p_other.bit_offset;
	bit_size = p_other.bit_size;
	is_reading = p_other.is_reading;
	buffer = p_other.buffer;
}

void DataBuffer::copy(const BitArray &p_buffer) {
	metadata_size = 0;
	bit_offset = 0;
	bit_size = p_buffer.size_in_bits();
	is_reading = true;
	buffer = p_buffer;
}

void DataBuffer::begin_write(int p_metadata_size) {
	NS_ASSERT_COND_MSG(p_metadata_size >= 0, "Metadata size can't be negative");
	metadata_size = p_metadata_size;
	bit_size = 0;
	bit_offset = 0;
	is_reading = false;
	buffer_failed = false;
}

void DataBuffer::dry() {
	buffer.resize_in_bits(metadata_size + bit_size);
}

void DataBuffer::seek(int p_bits) {
	if (p_bits >= metadata_size + bit_size + 1 || p_bits < 0) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY();
	}
	bit_offset = p_bits;
}

void DataBuffer::shrink_to(int p_metadata_bit_size, int p_bit_size) {
	NS_ASSERT_COND_MSG(p_metadata_bit_size >= 0, "Metadata size can't be negative");
	NS_ASSERT_COND_MSG(p_bit_size >= 0, "Bit size can't be negative");
	if (buffer.size_in_bits() < (p_metadata_bit_size + p_bit_size)) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_MSG("The buffer is smaller than the new given size.");
	}
	metadata_size = p_metadata_bit_size;
	bit_size = p_bit_size;
}

int DataBuffer::get_metadata_size() const {
	return metadata_size;
}

int DataBuffer::size() const {
	return bit_size;
}

int DataBuffer::total_size() const {
	return bit_size + metadata_size;
}

int DataBuffer::get_bit_offset() const {
	return bit_offset;
}

void DataBuffer::skip(int p_bits) {
	if ((metadata_size + bit_size) < (bit_offset + p_bits)) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY();
	}
	bit_offset += p_bits;
}

void DataBuffer::begin_read() {
	bit_offset = 0;
	is_reading = true;
	buffer_failed = false;
}

void DataBuffer::add(bool p_input) {
	add_bool(p_input);
}

void DataBuffer::read(bool &r_out) {
	r_out = read_bool();
}

void DataBuffer::add(std::uint8_t p_input) {
	add_uint(p_input, COMPRESSION_LEVEL_3);
}

void DataBuffer::read(std::uint8_t &r_out) {
	r_out = (std::uint8_t)read_uint(COMPRESSION_LEVEL_3);
}

void DataBuffer::add(std::uint16_t p_input) {
	add_uint(p_input, COMPRESSION_LEVEL_2);
}

void DataBuffer::read(std::uint16_t &r_out) {
	r_out = (std::uint16_t)read_uint(COMPRESSION_LEVEL_2);
}

void DataBuffer::add(std::uint32_t p_input) {
	add_uint(p_input, COMPRESSION_LEVEL_1);
}

void DataBuffer::read(std::uint32_t &r_out) {
	r_out = (std::uint32_t)read_uint(COMPRESSION_LEVEL_1);
}

void DataBuffer::add(std::uint64_t p_input) {
	add_uint(p_input, COMPRESSION_LEVEL_0);
}

void DataBuffer::read(std::uint64_t &r_out) {
	r_out = read_uint(COMPRESSION_LEVEL_0);
}

void DataBuffer::add(std::int8_t p_input) {
	add_int(p_input, COMPRESSION_LEVEL_3);
}

void DataBuffer::read(std::int8_t &r_out) {
	r_out = (std::int8_t)read_int(COMPRESSION_LEVEL_3);
}

void DataBuffer::add(std::int16_t p_input) {
	add_int(p_input, COMPRESSION_LEVEL_2);
}

void DataBuffer::read(std::int16_t &r_out) {
	r_out = (std::int16_t)read_int(COMPRESSION_LEVEL_2);
}

void DataBuffer::add(std::int32_t p_input) {
	add_int(p_input, COMPRESSION_LEVEL_1);
}

void DataBuffer::read(std::int32_t &r_out) {
	r_out = (std::int32_t)read_int(COMPRESSION_LEVEL_1);
}

void DataBuffer::add(std::int64_t p_input) {
	add_int(p_input, COMPRESSION_LEVEL_0);
}

void DataBuffer::read(std::int64_t &r_out) {
	r_out = read_int(COMPRESSION_LEVEL_0);
}

void DataBuffer::add(float p_input) {
	add_real(p_input, COMPRESSION_LEVEL_1);
}

void DataBuffer::read(float &p_out) {
	read_real(p_out, COMPRESSION_LEVEL_1);
}

void DataBuffer::add(double p_input) {
	add_real(p_input, COMPRESSION_LEVEL_0);
}

void DataBuffer::read(double &p_out) {
	read_real(p_out, COMPRESSION_LEVEL_0);
}

void DataBuffer::add(const std::string &p_string) {
	NS_ASSERT_COND(std::uint64_t(p_string.size()) <= std::uint64_t(NS__INT16_MAX));
	add_uint(std::uint64_t(p_string.size()), COMPRESSION_LEVEL_2);
	if (p_string.size() > 0) {
		add_bits(reinterpret_cast<const uint8_t *>(p_string.c_str()), int(p_string.size() * 8));
	}
}

void DataBuffer::read(std::string &r_out) {
	const uint16_t size = std::uint16_t(read_uint(COMPRESSION_LEVEL_2));
	if (size <= 0) {
		return;
	}

	std::vector<char> chars;
	chars.resize(size);
	read_bits(reinterpret_cast<uint8_t *>(chars.data()), int(size * 8));
	r_out = std::string(chars.data(), size);
}

void DataBuffer::add(const std::u16string &p_string) {
	NS_ASSERT_COND(std::uint64_t(p_string.size()) <= std::uint64_t(NS__UINT16_MAX));
	add_uint(p_string.size(), COMPRESSION_LEVEL_2);
	if (p_string.size() > 0) {
		add_bits(reinterpret_cast<const uint8_t *>(p_string.c_str()), int(p_string.size() * 8 * (sizeof(char16_t))));
	}
}

void DataBuffer::read(std::u16string &r_out) {
	const uint16_t size = std::uint16_t(read_uint(COMPRESSION_LEVEL_2));
	if (size <= 0) {
		return;
	}

	std::vector<char16_t> chars;
	chars.resize(size);
	read_bits(reinterpret_cast<uint8_t *>(chars.data()), size * 8 * (sizeof(char16_t)));
	r_out = std::u16string(chars.data(), size);
}

void DataBuffer::add(const DataBuffer &p_db) {
	add_data_buffer(p_db);
}

void DataBuffer::read(DataBuffer &r_db) {
	read_data_buffer(r_db);
}

bool DataBuffer::add_bool(bool p_input) {
	if (is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_V_MSG(p_input, "You can't add to the DataBuffer while reading!");
	}

	const int bits = get_bit_taken(DATA_TYPE_BOOL, COMPRESSION_LEVEL_0);

	make_room_in_bits(bits);
	if (!buffer.store_bits(bit_offset, p_input, bits)) {
		buffer_failed = true;
	}
	bit_offset += bits;

#ifdef NS_DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	NS_ASSERT_COND((metadata_size + bit_size) <= buffer.size_in_bits() && bit_offset <= buffer.size_in_bits());
#endif

	DEB_WRITE(DATA_TYPE_BOOL, COMPRESSION_LEVEL_0, p_input ? "TRUE" : "FALSE");

	return p_input;
}

bool DataBuffer::read_bool() {
	if (!is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_V_MSG(false, "You can't read from the DataBuffer while writing!");
	}

	const int bits = get_bit_taken(DATA_TYPE_BOOL, COMPRESSION_LEVEL_0);
	std::uint64_t d;
	if (!buffer.read_bits(bit_offset, bits, d)) {
		buffer_failed = true;
		return false;
	}
	bit_offset += bits;

	DEB_READ(DATA_TYPE_BOOL, COMPRESSION_LEVEL_0, d ? "TRUE" : "FALSE");

	return d;
}

int64_t DataBuffer::add_int(int64_t p_input, CompressionLevel p_compression_level) {
	if (is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_V_MSG(p_input, "You can't add to the DataBuffer while reading!");
	}

	const int bits = get_bit_taken(DATA_TYPE_INT, p_compression_level);

	int64_t value = p_input;

	// Clamp the value to the max that the bit can store.
	if (bits == 8) {
		value = MathFunc::clamp(value, NS__INT8_MIN, NS__INT8_MAX);
	} else if (bits == 16) {
		value = MathFunc::clamp(value, NS__INT16_MIN, NS__INT16_MAX);
	} else if (bits == 32) {
		value = MathFunc::clamp(value, NS__INT32_MIN, NS__INT32_MAX);
	} else {
		// Nothing to do here
	}

	make_room_in_bits(bits);

	// Safely convert int to uint.
	uint64_t uvalue;
	memcpy(&uvalue, &value, sizeof(uint64_t));

	if (!buffer.store_bits(bit_offset, uvalue, bits)) {
		buffer_failed = true;
	}
	bit_offset += bits;

#ifdef NS_DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	NS_ASSERT_COND((metadata_size + bit_size) <= buffer.size_in_bits() && bit_offset <= buffer.size_in_bits());
#endif

	DEB_WRITE(DATA_TYPE_INT, p_compression_level, std::to_string(value));

	return value;
}

int64_t DataBuffer::read_int(CompressionLevel p_compression_level) {
	if (!is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_V_MSG(0, "You can't read from the DataBuffer while writing!");
	}

	const int bits = get_bit_taken(DATA_TYPE_INT, p_compression_level);

	uint64_t uvalue;
	if (!buffer.read_bits(bit_offset, bits, uvalue)) {
		buffer_failed = true;
		return 0;
	}
	bit_offset += bits;

	int64_t value;
	memcpy(&value, &uvalue, sizeof(uint64_t));

	if (bits == 8) {
		DEB_READ(DATA_TYPE_INT, p_compression_level, std::to_string(static_cast<int8_t>(value)));
		return static_cast<int8_t>(value);
	} else if (bits == 16) {
		DEB_READ(DATA_TYPE_INT, p_compression_level, std::to_string(static_cast<int16_t>(value)));
		return static_cast<int16_t>(value);
	} else if (bits == 32) {
		DEB_READ(DATA_TYPE_INT, p_compression_level, std::to_string(static_cast<int32_t>(value)));
		return static_cast<int32_t>(value);
	} else {
		DEB_READ(DATA_TYPE_INT, p_compression_level, std::to_string(value));
		return value;
	}
}

std::uint64_t DataBuffer::add_uint(std::uint64_t p_input, CompressionLevel p_compression_level) {
	if (is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_V_MSG(p_input, "You can't add to the DataBuffer while reading!");
	}

	const int bits = get_bit_taken(DATA_TYPE_UINT, p_compression_level);

	uint64_t value = p_input;

	// Clamp the value to the max that the bit can store.
	if (bits == 8) {
		value = std::min(value, std::uint64_t(NS__UINT8_MAX));
	} else if (bits == 16) {
		value = std::min(value, std::uint64_t(NS__UINT16_MAX));
	} else if (bits == 32) {
		value = std::min(value, std::uint64_t(NS__UINT32_MAX));
	} else {
		// Nothing to do here
	}

	make_room_in_bits(bits);

	if (!buffer.store_bits(bit_offset, value, bits)) {
		buffer_failed = true;
	}
	bit_offset += bits;

#ifdef NS_DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	NS_ASSERT_COND((metadata_size + bit_size) <= buffer.size_in_bits() && bit_offset <= buffer.size_in_bits());
#endif

	DEB_WRITE(DATA_TYPE_UINT, p_compression_level, std::to_string(value));

	return value;
}

std::uint64_t DataBuffer::read_uint(CompressionLevel p_compression_level) {
	if (!is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_V_MSG(0, "You can't read from the DataBuffer while writing!");
	}

	const int bits = get_bit_taken(DATA_TYPE_UINT, p_compression_level);

	std::uint64_t value;
	if (!buffer.read_bits(bit_offset, bits, value)) {
		buffer_failed = true;
		return 0;
	}
	bit_offset += bits;

	DEB_READ(DATA_TYPE_UINT, p_compression_level, std::to_string(value));

	return value;
}

void DataBuffer::add_real(double p_input, CompressionLevel p_compression_level) {
	if (is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_MSG("You can't add to the DataBuffer while reading!");
	}

	if (p_compression_level == COMPRESSION_LEVEL_0) {
		const uint64_t val = fp64_to_bits(p_input);
		make_room_in_bits(64);
		if (!buffer.store_bits(bit_offset, val, 64)) {
			buffer_failed = true;
		}
		bit_offset += 64;

		DEB_WRITE(DATA_TYPE_REAL, p_compression_level, std::to_string(p_input));
	} else {
		add_real(float(p_input), p_compression_level);
	}
}

void DataBuffer::add_real(float p_input, CompressionLevel p_compression_level) {
	if (is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_MSG("You can't add to the DataBuffer while reading!");
	}

	if (p_compression_level == COMPRESSION_LEVEL_0) {
		SceneSynchronizerDebugger::singleton()->print(WARNING, "The real(float) fall back to compression level 1 as the level 0 is for double compression.");
		p_compression_level = COMPRESSION_LEVEL_1;
	}

	if (p_compression_level == COMPRESSION_LEVEL_1) {
		const uint32_t val = fp32_to_bits(p_input);
		make_room_in_bits(32);
		if (!buffer.store_bits(bit_offset, val, 32)) {
			buffer_failed = true;
		}
		bit_offset += 32;
	} else if (p_compression_level == COMPRESSION_LEVEL_2 || p_compression_level == COMPRESSION_LEVEL_3) {
		std::uint16_t val = fp16_ieee_from_fp32_value(p_input);
		make_room_in_bits(16);
		if (!buffer.store_bits(bit_offset, val, 16)) {
			buffer_failed = true;
		}
		bit_offset += 16;
	} else {
		// Unreachable.
		NS_ASSERT_NO_ENTRY();
	}

	DEB_WRITE(DATA_TYPE_REAL, p_compression_level, std::to_string(p_input));
}

void DataBuffer::read_real(double &r_value, CompressionLevel p_compression_level) {
	if (!is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_MSG("You can't read from the DataBuffer while writing!");
	}

	if (p_compression_level == COMPRESSION_LEVEL_0) {
		std::uint64_t bit_value;
		if (!buffer.read_bits(bit_offset, 64, bit_value)) {
			buffer_failed = true;
			return;
		}
		bit_offset += 64;

		r_value = fp64_from_bits(bit_value);
		DEB_READ(DATA_TYPE_REAL, p_compression_level, std::to_string(r_value));
	} else {
		float flt_value;
		read_real(flt_value, p_compression_level);
		r_value = flt_value;
	}
}

void DataBuffer::read_real(float &r_value, CompressionLevel p_compression_level) {
	if (!is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_MSG("You can't read from the DataBuffer while writing!");
	}

	if (p_compression_level == COMPRESSION_LEVEL_0) {
		SceneSynchronizerDebugger::singleton()->print(WARNING, "The real(float) fall back to compression level 1 as the level 0 is for double compression.");
		p_compression_level = COMPRESSION_LEVEL_1;
	}

	if (p_compression_level == COMPRESSION_LEVEL_1) {
		std::uint64_t bit_value;
		if (!buffer.read_bits(bit_offset, 32, bit_value)) {
			buffer_failed = true;
			return;
		}
		bit_offset += 32;

		r_value = fp32_from_bits(std::uint32_t(bit_value));
	} else if (p_compression_level == COMPRESSION_LEVEL_2 || p_compression_level == COMPRESSION_LEVEL_3) {
		std::uint64_t bit_value;
		if (!buffer.read_bits(bit_offset, 16, bit_value)) {
			buffer_failed = true;
			return;
		}
		bit_offset += 16;

		r_value = fp16_ieee_to_fp32_value(std::uint16_t(bit_value));
	} else {
		// Unreachable.
		NS_ASSERT_NO_ENTRY();
	}

	DEB_READ(DATA_TYPE_REAL, p_compression_level, std::to_string(r_value));
}

float DataBuffer::add_positive_unit_real(float p_input, CompressionLevel p_compression_level) {
	if (is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_V_MSG(p_input, "You can't add to the DataBuffer while reading!");
	}

	p_input = MathFunc::clamp(p_input, 0.0f, 1.0f);

	const int bits = get_bit_taken(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression_level);

	const float max_value = static_cast<float>(~(UINT64_MAX << bits));

	const uint64_t compressed_val = compress_unit_float<float>(p_input, max_value);

	make_room_in_bits(bits);
	if (!buffer.store_bits(bit_offset, compressed_val, bits)) {
		buffer_failed = true;
	}
	bit_offset += bits;

#ifdef NS_DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	NS_ASSERT_COND((metadata_size + bit_size) <= buffer.size_in_bits() && bit_offset <= buffer.size_in_bits());
#endif

	const float value = decompress_unit_float(compressed_val, max_value);
	DEB_WRITE(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression_level, std::to_string(value));
	return value;
}

float DataBuffer::read_positive_unit_real(CompressionLevel p_compression_level) {
	if (!is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_V_MSG(0.0f, "You can't read from the DataBuffer while writing!");
	}

	const int bits = get_bit_taken(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression_level);

	const float max_value = static_cast<float>(~(UINT64_MAX << bits));

	std::uint64_t compressed_val;
	if (!buffer.read_bits(bit_offset, bits, compressed_val)) {
		buffer_failed = true;
		return 0.0f;
	}
	bit_offset += bits;

	const float value = decompress_unit_float(compressed_val, max_value);

	DEB_READ(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression_level, std::to_string(value));

	return value;
}

float DataBuffer::add_unit_real(float p_input, CompressionLevel p_compression_level) {
	if (is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_V_MSG(p_input, "You can't add to the DataBuffer while reading!");
	}

	const float added_real = add_positive_unit_real(std::abs(p_input), p_compression_level);

	const int bits_for_sign = 1;
	const uint32_t is_negative = p_input < 0.0;
	make_room_in_bits(bits_for_sign);
	if (!buffer.store_bits(bit_offset, is_negative, bits_for_sign)) {
		buffer_failed = true;
	}
	bit_offset += bits_for_sign;

#ifdef NS_DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	NS_ASSERT_COND((metadata_size + bit_size) <= buffer.size_in_bits() && bit_offset <= buffer.size_in_bits());
#endif

	const float value = is_negative ? -added_real : added_real;
	DEB_WRITE(DATA_TYPE_UNIT_REAL, p_compression_level, std::to_string(value));

	return value;
}

float DataBuffer::read_unit_real(CompressionLevel p_compression_level) {
	if (!is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_V_MSG(0.0f, "You can't read from the DataBuffer while writing!");
	}

	const float value = read_positive_unit_real(p_compression_level);

	const int bits_for_sign = 1;
	std::uint64_t is_negative;
	if (!buffer.read_bits(bit_offset, bits_for_sign, is_negative)) {
		buffer_failed = true;
		return 0.0;
	}
	bit_offset += bits_for_sign;

	const float ret = is_negative != 0 ? -value : value;

	DEB_READ(DATA_TYPE_UNIT_REAL, p_compression_level, std::to_string(ret));

	return ret;
}

void DataBuffer::add_vector2(double x, double y, CompressionLevel p_compression_level) {
	DEB_DISABLE

	add_real(x, p_compression_level);
	add_real(y, p_compression_level);

	DEB_ENABLE

	DEB_WRITE(DATA_TYPE_VECTOR2, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y));
}

void DataBuffer::add_vector2(float x, float y, CompressionLevel p_compression_level) {
	DEB_DISABLE

	add_real(x, p_compression_level);
	add_real(y, p_compression_level);

	DEB_ENABLE

	DEB_WRITE(DATA_TYPE_VECTOR2, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y));
}

void DataBuffer::read_vector2(double &x, double &y, CompressionLevel p_compression_level) {
	DEB_DISABLE

	read_real(x, p_compression_level);
	read_real(y, p_compression_level);

	DEB_ENABLE

	DEB_READ(DATA_TYPE_VECTOR2, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y));
}

void DataBuffer::read_vector2(float &x, float &y, CompressionLevel p_compression_level) {
	DEB_DISABLE

	read_real(x, p_compression_level);
	read_real(y, p_compression_level);

	DEB_ENABLE

	DEB_READ(DATA_TYPE_VECTOR2, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y));
}

template void DataBuffer::add_normalized_vector2<float>(float, float, DataBuffer::CompressionLevel);
template void DataBuffer::add_normalized_vector2<double>(double, double, DataBuffer::CompressionLevel);

template <typename T>
void DataBuffer::add_normalized_vector2(T x, T y, CompressionLevel p_compression_level) {
	if (is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_MSG("You can't add to the DataBuffer while reading!");
	}

	const std::uint64_t is_not_zero = MathFunc::is_zero_approx(x) && MathFunc::is_zero_approx(y) ? 0 : 1;

	const int bits = get_bit_taken(DATA_TYPE_NORMALIZED_VECTOR2, p_compression_level);
	const int bits_for_the_angle = bits - 1;
	const int bits_for_zero = 1;

	const T angle = is_not_zero ? MathFunc::vec2_angle(x, y) : 0.0f;

	const T max_value = static_cast<T>(~(NS__UINT64_MAX << bits_for_the_angle));

	const uint64_t compressed_angle = compress_unit_float<T>((angle + T(M_PI)) / T(M_TAU), max_value);

	make_room_in_bits(bits);
	if (!buffer.store_bits(bit_offset, is_not_zero, bits_for_zero)) {
		buffer_failed = true;
	}
	if (!buffer.store_bits(bit_offset + 1, compressed_angle, bits_for_the_angle)) {
		buffer_failed = true;
	}
	bit_offset += bits;

#ifdef NS_DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	NS_ASSERT_COND((metadata_size + bit_size) <= buffer.size_in_bits() && bit_offset <= buffer.size_in_bits());
#endif

	DEB_WRITE(DATA_TYPE_NORMALIZED_VECTOR2, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y));
}

template void DataBuffer::read_normalized_vector2<float>(float &, float &, DataBuffer::CompressionLevel);
template void DataBuffer::read_normalized_vector2<double>(double &, double &, DataBuffer::CompressionLevel);

template <typename T>
void DataBuffer::read_normalized_vector2(T &x, T &y, CompressionLevel p_compression_level) {
	if (!is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_MSG("You can't read from the DataBuffer while writing!");
	}

	const int bits = get_bit_taken(DATA_TYPE_NORMALIZED_VECTOR2, p_compression_level);
	const int bits_for_the_angle = bits - 1;
	const int bits_for_zero = 1;

	const T max_value = static_cast<T>(~(UINT64_MAX << bits_for_the_angle));

	std::uint64_t is_not_zero;
	if (!buffer.read_bits(bit_offset, bits_for_zero, is_not_zero)) {
		buffer_failed = true;
		return;
	}
	std::uint64_t compressed_angle;
	if (!buffer.read_bits(bit_offset + 1, bits_for_the_angle, compressed_angle)) {
		buffer_failed = true;
		return;
	}
	bit_offset += bits;

	const T decompressed_angle = (decompress_unit_float<T>(compressed_angle, max_value) * T(M_TAU)) - T(M_PI);
	x = std::cos(decompressed_angle) * static_cast<T>(is_not_zero);
	y = std::sin(decompressed_angle) * static_cast<T>(is_not_zero);

	DEB_READ(DATA_TYPE_NORMALIZED_VECTOR2, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y));
}

void DataBuffer::add_vector3(double x, double y, double z, CompressionLevel p_compression_level) {
	DEB_DISABLE

	add_real(x, p_compression_level);
	add_real(y, p_compression_level);
	add_real(z, p_compression_level);

	DEB_ENABLE

	DEB_WRITE(DATA_TYPE_VECTOR3, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y) + " Z: " + std::to_string(z));
}

void DataBuffer::add_vector3(float x, float y, float z, CompressionLevel p_compression_level) {
	DEB_DISABLE

	add_real(x, p_compression_level);
	add_real(y, p_compression_level);
	add_real(z, p_compression_level);

	DEB_ENABLE

	DEB_WRITE(DATA_TYPE_VECTOR3, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y) + " Z: " + std::to_string(z));
}

void DataBuffer::read_vector3(double &x, double &y, double &z, CompressionLevel p_compression_level) {
	DEB_DISABLE

	read_real(x, p_compression_level);
	read_real(y, p_compression_level);
	read_real(z, p_compression_level);

	DEB_ENABLE

	DEB_READ(DATA_TYPE_VECTOR3, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y) + " Z: " + std::to_string(z));
}

void DataBuffer::read_vector3(float &x, float &y, float &z, CompressionLevel p_compression_level) {
	DEB_DISABLE

	read_real(x, p_compression_level);
	read_real(y, p_compression_level);
	read_real(z, p_compression_level);

	DEB_ENABLE

	DEB_READ(DATA_TYPE_VECTOR3, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y) + " Z: " + std::to_string(z));
}

template void DataBuffer::add_normalized_vector3<double>(double x, double y, double z, CompressionLevel p_compression_level);
template void DataBuffer::add_normalized_vector3<float>(float x, float y, float z, CompressionLevel p_compression_level);

template <typename T>
void DataBuffer::add_normalized_vector3(T x, T y, T z, CompressionLevel p_compression_level) {
	if (!MathFunc::is_zero_approx(x) || !MathFunc::is_zero_approx(y) || !MathFunc::is_zero_approx(z)) {
		MathFunc::vec3_normalize(x, y, z);
	}

	DEB_DISABLE

	add_unit_real(float(x), p_compression_level);
	add_unit_real(float(y), p_compression_level);
	add_unit_real(float(z), p_compression_level);

	DEB_ENABLE

	DEB_WRITE(DATA_TYPE_NORMALIZED_VECTOR3, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y) + " Z: " + std::to_string(z));
}

template void DataBuffer::read_normalized_vector3<double>(double &x, double &y, double &z, CompressionLevel p_compression_level);
template void DataBuffer::read_normalized_vector3<float>(float &x, float &y, float &z, CompressionLevel p_compression_level);

template <typename T>
void DataBuffer::read_normalized_vector3(T &x, T &y, T &z, CompressionLevel p_compression_level) {
	DEB_DISABLE

	x = read_unit_real(p_compression_level);
	y = read_unit_real(p_compression_level);
	z = read_unit_real(p_compression_level);

	DEB_ENABLE

	DEB_READ(DATA_TYPE_NORMALIZED_VECTOR3, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y) + " Z: " + std::to_string(z));
}

void DataBuffer::add_data_buffer(const DataBuffer &p_db) {
	if (is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_MSG("You can't add to the DataBuffer while reading!");
	}

	const std::uint32_t other_db_bit_size = p_db.metadata_size + p_db.bit_size;
	NS_ASSERT_COND_MSG(other_db_bit_size <= NS__UINT32_MAX, "DataBuffer can't add DataBuffer bigger than `" + std::to_string(NS__UINT32_MAX) + "` bits at the moment. [If this feature is needed ask for it.]");

	const bool using_compression_lvl_2 = other_db_bit_size < NS__UINT16_MAX;
	add(using_compression_lvl_2);
	add_uint(other_db_bit_size, using_compression_lvl_2 ? COMPRESSION_LEVEL_2 : COMPRESSION_LEVEL_1);

	make_room_pad_to_next_byte();
	const std::uint8_t *ptr = p_db.buffer.get_bytes().data();
	add_bits(ptr, other_db_bit_size);
}

void DataBuffer::read_data_buffer(DataBuffer &r_db) {
	if (!is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_MSG("You can't read from the DataBuffer while writing!");
	}

	NS_ASSERT_COND(!r_db.is_reading);

	bool using_compression_lvl_2 = false;
	read(using_compression_lvl_2);
	NS_ENSURE(!is_buffer_failed());
	const int other_db_bit_size = int(read_uint(using_compression_lvl_2 ? COMPRESSION_LEVEL_2 : COMPRESSION_LEVEL_1));

	pad_to_next_byte();
	r_db.add_bits(&(buffer.get_bytes()[bit_offset / 8]), other_db_bit_size);

	bit_offset += other_db_bit_size;
}

void DataBuffer::add_bits(const uint8_t *p_data, int p_bit_count) {
	if (is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_MSG("You can't add to the DataBuffer while reading!");
	}

	const int initial_bit_count = p_bit_count;

	make_room_in_bits(p_bit_count);

	for (int i = 0; p_bit_count > 0; ++i) {
		const int this_bit_count = std::min(p_bit_count, 8);
		p_bit_count -= this_bit_count;

		if (!buffer.store_bits(bit_offset, p_data[i], this_bit_count)) {
			buffer_failed = true;
		}

		bit_offset += this_bit_count;
	}

	DEB_WRITE(DATA_TYPE_BITS, COMPRESSION_LEVEL_0, "buffer of `" + std::to_string(initial_bit_count) + "` bits.");
}

void DataBuffer::read_bits(uint8_t *r_data, int p_bit_count) {
	if (!is_reading) {
		buffer_failed = true;
		NS_ENSURE_NO_ENTRY_MSG("You can't read from the DataBuffer while writing!");
	}

	const int initial_bit_count = p_bit_count;

	for (int i = 0; p_bit_count > 0; ++i) {
		const int this_bit_count = std::min(p_bit_count, 8);
		p_bit_count -= this_bit_count;

		std::uint64_t d;
		if (!buffer.read_bits(bit_offset, this_bit_count, d)) {
			buffer_failed = true;
			return;
		}
		r_data[i] = std::uint8_t(d);

		bit_offset += this_bit_count;
	}

	DEB_READ(DATA_TYPE_BITS, COMPRESSION_LEVEL_0, "buffer of `" + std::to_string(initial_bit_count) + "` bits.");
}

void DataBuffer::zero() {
	buffer.zero();
}

void DataBuffer::skip_bool() {
	const int bits = get_bool_size();
	skip(bits);
}

void DataBuffer::skip_int(CompressionLevel p_compression) {
	const int bits = get_int_size(p_compression);
	skip(bits);
}

void DataBuffer::skip_uint(CompressionLevel p_compression) {
	const int bits = get_uint_size(p_compression);
	skip(bits);
}

void DataBuffer::skip_real(CompressionLevel p_compression) {
	const int bits = get_real_size(p_compression);
	skip(bits);
}

void DataBuffer::skip_positive_unit_real(CompressionLevel p_compression) {
	const int bits = get_positive_unit_real_size(p_compression);
	skip(bits);
}

void DataBuffer::skip_unit_real(CompressionLevel p_compression) {
	const int bits = get_unit_real_size(p_compression);
	skip(bits);
}

void DataBuffer::skip_vector2(CompressionLevel p_compression) {
	const int bits = get_vector2_size(p_compression);
	skip(bits);
}

void DataBuffer::skip_normalized_vector2(CompressionLevel p_compression) {
	const int bits = get_normalized_vector2_size(p_compression);
	skip(bits);
}

void DataBuffer::skip_vector3(CompressionLevel p_compression) {
	const int bits = get_vector3_size(p_compression);
	skip(bits);
}

void DataBuffer::skip_normalized_vector3(CompressionLevel p_compression) {
	const int bits = get_normalized_vector3_size(p_compression);
	skip(bits);
}

void DataBuffer::skip_buffer() {
	// This already seek the offset as `skip` does.
	read_buffer_size();
}

int DataBuffer::get_bool_size() const {
	return DataBuffer::get_bit_taken(DATA_TYPE_BOOL, COMPRESSION_LEVEL_0);
}

int DataBuffer::get_int_size(CompressionLevel p_compression) const {
	return DataBuffer::get_bit_taken(DATA_TYPE_INT, p_compression);
}

int DataBuffer::get_uint_size(CompressionLevel p_compression) const {
	return DataBuffer::get_bit_taken(DATA_TYPE_UINT, p_compression);
}

int DataBuffer::get_real_size(CompressionLevel p_compression) const {
	return DataBuffer::get_bit_taken(DATA_TYPE_REAL, p_compression);
}

int DataBuffer::get_positive_unit_real_size(CompressionLevel p_compression) const {
	return DataBuffer::get_bit_taken(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression);
}

int DataBuffer::get_unit_real_size(CompressionLevel p_compression) const {
	return DataBuffer::get_bit_taken(DATA_TYPE_UNIT_REAL, p_compression);
}

int DataBuffer::get_vector2_size(CompressionLevel p_compression) const {
	return DataBuffer::get_bit_taken(DATA_TYPE_VECTOR2, p_compression);
}

int DataBuffer::get_normalized_vector2_size(CompressionLevel p_compression) const {
	return DataBuffer::get_bit_taken(DATA_TYPE_NORMALIZED_VECTOR2, p_compression);
}

int DataBuffer::get_vector3_size(CompressionLevel p_compression) const {
	return DataBuffer::get_bit_taken(DATA_TYPE_VECTOR3, p_compression);
}

int DataBuffer::get_normalized_vector3_size(CompressionLevel p_compression) const {
	return DataBuffer::get_bit_taken(DATA_TYPE_NORMALIZED_VECTOR3, p_compression);
}

int DataBuffer::read_bool_size() {
	const int bits = get_bool_size();
	skip(bits);
	return bits;
}

int DataBuffer::read_int_size(CompressionLevel p_compression) {
	const int bits = get_int_size(p_compression);
	skip(bits);
	return bits;
}

int DataBuffer::read_uint_size(CompressionLevel p_compression) {
	const int bits = get_uint_size(p_compression);
	skip(bits);
	return bits;
}

int DataBuffer::read_real_size(CompressionLevel p_compression) {
	const int bits = get_real_size(p_compression);
	skip(bits);
	return bits;
}

int DataBuffer::read_positive_unit_real_size(CompressionLevel p_compression) {
	const int bits = get_positive_unit_real_size(p_compression);
	skip(bits);
	return bits;
}

int DataBuffer::read_unit_real_size(CompressionLevel p_compression) {
	const int bits = get_unit_real_size(p_compression);
	skip(bits);
	return bits;
}

int DataBuffer::read_vector2_size(CompressionLevel p_compression) {
	const int bits = get_vector2_size(p_compression);
	skip(bits);
	return bits;
}

int DataBuffer::read_normalized_vector2_size(CompressionLevel p_compression) {
	const int bits = get_normalized_vector2_size(p_compression);
	skip(bits);
	return bits;
}

int DataBuffer::read_vector3_size(CompressionLevel p_compression) {
	const int bits = get_vector3_size(p_compression);
	skip(bits);
	return bits;
}

int DataBuffer::read_normalized_vector3_size(CompressionLevel p_compression) {
	const int bits = get_normalized_vector3_size(p_compression);
	skip(bits);
	return bits;
}

int DataBuffer::read_buffer_size() {
	bool using_compression_lvl_2;
	read(using_compression_lvl_2);
	NS_ENSURE_V(!is_buffer_failed(), 0);

	const int other_db_bit_size = int(read_uint(using_compression_lvl_2 ? COMPRESSION_LEVEL_2 : COMPRESSION_LEVEL_1));

	pad_to_next_byte();
	skip(other_db_bit_size);

	return other_db_bit_size;
}

int DataBuffer::get_bit_taken(DataType p_data_type, CompressionLevel p_compression) {
	switch (p_data_type) {
		case DATA_TYPE_BOOL:
			// No matter what, 1 bit.
			return 1;
		case DATA_TYPE_INT: {
			switch (p_compression) {
				case COMPRESSION_LEVEL_0:
					return 64;
				case COMPRESSION_LEVEL_1:
					return 32;
				case COMPRESSION_LEVEL_2:
					return 16;
				case COMPRESSION_LEVEL_3:
					return 8;
				default:
					// Unreachable
					NS_ASSERT_NO_ENTRY_MSG("Compression level not supported!");
			}
		}
		break;
		case DATA_TYPE_UINT: {
			switch (p_compression) {
				case COMPRESSION_LEVEL_0:
					return 64;
				case COMPRESSION_LEVEL_1:
					return 32;
				case COMPRESSION_LEVEL_2:
					return 16;
				case COMPRESSION_LEVEL_3:
					return 8;
				default:
					// Unreachable
					NS_ASSERT_NO_ENTRY_MSG("Compression level not supported!");
			}
		}
		break;
		case DATA_TYPE_REAL: {
			switch (p_compression) {
				case COMPRESSION_LEVEL_0:
					return 64;
				case COMPRESSION_LEVEL_1:
					return 32;
				case COMPRESSION_LEVEL_2:
				case COMPRESSION_LEVEL_3:
					return 16;
				default:
					// Unreachable
					NS_ASSERT_NO_ENTRY_MSG("Compression level not supported!");
			}
		}
		break;
		case DATA_TYPE_POSITIVE_UNIT_REAL: {
			switch (p_compression) {
				case COMPRESSION_LEVEL_0:
					return 10;
				case COMPRESSION_LEVEL_1:
					return 8;
				case COMPRESSION_LEVEL_2:
					return 6;
				case COMPRESSION_LEVEL_3:
					return 4;
				default:
					// Unreachable
					NS_ASSERT_NO_ENTRY_MSG("Compression level not supported!");
			}
		}
		break;
		case DATA_TYPE_UNIT_REAL: {
			return get_bit_taken(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression) + 1;
		}
		break;
		case DATA_TYPE_VECTOR2: {
			return get_bit_taken(DATA_TYPE_REAL, p_compression) * 2;
		}
		break;
		case DATA_TYPE_NORMALIZED_VECTOR2: {
			// +1 bit to know if the vector is 0 or a direction
			switch (p_compression) {
				case CompressionLevel::COMPRESSION_LEVEL_0:
					return 11 + 1;
				case CompressionLevel::COMPRESSION_LEVEL_1:
					return 10 + 1;
				case CompressionLevel::COMPRESSION_LEVEL_2:
					return 9 + 1;
				case CompressionLevel::COMPRESSION_LEVEL_3:
					return 8 + 1;
			}
		}
		break;
		case DATA_TYPE_VECTOR3: {
			return get_bit_taken(DATA_TYPE_REAL, p_compression) * 3;
		}
		break;
		case DATA_TYPE_NORMALIZED_VECTOR3: {
			return get_bit_taken(DATA_TYPE_UNIT_REAL, p_compression) * 3;
		}
		break;
		case DATA_TYPE_BITS: {
			NS_ENSURE_NO_ENTRY_V_MSG(0, "The bits size specified by the user and is not determined according to the compression level.");
		}
		case DATA_TYPE_DATABUFFER: {
			NS_ENSURE_NO_ENTRY_V_MSG(0, "The variant size is dynamic and can't be know at compile time.");
		}
		default:
			// Unreachable
			NS_ASSERT_NO_ENTRY_MSG("Input type not supported!");
	}

	// Unreachable
	NS_ASSERT_NO_ENTRY_MSG("It was not possible to obtain the bit taken by this input data.");
	return 0; // Useless, but MS CI is too noisy.
}

template double DataBuffer::get_real_epsilon<double>(DataType p_data_type, CompressionLevel p_compression);
template float DataBuffer::get_real_epsilon<float>(DataType p_data_type, CompressionLevel p_compression);

template <typename T>
T DataBuffer::get_real_epsilon(DataType p_data_type, CompressionLevel p_compression) {
	switch (p_data_type) {
		case DATA_TYPE_VECTOR2:
		case DATA_TYPE_VECTOR3:
		case DATA_TYPE_REAL: {
			// https://en.wikipedia.org/wiki/IEEE_754#Basic_and_interchange_formats
			// To get the exact precision for the stored number, you need to find the lower power of two relative to the number and divide it by 2^mantissa_bits.
			// To get the mantissa or exponent bits for a specific compression level, you can use the get_mantissa_bits and get_exponent_bits functions.

			T mantissa_bits;
			switch (p_compression) {
				case CompressionLevel::COMPRESSION_LEVEL_0:
					mantissa_bits = T(53); // Binary64 format
					break;
				case CompressionLevel::COMPRESSION_LEVEL_1:
					mantissa_bits = T(24); // Binary32 format
					break;
				case CompressionLevel::COMPRESSION_LEVEL_2:
				case CompressionLevel::COMPRESSION_LEVEL_3:
					mantissa_bits = T(11); // Binary16 format
					break;
			}

			return std::pow(T(2.0), -(mantissa_bits - T(1.0)));
		}
		case DATA_TYPE_NORMALIZED_VECTOR3:
		case DATA_TYPE_UNIT_REAL:
		case DATA_TYPE_POSITIVE_UNIT_REAL: {
			/// COMPRESSION_LEVEL_0: 10 bits are used - Max loss ~0.005%
			/// COMPRESSION_LEVEL_1: 8 bits are used - Max loss ~0.020%
			/// COMPRESSION_LEVEL_2: 6 bits are used - Max loss ~0.793%
			/// COMPRESSION_LEVEL_3: 4 bits are used - Max loss ~3.333%
			switch (p_compression) {
				case CompressionLevel::COMPRESSION_LEVEL_0:
					return T(0.0005);
				case CompressionLevel::COMPRESSION_LEVEL_1:
					return T(0.002);
				case CompressionLevel::COMPRESSION_LEVEL_2:
					return T(0.008);
				case CompressionLevel::COMPRESSION_LEVEL_3:
					return T(0.35);
			}
		}
		case DATA_TYPE_NORMALIZED_VECTOR2: {
			switch (p_compression) {
				case CompressionLevel::COMPRESSION_LEVEL_0:
					return T(0.002);
				case CompressionLevel::COMPRESSION_LEVEL_1:
					return T(0.007);
				case CompressionLevel::COMPRESSION_LEVEL_2:
					return T(0.01);
				case CompressionLevel::COMPRESSION_LEVEL_3:
					return T(0.02);
			}
		}

		default:
			return T(0.0);
	}
}

template uint64_t DataBuffer::compress_unit_float<double>(double p_value, double p_scale_factor);
template uint64_t DataBuffer::compress_unit_float<float>(float p_value, float p_scale_factor);

template <typename T>
uint64_t DataBuffer::compress_unit_float(T p_value, T p_scale_factor) {
	return std::uint64_t(std::round(std::min(p_value * p_scale_factor, p_scale_factor)));
}

template double DataBuffer::decompress_unit_float<double>(uint64_t p_value, double p_scale_factor);
template float DataBuffer::decompress_unit_float<float>(uint64_t p_value, float p_scale_factor);

template <typename T>
T DataBuffer::decompress_unit_float(uint64_t p_value, T p_scale_factor) {
	return static_cast<T>(p_value) / p_scale_factor;
}

void DataBuffer::make_room_in_bits(int p_dim) {
	const int array_min_dim = bit_offset + p_dim;
	if (array_min_dim > buffer.size_in_bits()) {
		buffer.resize_in_bits(array_min_dim);
	}

	if (array_min_dim > metadata_size) {
		const int new_bit_size = array_min_dim - metadata_size;
		if (new_bit_size > bit_size) {
			bit_size = new_bit_size;
		}
	}
}

void DataBuffer::make_room_pad_to_next_byte() {
	const int bits_to_next_byte = ((bit_offset + 7) & ~7) - bit_offset;
	make_room_in_bits(bits_to_next_byte);
	bit_offset += bits_to_next_byte;
}

bool DataBuffer::pad_to_next_byte(int *p_bits_to_next_byte) {
	const int bits_to_next_byte = ((bit_offset + 7) & ~7) - bit_offset;
	NS_ENSURE_V(
			(bit_offset + bits_to_next_byte) <= buffer.size_in_bits(),
			false);
	bit_offset += bits_to_next_byte;
	if (p_bits_to_next_byte) {
		*p_bits_to_next_byte = bits_to_next_byte;
	}
	return true;
}

NS_NAMESPACE_END