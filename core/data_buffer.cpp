#include "data_buffer.h"

#include "ensure.h"
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

#ifdef DEBUG_ENABLED
#define DEBUG_DATA_BUFFER

#ifdef DISABLE_DEBUG_DATA_BUFFER
#undef DEBUG_DATA_BUFFER
#endif
#endif

#ifdef DEBUG_DATA_BUFFER
#define DEB_WRITE(dt, compression, input)                                                             \
	if (debug_enabled) {                                                                              \
		SceneSynchronizerDebugger::singleton()->databuffer_write(dt, compression, bit_offset, input); \
	}

#define DEB_READ(dt, compression, input)                                                             \
	if (debug_enabled) {                                                                             \
		SceneSynchronizerDebugger::singleton()->databuffer_read(dt, compression, bit_offset, input); \
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
		buffer(p_other.buffer) {}

DataBuffer::DataBuffer(const BitArray &p_buffer) :
		bit_size(p_buffer.size_in_bits()),
		is_reading(true),
		buffer(p_buffer) {}

// TODO : Implemet this.
//DataBuffer &DataBuffer::operator=(DataBuffer &&p_other) {
//	metadata_size = std::move(p_other.metadata_size);
//	bit_offset = std::move(p_other.bit_offset);
//	bit_size = std::move(p_other.bit_size);
//	is_reading = std::move(p_other.is_reading);
//	buffer = std::move(p_other.buffer);
//}

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
	ASSERT_COND_MSG(p_metadata_size >= 0, "Metadata size can't be negative");
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
	ENSURE(p_bits < metadata_size + bit_size + 1);
	bit_offset = p_bits;
}

void DataBuffer::shrink_to(int p_metadata_bit_size, int p_bit_size) {
	ASSERT_COND_MSG(p_metadata_bit_size >= 0, "Metadata size can't be negative");
	ENSURE_MSG(p_bit_size >= 0, "Bit size can't be negative");
	ENSURE_MSG(buffer.size_in_bits() >= (p_metadata_bit_size + p_bit_size), "The buffer is smaller than the new given size.");
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
	ENSURE((metadata_size + bit_size) >= (bit_offset + p_bits));
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
	r_out = read_uint(COMPRESSION_LEVEL_3);
}

void DataBuffer::add(std::uint16_t p_input) {
	add_uint(p_input, COMPRESSION_LEVEL_2);
}

void DataBuffer::read(std::uint16_t &r_out) {
	r_out = read_uint(COMPRESSION_LEVEL_2);
}

void DataBuffer::add(std::uint32_t p_input) {
	add_uint(p_input, COMPRESSION_LEVEL_1);
}

void DataBuffer::read(std::uint32_t &r_out) {
	r_out = read_uint(COMPRESSION_LEVEL_1);
}

void DataBuffer::add(int p_input) {
	add_int(p_input, COMPRESSION_LEVEL_1);
}

void DataBuffer::read(int &r_out) {
	r_out = read_int(COMPRESSION_LEVEL_1);
}

void DataBuffer::add(std::uint64_t p_input) {
	add_uint(p_input, COMPRESSION_LEVEL_0);
}

void DataBuffer::read(std::uint64_t &r_out) {
	r_out = read_uint(COMPRESSION_LEVEL_0);
}

void DataBuffer::add(const std::string &p_string) {
	ASSERT_COND(std::uint64_t(p_string.size()) <= std::uint64_t(NS__INT16_MAX));
	add_uint(p_string.size(), COMPRESSION_LEVEL_2);
	if (p_string.size() > 0) {
		add_bits(reinterpret_cast<const uint8_t *>(p_string.c_str()), p_string.size() * 8);
	}
}

void DataBuffer::read(std::string &r_out) {
	const uint16_t size = read_uint(COMPRESSION_LEVEL_2);
	if (size <= 0) {
		return;
	}

	std::vector<char> chars;
	chars.resize(size);
	read_bits(reinterpret_cast<uint8_t *>(chars.data()), size * 8);
	r_out = std::string(chars.data(), size);
}

void DataBuffer::add(const std::u16string &p_string) {
	ASSERT_COND(std::uint64_t(p_string.size()) <= std::uint64_t(NS__UINT16_MAX));
	add_uint(p_string.size(), COMPRESSION_LEVEL_2);
	if (p_string.size() > 0) {
		add_bits(reinterpret_cast<const uint8_t *>(p_string.c_str()), p_string.size() * 8 * (sizeof(char16_t)));
	}
}

void DataBuffer::read(std::u16string &r_out) {
	const uint16_t size = read_uint(COMPRESSION_LEVEL_2);
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
	ENSURE_V(!is_reading, p_input);

	const int bits = get_bit_taken(DATA_TYPE_BOOL, COMPRESSION_LEVEL_0);

	make_room_in_bits(bits);
	if (!buffer.store_bits(bit_offset, p_input, bits)) {
		buffer_failed = true;
	}
	bit_offset += bits;

#ifdef DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	ASSERT_COND((metadata_size + bit_size) <= buffer.size_in_bits() && bit_offset <= buffer.size_in_bits());
#endif

	DEB_WRITE(DATA_TYPE_BOOL, COMPRESSION_LEVEL_0, p_input ? "TRUE" : "FALSE");

	return p_input;
}

bool DataBuffer::read_bool() {
	ENSURE_V(is_reading, false);

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
	ENSURE_V(!is_reading, p_input);

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

#ifdef DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	ASSERT_COND((metadata_size + bit_size) <= buffer.size_in_bits() && bit_offset <= buffer.size_in_bits());
#endif

	DEB_WRITE(DATA_TYPE_INT, p_compression_level, std::to_string(value));

	return value;
}

int64_t DataBuffer::read_int(CompressionLevel p_compression_level) {
	ENSURE_V(is_reading, 0);

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
	ENSURE_V(!is_reading, p_input);

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

#ifdef DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	ASSERT_COND((metadata_size + bit_size) <= buffer.size_in_bits() && bit_offset <= buffer.size_in_bits());
#endif

	DEB_WRITE(DATA_TYPE_UINT, p_compression_level, std::to_string(value));

	return value;
}

std::uint64_t DataBuffer::read_uint(CompressionLevel p_compression_level) {
	ENSURE_V(is_reading, 0);

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

double DataBuffer::add_real(double p_input, CompressionLevel p_compression_level) {
	ENSURE_V(!is_reading, p_input);

	// Clamp the input value according to the compression level
	// Minifloat (compression level 0) have a special bias
	const int exponent_bits = get_exponent_bits(p_compression_level);
	const int mantissa_bits = get_mantissa_bits(p_compression_level);
	const double bias = p_compression_level == COMPRESSION_LEVEL_3 ? std::pow(2.0, exponent_bits) - 3 : std::pow(2.0, exponent_bits - 1) - 1;
	const double max_value = (2.0 - std::pow(2.0, -(mantissa_bits - 1))) * std::pow(2.0, bias);
	const double clamped_input = MathFunc::clamp(p_input, -max_value, max_value);

	// Split number according to IEEE 754 binary format.
	// Mantissa floating point value represented in range (-1;-0.5], [0.5; 1).
	int exponent;
	double mantissa = frexp(clamped_input, &exponent);

	// Extract sign.
	const bool sign = mantissa < 0;
	mantissa = std::abs(mantissa);

	// Round mantissa into the specified number of bits (like float -> double conversion).
	double mantissa_scale = std::pow(2.0, mantissa_bits);
	if (exponent <= 0) {
		// Subnormal value, apply exponent to mantissa and reduce power of scale by one.
		mantissa *= std::pow(2.0, exponent);
		exponent = 0;
		mantissa_scale /= 2.0;
	}
	mantissa = std::round(mantissa * mantissa_scale) / mantissa_scale; // Round to specified number of bits.
	if (mantissa < 0.5 && mantissa != 0) {
		// Check underflow, extract exponent from mantissa.
		exponent += ilogb(mantissa) + 1;
		mantissa /= std::pow(2.0, exponent);
	} else if (mantissa == 1) {
		// Check overflow, increment the exponent.
		++exponent;
		mantissa = 0.5;
	}
	// Convert the mantissa to an integer that represents the offset index (IEE 754 floating point representation) to send over network safely.
	const uint64_t integer_mantissa = exponent <= 0 ? mantissa * mantissa_scale * std::pow(2.0, exponent) : (mantissa - 0.5) * mantissa_scale;

	make_room_in_bits(mantissa_bits + exponent_bits);
	if (!buffer.store_bits(bit_offset, sign, 1)) {
		buffer_failed = true;
	}
	bit_offset += 1;
	if (!buffer.store_bits(bit_offset, integer_mantissa, mantissa_bits - 1)) {
		buffer_failed = true;
	}
	bit_offset += mantissa_bits - 1;
	// Send unsigned value (just shift it by bias) to avoid sign issues.
	if (!buffer.store_bits(bit_offset, exponent + bias, exponent_bits)) {
		buffer_failed = true;
	}
	bit_offset += exponent_bits;

	const double value = ldexp(sign ? -mantissa : mantissa, exponent);
	DEB_WRITE(DATA_TYPE_REAL, p_compression_level, std::to_string(value));
	return value;
}

double DataBuffer::read_real(CompressionLevel p_compression_level) {
	ENSURE_V(is_reading, 0.0);

	std::uint64_t sign;
	if (!buffer.read_bits(bit_offset, 1, sign)) {
		buffer_failed = true;
		return 0.0;
	}
	bit_offset += 1;

	const int mantissa_bits = get_mantissa_bits(p_compression_level);
	std::uint64_t integer_mantissa;
	if (!buffer.read_bits(bit_offset, mantissa_bits - 1, integer_mantissa)) {
		buffer_failed = true;
		return 0.0;
	}
	bit_offset += mantissa_bits - 1;

	const int exponent_bits = get_exponent_bits(p_compression_level);
	const double bias = p_compression_level == COMPRESSION_LEVEL_3 ? std::pow(2.0, exponent_bits) - 3 : std::pow(2.0, exponent_bits - 1) - 1;
	std::uint64_t encoded_exponent;
	if (!buffer.read_bits(bit_offset, exponent_bits, encoded_exponent)) {
		buffer_failed = true;
		return 0.0;
	}
	const int exponent = static_cast<int>(encoded_exponent) - static_cast<int>(bias);
	bit_offset += exponent_bits;

	// Convert integer mantissa into the floating point representation
	// When the index of the mantissa and exponent are 0, then this is a special case and the mantissa is 0.
	const double mantissa_scale = std::pow(2.0, exponent <= 0 ? mantissa_bits - 1 : mantissa_bits);
	const double mantissa = exponent <= 0 ? integer_mantissa / mantissa_scale / std::pow(2.0, exponent) : integer_mantissa / mantissa_scale + 0.5;

	const double value = ldexp(sign != 0 ? -mantissa : mantissa, exponent);

	DEB_READ(DATA_TYPE_REAL, p_compression_level, std::to_string(value));

	return value;
}

float DataBuffer::add_positive_unit_real(float p_input, CompressionLevel p_compression_level) {
	ENSURE_V(!is_reading, p_input);

#ifdef DEBUG_ENABLED
	ENSURE_V_MSG(p_input >= 0 && p_input <= 1, p_input, "Value must be between zero and one.");
#endif

	const int bits = get_bit_taken(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression_level);

	const double max_value = static_cast<double>(~(UINT64_MAX << bits));

	const uint64_t compressed_val = compress_unit_float(p_input, max_value);

	make_room_in_bits(bits);
	if (!buffer.store_bits(bit_offset, compressed_val, bits)) {
		buffer_failed = true;
	}
	bit_offset += bits;

#ifdef DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	ASSERT_COND((metadata_size + bit_size) <= buffer.size_in_bits() && bit_offset <= buffer.size_in_bits());
#endif

	const float value = decompress_unit_float(compressed_val, max_value);
	DEB_WRITE(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression_level, std::to_string(value));
	return value;
}

float DataBuffer::read_positive_unit_real(CompressionLevel p_compression_level) {
	ENSURE_V(is_reading, 0.0);

	const int bits = get_bit_taken(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression_level);

	const double max_value = static_cast<double>(~(UINT64_MAX << bits));

	std::uint64_t compressed_val;
	if (!buffer.read_bits(bit_offset, bits, compressed_val)) {
		buffer_failed = true;
		return 0.0;
	}
	bit_offset += bits;

	const float value = decompress_unit_float(compressed_val, max_value);

	DEB_READ(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression_level, std::to_string(value));

	return value;
}

float DataBuffer::add_unit_real(float p_input, CompressionLevel p_compression_level) {
	ENSURE_V(!is_reading, p_input);

	const float added_real = add_positive_unit_real(std::abs(p_input), p_compression_level);

	const int bits_for_sign = 1;
	const uint32_t is_negative = p_input < 0.0;
	make_room_in_bits(bits_for_sign);
	if (!buffer.store_bits(bit_offset, is_negative, bits_for_sign)) {
		buffer_failed = true;
	}
	bit_offset += bits_for_sign;

#ifdef DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	ASSERT_COND((metadata_size + bit_size) <= buffer.size_in_bits() && bit_offset <= buffer.size_in_bits());
#endif

	const float value = is_negative ? -added_real : added_real;
	DEB_WRITE(DATA_TYPE_UNIT_REAL, p_compression_level, std::to_string(value));

	return value;
}

float DataBuffer::read_unit_real(CompressionLevel p_compression_level) {
	ENSURE_V(is_reading, 0.0);

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
	ENSURE(!is_reading);

	DEB_DISABLE

	add_real(x, p_compression_level);
	add_real(y, p_compression_level);

	DEB_ENABLE

	DEB_WRITE(DATA_TYPE_VECTOR2, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y));
}

void DataBuffer::read_vector2(double &x, double &y, CompressionLevel p_compression_level) {
	ENSURE(is_reading);

	DEB_DISABLE

	x = read_real(p_compression_level);
	y = read_real(p_compression_level);

	DEB_ENABLE

	DEB_WRITE(DATA_TYPE_VECTOR2, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y));
}

void DataBuffer::add_normalized_vector2(double x, double y, CompressionLevel p_compression_level) {
	ENSURE(!is_reading);

	const std::uint64_t is_not_zero = MathFunc::is_zero_approx(x) && MathFunc::is_zero_approx(y) ? 1 : 0;

#ifdef DEBUG_ENABLED
	if (!is_not_zero) {
		ENSURE_MSG(MathFunc::vec2_is_normalized(x, y), "[FATAL] The encoding failed because this function expects a normalized vector.");
	}
#endif

	const int bits = get_bit_taken(DATA_TYPE_NORMALIZED_VECTOR2, p_compression_level);
	const int bits_for_the_angle = bits - 1;
	const int bits_for_zero = 1;

	const double angle = MathFunc::vec2_angle(x, y);

	const double max_value = static_cast<double>(~(UINT64_MAX << bits_for_the_angle));

	const uint64_t compressed_angle = compress_unit_float((angle + M_PI) / M_TAU, max_value);

	make_room_in_bits(bits);
	if (!buffer.store_bits(bit_offset, is_not_zero, bits_for_zero)) {
		buffer_failed = true;
	}
	if (!buffer.store_bits(bit_offset + 1, compressed_angle, bits_for_the_angle)) {
		buffer_failed = true;
	}
	bit_offset += bits;

	// Can't never happen because the buffer size is correctly handled.
	ASSERT_COND((metadata_size + bit_size) <= buffer.size_in_bits() && bit_offset <= buffer.size_in_bits());

	DEB_WRITE(DATA_TYPE_NORMALIZED_VECTOR2, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y));
}

void DataBuffer::read_normalized_vector2(double &x, double &y, CompressionLevel p_compression_level) {
	ENSURE(is_reading);

	const int bits = get_bit_taken(DATA_TYPE_NORMALIZED_VECTOR2, p_compression_level);
	const int bits_for_the_angle = bits - 1;
	const int bits_for_zero = 1;

	const double max_value = static_cast<double>(~(UINT64_MAX << bits_for_the_angle));

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

	const double decompressed_angle = (decompress_unit_float(compressed_angle, max_value) * M_TAU) - M_PI;
	x = std::cos(decompressed_angle);
	y = std::sin(decompressed_angle);

	DEB_READ(DATA_TYPE_NORMALIZED_VECTOR2, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y));
}

void DataBuffer::add_vector3(double x, double y, double z, CompressionLevel p_compression_level) {
	ENSURE(!is_reading);

	DEB_DISABLE

	add_real(x, p_compression_level);
	add_real(y, p_compression_level);
	add_real(z, p_compression_level);

	DEB_ENABLE

	DEB_WRITE(DATA_TYPE_VECTOR3, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y) + " Z: " + std::to_string(z));
}

void DataBuffer::read_vector3(double &x, double &y, double &z, CompressionLevel p_compression_level) {
	ENSURE(is_reading);

	DEB_DISABLE

	x = read_real(p_compression_level);
	y = read_real(p_compression_level);
	z = read_real(p_compression_level);

	DEB_ENABLE

	DEB_READ(DATA_TYPE_VECTOR3, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y) + " Z: " + std::to_string(z));
}

void DataBuffer::add_normalized_vector3(double x, double y, double z, CompressionLevel p_compression_level) {
	ENSURE(!is_reading);

#ifdef DEBUG_ENABLED
	if (MathFunc::is_zero_approx(x) && MathFunc::is_zero_approx(y) && MathFunc::is_zero_approx(z)) {
		ENSURE_MSG(MathFunc::vec3_is_normalized(x, y, z), "[FATAL] This function expects a normalized vector.");
	}
#endif

	DEB_DISABLE

	add_unit_real(x, p_compression_level);
	add_unit_real(y, p_compression_level);
	add_unit_real(z, p_compression_level);

	DEB_ENABLE

	DEB_WRITE(DATA_TYPE_NORMALIZED_VECTOR3, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y) + " Z: " + std::to_string(z));
}

void DataBuffer::read_normalized_vector3(double &x, double &y, double &z, CompressionLevel p_compression_level) {
	ENSURE(is_reading);

	DEB_DISABLE

	x = read_unit_real(p_compression_level);
	y = read_unit_real(p_compression_level);
	z = read_unit_real(p_compression_level);

	DEB_ENABLE

	DEB_READ(DATA_TYPE_NORMALIZED_VECTOR3, p_compression_level, "X: " + std::to_string(x) + " Y: " + std::to_string(y) + " Z: " + std::to_string(z));
}

void DataBuffer::add_data_buffer(const DataBuffer &p_db) {
	const std::uint32_t other_db_bit_size = p_db.metadata_size + p_db.bit_size;
	ASSERT_COND_MSG(other_db_bit_size <= NS__UINT32_MAX, "DataBuffer can't add DataBuffer bigger than `" + std::to_string(NS__UINT32_MAX) + "` bits at the moment. [If this feature is needed ask for it.]");

	const bool using_compression_lvl_2 = other_db_bit_size < NS__UINT16_MAX;
	add(using_compression_lvl_2);
	add_uint(other_db_bit_size, using_compression_lvl_2 ? COMPRESSION_LEVEL_2 : COMPRESSION_LEVEL_1);

	make_room_pad_to_next_byte();
	const std::uint8_t *ptr = p_db.buffer.get_bytes().data();
	add_bits(ptr, other_db_bit_size);
}

void DataBuffer::read_data_buffer(DataBuffer &r_db) {
	ENSURE(is_reading);
	ASSERT_COND(!r_db.is_reading);

	bool using_compression_lvl_2 = false;
	read(using_compression_lvl_2);
	ENSURE(!is_buffer_failed());
	const int other_db_bit_size = read_uint(using_compression_lvl_2 ? COMPRESSION_LEVEL_2 : COMPRESSION_LEVEL_1);

	pad_to_next_byte();
	r_db.add_bits(&(buffer.get_bytes()[bit_offset / 8]), other_db_bit_size);

	bit_offset += other_db_bit_size;
}

void DataBuffer::add_bits(const uint8_t *p_data, int p_bit_count) {
	ENSURE(!is_reading);
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
	ENSURE(is_reading);

	const int initial_bit_count = p_bit_count;

	for (int i = 0; p_bit_count > 0; ++i) {
		const int this_bit_count = std::min(p_bit_count, 8);
		p_bit_count -= this_bit_count;

		std::uint64_t d;
		if (!buffer.read_bits(bit_offset, this_bit_count, d)) {
			buffer_failed = true;
			return;
		}
		r_data[i] = d;

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
	ENSURE_V(!is_buffer_failed(), 0);

	const int other_db_bit_size = read_uint(using_compression_lvl_2 ? COMPRESSION_LEVEL_2 : COMPRESSION_LEVEL_1);

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
					ASSERT_NO_ENTRY_MSG("Compression level not supported!");
			}
		} break;
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
					ASSERT_NO_ENTRY_MSG("Compression level not supported!");
			}
		} break;
		case DATA_TYPE_REAL: {
			return get_mantissa_bits(p_compression) +
					get_exponent_bits(p_compression);
		} break;
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
					ASSERT_NO_ENTRY_MSG("Compression level not supported!");
			}
		} break;
		case DATA_TYPE_UNIT_REAL: {
			return get_bit_taken(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression) + 1;
		} break;
		case DATA_TYPE_VECTOR2: {
			return get_bit_taken(DATA_TYPE_REAL, p_compression) * 2;
		} break;
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
		} break;
		case DATA_TYPE_VECTOR3: {
			return get_bit_taken(DATA_TYPE_REAL, p_compression) * 3;
		} break;
		case DATA_TYPE_NORMALIZED_VECTOR3: {
			return get_bit_taken(DATA_TYPE_UNIT_REAL, p_compression) * 3;
		} break;
		case DATA_TYPE_BITS: {
			ENSURE_V_MSG(false, 0, "The bits size specified by the user and is not determined according to the compression level.");
		}
		case DATA_TYPE_DATABUFFER: {
			ENSURE_V_MSG(false, 0, "The variant size is dynamic and can't be know at compile time.");
		}
		default:
			// Unreachable
			ASSERT_NO_ENTRY_MSG("Input type not supported!");
	}

	// Unreachable
	ASSERT_NO_ENTRY_MSG("It was not possible to obtain the bit taken by this input data.");
	return 0; // Useless, but MS CI is too noisy.
}

int DataBuffer::get_mantissa_bits(CompressionLevel p_compression) {
	// https://en.wikipedia.org/wiki/IEEE_754#Basic_and_interchange_formats
	switch (p_compression) {
		case CompressionLevel::COMPRESSION_LEVEL_0:
			return 53; // Binary64 format
		case CompressionLevel::COMPRESSION_LEVEL_1:
			return 24; // Binary32 format
		case CompressionLevel::COMPRESSION_LEVEL_2:
			return 11; // Binary16 format
		case CompressionLevel::COMPRESSION_LEVEL_3:
			return 4; // https://en.wikipedia.org/wiki/Minifloat
	}

	// Unreachable
	ASSERT_NO_ENTRY_MSG("Unknown compression level.");
	return 0; // Useless, but MS CI is too noisy.
}

int DataBuffer::get_exponent_bits(CompressionLevel p_compression) {
	// https://en.wikipedia.org/wiki/IEEE_754#Basic_and_interchange_formats
	switch (p_compression) {
		case CompressionLevel::COMPRESSION_LEVEL_0:
			return 11; // Binary64 format
		case CompressionLevel::COMPRESSION_LEVEL_1:
			return 8; // Binary32 format
		case CompressionLevel::COMPRESSION_LEVEL_2:
			return 5; // Binary16 format
		case CompressionLevel::COMPRESSION_LEVEL_3:
			return 4; // https://en.wikipedia.org/wiki/Minifloat
	}

	// Unreachable
	ASSERT_NO_ENTRY_MSG("Unknown compression level.");
	return 0; // Useless, but MS CI is too noisy.
}

uint64_t DataBuffer::compress_unit_float(double p_value, double p_scale_factor) {
	return std::round(std::min(p_value * p_scale_factor, p_scale_factor));
}

double DataBuffer::decompress_unit_float(uint64_t p_value, double p_scale_factor) {
	return static_cast<double>(p_value) / p_scale_factor;
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
	ENSURE_V(
			(bit_offset + bits_to_next_byte) <= buffer.size_in_bits(),
			false);
	bit_offset += bits_to_next_byte;
	if (p_bits_to_next_byte) {
		*p_bits_to_next_byte = bits_to_next_byte;
	}
	return true;
}

NS_NAMESPACE_END
