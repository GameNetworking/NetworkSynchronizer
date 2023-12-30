#include "input_network_encoder.h"

#include "modules/network_synchronizer/godot4/gd_network_interface.h"
#include "modules/network_synchronizer/godot4/gd_scene_synchronizer.h"
#include "scene_synchronizer.h"

void InputNetworkEncoder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_input", "name", "default_value", "type", "compression_level", "comparison_floating_point_precision"), &InputNetworkEncoder::register_input, DEFVAL(CMP_EPSILON));
	ClassDB::bind_method(D_METHOD("find_input_id", "name"), &InputNetworkEncoder::find_input_id);
	ClassDB::bind_method(D_METHOD("encode", "inputs", "buffer"), &InputNetworkEncoder::script_encode);
	ClassDB::bind_method(D_METHOD("decode", "buffer"), &InputNetworkEncoder::script_decode);
	ClassDB::bind_method(D_METHOD("get_defaults"), &InputNetworkEncoder::script_get_defaults);
	ClassDB::bind_method(D_METHOD("are_different", "buffer_a", "buffer_b"), &InputNetworkEncoder::script_are_different);
	ClassDB::bind_method(D_METHOD("count_size", "buffer"), &InputNetworkEncoder::script_count_size);
}

uint32_t InputNetworkEncoder::register_input(
		const StringName &p_name,
		const Variant &p_default_value,
		DataBuffer::DataType p_type,
		DataBuffer::CompressionLevel p_compression_level,
		float p_comparison_floating_point_precision) {
	switch (p_type) {
		case DataBuffer::DATA_TYPE_BOOL:
			ERR_FAIL_COND_V_MSG(p_default_value.get_type() != Variant::BOOL, UINT32_MAX, "The moveset initialization failed for" + p_name + " the specified data type is `BOOL` but the default parameter is " + itos(p_default_value.get_type()));
			break;
		case DataBuffer::DATA_TYPE_INT:
			ERR_FAIL_COND_V_MSG(p_default_value.get_type() != Variant::INT, UINT32_MAX, "The moveset initialization failed for" + p_name + " the specified data type is `INT` but the default parameter is " + itos(p_default_value.get_type()));
			break;
		case DataBuffer::DATA_TYPE_UINT:
			ERR_FAIL_COND_V_MSG(p_default_value.get_type() != Variant::INT, UINT32_MAX, "The moveset initialization failed for" + p_name + " the specified data type is `UINT` but the default parameter is " + itos(p_default_value.get_type()));
			break;
		case DataBuffer::DATA_TYPE_REAL:
			ERR_FAIL_COND_V_MSG(p_default_value.get_type() != Variant::FLOAT, UINT32_MAX, "The moveset initialization failed for" + p_name + " the specified data type is `REAL` but the default parameter is " + itos(p_default_value.get_type()));
			break;
		case DataBuffer::DATA_TYPE_POSITIVE_UNIT_REAL:
			ERR_FAIL_COND_V_MSG(p_default_value.get_type() != Variant::FLOAT, UINT32_MAX, "The moveset initialization failed for" + p_name + " the specified data type is `REAL` but the default parameter is " + itos(p_default_value.get_type()));
			break;
		case DataBuffer::DATA_TYPE_UNIT_REAL:
			ERR_FAIL_COND_V_MSG(p_default_value.get_type() != Variant::FLOAT, UINT32_MAX, "The moveset initialization failed for" + p_name + " the specified data type is `REAL` but the default parameter is " + itos(p_default_value.get_type()));
			break;
		case DataBuffer::DATA_TYPE_VECTOR2:
			ERR_FAIL_COND_V_MSG(p_default_value.get_type() != Variant::VECTOR2, UINT32_MAX, "The moveset initialization failed for" + p_name + " the specified data type is `Vector2` but the default parameter is " + itos(p_default_value.get_type()));
			break;
		case DataBuffer::DATA_TYPE_NORMALIZED_VECTOR2:
			ERR_FAIL_COND_V_MSG(p_default_value.get_type() != Variant::VECTOR2, UINT32_MAX, "The moveset initialization failed for" + p_name + " the specified data type is `Vector2` but the default parameter is " + itos(p_default_value.get_type()));
			break;
		case DataBuffer::DATA_TYPE_VECTOR3:
			ERR_FAIL_COND_V_MSG(p_default_value.get_type() != Variant::VECTOR3, UINT32_MAX, "The moveset initialization failed for" + p_name + " the specified data type is `Vector3` but the default parameter is " + itos(p_default_value.get_type()));
			break;
		case DataBuffer::DATA_TYPE_NORMALIZED_VECTOR3:
			ERR_FAIL_COND_V_MSG(p_default_value.get_type() != Variant::VECTOR3, UINT32_MAX, "The moveset initialization failed for" + p_name + " the specified data type is `Vector3` but the default parameter is " + itos(p_default_value.get_type()));
			break;
		case DataBuffer::DATA_TYPE_BITS:
			CRASH_NOW_MSG("NOT SUPPORTED.");
			break;
		case DataBuffer::DATA_TYPE_VARIANT:
			/* No need to check variant, anything is accepted at this point.*/
			break;
	};

	const uint32_t index = input_info.size();

	input_info.resize(input_info.size() + 1);
	input_info[index].name = p_name;
	input_info[index].default_value = p_default_value;
	input_info[index].data_type = p_type;
	input_info[index].compression_level = p_compression_level;
	input_info[index].comparison_floating_point_precision = p_comparison_floating_point_precision;

	return index;
}

uint32_t InputNetworkEncoder::find_input_id(const StringName &p_name) const {
	for (uint32_t i = 0; i < input_info.size(); i += 1) {
		if (input_info[i].name == p_name) {
			return i;
		}
	}
	return INDEX_NONE;
}

const LocalVector<NetworkedInputInfo> &InputNetworkEncoder::get_input_info() const {
	return input_info;
}

void InputNetworkEncoder::encode(const LocalVector<Variant> &p_input, DataBuffer &r_buffer) const {
	for (uint32_t i = 0; i < input_info.size(); i += 1) {
		const NetworkedInputInfo &info = input_info[i];

#ifdef DEBUG_ENABLED
		if (i < p_input.size() && info.default_value.get_type() != p_input[i].get_type() && p_input[i].get_type() != Variant::NIL) {
			NET_DEBUG_ERR("During the input encoding the passed value `" + p_input[i].stringify() + "` has a different type to the expected one. Using the default value `" + info.default_value.stringify() + "`.");
		}
#endif

		const bool is_default =
				// If the input exist into the array.
				i >= p_input.size() ||
				// Use default if the variable type is different.
				info.default_value.get_type() != p_input[i].get_type() ||
				// Use default if the variable value is equal to default.
				info.default_value == p_input[i];

		if (info.default_value.get_type() != Variant::BOOL) {
			r_buffer.add_bool(is_default);

			if (!is_default) {
				const Variant &pending_input = p_input[i];
				switch (info.data_type) {
					case DataBuffer::DATA_TYPE_BOOL:
						CRASH_NOW_MSG("Boolean are handled differently. Thanks to the above IF this condition never occurs.");
						break;
					case DataBuffer::DATA_TYPE_UINT:
						r_buffer.add_uint(pending_input.operator unsigned int(), info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_INT:
						r_buffer.add_int(pending_input.operator int(), info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_REAL:
						r_buffer.add_real(pending_input.operator real_t(), info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_POSITIVE_UNIT_REAL:
						r_buffer.add_positive_unit_real(pending_input.operator real_t(), info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_UNIT_REAL:
						r_buffer.add_unit_real(pending_input.operator real_t(), info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_VECTOR2:
						_ALLOW_DISCARD_ r_buffer.add_vector2(pending_input.operator Vector2(), info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_NORMALIZED_VECTOR2:
						_ALLOW_DISCARD_ r_buffer.add_normalized_vector2(pending_input.operator Vector2(), info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_VECTOR3:
						_ALLOW_DISCARD_ r_buffer.add_vector3(pending_input.operator Vector3(), info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_NORMALIZED_VECTOR3:
						_ALLOW_DISCARD_ r_buffer.add_normalized_vector3(pending_input.operator Vector3(), info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_BITS:
						CRASH_NOW_MSG("Not supported.");
						break;
					case DataBuffer::DATA_TYPE_VARIANT:
						_ALLOW_DISCARD_ r_buffer.add_variant(pending_input);
						break;
				};
			}
		} else {
			// If the data is bool no need to set the default.
			if (!is_default) {
				r_buffer.add_bool(p_input[i].operator bool());
			} else {
				r_buffer.add_bool(info.default_value.operator bool());
			}
		}
	}
}

void InputNetworkEncoder::decode(DataBuffer &p_buffer, LocalVector<Variant> &r_inputs) const {
	if (r_inputs.size() < input_info.size()) {
		r_inputs.resize(input_info.size());
	}

	for (uint32_t i = 0; i < input_info.size(); i += 1) {
		const NetworkedInputInfo &info = input_info[i];

		const bool is_bool = info.default_value.get_type() == Variant::BOOL;

		bool is_default = false;
		if (is_bool == false) {
			is_default = p_buffer.read_bool();
		}

		if (is_default) {
			r_inputs[i] = info.default_value;
		} else {
			switch (info.data_type) {
				case DataBuffer::DATA_TYPE_BOOL:
					r_inputs[i] = p_buffer.read_bool();
					break;
				case DataBuffer::DATA_TYPE_UINT:
					r_inputs[i] = p_buffer.read_uint(info.compression_level);
					break;
				case DataBuffer::DATA_TYPE_INT:
					r_inputs[i] = p_buffer.read_int(info.compression_level);
					break;
				case DataBuffer::DATA_TYPE_REAL:
					r_inputs[i] = p_buffer.read_real(info.compression_level);
					break;
				case DataBuffer::DATA_TYPE_POSITIVE_UNIT_REAL:
					r_inputs[i] = p_buffer.read_positive_unit_real(info.compression_level);
					break;
				case DataBuffer::DATA_TYPE_UNIT_REAL:
					r_inputs[i] = p_buffer.read_unit_real(info.compression_level);
					break;
				case DataBuffer::DATA_TYPE_VECTOR2:
					r_inputs[i] = p_buffer.read_vector2(info.compression_level);
					break;
				case DataBuffer::DATA_TYPE_NORMALIZED_VECTOR2:
					r_inputs[i] = p_buffer.read_normalized_vector2(info.compression_level);
					break;
				case DataBuffer::DATA_TYPE_VECTOR3:
					r_inputs[i] = p_buffer.read_vector3(info.compression_level);
					break;
				case DataBuffer::DATA_TYPE_NORMALIZED_VECTOR3:
					r_inputs[i] = p_buffer.read_normalized_vector3(info.compression_level);
					break;
				case DataBuffer::DATA_TYPE_BITS:
					CRASH_NOW_MSG("Not supported.");
					break;
				case DataBuffer::DATA_TYPE_VARIANT:
					r_inputs[i] = p_buffer.read_variant();
					break;
			};
		}
	}
}

void InputNetworkEncoder::reset_inputs_to_defaults(LocalVector<Variant> &r_input) const {
	const uint32_t size = r_input.size() < input_info.size() ? r_input.size() : input_info.size();

	for (uint32_t i = 0; i < size; i += 1) {
		r_input[i] = input_info[i].default_value;
	}
}

bool compare(const Vector2 &p_first, const Vector2 &p_second, float p_tolerance) {
	return Math::is_equal_approx(p_first.x, p_second.x, p_tolerance) &&
			Math::is_equal_approx(p_first.y, p_second.y, p_tolerance);
}

bool compare(const Vector3 &p_first, const Vector3 &p_second, float p_tolerance) {
	return Math::is_equal_approx(p_first.x, p_second.x, p_tolerance) &&
			Math::is_equal_approx(p_first.y, p_second.y, p_tolerance) &&
			Math::is_equal_approx(p_first.z, p_second.z, p_tolerance);
}

bool compare(const Variant &p_first, const Variant &p_second, float p_tolerance) {
	if (p_first.get_type() != p_second.get_type()) {
		return false;
	}

	// Custom evaluation methods
	switch (p_first.get_type()) {
		case Variant::FLOAT: {
			return Math::is_equal_approx(p_first, p_second, p_tolerance);
		}
		case Variant::VECTOR2: {
			return compare(Vector2(p_first), Vector2(p_second), p_tolerance);
		}
		case Variant::RECT2: {
			const Rect2 a(p_first);
			const Rect2 b(p_second);
			if (compare(a.position, b.position, p_tolerance)) {
				if (compare(a.size, b.size, p_tolerance)) {
					return true;
				}
			}
			return false;
		}
		case Variant::TRANSFORM2D: {
			const Transform2D a(p_first);
			const Transform2D b(p_second);
			if (compare(a.columns[0], b.columns[0], p_tolerance)) {
				if (compare(a.columns[1], b.columns[1], p_tolerance)) {
					if (compare(a.columns[2], b.columns[2], p_tolerance)) {
						return true;
					}
				}
			}
			return false;
		}
		case Variant::VECTOR3: {
			return compare(Vector3(p_first), Vector3(p_second), p_tolerance);
		}
		case Variant::QUATERNION: {
			const Quaternion a = p_first;
			const Quaternion b = p_second;
			const Quaternion r(a - b); // Element wise subtraction.
			return (r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w) <= (p_tolerance * p_tolerance);
		}
		case Variant::PLANE: {
			const Plane a(p_first);
			const Plane b(p_second);
			if (Math::is_equal_approx(a.d, b.d, p_tolerance)) {
				if (compare(a.normal, b.normal, p_tolerance)) {
					return true;
				}
			}
			return false;
		}
		case Variant::AABB: {
			const AABB a(p_first);
			const AABB b(p_second);
			if (compare(a.position, b.position, p_tolerance)) {
				if (compare(a.size, b.size, p_tolerance)) {
					return true;
				}
			}
			return false;
		}
		case Variant::BASIS: {
			const Basis a = p_first;
			const Basis b = p_second;
			if (compare(a.rows[0], b.rows[0], p_tolerance)) {
				if (compare(a.rows[1], b.rows[1], p_tolerance)) {
					if (compare(a.rows[2], b.rows[2], p_tolerance)) {
						return true;
					}
				}
			}
			return false;
		}
		case Variant::TRANSFORM3D: {
			const Transform3D a = p_first;
			const Transform3D b = p_second;
			if (compare(a.origin, b.origin, p_tolerance)) {
				if (compare(a.basis.rows[0], b.basis.rows[0], p_tolerance)) {
					if (compare(a.basis.rows[1], b.basis.rows[1], p_tolerance)) {
						if (compare(a.basis.rows[2], b.basis.rows[2], p_tolerance)) {
							return true;
						}
					}
				}
			}
			return false;
		}
		case Variant::ARRAY: {
			const Array a = p_first;
			const Array b = p_second;
			if (a.size() != b.size()) {
				return false;
			}
			for (int i = 0; i < a.size(); i += 1) {
				if (compare(a[i], b[i], p_tolerance) == false) {
					return false;
				}
			}
			return true;
		}
		case Variant::DICTIONARY: {
			const Dictionary a = p_first;
			const Dictionary b = p_second;

			if (a.size() != b.size()) {
				return false;
			}

			List<Variant> l;
			a.get_key_list(&l);

			for (const List<Variant>::Element *key = l.front(); key; key = key->next()) {
				if (b.has(key->get()) == false) {
					return false;
				}

				if (compare(
							a.get(key->get(), Variant()),
							b.get(key->get(), Variant()),
							p_tolerance) == false) {
					return false;
				}
			}

			return true;
		}
		default:
			return p_first == p_second;
	}
}

bool compare(const Variant &p_first, const Variant &p_second) {
	return compare(p_first, p_second, FLT_EPSILON);
}

bool InputNetworkEncoder::are_different(DataBuffer &p_buffer_A, DataBuffer &p_buffer_B) const {
	for (uint32_t i = 0; i < input_info.size(); i += 1) {
		const NetworkedInputInfo &info = input_info[i];

		const bool is_bool = info.default_value.get_type() == Variant::BOOL;

		bool is_default_A = false;
		bool is_default_B = false;
		if (is_bool == false) {
			is_default_A = p_buffer_A.read_bool();
			is_default_B = p_buffer_B.read_bool();
		}

		bool are_equals = true;
		if (is_default_A && is_default_B) {
			are_equals = true;
		} else {
			switch (info.data_type) {
				case DataBuffer::DATA_TYPE_BOOL:
					are_equals = p_buffer_A.read_bool() == p_buffer_B.read_bool();
					break;
				case DataBuffer::DATA_TYPE_UINT:
					are_equals = Math::is_equal_approx(p_buffer_A.read_uint(info.compression_level), p_buffer_B.read_uint(info.compression_level), info.comparison_floating_point_precision);
					break;
				case DataBuffer::DATA_TYPE_INT:
					are_equals = Math::is_equal_approx(p_buffer_A.read_int(info.compression_level), p_buffer_B.read_int(info.compression_level), info.comparison_floating_point_precision);
					break;
				case DataBuffer::DATA_TYPE_REAL:
					are_equals = Math::is_equal_approx(static_cast<real_t>(p_buffer_A.read_real(info.compression_level)), static_cast<real_t>(p_buffer_B.read_real(info.compression_level)), info.comparison_floating_point_precision);
					break;
				case DataBuffer::DATA_TYPE_POSITIVE_UNIT_REAL:
					are_equals = Math::is_equal_approx(p_buffer_A.read_positive_unit_real(info.compression_level), p_buffer_B.read_positive_unit_real(info.compression_level), info.comparison_floating_point_precision);
					break;
				case DataBuffer::DATA_TYPE_UNIT_REAL:
					are_equals = Math::is_equal_approx(p_buffer_A.read_unit_real(info.compression_level), p_buffer_B.read_unit_real(info.compression_level), info.comparison_floating_point_precision);
					break;
				case DataBuffer::DATA_TYPE_VECTOR2:
					are_equals = compare(p_buffer_A.read_vector2(info.compression_level), p_buffer_B.read_vector2(info.compression_level), info.comparison_floating_point_precision);
					break;
				case DataBuffer::DATA_TYPE_NORMALIZED_VECTOR2:
					are_equals = compare(p_buffer_A.read_normalized_vector2(info.compression_level), p_buffer_B.read_normalized_vector2(info.compression_level), info.comparison_floating_point_precision);
					break;
				case DataBuffer::DATA_TYPE_VECTOR3:
					are_equals = compare(p_buffer_A.read_vector3(info.compression_level), p_buffer_B.read_vector3(info.compression_level), info.comparison_floating_point_precision);
					break;
				case DataBuffer::DATA_TYPE_NORMALIZED_VECTOR3:
					are_equals = compare(p_buffer_A.read_normalized_vector3(info.compression_level), p_buffer_B.read_normalized_vector3(info.compression_level), info.comparison_floating_point_precision);
					break;
				case DataBuffer::DATA_TYPE_BITS:
					CRASH_NOW_MSG("Not supported.");
					break;
				case DataBuffer::DATA_TYPE_VARIANT:
					are_equals = compare(p_buffer_A.read_variant(), p_buffer_B.read_variant(), info.comparison_floating_point_precision);
					break;
			};
		}

		if (!are_equals) {
			return true;
		}
	}

	return false;
}

uint32_t InputNetworkEncoder::count_size(DataBuffer &p_buffer) const {
	int size = 0;
	for (uint32_t i = 0; i < input_info.size(); i += 1) {
		const NetworkedInputInfo &info = input_info[i];

		const bool is_bool = info.default_value.get_type() == Variant::BOOL;
		if (is_bool) {
			// The bool data.
			size += p_buffer.read_bool_size();
		} else {
			// The default marker
			const bool is_default = p_buffer.read_bool();
			size += p_buffer.get_bool_size();

			if (is_default == false) {
				// Non default data set the actual data, so we need to count
				// the size.
				switch (info.data_type) {
					case DataBuffer::DATA_TYPE_BOOL:
						CRASH_NOW_MSG("This can't ever happen, as the bool is already handled.");
						break;
					case DataBuffer::DATA_TYPE_UINT:
						size += p_buffer.read_uint_size(info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_INT:
						size += p_buffer.read_int_size(info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_REAL:
						size += p_buffer.read_real_size(info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_POSITIVE_UNIT_REAL:
						size += p_buffer.read_positive_unit_real_size(info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_UNIT_REAL:
						size += p_buffer.read_unit_real_size(info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_VECTOR2:
						size += p_buffer.read_vector2_size(info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_NORMALIZED_VECTOR2:
						size += p_buffer.read_normalized_vector2_size(info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_VECTOR3:
						size += p_buffer.read_vector3_size(info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_NORMALIZED_VECTOR3:
						size += p_buffer.read_normalized_vector3_size(info.compression_level);
						break;
					case DataBuffer::DATA_TYPE_BITS:
						CRASH_NOW_MSG("Not supported.");
						break;
					case DataBuffer::DATA_TYPE_VARIANT:
						size += p_buffer.read_variant_size();
						break;
				};
			}
		}
	}

	return size;
}

void InputNetworkEncoder::script_encode(const Array &p_inputs, Object *r_buffer) const {
	ERR_FAIL_COND(r_buffer == nullptr);
	DataBuffer *db = Object::cast_to<DataBuffer>(r_buffer);
	ERR_FAIL_COND(db == nullptr);

	LocalVector<Variant> inputs;
	inputs.resize(p_inputs.size());
	for (int i = 0; i < p_inputs.size(); i += 1) {
		inputs[i] = p_inputs[i];
	}

	encode(inputs, *db);
}

Array InputNetworkEncoder::script_decode(Object *p_buffer) const {
	ERR_FAIL_COND_V(p_buffer == nullptr, Array());
	DataBuffer *db = Object::cast_to<DataBuffer>(p_buffer);
	ERR_FAIL_COND_V(db == nullptr, Array());

	LocalVector<Variant> inputs;
	decode(*db, inputs);

	Array out;
	out.resize(inputs.size());
	for (uint32_t i = 0; i < inputs.size(); i += 1) {
		out[i] = inputs[i];
	}
	return out;
}

Array InputNetworkEncoder::script_get_defaults() const {
	LocalVector<Variant> inputs;
	inputs.resize(input_info.size());

	reset_inputs_to_defaults(inputs);

	Array out;
	out.resize(inputs.size());
	for (uint32_t i = 0; i < inputs.size(); i += 1) {
		out[i] = inputs[i];
	}
	return out;
}

bool InputNetworkEncoder::script_are_different(Object *p_buffer_A, Object *p_buffer_B) const {
	ERR_FAIL_COND_V(p_buffer_A == nullptr, true);
	DataBuffer *db_A = Object::cast_to<DataBuffer>(p_buffer_A);
	ERR_FAIL_COND_V(db_A == nullptr, false);

	ERR_FAIL_COND_V(p_buffer_B == nullptr, true);
	DataBuffer *db_B = Object::cast_to<DataBuffer>(p_buffer_B);
	ERR_FAIL_COND_V(db_B == nullptr, true);

	return are_different(*db_A, *db_B);
}

uint32_t InputNetworkEncoder::script_count_size(Object *p_buffer) const {
	ERR_FAIL_COND_V(p_buffer == nullptr, 0);
	DataBuffer *db = Object::cast_to<DataBuffer>(p_buffer);
	ERR_FAIL_COND_V(db == nullptr, 0);

	return count_size(*db);
}
