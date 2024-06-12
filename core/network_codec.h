#pragma once

#include "modules/network_synchronizer/data_buffer.h"
#include "var_data.h"

NS_NAMESPACE_BEGIN

void encode_variable(bool val, DataBuffer &r_buffer);
void decode_variable(bool &val, DataBuffer &p_buffer);

void encode_variable(int val, DataBuffer &r_buffer);
void decode_variable(int &val, DataBuffer &p_buffer);

void encode_variable(float val, DataBuffer &r_buffer);
void decode_variable(float &val, DataBuffer &r_buffer);

void encode_variable(double val, DataBuffer &r_buffer);
void decode_variable(double &val, DataBuffer &p_buffer);

void encode_variable(const std::vector<std::uint8_t> &val, DataBuffer &r_buffer);
void decode_variable(std::vector<std::uint8_t> &val, DataBuffer &p_buffer);

void encode_variable(const DataBuffer &val, DataBuffer &r_buffer);
void decode_variable(DataBuffer &val, DataBuffer &p_buffer);

void encode_variable(const VarData &val, DataBuffer &r_buffer);
void decode_variable(VarData &val, DataBuffer &p_buffer);

template <int Index>
void encode_variables(DataBuffer &r_buffer) {}

template <int Index, typename A, typename... ARGS>
void encode_variables(DataBuffer &r_buffer, const A &p_a, const ARGS &...p_args) {
	encode_variable(p_a, r_buffer);
	encode_variables<Index + 1>(r_buffer, p_args...);
}

NS_NAMESPACE_END
