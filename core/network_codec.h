#pragma once

#include "core.h"
#include "core/variant/variant.h"
#include "modules/network_synchronizer/data_buffer.h"
#include "var_data.h"

NS_NAMESPACE_BEGIN

void encode_variable(bool val, DataBuffer &r_buffer, const class NetworkInterface &p_interface);
void decode_variable(bool &val, DataBuffer &p_buffer, const class NetworkInterface &p_interface);

void encode_variable(int val, DataBuffer &r_buffer, const NetworkInterface &p_interface);
void decode_variable(int &val, DataBuffer &p_buffer, const NetworkInterface &p_interface);

void encode_variable(float val, DataBuffer &r_buffer, const NetworkInterface &p_interface);
void decode_variable(float &val, DataBuffer &r_buffer, const NetworkInterface &p_interface);

void encode_variable(double val, DataBuffer &r_buffer, const NetworkInterface &p_interface);
void decode_variable(double &val, DataBuffer &p_buffer, const NetworkInterface &p_interface);

void encode_variable(const Vector<Variant> &val, DataBuffer &r_buffer, const NetworkInterface &p_interface);
void decode_variable(Vector<Variant> &val, DataBuffer &p_buffer, const NetworkInterface &p_interface);

void encode_variable(const Variant &val, DataBuffer &r_buffer, const NetworkInterface &p_interface);
void decode_variable(Variant &val, DataBuffer &p_buffer, const NetworkInterface &p_interface);

void encode_variable(const Vector<uint8_t> &val, DataBuffer &r_buffer, const NetworkInterface &p_interface);
void decode_variable(Vector<uint8_t> &val, DataBuffer &p_buffer, const NetworkInterface &p_interface);

void encode_variable(const VarData &val, DataBuffer &r_buffer, const NetworkInterface &p_interface);
void decode_variable(VarData &val, DataBuffer &p_buffer, const NetworkInterface &p_interface);

template <int Index>
void encode_variables(const NetworkInterface &p_interface, DataBuffer &r_buffer) {}

template <int Index, typename A, typename... ARGS>
void encode_variables(const NetworkInterface &p_interface, DataBuffer &r_buffer, const A &p_a, const ARGS &...p_args) {
	encode_variable(p_a, r_buffer, p_interface);
	encode_variables<Index + 1>(p_interface, r_buffer, p_args...);
}

NS_NAMESPACE_END
