#include "network_codec.h"

#include "modules/network_synchronizer/core/network_interface.h"
#include "var_data.h"
#include <vector>

NS_NAMESPACE_BEGIN

void encode_variable(bool val, DataBuffer &r_buffer, const NetworkInterface &p_interface) {
	r_buffer.add_bool(val);
}

void encode_variable(int val, DataBuffer &r_buffer, const NetworkInterface &p_interface) {
	// TODO optimize
	r_buffer.add_int(val, DataBuffer::COMPRESSION_LEVEL_0);
}

void encode_variable(float val, DataBuffer &r_buffer, const NetworkInterface &p_interface) {
	r_buffer.add_real(val, DataBuffer::COMPRESSION_LEVEL_1);
}

void encode_variable(double val, DataBuffer &r_buffer, const NetworkInterface &p_interface) {
	r_buffer.add_real(val, DataBuffer::COMPRESSION_LEVEL_0);
}

void encode_variable(const Vector<Variant> &val, DataBuffer &r_buffer, const NetworkInterface &p_interface) {
	CRASH_COND(val.size() >= 65000);
	r_buffer.add_uint(val.size(), DataBuffer::COMPRESSION_LEVEL_2);
	for (const auto &v : val) {
		r_buffer.add_variant(v);
	}
}

void encode_variable(const Vector<uint8_t> &val, DataBuffer &r_buffer, const NetworkInterface &p_interface) {
	// TODO optimize?
	CRASH_COND(val.size() >= 4294967295);
	r_buffer.add_uint(val.size(), DataBuffer::COMPRESSION_LEVEL_1);
	for (const auto v : val) {
		r_buffer.add_uint(v, DataBuffer::COMPRESSION_LEVEL_3);
	}
}

void encode_variable(const VarData &val, DataBuffer &r_buffer, const NetworkInterface &p_interface) {
	p_interface.encode(val, r_buffer);
}

NS_NAMESPACE_END
