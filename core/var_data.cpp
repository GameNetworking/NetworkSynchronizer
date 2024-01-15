#include "var_data.h"

#include <stdlib.h>
#include <string.h> // Needed to include `memset` in linux.
#include <utility>

NS_NAMESPACE_BEGIN

VarData::VarData() {
	memset(&data, 0, sizeof(data));
}

VarData::VarData(double x, double y, double z, double w) :
		VarData() {
	data.vec.x = x;
	data.vec.y = y;
	data.vec.z = z;
	data.vec.w = w;
}

VarData::VarData(VarData &&p_other) :
		type(std::move(p_other.type)),
		data(std::move(p_other.data)),
		shared_buffer(std::move(p_other.shared_buffer)) {}

VarData &VarData::operator=(VarData &&p_other) {
	type = std::move(p_other.type);
	data = std::move(p_other.data);
	shared_buffer = std::move(p_other.shared_buffer);
	return *this;
}

VarData VarData::make_copy(const VarData &p_other) {
	VarData vd;
	vd.copy(p_other);
	return vd;
}

void VarData::copy(const VarData &p_other) {
	type = p_other.type;
	data = p_other.data;
	shared_buffer = p_other.shared_buffer;
}

NS_NAMESPACE_END
