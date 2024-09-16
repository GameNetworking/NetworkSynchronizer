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

#ifdef NS_DEBUG_ENABLED
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
	BIND_ENUM_CONSTANT(DATA_TYPE_DATABUFFER);

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

void GdDataBuffer::begin_write(int p_metadata_size) {
	data_buffer->begin_write(p_metadata_size);
}

int GdDataBuffer::size() const {
	return data_buffer->size();
}

void GdDataBuffer::dry() {
	data_buffer->dry();
}

void GdDataBuffer::begin_read() {
	data_buffer->begin_read();
}

bool GdDataBuffer::add_bool(bool p_input) {
	data_buffer->add(p_input);
	return p_input;
}

bool GdDataBuffer::read_bool() {
	bool d;
	data_buffer->read(d);
	return d;
}

int64_t GdDataBuffer::add_int(int64_t p_input, CompressionLevel p_compression_level) {
	return data_buffer->add_int(p_input, (NS::DataBuffer::CompressionLevel)p_compression_level);
}

int64_t GdDataBuffer::read_int(CompressionLevel p_compression_level) {
	return data_buffer->read_int((NS::DataBuffer::CompressionLevel)p_compression_level);
}

uint64_t GdDataBuffer::add_uint(uint64_t p_input, CompressionLevel p_compression_level) {
	return data_buffer->add_uint(p_input, (NS::DataBuffer::CompressionLevel)p_compression_level);
}

uint64_t GdDataBuffer::read_uint(CompressionLevel p_compression_level) {
	return data_buffer->read_uint((NS::DataBuffer::CompressionLevel)p_compression_level);
}

double GdDataBuffer::add_real(double p_input, CompressionLevel p_compression_level) {
	data_buffer->add_real(p_input, (NS::DataBuffer::CompressionLevel)p_compression_level);
	return p_input;
}

double GdDataBuffer::read_real(CompressionLevel p_compression_level) {
	double ret;
	data_buffer->read_real(ret, (NS::DataBuffer::CompressionLevel)p_compression_level);
	return ret;
}

float GdDataBuffer::add_positive_unit_real(float p_input, CompressionLevel p_compression_level) {
	return data_buffer->add_positive_unit_real(p_input, (NS::DataBuffer::CompressionLevel)p_compression_level);
}

float GdDataBuffer::read_positive_unit_real(CompressionLevel p_compression_level) {
	return data_buffer->read_positive_unit_real((NS::DataBuffer::CompressionLevel)p_compression_level);
}

float GdDataBuffer::add_unit_real(float p_input, CompressionLevel p_compression_level) {
	return data_buffer->add_unit_real(p_input, (NS::DataBuffer::CompressionLevel)p_compression_level);
}

float GdDataBuffer::read_unit_real(CompressionLevel p_compression_level) {
	return data_buffer->read_unit_real((NS::DataBuffer::CompressionLevel)p_compression_level);
}

Vector2 GdDataBuffer::add_vector2(Vector2 p_input, CompressionLevel p_compression_level) {
	Vector2 r;
	data_buffer->add_vector2(r.x, r.y, (NS::DataBuffer::CompressionLevel)p_compression_level);
	return r;
}

Vector2 GdDataBuffer::read_vector2(CompressionLevel p_compression_level) {
	double x;
	double y;
	data_buffer->read_vector2(x, y, (NS::DataBuffer::CompressionLevel)p_compression_level);
	return Vector2(x, y);
}

Vector2 GdDataBuffer::add_normalized_vector2(Vector2 p_input, CompressionLevel p_compression_level) {
	data_buffer->add_normalized_vector2(p_input.x, p_input.y, (NS::DataBuffer::CompressionLevel)p_compression_level);
	return p_input;
}

Vector2 GdDataBuffer::read_normalized_vector2(CompressionLevel p_compression_level) {
	double x;
	double y;
	data_buffer->read_normalized_vector2(x, y, (NS::DataBuffer::CompressionLevel)p_compression_level);
	return Vector2(x, y);
}

Vector3 GdDataBuffer::add_vector3(Vector3 p_input, CompressionLevel p_compression_level) {
	data_buffer->add_vector3(p_input.x, p_input.y, p_input.z, (NS::DataBuffer::CompressionLevel)p_compression_level);
	return p_input;
}

Vector3 GdDataBuffer::read_vector3(CompressionLevel p_compression_level) {
	double x;
	double y;
	double z;
	data_buffer->read_vector3(x, y, z, (NS::DataBuffer::CompressionLevel)p_compression_level);
	return Vector3(x, y, z);
}

Vector3 GdDataBuffer::add_normalized_vector3(Vector3 p_input, CompressionLevel p_compression_level) {
	data_buffer->add_normalized_vector3(p_input.x, p_input.y, p_input.z, (NS::DataBuffer::CompressionLevel)p_compression_level);
	return p_input;
}

Vector3 GdDataBuffer::read_normalized_vector3(CompressionLevel p_compression_level) {
	double x;
	double y;
	double z;
	data_buffer->read_normalized_vector3(x, y, z, (NS::DataBuffer::CompressionLevel)p_compression_level);
	return Vector3(x, y, z);
}

Variant GdDataBuffer::add_variant(const Variant &p_input) {
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

	NS::DataBuffer variant_db;
	variant_db.begin_write(get_debugger(), 0);
	variant_db.add(len);
	variant_db.make_room_pad_to_next_byte();
	variant_db.make_room_in_bits(len * 8);

#ifdef NS_DEBUG_ENABLED
	// This condition is always false thanks to the `make_room_pad_to_next_byte`.
	// so it's safe to assume we are starting from the begin of the byte.
	CRASH_COND((variant_db.get_bit_offset() % 8) != 0);
#endif

	const Error write_err = encode_variant(
			p_input,
			variant_db.get_buffer_mut().get_bytes_mut().data(),
			len,
			false);

	ERR_FAIL_COND_V_MSG(
			write_err != OK,
			Variant(),
			"Was not possible encode the variant.");

	variant_db.skip(len * 8);

	data_buffer->add_data_buffer(variant_db);

	return p_input;
}

/// This is an optimization for when we want a null Variant to be a single bit in the buffer.
Variant GdDataBuffer::add_optional_variant(const Variant &p_input, const Variant &p_default) {
	if (p_input == p_default) {
		data_buffer->add(true);
		return p_default;
	} else {
		data_buffer->add(false);
		add_variant(p_input);
		return p_input;
	}
}

Variant GdDataBuffer::read_optional_variant(const Variant &p_default) {
	bool is_def = true;
	data_buffer->read(is_def);
	if (is_def) {
		return p_default;
	} else {
		return read_variant();
	}
}

Variant GdDataBuffer::read_variant() {
	NS::DataBuffer variant_db;
	data_buffer->read_data_buffer(variant_db);

	ERR_FAIL_COND_V(!data_buffer->is_buffer_failed(), Variant());

	Variant ret;

	int len = 0;
	variant_db.read(len);
	variant_db.pad_to_next_byte();

#ifdef NS_DEBUG_ENABLED
	// This condition is always false thanks to the `pad_to_next_byte`; So is
	// safe to assume we are starting from the begin of the byte.
	CRASH_COND((variant_db.get_bit_offset() % 8) != 0);
#endif

	const Error read_err = decode_variant(
			ret,
			variant_db.get_buffer_mut().get_bytes().data() + (variant_db.get_bit_offset() / 8),
			len,
			nullptr,
			false);

	ERR_FAIL_COND_V_MSG(
			read_err != OK,
			Variant(),
			"Was not possible decode the variant.");

	return ret;
}

void GdDataBuffer::skip_bool() {
	data_buffer->skip_bool();
}

void GdDataBuffer::skip_int(CompressionLevel p_compression) {
	data_buffer->skip_int((NS::DataBuffer::CompressionLevel)p_compression);
}

void GdDataBuffer::skip_uint(CompressionLevel p_compression) {
	data_buffer->skip_uint((NS::DataBuffer::CompressionLevel)p_compression);
}

void GdDataBuffer::skip_real(CompressionLevel p_compression) {
	data_buffer->skip_real((NS::DataBuffer::CompressionLevel)p_compression);
}

void GdDataBuffer::skip_positive_unit_real(CompressionLevel p_compression) {
	data_buffer->skip_positive_unit_real((NS::DataBuffer::CompressionLevel)p_compression);
}

void GdDataBuffer::skip_unit_real(CompressionLevel p_compression) {
	data_buffer->skip_unit_real((NS::DataBuffer::CompressionLevel)p_compression);
}

void GdDataBuffer::skip_vector2(CompressionLevel p_compression) {
	data_buffer->skip_vector2((NS::DataBuffer::CompressionLevel)p_compression);
}

void GdDataBuffer::skip_normalized_vector2(CompressionLevel p_compression) {
	data_buffer->skip_normalized_vector2((NS::DataBuffer::CompressionLevel)p_compression);
}

void GdDataBuffer::skip_vector3(CompressionLevel p_compression) {
	data_buffer->skip_vector3((NS::DataBuffer::CompressionLevel)p_compression);
}

void GdDataBuffer::skip_normalized_vector3(CompressionLevel p_compression) {
	data_buffer->skip_normalized_vector3((NS::DataBuffer::CompressionLevel)p_compression);
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
	return data_buffer->get_bool_size();
}

int GdDataBuffer::get_int_size(CompressionLevel p_compression) const {
	return data_buffer->get_int_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::get_uint_size(CompressionLevel p_compression) const {
	return data_buffer->get_uint_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::get_real_size(CompressionLevel p_compression) const {
	return data_buffer->get_real_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::get_positive_unit_real_size(CompressionLevel p_compression) const {
	return data_buffer->get_positive_unit_real_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::get_unit_real_size(CompressionLevel p_compression) const {
	return data_buffer->get_unit_real_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::get_vector2_size(CompressionLevel p_compression) const {
	return data_buffer->get_vector2_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::get_normalized_vector2_size(CompressionLevel p_compression) const {
	return data_buffer->get_normalized_vector2_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::get_vector3_size(CompressionLevel p_compression) const {
	return data_buffer->get_vector3_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::get_normalized_vector3_size(CompressionLevel p_compression) const {
	return data_buffer->get_normalized_vector3_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::read_bool_size() {
	return data_buffer->read_bool_size();
}

int GdDataBuffer::read_int_size(CompressionLevel p_compression) {
	return data_buffer->read_int_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::read_uint_size(CompressionLevel p_compression) {
	return data_buffer->read_uint_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::read_real_size(CompressionLevel p_compression) {
	return data_buffer->read_real_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::read_positive_unit_real_size(CompressionLevel p_compression) {
	return data_buffer->read_positive_unit_real_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::read_unit_real_size(CompressionLevel p_compression) {
	return data_buffer->read_unit_real_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::read_vector2_size(CompressionLevel p_compression) {
	return data_buffer->read_vector2_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::read_normalized_vector2_size(CompressionLevel p_compression) {
	return data_buffer->read_normalized_vector2_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::read_vector3_size(CompressionLevel p_compression) {
	return data_buffer->read_vector3_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::read_normalized_vector3_size(CompressionLevel p_compression) {
	return data_buffer->read_normalized_vector3_size((NS::DataBuffer::CompressionLevel)p_compression);
}

int GdDataBuffer::read_variant_size() {
	return data_buffer->read_buffer_size();
}

int GdDataBuffer::read_optional_variant_size(const Variant &p_def) {
	int len = get_bool_size();

	bool is_def = true;
	data_buffer->read(is_def);

	if (!is_def) {
		len += read_variant_size();
	}

	return len;
}
