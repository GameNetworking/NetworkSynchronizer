#include "var_data.h"

#include <string.h> // Needed to include `memset` in linux.

NS_NAMESPACE_BEGIN
VarData::VarData() {
	type = 0;
	memset(&data, 0, sizeof(data));
}

VarData::VarData(float x, float y, float z, float w) :
	VarData() {
	data.vec_f32.x = x;
	data.vec_f32.y = y;
	data.vec_f32.z = z;
	data.vec_f32.w = w;
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
	shared_buffer(std::move(p_other.shared_buffer)) {
}

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