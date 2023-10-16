#include "var_data.h"

#include <stdlib.h>
#include <utility>

NS_NAMESPACE_BEGIN

VarData::VarData(double x, double y, double z, double w) {
	data.x = x;
	data.y = y;
	data.z = z;
	data.w = w;
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
