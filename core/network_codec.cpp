#include "network_codec.h"

#include "../scene_synchronizer.h"
#include "data_buffer.h"
#include "var_data.h"

NS_NAMESPACE_BEGIN
void encode_variable(bool val, DataBuffer &r_buffer) {
	r_buffer.add_bool(val);
}

void decode_variable(bool &val, DataBuffer &p_buffer) {
	val = p_buffer.read_bool();
}

void encode_variable(std::uint8_t val, DataBuffer &r_buffer) {
	r_buffer.add(val);
}

void decode_variable(std::uint8_t &val, DataBuffer &p_buffer) {
	p_buffer.read(val);
}

void encode_variable(std::uint16_t val, DataBuffer &r_buffer) {
	r_buffer.add(val);
}

void decode_variable(std::uint16_t &val, DataBuffer &p_buffer) {
	p_buffer.read(val);
}

void encode_variable(int val, DataBuffer &r_buffer) {
	// TODO optimize
	r_buffer.add_int(val, DataBuffer::COMPRESSION_LEVEL_0);
}

void decode_variable(int &val, DataBuffer &p_buffer) {
	// TODO optimize
	val = int(p_buffer.read_int(DataBuffer::COMPRESSION_LEVEL_0));
}

void encode_variable(ObjectNetId val, DataBuffer &r_buffer) {
	r_buffer.add(val.id);
}

void decode_variable(ObjectNetId &val, DataBuffer &p_buffer) {
	p_buffer.read(val.id);
}

void encode_variable(FrameIndex val, DataBuffer &r_buffer) {
	r_buffer.add(val.id);
}

void decode_variable(FrameIndex &val, DataBuffer &p_buffer) {
	p_buffer.read(val.id);
}

void encode_variable(GlobalFrameIndex val, DataBuffer &r_buffer) {
	r_buffer.add(val.id);
}

void decode_variable(GlobalFrameIndex &val, DataBuffer &p_buffer) {
	p_buffer.read(val.id);
}

void encode_variable(ScheduledProcedureId val, DataBuffer &r_buffer) {
	r_buffer.add(val.id);
}

void decode_variable(ScheduledProcedureId &val, DataBuffer &p_buffer) {
	p_buffer.read(val.id);
}

void encode_variable(float val, DataBuffer &r_buffer) {
	r_buffer.add_real(val, DataBuffer::COMPRESSION_LEVEL_1);
}

void decode_variable(float &val, DataBuffer &p_buffer) {
	p_buffer.read_real(val, DataBuffer::COMPRESSION_LEVEL_1);
}

void encode_variable(double val, DataBuffer &r_buffer) {
	r_buffer.add_real(val, DataBuffer::COMPRESSION_LEVEL_0);
}

void decode_variable(double &val, DataBuffer &p_buffer) {
	p_buffer.read_real(val, DataBuffer::COMPRESSION_LEVEL_0);
}

void encode_variable(const std::vector<std::uint8_t> &val, DataBuffer &r_buffer) {
	// TODO optimize?
	NS_ASSERT_COND(val.size() < 4294967295);
	r_buffer.add_uint(val.size(), DataBuffer::COMPRESSION_LEVEL_1);
	for (const auto v : val) {
		r_buffer.add_uint(v, DataBuffer::COMPRESSION_LEVEL_3);
	}
}

void decode_variable(std::vector<std::uint8_t> &val, DataBuffer &p_buffer) {
	// TODO optimize?
	const std::uint64_t size = p_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_1);
	val.resize(size);
	for (int i = 0; i < size; i++) {
		val[i] = std::uint8_t(p_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_3));
	}
}

void encode_variable(const DataBuffer &val, DataBuffer &r_buffer) {
	r_buffer.add(val);
}

void decode_variable(DataBuffer &val, DataBuffer &p_buffer) {
	p_buffer.read(val);
}

NS_NAMESPACE_END