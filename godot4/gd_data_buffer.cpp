#include "gd_data_buffer.h"

#include "core/error/error_macros.h"
#include "core/io/marshalls.h"
#include "core/math/vector2.h"
#include "core/variant/variant.h"

#include "../core/scene_synchronizer_debugger.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

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

void GdDataBuffer::_bind_methods() {
	BIND_ENUM_CONSTANT(DATA_TYPE_BOOL);
	BIND_ENUM_CONSTANT(DATA_TYPE_INT);
	BIND_ENUM_CONSTANT(DATA_TYPE_UINT);
	BIND_ENUM_CONSTANT(DATA_TYPE_REAL);
	BIND_ENUM_CONSTANT(DATA_TYPE_POSITIVE_UNIT_REAL);
	BIND_ENUM_CONSTANT(DATA_TYPE_UNIT_REAL);
	BIND_ENUM_CONSTANT(DATA_TYPE_VECTOR2);
	BIND_ENUM_CONSTANT(DATA_TYPE_NORMALIZED_VECTOR2);
	BIND_ENUM_CONSTANT(DATA_TYPE_VECTOR3);
	BIND_ENUM_CONSTANT(DATA_TYPE_NORMALIZED_VECTOR3);
	BIND_ENUM_CONSTANT(DATA_TYPE_BITS);
	BIND_ENUM_CONSTANT(DATA_TYPE_VARIANT);

	BIND_ENUM_CONSTANT(COMPRESSION_LEVEL_0);
	BIND_ENUM_CONSTANT(COMPRESSION_LEVEL_1);
	BIND_ENUM_CONSTANT(COMPRESSION_LEVEL_2);
	BIND_ENUM_CONSTANT(COMPRESSION_LEVEL_3);

	ClassDB::bind_method(D_METHOD("size"), &GdDataBuffer::size);

	ClassDB::bind_method(D_METHOD("add_bool", "value"), &GdDataBuffer::add_bool);
	ClassDB::bind_method(D_METHOD("add_int", "value", "compression_level"), &GdDataBuffer::add_int, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("add_uint", "value", "compression_level"), &GdDataBuffer::add_uint, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("add_real", "value", "compression_level"), &GdDataBuffer::add_real, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("add_positive_unit_real", "value", "compression_level"), &GdDataBuffer::add_positive_unit_real, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("add_unit_real", "value", "compression_level"), &GdDataBuffer::add_unit_real, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("add_vector2", "value", "compression_level"), &GdDataBuffer::add_vector2, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("add_normalized_vector2", "value", "compression_level"), &GdDataBuffer::add_normalized_vector2, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("add_vector3", "value", "compression_level"), &GdDataBuffer::add_vector3, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("add_normalized_vector3", "value", "compression_level"), &GdDataBuffer::add_normalized_vector3, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("add_variant", "value"), &GdDataBuffer::add_variant);
	ClassDB::bind_method(D_METHOD("add_optional_variant", "value", "default_value"), &GdDataBuffer::add_optional_variant);

	ClassDB::bind_method(D_METHOD("read_bool"), &GdDataBuffer::read_bool);
	ClassDB::bind_method(D_METHOD("read_int", "compression_level"), &GdDataBuffer::read_int, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_uint", "compression_level"), &GdDataBuffer::read_uint, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_real", "compression_level"), &GdDataBuffer::read_real, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_positive_unit_real", "compression_level"), &GdDataBuffer::read_positive_unit_real, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_unit_real", "compression_level"), &GdDataBuffer::read_unit_real, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_vector2", "compression_level"), &GdDataBuffer::read_vector2, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_normalized_vector2", "compression_level"), &GdDataBuffer::read_normalized_vector2, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_vector3", "compression_level"), &GdDataBuffer::read_vector3, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_normalized_vector3", "compression_level"), &GdDataBuffer::read_normalized_vector3, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_variant"), &GdDataBuffer::read_variant);
	ClassDB::bind_method(D_METHOD("read_optional_variant", "default"), &GdDataBuffer::read_optional_variant);

	ClassDB::bind_method(D_METHOD("skip_bool"), &GdDataBuffer::skip_bool);
	ClassDB::bind_method(D_METHOD("skip_int", "compression_level"), &GdDataBuffer::skip_int, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("skip_uint", "compression_level"), &GdDataBuffer::skip_uint, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("skip_real", "compression_level"), &GdDataBuffer::skip_real, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("skip_unit_real", "compression_level"), &GdDataBuffer::skip_unit_real, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("skip_vector2", "compression_level"), &GdDataBuffer::skip_vector2, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("skip_normalized_vector2", "compression_level"), &GdDataBuffer::skip_normalized_vector2, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("skip_vector3", "compression_level"), &GdDataBuffer::skip_vector3, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("skip_normalized_vector3", "compression_level"), &GdDataBuffer::skip_normalized_vector3, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("skip_variant"), &GdDataBuffer::skip_variant);
	ClassDB::bind_method(D_METHOD("skip_optional_variant", "default_value"), &GdDataBuffer::skip_optional_variant);

	ClassDB::bind_method(D_METHOD("get_bool_size"), &GdDataBuffer::get_bool_size);
	ClassDB::bind_method(D_METHOD("get_int_size", "compression_level"), &GdDataBuffer::get_int_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("get_uint_size", "compression_level"), &GdDataBuffer::get_uint_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("get_real_size", "compression_level"), &GdDataBuffer::get_real_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("get_unit_real_size", "compression_level"), &GdDataBuffer::get_unit_real_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("get_vector2_size", "compression_level"), &GdDataBuffer::get_vector2_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("get_normalized_vector2_size", "compression_level"), &GdDataBuffer::get_normalized_vector2_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("get_vector3_size", "compression_level"), &GdDataBuffer::get_vector3_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("get_normalized_vector3_size", "compression_level"), &GdDataBuffer::get_normalized_vector3_size, DEFVAL(COMPRESSION_LEVEL_1));

	ClassDB::bind_method(D_METHOD("read_bool_size"), &GdDataBuffer::read_bool_size);
	ClassDB::bind_method(D_METHOD("read_int_size", "compression_level"), &GdDataBuffer::read_int_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_uint_size", "compression_level"), &GdDataBuffer::read_uint_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_real_size", "compression_level"), &GdDataBuffer::read_real_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_unit_real_size", "compression_level"), &GdDataBuffer::read_unit_real_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_vector2_size", "compression_level"), &GdDataBuffer::read_vector2_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_normalized_vector2_size", "compression_level"), &GdDataBuffer::read_normalized_vector2_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_vector3_size", "compression_level"), &GdDataBuffer::read_vector3_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_normalized_vector3_size", "compression_level"), &GdDataBuffer::read_normalized_vector3_size, DEFVAL(COMPRESSION_LEVEL_1));
	ClassDB::bind_method(D_METHOD("read_variant_size"), &GdDataBuffer::read_variant_size);
	ClassDB::bind_method(D_METHOD("read_optional_variant_size", "default_value"), &GdDataBuffer::read_optional_variant_size);

	ClassDB::bind_method(D_METHOD("begin_read"), &GdDataBuffer::begin_read);
	ClassDB::bind_method(D_METHOD("begin_write", "meta_size"), &GdDataBuffer::begin_write);
	ClassDB::bind_method(D_METHOD("dry"), &GdDataBuffer::dry);
}

GdDataBuffer::GdDataBuffer(const GdDataBuffer &p_other) :
		Object(),
		metadata_size(p_other.metadata_size),
		bit_offset(p_other.bit_offset),
		bit_size(p_other.bit_size),
		is_reading(p_other.is_reading),
		buffer(p_other.buffer) {}

GdDataBuffer::GdDataBuffer(const BitArray &p_buffer) :
		Object(),
		bit_size(p_buffer.size_in_bits()),
		is_reading(true),
		buffer(p_buffer) {}

// TODO : Implemet this.
//GdDataBuffer &GdDataBuffer::operator=(GdDataBuffer &&p_other) {
//	metadata_size = std::move(p_other.metadata_size);
//	bit_offset = std::move(p_other.bit_offset);
//	bit_size = std::move(p_other.bit_size);
//	is_reading = std::move(p_other.is_reading);
//	buffer = std::move(p_other.buffer);
//}

void GdDataBuffer::copy(const GdDataBuffer &p_other) {
	metadata_size = p_other.metadata_size;
	bit_offset = p_other.bit_offset;
	bit_size = p_other.bit_size;
	is_reading = p_other.is_reading;
	buffer = p_other.buffer;
}

void GdDataBuffer::copy(const BitArray &p_buffer) {
	metadata_size = 0;
	bit_offset = 0;
	bit_size = p_buffer.size_in_bits();
	is_reading = true;
	buffer = p_buffer;
}

void GdDataBuffer::begin_write(int p_metadata_size) {
	CRASH_COND_MSG(p_metadata_size < 0, "Metadata size can't be negative");
	metadata_size = p_metadata_size;
	bit_size = 0;
	bit_offset = 0;
	is_reading = false;
	buffer_failed = false;
}

void GdDataBuffer::dry() {
	buffer.resize_in_bits(metadata_size + bit_size);
}

void GdDataBuffer::seek(int p_bits) {
	ERR_FAIL_INDEX(p_bits, metadata_size + bit_size + 1);
	bit_offset = p_bits;
}

void GdDataBuffer::shrink_to(int p_metadata_bit_size, int p_bit_size) {
	CRASH_COND_MSG(p_metadata_bit_size < 0, "Metadata size can't be negative");
	ERR_FAIL_COND_MSG(p_bit_size < 0, "Bit size can't be negative");
	ERR_FAIL_COND_MSG(buffer.size_in_bits() < (p_metadata_bit_size + p_bit_size), "The buffer is smaller than the new given size.");
	metadata_size = p_metadata_bit_size;
	bit_size = p_bit_size;
}

int GdDataBuffer::get_metadata_size() const {
	return metadata_size;
}

int GdDataBuffer::size() const {
	return bit_size;
}

int GdDataBuffer::total_size() const {
	return bit_size + metadata_size;
}

int GdDataBuffer::get_bit_offset() const {
	return bit_offset;
}

void GdDataBuffer::skip(int p_bits) {
	ERR_FAIL_COND((metadata_size + bit_size) < (bit_offset + p_bits));
	bit_offset += p_bits;
}

void GdDataBuffer::begin_read() {
	bit_offset = 0;
	is_reading = true;
	buffer_failed = false;
}

void GdDataBuffer::add(bool p_input) {
	add_bool(p_input);
}

void GdDataBuffer::read(bool &r_out) {
	r_out = read_bool();
}

void GdDataBuffer::add(std::uint8_t p_input) {
	add_uint(p_input, COMPRESSION_LEVEL_3);
}

void GdDataBuffer::read(std::uint8_t &r_out) {
	r_out = read_uint(COMPRESSION_LEVEL_3);
}

void GdDataBuffer::add(std::uint16_t p_input) {
	add_uint(p_input, COMPRESSION_LEVEL_2);
}

void GdDataBuffer::read(std::uint16_t &r_out) {
	r_out = read_uint(COMPRESSION_LEVEL_2);
}

void GdDataBuffer::add(std::uint32_t p_input) {
	add_uint(p_input, COMPRESSION_LEVEL_1);
}

void GdDataBuffer::read(std::uint32_t &r_out) {
	r_out = read_uint(COMPRESSION_LEVEL_1);
}

void GdDataBuffer::add(int p_input) {
	add_int(p_input, COMPRESSION_LEVEL_1);
}

void GdDataBuffer::read(int &r_out) {
	r_out = read_int(COMPRESSION_LEVEL_1);
}

void GdDataBuffer::add(std::uint64_t p_input) {
	add_uint(p_input, COMPRESSION_LEVEL_0);
}

void GdDataBuffer::read(std::uint64_t &r_out) {
	r_out = read_uint(COMPRESSION_LEVEL_0);
}

void GdDataBuffer::add(const std::string &p_string) {
	CRASH_COND(std::uint64_t(p_string.size()) > std::uint64_t(std::numeric_limits<std::uint16_t>::max()));
	add_uint(p_string.size(), COMPRESSION_LEVEL_2);
	if (p_string.size() > 0) {
		add_bits(reinterpret_cast<const uint8_t *>(p_string.c_str()), p_string.size() * 8);
	}
}

void GdDataBuffer::read(std::string &r_out) {
	const uint16_t size = read_uint(COMPRESSION_LEVEL_2);
	if (size <= 0) {
		return;
	}

	std::vector<char> chars;
	chars.resize(size);
	read_bits(reinterpret_cast<uint8_t *>(chars.data()), size * 8);
	r_out = std::string(chars.data(), size);
}

void GdDataBuffer::add(const std::u16string &p_string) {
	CRASH_COND(std::uint64_t(p_string.size()) > std::uint64_t(std::numeric_limits<std::uint16_t>::max()));
	add_uint(p_string.size(), COMPRESSION_LEVEL_2);
	if (p_string.size() > 0) {
		add_bits(reinterpret_cast<const uint8_t *>(p_string.c_str()), p_string.size() * 8 * (sizeof(char16_t)));
	}
}

void GdDataBuffer::read(std::u16string &r_out) {
	const uint16_t size = read_uint(COMPRESSION_LEVEL_2);
	if (size <= 0) {
		return;
	}

	std::vector<char16_t> chars;
	chars.resize(size);
	read_bits(reinterpret_cast<uint8_t *>(chars.data()), size * 8 * (sizeof(char16_t)));
	r_out = std::u16string(chars.data(), size);
}

void GdDataBuffer::add(const GdDataBuffer &p_db) {
	add_data_buffer(p_db);
}

void GdDataBuffer::read(GdDataBuffer &r_db) {
	read_data_buffer(r_db);
}

bool GdDataBuffer::add_bool(bool p_input) {
	ERR_FAIL_COND_V(is_reading == true, p_input);

	const int bits = get_bit_taken(DATA_TYPE_BOOL, COMPRESSION_LEVEL_0);

	make_room_in_bits(bits);
	if (!buffer.store_bits(bit_offset, p_input, bits)) {
		buffer_failed = true;
	}
	bit_offset += bits;

#ifdef DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	CRASH_COND((metadata_size + bit_size) > buffer.size_in_bits() && bit_offset > buffer.size_in_bits());
#endif

	DEB_WRITE(DATA_TYPE_BOOL, COMPRESSION_LEVEL_0, p_input ? "TRUE" : "FALSE");

	return p_input;
}

bool GdDataBuffer::read_bool() {
	ERR_FAIL_COND_V(is_reading == false, false);

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

int64_t GdDataBuffer::add_int(int64_t p_input, CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == true, p_input);

	const int bits = get_bit_taken(DATA_TYPE_INT, p_compression_level);

	int64_t value = p_input;

	// Clamp the value to the max that the bit can store.
	if (bits == 8) {
		value = CLAMP(value, INT8_MIN, INT8_MAX);
	} else if (bits == 16) {
		value = CLAMP(value, INT16_MIN, INT16_MAX);
	} else if (bits == 32) {
		value = CLAMP(value, INT32_MIN, INT32_MAX);
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
	CRASH_COND((metadata_size + bit_size) > buffer.size_in_bits() && bit_offset > buffer.size_in_bits());
#endif

	DEB_WRITE(DATA_TYPE_INT, p_compression_level, itos(value).utf8());

	return value;
}

int64_t GdDataBuffer::read_int(CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == false, 0);

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
		DEB_READ(DATA_TYPE_INT, p_compression_level, itos(static_cast<int8_t>(value)).utf8());
		return static_cast<int8_t>(value);

	} else if (bits == 16) {
		DEB_READ(DATA_TYPE_INT, p_compression_level, itos(static_cast<int16_t>(value)).utf8());
		return static_cast<int16_t>(value);

	} else if (bits == 32) {
		DEB_READ(DATA_TYPE_INT, p_compression_level, itos(static_cast<int32_t>(value)).utf8());
		return static_cast<int32_t>(value);

	} else {
		DEB_READ(DATA_TYPE_INT, p_compression_level, itos(value).utf8());
		return value;
	}
}

uint64_t GdDataBuffer::add_uint(uint64_t p_input, CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == true, p_input);

	const int bits = get_bit_taken(DATA_TYPE_UINT, p_compression_level);

	uint64_t value = p_input;

	// Clamp the value to the max that the bit can store.
	if (bits == 8) {
		value = MIN(value, uint64_t(UINT8_MAX));
	} else if (bits == 16) {
		value = MIN(value, uint64_t(UINT16_MAX));
	} else if (bits == 32) {
		value = MIN(value, uint64_t(UINT32_MAX));
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
	CRASH_COND((metadata_size + bit_size) > buffer.size_in_bits() && bit_offset > buffer.size_in_bits());
#endif

	DEB_WRITE(DATA_TYPE_UINT, p_compression_level, uitos(value).utf8());

	return value;
}

uint64_t GdDataBuffer::read_uint(CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == false, 0);

	const int bits = get_bit_taken(DATA_TYPE_UINT, p_compression_level);

	uint64_t value;
	if (!buffer.read_bits(bit_offset, bits, value)) {
		buffer_failed = true;
		return 0;
	}
	bit_offset += bits;

	DEB_READ(DATA_TYPE_UINT, p_compression_level, uitos(value).utf8());

	return value;
}

double GdDataBuffer::add_real(double p_input, CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == true, p_input);

	// Clamp the input value according to the compression level
	// Minifloat (compression level 0) have a special bias
	const int exponent_bits = get_exponent_bits(p_compression_level);
	const int mantissa_bits = get_mantissa_bits(p_compression_level);
	const double bias = p_compression_level == COMPRESSION_LEVEL_3 ? Math::pow(2.0, exponent_bits) - 3 : Math::pow(2.0, exponent_bits - 1) - 1;
	const double max_value = (2.0 - Math::pow(2.0, -(mantissa_bits - 1))) * Math::pow(2.0, bias);
	const double clamped_input = CLAMP(p_input, -max_value, max_value);

	// Split number according to IEEE 754 binary format.
	// Mantissa floating point value represented in range (-1;-0.5], [0.5; 1).
	int exponent;
	double mantissa = frexp(clamped_input, &exponent);

	// Extract sign.
	const bool sign = mantissa < 0;
	mantissa = Math::abs(mantissa);

	// Round mantissa into the specified number of bits (like float -> double conversion).
	double mantissa_scale = Math::pow(2.0, mantissa_bits);
	if (exponent <= 0) {
		// Subnormal value, apply exponent to mantissa and reduce power of scale by one.
		mantissa *= Math::pow(2.0, exponent);
		exponent = 0;
		mantissa_scale /= 2.0;
	}
	mantissa = Math::round(mantissa * mantissa_scale) / mantissa_scale; // Round to specified number of bits.
	if (mantissa < 0.5 && mantissa != 0) {
		// Check underflow, extract exponent from mantissa.
		exponent += ilogb(mantissa) + 1;
		mantissa /= Math::pow(2.0, exponent);
	} else if (mantissa == 1) {
		// Check overflow, increment the exponent.
		++exponent;
		mantissa = 0.5;
	}
	// Convert the mantissa to an integer that represents the offset index (IEE 754 floating point representation) to send over network safely.
	const uint64_t integer_mantissa = exponent <= 0 ? mantissa * mantissa_scale * Math::pow(2.0, exponent) : (mantissa - 0.5) * mantissa_scale;

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
	DEB_WRITE(DATA_TYPE_REAL, p_compression_level, rtos(value).utf8());
	return value;
}

double GdDataBuffer::read_real(CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == false, 0.0);

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
	const double bias = p_compression_level == COMPRESSION_LEVEL_3 ? Math::pow(2.0, exponent_bits) - 3 : Math::pow(2.0, exponent_bits - 1) - 1;
	std::uint64_t encoded_exponent;
	if (!buffer.read_bits(bit_offset, exponent_bits, encoded_exponent)) {
		buffer_failed = true;
		return 0.0;
	}
	const int exponent = static_cast<int>(encoded_exponent) - static_cast<int>(bias);
	bit_offset += exponent_bits;

	// Convert integer mantissa into the floating point representation
	// When the index of the mantissa and exponent are 0, then this is a special case and the mantissa is 0.
	const double mantissa_scale = Math::pow(2.0, exponent <= 0 ? mantissa_bits - 1 : mantissa_bits);
	const double mantissa = exponent <= 0 ? integer_mantissa / mantissa_scale / Math::pow(2.0, exponent) : integer_mantissa / mantissa_scale + 0.5;

	const double value = ldexp(sign != 0 ? -mantissa : mantissa, exponent);

	DEB_READ(DATA_TYPE_REAL, p_compression_level, rtos(value).utf8());

	return value;
}

float GdDataBuffer::add_positive_unit_real(float p_input, CompressionLevel p_compression_level) {
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_V_MSG(p_input < 0 || p_input > 1, p_input, "Value must be between zero and one.");
#endif
	ERR_FAIL_COND_V(is_reading == true, p_input);

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
	CRASH_COND((metadata_size + bit_size) > buffer.size_in_bits() && bit_offset > buffer.size_in_bits());
#endif

	const float value = decompress_unit_float(compressed_val, max_value);
	DEB_WRITE(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression_level, rtos(value).utf8());
	return value;
}

float GdDataBuffer::read_positive_unit_real(CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == false, 0.0);

	const int bits = get_bit_taken(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression_level);

	const double max_value = static_cast<double>(~(UINT64_MAX << bits));

	std::uint64_t compressed_val;
	if (!buffer.read_bits(bit_offset, bits, compressed_val)) {
		buffer_failed = true;
		return 0.0;
	}
	bit_offset += bits;

	const float value = decompress_unit_float(compressed_val, max_value);

	DEB_READ(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression_level, rtos(value).utf8());

	return value;
}

float GdDataBuffer::add_unit_real(float p_input, CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == true, p_input);

	const float added_real = add_positive_unit_real(ABS(p_input), p_compression_level);

	const int bits_for_sign = 1;
	const uint32_t is_negative = p_input < 0.0;
	make_room_in_bits(bits_for_sign);
	if (!buffer.store_bits(bit_offset, is_negative, bits_for_sign)) {
		buffer_failed = true;
	}
	bit_offset += bits_for_sign;

#ifdef DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	CRASH_COND((metadata_size + bit_size) > buffer.size_in_bits() && bit_offset > buffer.size_in_bits());
#endif

	const float value = is_negative ? -added_real : added_real;
	DEB_WRITE(DATA_TYPE_UNIT_REAL, p_compression_level, rtos(value).utf8());

	return value;
}

float GdDataBuffer::read_unit_real(CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == false, 0.0);

	const float value = read_positive_unit_real(p_compression_level);

	const int bits_for_sign = 1;
	std::uint64_t is_negative;
	if (!buffer.read_bits(bit_offset, bits_for_sign, is_negative)) {
		buffer_failed = true;
		return 0.0;
	}
	bit_offset += bits_for_sign;

	const float ret = is_negative != 0 ? -value : value;

	DEB_READ(DATA_TYPE_UNIT_REAL, p_compression_level, rtos(ret).utf8());

	return ret;
}

Vector2 GdDataBuffer::add_vector2(Vector2 p_input, CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == true, p_input);

	DEB_DISABLE

	Vector2 r;
	r[0] = add_real(p_input[0], p_compression_level);
	r[1] = add_real(p_input[1], p_compression_level);

	DEB_ENABLE

	DEB_WRITE(DATA_TYPE_VECTOR2, p_compression_level, String("X: " + rtos(r.x) + " Y: " + rtos(r.y)).utf8());

	return r;
}

Vector2 GdDataBuffer::read_vector2(CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == false, Vector2());

	DEB_DISABLE

	Vector2 r;
	r[0] = read_real(p_compression_level);
	r[1] = read_real(p_compression_level);

	DEB_ENABLE

	DEB_READ(DATA_TYPE_VECTOR2, p_compression_level, String("X: " + rtos(r.x) + " Y: " + rtos(r.y)).utf8());

	return r;
}

Vector2 GdDataBuffer::add_normalized_vector2(Vector2 p_input, CompressionLevel p_compression_level) {
	const std::uint64_t is_not_zero = p_input.length_squared() > CMP_EPSILON ? 1 : 0;

#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_V_MSG(p_input.is_normalized() == false && is_not_zero, p_input, "[FATAL] The encoding failed because this function expects a normalized vector.");
#endif

	ERR_FAIL_COND_V(is_reading == true, p_input);

	const int bits = get_bit_taken(DATA_TYPE_NORMALIZED_VECTOR2, p_compression_level);
	const int bits_for_the_angle = bits - 1;
	const int bits_for_zero = 1;

	const double angle = p_input.angle();

	const double max_value = static_cast<double>(~(UINT64_MAX << bits_for_the_angle));

	const uint64_t compressed_angle = compress_unit_float((angle + Math_PI) / Math_TAU, max_value);

	make_room_in_bits(bits);
	if (!buffer.store_bits(bit_offset, is_not_zero, bits_for_zero)) {
		buffer_failed = true;
	}
	if (!buffer.store_bits(bit_offset + 1, compressed_angle, bits_for_the_angle)) {
		buffer_failed = true;
	}
	bit_offset += bits;

	const double decompressed_angle = (decompress_unit_float(compressed_angle, max_value) * Math_TAU) - Math_PI;
	const double x = Math::cos(decompressed_angle);
	const double y = Math::sin(decompressed_angle);

#ifdef DEBUG_ENABLED
	// Can't never happen because the buffer size is correctly handled.
	CRASH_COND((metadata_size + bit_size) > buffer.size_in_bits() && bit_offset > buffer.size_in_bits());
#endif

	const Vector2 value = Vector2(x, y) * static_cast<float>(is_not_zero);
	DEB_WRITE(DATA_TYPE_NORMALIZED_VECTOR2, p_compression_level, String("X: " + rtos(value.x) + " Y: " + rtos(value.y)).utf8());
	return value;
}

Vector2 GdDataBuffer::read_normalized_vector2(CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == false, Vector2());

	const int bits = get_bit_taken(DATA_TYPE_NORMALIZED_VECTOR2, p_compression_level);
	const int bits_for_the_angle = bits - 1;
	const int bits_for_zero = 1;

	const double max_value = static_cast<double>(~(UINT64_MAX << bits_for_the_angle));

	std::uint64_t is_not_zero;
	if (!buffer.read_bits(bit_offset, bits_for_zero, is_not_zero)) {
		buffer_failed = true;
		return Vector2();
	}
	std::uint64_t compressed_angle;
	if (!buffer.read_bits(bit_offset + 1, bits_for_the_angle, compressed_angle)) {
		buffer_failed = true;
		return Vector2();
	}
	bit_offset += bits;

	const double decompressed_angle = (decompress_unit_float(compressed_angle, max_value) * Math_TAU) - Math_PI;
	const double x = Math::cos(decompressed_angle);
	const double y = Math::sin(decompressed_angle);

	const Vector2 value = Vector2(x, y) * static_cast<float>(is_not_zero);

	DEB_READ(DATA_TYPE_NORMALIZED_VECTOR2, p_compression_level, String("X: " + rtos(value.x) + " Y: " + rtos(value.y)).utf8());
	return value;
}

Vector3 GdDataBuffer::add_vector3(Vector3 p_input, CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == true, p_input);

	DEB_DISABLE

	Vector3 r;
	r[0] = add_real(p_input[0], p_compression_level);
	r[1] = add_real(p_input[1], p_compression_level);
	r[2] = add_real(p_input[2], p_compression_level);

	DEB_ENABLE

	DEB_WRITE(DATA_TYPE_VECTOR3, p_compression_level, String("X: " + rtos(r.x) + " Y: " + rtos(r.y) + " Z: " + rtos(r.z)).utf8());
	return r;
}

Vector3 GdDataBuffer::read_vector3(CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == false, Vector3());

	DEB_DISABLE

	Vector3 r;
	r[0] = read_real(p_compression_level);
	r[1] = read_real(p_compression_level);
	r[2] = read_real(p_compression_level);

	DEB_ENABLE

	DEB_READ(DATA_TYPE_VECTOR3, p_compression_level, String("X: " + rtos(r.x) + " Y: " + rtos(r.y) + " Z: " + rtos(r.z)).utf8());

	return r;
}

Vector3 GdDataBuffer::add_normalized_vector3(Vector3 p_input, CompressionLevel p_compression_level) {
#ifdef DEBUG_ENABLED
	const uint32_t is_not_zero = p_input.length_squared() > CMP_EPSILON;
	ERR_FAIL_COND_V_MSG(p_input.is_normalized() == false && is_not_zero, p_input, "[FATAL] This function expects a normalized vector.");
#endif
	ERR_FAIL_COND_V(is_reading == true, p_input);

	DEB_DISABLE

	const float x_axis = add_unit_real(p_input.x, p_compression_level);
	const float y_axis = add_unit_real(p_input.y, p_compression_level);
	const float z_axis = add_unit_real(p_input.z, p_compression_level);

	DEB_ENABLE

	const Vector3 value = Vector3(x_axis, y_axis, z_axis);
	DEB_WRITE(DATA_TYPE_NORMALIZED_VECTOR3, p_compression_level, String("X: " + rtos(value.x) + " Y: " + rtos(value.y) + " Z: " + rtos(value.z)).utf8());
	return value;
}

Vector3 GdDataBuffer::read_normalized_vector3(CompressionLevel p_compression_level) {
	ERR_FAIL_COND_V(is_reading == false, Vector3());

	DEB_DISABLE

	const float x_axis = read_unit_real(p_compression_level);
	const float y_axis = read_unit_real(p_compression_level);
	const float z_axis = read_unit_real(p_compression_level);

	DEB_ENABLE

	const Vector3 value(x_axis, y_axis, z_axis);

	DEB_READ(DATA_TYPE_NORMALIZED_VECTOR3, p_compression_level, String("X: " + rtos(value.x) + " Y: " + rtos(value.y) + " Z: " + rtos(value.z)).utf8());

	return value;
}

Variant GdDataBuffer::add_variant(const Variant &p_input) {
	ERR_FAIL_COND_V(is_reading, Variant());
	// TODO consider to use a method similar to `_encode_and_compress_variant`
	// to compress the encoded data a bit.

	// Get the variant size.
	int len = 0;

	const Error len_err = encode_variant(
			p_input,
			nullptr,
			len,
			false);

	ERR_FAIL_COND_V_MSG(
			len_err != OK,
			Variant(),
			"Was not possible encode the variant.");

	// Variant encoding pads the data to byte, so doesn't make sense write it
	// unpadded.
	make_room_pad_to_next_byte();
	make_room_in_bits(len * 8);

#ifdef DEBUG_ENABLED
	// This condition is always false thanks to the `make_room_pad_to_next_byte`.
	// so it's safe to assume we are starting from the begin of the byte.
	CRASH_COND((bit_offset % 8) != 0);
#endif

	const Error write_err = encode_variant(
			p_input,
			buffer.get_bytes_mut().data() + (bit_offset / 8),
			len,
			false);

	ERR_FAIL_COND_V_MSG(
			write_err != OK,
			Variant(),
			"Was not possible encode the variant.");

	bit_offset += len * 8;

	DEB_WRITE(DATA_TYPE_VARIANT, COMPRESSION_LEVEL_0, p_input.stringify().utf8());
	return p_input;
}

/// This is an optimization for when we want a null Variant to be a single bit in the buffer.
Variant GdDataBuffer::add_optional_variant(const Variant &p_input, const Variant &p_default) {
	if (p_input == p_default) {
		add(true);
		return p_default;
	} else {
		add(false);
		add_variant(p_input);
		return p_input;
	}
}

Variant GdDataBuffer::read_optional_variant(const Variant &p_default) {
	bool is_def = true;
	read(is_def);
	if (is_def) {
		return p_default;
	} else {
		return read_variant();
	}
}

Variant GdDataBuffer::read_variant() {
	ERR_FAIL_COND_V(!is_reading, Variant());
	Variant ret;

	int len = 0;

	// The Variant is always written starting from the beginning of the byte.
	const bool success = pad_to_next_byte();
	ERR_FAIL_COND_V_MSG(success == false, Variant(), "Padding failed.");

#ifdef DEBUG_ENABLED
	// This condition is always false thanks to the `pad_to_next_byte`; So is
	// safe to assume we are starting from the begin of the byte.
	CRASH_COND((bit_offset % 8) != 0);
#endif

	const Error read_err = decode_variant(
			ret,
			buffer.get_bytes().data() + (bit_offset / 8),
			buffer.size_in_bytes() - (bit_offset / 8),
			&len,
			false);

	ERR_FAIL_COND_V_MSG(
			read_err != OK,
			Variant(),
			"Was not possible decode the variant.");

	bit_offset += len * 8;

	DEB_READ(DATA_TYPE_VARIANT, COMPRESSION_LEVEL_0, ret.stringify().utf8());

	return ret;
}

void GdDataBuffer::add_data_buffer(const GdDataBuffer &p_db) {
	const std::uint32_t other_db_bit_size = p_db.metadata_size + p_db.bit_size;
	CRASH_COND_MSG(other_db_bit_size > std::numeric_limits<std::uint32_t>::max(), "GdDataBuffer can't add GdDataBuffer bigger than `" + itos(std::numeric_limits<std::uint32_t>::max()) + "` bits at the moment. [If this feature is needed ask for it.]");

	const bool using_compression_lvl_2 = other_db_bit_size < std::numeric_limits<std::uint16_t>::max();
	add(using_compression_lvl_2);
	add_uint(other_db_bit_size, using_compression_lvl_2 ? COMPRESSION_LEVEL_2 : COMPRESSION_LEVEL_1);

	make_room_pad_to_next_byte();
	const std::uint8_t *ptr = p_db.buffer.get_bytes().data();
	add_bits(ptr, other_db_bit_size);
}

void GdDataBuffer::read_data_buffer(GdDataBuffer &r_db) {
	ERR_FAIL_COND(!is_reading);
	CRASH_COND(r_db.is_reading);

	bool using_compression_lvl_2 = false;
	read(using_compression_lvl_2);
	ERR_FAIL_COND(is_buffer_failed());
	const int other_db_bit_size = read_uint(using_compression_lvl_2 ? COMPRESSION_LEVEL_2 : COMPRESSION_LEVEL_1);

	pad_to_next_byte();
	r_db.add_bits(&(buffer.get_bytes()[bit_offset / 8]), other_db_bit_size);

	bit_offset += other_db_bit_size;
}

void GdDataBuffer::add_bits(const uint8_t *p_data, int p_bit_count) {
	ERR_FAIL_COND(is_reading);
	const int initial_bit_count = p_bit_count;

	make_room_in_bits(p_bit_count);

	for (int i = 0; p_bit_count > 0; ++i) {
		const int this_bit_count = MIN(p_bit_count, 8);
		p_bit_count -= this_bit_count;

		if (!buffer.store_bits(bit_offset, p_data[i], this_bit_count)) {
			buffer_failed = true;
		}

		bit_offset += this_bit_count;
	}

	DEB_WRITE(DATA_TYPE_BITS, COMPRESSION_LEVEL_0, String("buffer of `" + itos(initial_bit_count) + "` bits.").utf8());
}

void GdDataBuffer::read_bits(uint8_t *r_data, int p_bit_count) {
	ERR_FAIL_COND(!is_reading);
	const int initial_bit_count = p_bit_count;

	for (int i = 0; p_bit_count > 0; ++i) {
		const int this_bit_count = MIN(p_bit_count, 8);
		p_bit_count -= this_bit_count;

		std::uint64_t d;
		if (!buffer.read_bits(bit_offset, this_bit_count, d)) {
			buffer_failed = true;
			return;
		}
		r_data[i] = d;

		bit_offset += this_bit_count;
	}

	DEB_READ(DATA_TYPE_BITS, COMPRESSION_LEVEL_0, String("buffer of `" + itos(initial_bit_count) + "` bits.").utf8());
}

void GdDataBuffer::zero() {
	buffer.zero();
}

void GdDataBuffer::skip_bool() {
	const int bits = get_bool_size();
	skip(bits);
}

void GdDataBuffer::skip_int(CompressionLevel p_compression) {
	const int bits = get_int_size(p_compression);
	skip(bits);
}

void GdDataBuffer::skip_uint(CompressionLevel p_compression) {
	const int bits = get_uint_size(p_compression);
	skip(bits);
}

void GdDataBuffer::skip_real(CompressionLevel p_compression) {
	const int bits = get_real_size(p_compression);
	skip(bits);
}

void GdDataBuffer::skip_positive_unit_real(CompressionLevel p_compression) {
	const int bits = get_positive_unit_real_size(p_compression);
	skip(bits);
}

void GdDataBuffer::skip_unit_real(CompressionLevel p_compression) {
	const int bits = get_unit_real_size(p_compression);
	skip(bits);
}

void GdDataBuffer::skip_vector2(CompressionLevel p_compression) {
	const int bits = get_vector2_size(p_compression);
	skip(bits);
}

void GdDataBuffer::skip_normalized_vector2(CompressionLevel p_compression) {
	const int bits = get_normalized_vector2_size(p_compression);
	skip(bits);
}

void GdDataBuffer::skip_vector3(CompressionLevel p_compression) {
	const int bits = get_vector3_size(p_compression);
	skip(bits);
}

void GdDataBuffer::skip_normalized_vector3(CompressionLevel p_compression) {
	const int bits = get_normalized_vector3_size(p_compression);
	skip(bits);
}

void GdDataBuffer::skip_variant() {
	// This already seek the offset as `skip` does.
	read_variant_size();
}

void GdDataBuffer::skip_optional_variant(const Variant &p_def) {
	// This already seek the offset as `skip` does.
	read_optional_variant_size(p_def);
}

int GdDataBuffer::get_bool_size() const {
	return GdDataBuffer::get_bit_taken(DATA_TYPE_BOOL, COMPRESSION_LEVEL_0);
}

int GdDataBuffer::get_int_size(CompressionLevel p_compression) const {
	return GdDataBuffer::get_bit_taken(DATA_TYPE_INT, p_compression);
}

int GdDataBuffer::get_uint_size(CompressionLevel p_compression) const {
	return GdDataBuffer::get_bit_taken(DATA_TYPE_UINT, p_compression);
}

int GdDataBuffer::get_real_size(CompressionLevel p_compression) const {
	return GdDataBuffer::get_bit_taken(DATA_TYPE_REAL, p_compression);
}

int GdDataBuffer::get_positive_unit_real_size(CompressionLevel p_compression) const {
	return GdDataBuffer::get_bit_taken(DATA_TYPE_POSITIVE_UNIT_REAL, p_compression);
}

int GdDataBuffer::get_unit_real_size(CompressionLevel p_compression) const {
	return GdDataBuffer::get_bit_taken(DATA_TYPE_UNIT_REAL, p_compression);
}

int GdDataBuffer::get_vector2_size(CompressionLevel p_compression) const {
	return GdDataBuffer::get_bit_taken(DATA_TYPE_VECTOR2, p_compression);
}

int GdDataBuffer::get_normalized_vector2_size(CompressionLevel p_compression) const {
	return GdDataBuffer::get_bit_taken(DATA_TYPE_NORMALIZED_VECTOR2, p_compression);
}

int GdDataBuffer::get_vector3_size(CompressionLevel p_compression) const {
	return GdDataBuffer::get_bit_taken(DATA_TYPE_VECTOR3, p_compression);
}

int GdDataBuffer::get_normalized_vector3_size(CompressionLevel p_compression) const {
	return GdDataBuffer::get_bit_taken(DATA_TYPE_NORMALIZED_VECTOR3, p_compression);
}

int GdDataBuffer::read_bool_size() {
	const int bits = get_bool_size();
	skip(bits);
	return bits;
}

int GdDataBuffer::read_int_size(CompressionLevel p_compression) {
	const int bits = get_int_size(p_compression);
	skip(bits);
	return bits;
}

int GdDataBuffer::read_uint_size(CompressionLevel p_compression) {
	const int bits = get_uint_size(p_compression);
	skip(bits);
	return bits;
}

int GdDataBuffer::read_real_size(CompressionLevel p_compression) {
	const int bits = get_real_size(p_compression);
	skip(bits);
	return bits;
}

int GdDataBuffer::read_positive_unit_real_size(CompressionLevel p_compression) {
	const int bits = get_positive_unit_real_size(p_compression);
	skip(bits);
	return bits;
}

int GdDataBuffer::read_unit_real_size(CompressionLevel p_compression) {
	const int bits = get_unit_real_size(p_compression);
	skip(bits);
	return bits;
}

int GdDataBuffer::read_vector2_size(CompressionLevel p_compression) {
	const int bits = get_vector2_size(p_compression);
	skip(bits);
	return bits;
}

int GdDataBuffer::read_normalized_vector2_size(CompressionLevel p_compression) {
	const int bits = get_normalized_vector2_size(p_compression);
	skip(bits);
	return bits;
}

int GdDataBuffer::read_vector3_size(CompressionLevel p_compression) {
	const int bits = get_vector3_size(p_compression);
	skip(bits);
	return bits;
}

int GdDataBuffer::read_normalized_vector3_size(CompressionLevel p_compression) {
	const int bits = get_normalized_vector3_size(p_compression);
	skip(bits);
	return bits;
}

int GdDataBuffer::read_variant_size() {
	Variant ret;

	// The Variant is always written starting from the beginning of the byte.
	int padding_bits;
	const bool success = pad_to_next_byte(&padding_bits);
	ERR_FAIL_COND_V_MSG(success == false, Variant(), "Padding failed.");

#ifdef DEBUG_ENABLED
	// This condition is always false thanks to the `pad_to_next_byte`; So is
	// safe to assume we are starting from the begin of the byte.
	CRASH_COND((bit_offset % 8) != 0);
#endif

	int len = 0;
	const Error read_err = decode_variant(
			ret,
			buffer.get_bytes().data() + (bit_offset / 8),
			buffer.size_in_bytes() - (bit_offset / 8),
			&len,
			false);

	ERR_FAIL_COND_V_MSG(
			read_err != OK,
			0,
			"Was not possible to decode the variant, error: " + itos(read_err));

	bit_offset += len * 8;

	return padding_bits + (len * 8);
}

int GdDataBuffer::read_optional_variant_size(const Variant &p_def) {
	int len = get_bool_size();

	bool is_def = true;
	read(is_def);

	if (!is_def) {
		len += read_variant_size();
	}

	return len;
}

int GdDataBuffer::get_bit_taken(DataType p_data_type, CompressionLevel p_compression) {
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
					CRASH_NOW_MSG("Compression level not supported!");
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
					CRASH_NOW_MSG("Compression level not supported!");
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
					CRASH_NOW_MSG("Compression level not supported!");
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
			ERR_FAIL_V_MSG(0, "The bits size specified by the user and is not determined according to the compression level.");
		}
		case DATA_TYPE_VARIANT: {
			ERR_FAIL_V_MSG(0, "The variant size is dynamic and can't be know at compile time.");
		}
		default:
			// Unreachable
			CRASH_NOW_MSG("Input type not supported!");
	}

	// Unreachable
	CRASH_NOW_MSG("It was not possible to obtain the bit taken by this input data.");
	return 0; // Useless, but MS CI is too noisy.
}

int GdDataBuffer::get_mantissa_bits(CompressionLevel p_compression) {
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
	CRASH_NOW_MSG("Unknown compression level.");
	return 0; // Useless, but MS CI is too noisy.
}

int GdDataBuffer::get_exponent_bits(CompressionLevel p_compression) {
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
	CRASH_NOW_MSG("Unknown compression level.");
	return 0; // Useless, but MS CI is too noisy.
}

uint64_t GdDataBuffer::compress_unit_float(double p_value, double p_scale_factor) {
	return Math::round(MIN(p_value * p_scale_factor, p_scale_factor));
}

double GdDataBuffer::decompress_unit_float(uint64_t p_value, double p_scale_factor) {
	return static_cast<double>(p_value) / p_scale_factor;
}

void GdDataBuffer::make_room_in_bits(int p_dim) {
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

void GdDataBuffer::make_room_pad_to_next_byte() {
	const int bits_to_next_byte = ((bit_offset + 7) & ~7) - bit_offset;
	make_room_in_bits(bits_to_next_byte);
	bit_offset += bits_to_next_byte;
}

bool GdDataBuffer::pad_to_next_byte(int *p_bits_to_next_byte) {
	const int bits_to_next_byte = ((bit_offset + 7) & ~7) - bit_offset;
	ERR_FAIL_COND_V_MSG(
			bit_offset + bits_to_next_byte > buffer.size_in_bits(),
			false,
			"");
	bit_offset += bits_to_next_byte;
	if (p_bits_to_next_byte) {
		*p_bits_to_next_byte = bits_to_next_byte;
	}
	return true;
}
