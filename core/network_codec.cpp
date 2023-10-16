#include "network_codec.h"

#include "modules/network_synchronizer/core/network_interface.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include "var_data.h"
#include <vector>

NS_NAMESPACE_BEGIN

void encode_variable(bool val, DataBuffer &r_buffer) {
	r_buffer.add_bool(val);
}

void decode_variable(bool &val, DataBuffer &p_buffer) {
	val = p_buffer.read_bool();
}

void encode_variable(int val, DataBuffer &r_buffer) {
	// TODO optimize
	r_buffer.add_int(val, DataBuffer::COMPRESSION_LEVEL_0);
}

void decode_variable(int &val, DataBuffer &p_buffer) {
	// TODO optimize
	val = p_buffer.read_int(DataBuffer::COMPRESSION_LEVEL_0);
}

void encode_variable(float val, DataBuffer &r_buffer) {
	r_buffer.add_real(val, DataBuffer::COMPRESSION_LEVEL_1);
}

void decode_variable(float &val, DataBuffer &p_buffer) {
	val = p_buffer.read_real(DataBuffer::COMPRESSION_LEVEL_1);
}

void encode_variable(double val, DataBuffer &r_buffer) {
	r_buffer.add_real(val, DataBuffer::COMPRESSION_LEVEL_0);
}

void decode_variable(double &val, DataBuffer &p_buffer) {
	val = p_buffer.read_real(DataBuffer::COMPRESSION_LEVEL_0);
}

void encode_variable(const Variant &val, DataBuffer &r_buffer) {
	r_buffer.add_variant(val);
}

void decode_variable(Variant &val, DataBuffer &p_buffer) {
	val = p_buffer.read_variant();
}

void encode_variable(const Vector<uint8_t> &val, DataBuffer &r_buffer) {
	// TODO optimize?
	CRASH_COND(val.size() >= 4294967295);
	r_buffer.add_uint(val.size(), DataBuffer::COMPRESSION_LEVEL_1);
	for (const auto v : val) {
		r_buffer.add_uint(v, DataBuffer::COMPRESSION_LEVEL_3);
	}
}

void decode_variable(Vector<uint8_t> &val, DataBuffer &p_buffer) {
	// TODO optimize?
	const int size = p_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_1);
	val.resize(size);
	for (int i = 0; i < size; i++) {
		val.write[i] = p_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_3);
	}
}

void encode_variable(const DataBuffer &val, DataBuffer &r_buffer) {
	r_buffer.add(val);
}

void decode_variable(DataBuffer &val, DataBuffer &p_buffer) {
	p_buffer.read(val);
}

void encode_variable(const VarData &val, DataBuffer &r_buffer) {
	SceneSynchronizerBase::var_data_encode(r_buffer, val);
}

void decode_variable(VarData &val, DataBuffer &p_buffer) {
	SceneSynchronizerBase::var_data_decode(val, p_buffer);
}

NS_NAMESPACE_END
