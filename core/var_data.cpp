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

VarData::VarData(VarData &&p_other) :
		data(std::move(p_other.data)) {}

VarData &VarData::operator=(VarData &&p_other) {
	data = std::move(p_other.data);
}

void VarData::copy(const VarData &p_other) {
	data = p_other.data;
	shared_buffer = p_other.shared_buffer;
}

NS_NAMESPACE_END
