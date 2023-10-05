#include "var_data.h"

#include <stdlib.h>
#include <utility>

NS_NAMESPACE_BEGIN

Buffer::Buffer(std::uint32_t p_size) {
	size = p_size;
	data = new std::uint8_t(size);
}

Buffer::~Buffer() {
	delete[] data;
}

VarData::VarData(double x, double y, double z, double w) {
	data.x = x;
	data.y = y;
	data.z = z;
	data.w = w;
}

VarData::VarData(VarData &&p_other) :
		data(std::move(p_other.data)),
		shared_buffer(std::move(p_other.shared_buffer)) {}

VarData &VarData::operator=(VarData &&p_other) {
	data = std::move(p_other.data);
	shared_buffer = std::move(p_other.shared_buffer);
	return *this;
}

void VarData::copy(const VarData &p_other) {
	data = p_other.data;
	shared_buffer = p_other.shared_buffer;
}

NS_NAMESPACE_END
