#include "gd_network_interface.h"

#include "core/error/error_macros.h"
#include "core/math/aabb.h"
#include "core/math/basis.h"
#include "core/math/math_defs.h"
#include "core/math/projection.h"
#include "core/math/quaternion.h"
#include "core/math/rect2i.h"
#include "core/math/transform_2d.h"
#include "core/math/transform_3d.h"
#include "core/math/vector2.h"
#include "core/math/vector2i.h"
#include "core/math/vector3.h"
#include "core/math/vector3i.h"
#include "core/math/vector4i.h"
#include "core/object/callable_method_pointer.h"
#include "core/string/string_name.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "modules/network_synchronizer/core/var_data.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/node.h"
#include <cfloat>
#include <cstring>
#include <functional>
#include <memory>

GdNetworkInterface::GdNetworkInterface() {
}

GdNetworkInterface::~GdNetworkInterface() {
}

String GdNetworkInterface::get_name() const {
	return owner->get_path();
}

void GdNetworkInterface::start_listening_peer_connection(
		std::function<void(int /*p_peer*/)> p_on_peer_connected_callback,
		std::function<void(int /*p_peer*/)> p_on_peer_disconnected_callback) {
	on_peer_connected_callback = p_on_peer_connected_callback;
	on_peer_disconnected_callback = p_on_peer_disconnected_callback;

	if (!owner->get_multiplayer()->is_connected(SNAME("peer_connected"), callable_mp(this, &GdNetworkInterface::on_peer_connected))) {
		owner->get_multiplayer()->connect(SNAME("peer_connected"), callable_mp(this, &GdNetworkInterface::on_peer_connected));
		owner->get_multiplayer()->connect(SNAME("peer_disconnected"), callable_mp(this, &GdNetworkInterface::on_peer_disconnected));
	}
}

void GdNetworkInterface::on_peer_connected(int p_peer) {
	ERR_FAIL_COND_MSG(!on_peer_connected_callback, "The callback `on_peer_connected_callback` is not valid.");
	on_peer_connected_callback(p_peer);
}

void GdNetworkInterface::on_peer_disconnected(int p_peer) {
	ERR_FAIL_COND_MSG(!on_peer_disconnected_callback, "The callback `on_peer_connected_callback` is not valid.");
	on_peer_disconnected_callback(p_peer);
}

void GdNetworkInterface::stop_listening_peer_connection() {
	if (owner->get_multiplayer()->is_connected(SNAME("peer_connected"), callable_mp(this, &GdNetworkInterface::on_peer_connected))) {
		owner->get_multiplayer()->disconnect(SNAME("peer_connected"), callable_mp(this, &GdNetworkInterface::on_peer_connected));
		owner->get_multiplayer()->disconnect(SNAME("peer_disconnected"), callable_mp(this, &GdNetworkInterface::on_peer_disconnected));
	}
	on_peer_connected_callback = [](int) {};
	on_peer_disconnected_callback = [](int) {};
}

int GdNetworkInterface::fetch_local_peer_id() const {
	if (owner->get_multiplayer().is_valid()) {
		return owner->get_multiplayer()->get_unique_id();
	}
	return 0;
}

void GdNetworkInterface::fetch_connected_peers(std::vector<int> &p_connected_peers) const {
	p_connected_peers.clear();
	if (
			owner->get_tree() &&
			owner->get_tree()->get_multiplayer().is_valid()) {
		for (auto peer : owner->get_tree()->get_multiplayer()->get_peer_ids()) {
			p_connected_peers.push_back(peer);
		}
	}
}

int GdNetworkInterface::get_unit_authority() const {
	return owner->get_multiplayer_authority();
}

bool GdNetworkInterface::is_local_peer_networked() const {
	return owner->get_tree() &&
			owner->get_tree()->get_multiplayer()->get_multiplayer_peer()->get_class_name() != "OfflineMultiplayerPeer";
}

bool GdNetworkInterface::is_local_peer_server() const {
	if (is_local_peer_networked()) {
		return owner->get_tree()->get_multiplayer()->is_server();
	} else {
		return false;
	}
}

bool GdNetworkInterface::is_local_peer_authority_of_this_unit() const {
	return owner->is_multiplayer_authority();
}

void GdNetworkInterface::encode(DataBuffer &r_buffer, const NS::VarData &p_val) const {
	Variant vA;
	convert(vA, p_val);
	r_buffer.add_variant(vA);
}

void GdNetworkInterface::decode(NS::VarData &r_val, DataBuffer &p_buffer) const {
	Variant vA = p_buffer.read_variant();
	convert(r_val, vA);
}

#define CONVERT_VARDATA(CLAZZ, variant, vardata)           \
	CLAZZ v;                                               \
	std::memcpy((void *)&v, &vardata.data.ptr, sizeof(v)); \
	variant = v;

void GdNetworkInterface::convert(Variant &r_variant, const NS::VarData &p_vd) {
	const Variant::Type t = static_cast<Variant::Type>(p_vd.type);
	switch (t) {
		case Variant::NIL: {
			r_variant = Variant();
		} break;
		case Variant::BOOL: {
			CONVERT_VARDATA(bool, r_variant, p_vd);
		} break;
		case Variant::INT: {
			CONVERT_VARDATA(std::int64_t, r_variant, p_vd);
		} break;
		case Variant::FLOAT: {
			CONVERT_VARDATA(double, r_variant, p_vd);
		} break;
		case Variant::VECTOR2: {
			CONVERT_VARDATA(Vector2, r_variant, p_vd);
		} break;
		case Variant::VECTOR2I: {
			CONVERT_VARDATA(Vector2i, r_variant, p_vd);
		} break;
		case Variant::RECT2: {
			CONVERT_VARDATA(Rect2, r_variant, p_vd);
		} break;
		case Variant::RECT2I: {
			CONVERT_VARDATA(Rect2i, r_variant, p_vd);
		} break;
		case Variant::VECTOR3: {
			CONVERT_VARDATA(Vector3, r_variant, p_vd);
		} break;
		case Variant::VECTOR3I: {
			CONVERT_VARDATA(Vector3i, r_variant, p_vd);
		} break;
		case Variant::TRANSFORM2D: {
			CONVERT_VARDATA(Transform2D, r_variant, p_vd);
		} break;
		case Variant::VECTOR4: {
			CONVERT_VARDATA(Vector4, r_variant, p_vd);
		} break;
		case Variant::VECTOR4I: {
			CONVERT_VARDATA(Vector4i, r_variant, p_vd);
		} break;
		case Variant::PLANE: {
			CONVERT_VARDATA(Plane, r_variant, p_vd);
		} break;
		case Variant::QUATERNION: {
			CONVERT_VARDATA(Quaternion, r_variant, p_vd);
		} break;
		case Variant::AABB: {
			CONVERT_VARDATA(AABB, r_variant, p_vd);
		} break;
		case Variant::BASIS: {
			CONVERT_VARDATA(Basis, r_variant, p_vd);
		} break;
		case Variant::TRANSFORM3D: {
			CONVERT_VARDATA(Transform3D, r_variant, p_vd);
		} break;
		case Variant::PROJECTION: {
			CONVERT_VARDATA(Projection, r_variant, p_vd);
		} break;
		case Variant::COLOR: {
			CONVERT_VARDATA(Color, r_variant, p_vd);
		} break;

		case Variant::STRING_NAME:
		case Variant::NODE_PATH:
		case Variant::STRING:
		case Variant::DICTIONARY:
		case Variant::ARRAY:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY: {
			if (p_vd.shared_buffer) {
				r_variant = *std::static_pointer_cast<Variant>(p_vd.shared_buffer);
			}
		} break;

		default:
			ERR_PRINT("This VarDta can't be converted to a Variant. Type not supported: " + itos(p_vd.type));
			r_variant = Variant();
	}
}

#undef CONVERT_VARDATA
#define CONVERT_VARDATA(CLAZZ, variant, vardata) \
	const CLAZZ v = variant;                     \
	std::memcpy(&vardata.data.ptr, &v, sizeof(v));

void GdNetworkInterface::convert(NS::VarData &r_vd, const Variant &p_variant) {
	r_vd.type = static_cast<std::uint8_t>(p_variant.get_type());
	switch (p_variant.get_type()) {
		case Variant::NIL: {
			r_vd.data.ptr = nullptr;
		} break;
		case Variant::BOOL: {
			CONVERT_VARDATA(bool, p_variant, r_vd);
		} break;
		case Variant::INT: {
			CONVERT_VARDATA(std::int64_t, p_variant, r_vd);
		} break;
		case Variant::FLOAT: {
			CONVERT_VARDATA(double, p_variant, r_vd);
		} break;
		case Variant::VECTOR2: {
			CONVERT_VARDATA(Vector2, p_variant, r_vd);
		} break;
		case Variant::VECTOR2I: {
			CONVERT_VARDATA(Vector2i, p_variant, r_vd);
		} break;
		case Variant::RECT2: {
			CONVERT_VARDATA(Rect2, p_variant, r_vd);
		} break;
		case Variant::RECT2I: {
			CONVERT_VARDATA(Rect2i, p_variant, r_vd);
		} break;
		case Variant::VECTOR3: {
			CONVERT_VARDATA(Vector3, p_variant, r_vd);
		} break;
		case Variant::VECTOR3I: {
			CONVERT_VARDATA(Vector3i, p_variant, r_vd);
		} break;
		case Variant::TRANSFORM2D: {
			CONVERT_VARDATA(Transform2D, p_variant, r_vd);
		} break;
		case Variant::VECTOR4: {
			CONVERT_VARDATA(Vector4, p_variant, r_vd);
		} break;
		case Variant::VECTOR4I: {
			CONVERT_VARDATA(Vector4i, p_variant, r_vd);
		} break;
		case Variant::PLANE: {
			CONVERT_VARDATA(Plane, p_variant, r_vd);
		} break;
		case Variant::QUATERNION: {
			CONVERT_VARDATA(Quaternion, p_variant, r_vd);
		} break;
		case Variant::AABB: {
			CONVERT_VARDATA(AABB, p_variant, r_vd);
		} break;
		case Variant::BASIS: {
			CONVERT_VARDATA(Basis, p_variant, r_vd);
		} break;
		case Variant::TRANSFORM3D: {
			CONVERT_VARDATA(Transform3D, p_variant, r_vd);
		} break;
		case Variant::PROJECTION: {
			CONVERT_VARDATA(Projection, p_variant, r_vd);
		} break;
		case Variant::COLOR: {
			CONVERT_VARDATA(Color, p_variant, r_vd);
		} break;

		case Variant::STRING_NAME:
		case Variant::NODE_PATH:
		case Variant::STRING:
		case Variant::DICTIONARY:
		case Variant::ARRAY:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY: {
			r_vd.shared_buffer = std::make_shared<Variant>(p_variant.duplicate(true));
		} break;

		default:
			ERR_PRINT("This variant can't be converted: " + p_variant.stringify());
			r_vd.type = Variant::VARIANT_MAX;
	}
}

#undef CONVERT_VARDATA

bool GdNetworkInterface::compare(const NS::VarData &p_A, const NS::VarData &p_B) const {
	return compare_static(p_A, p_B);
}

bool GdNetworkInterface::compare_static(const NS::VarData &p_A, const NS::VarData &p_B) {
	Variant vA;
	Variant vB;
	convert(vA, p_A);
	convert(vB, p_B);
	return compare(vA, vB, FLT_EPSILON);
}

bool GdNetworkInterface::compare(const Variant &p_first, const Variant &p_second) const {
	return compare(p_first, p_second, FLT_EPSILON);
}

bool GdNetworkInterface::compare(const Vector2 &p_first, const Vector2 &p_second, real_t p_tolerance) {
	return Math::is_equal_approx(p_first.x, p_second.x, p_tolerance) &&
			Math::is_equal_approx(p_first.y, p_second.y, p_tolerance);
}

bool GdNetworkInterface::compare(const Vector3 &p_first, const Vector3 &p_second, real_t p_tolerance) {
	return Math::is_equal_approx(p_first.x, p_second.x, p_tolerance) &&
			Math::is_equal_approx(p_first.y, p_second.y, p_tolerance) &&
			Math::is_equal_approx(p_first.z, p_second.z, p_tolerance);
}

bool GdNetworkInterface::compare(const Variant &p_first, const Variant &p_second, real_t p_tolerance) {
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

void NS_GD_Test::test_var_data_conversin() {
	// Test Transform
	{
		Basis b;
		b.set_axis_angle(Vector3(1, 0, 0), 0.12);
		Vector3 o(1, 2, 3);

		Transform3D initial_transform(b, o);
		Variant variant = initial_transform;

		NS::VarData vd;
		GdNetworkInterface::convert(vd, variant);

		Variant final_variant;
		GdNetworkInterface::convert(final_variant, vd);

		Transform3D final_transform = final_variant;

		CRASH_COND(!final_transform.is_equal_approx(initial_transform));
	}

	// Test bool
	{
		Variant from = true;
		NS::VarData vd_from;
		GdNetworkInterface::convert(vd_from, from);

		Variant to;
		GdNetworkInterface::convert(to, vd_from);

		CRASH_COND(from != to);

		// Test compare.
		{
			Variant from2 = true;
			NS::VarData vd_from2;
			GdNetworkInterface::convert(vd_from2, from2);

			CRASH_COND(!GdNetworkInterface::compare_static(vd_from, vd_from2));
			NS::VarData vd;
			CRASH_COND(GdNetworkInterface::compare_static(vd_from, vd));
			CRASH_COND(GdNetworkInterface::compare_static(vd_from2, vd));
		}
	}

	// Test StringName
	{
		Variant from = StringName("GHUEIAiasfjasdfkadjfak");
		CRASH_COND(from.get_type() != Variant::STRING_NAME);
		NS::VarData vd_from;
		GdNetworkInterface::convert(vd_from, from);

		NS::VarData vd_from_copy;
		vd_from_copy.copy(vd_from);

		Variant to;
		GdNetworkInterface::convert(to, vd_from_copy);

		CRASH_COND(from != to);
		CRASH_COND(vd_from.shared_buffer != vd_from_copy.shared_buffer);
	}

	// Test NodePath
	{
		Variant from = NodePath("/root/asdf/fieae");
		CRASH_COND(from.get_type() != Variant::NODE_PATH);
		NS::VarData vd_from;
		GdNetworkInterface::convert(vd_from, from);

		NS::VarData vd_from_copy;
		vd_from_copy.copy(vd_from);

		Variant to;
		GdNetworkInterface::convert(to, vd_from_copy);

		CRASH_COND(from != to);
		CRASH_COND(vd_from.shared_buffer != vd_from_copy.shared_buffer);
	}

	// Test String
	{
		Variant from = "GHUEIAiasfjasdfkadjfak";
		CRASH_COND(from.get_type() != Variant::STRING);
		NS::VarData vd_from;
		GdNetworkInterface::convert(vd_from, from);

		NS::VarData vd_from_copy;
		vd_from_copy.copy(vd_from);

		Variant to;
		GdNetworkInterface::convert(to, vd_from_copy);

		CRASH_COND(from != to);
		CRASH_COND(vd_from.shared_buffer != vd_from_copy.shared_buffer);
	}

	// Test int vector
	{
		Vector<int32_t> integers;
		integers.push_back(1);
		integers.push_back(2);
		integers.push_back(3);
		Variant from = integers;
		CRASH_COND(from.get_type() != Variant::PACKED_INT32_ARRAY);
		NS::VarData vd_from;
		GdNetworkInterface::convert(vd_from, from);

		NS::VarData vd_from_copy;
		vd_from_copy.copy(vd_from);

		Variant to;
		GdNetworkInterface::convert(to, vd_from_copy);

		CRASH_COND(from != to);
		CRASH_COND(vd_from.shared_buffer != vd_from_copy.shared_buffer);
	}

	// Test Array
	{
		Dictionary dic;
		dic["Test"] = "www";

		Array arr;
		arr.push_back(1);
		arr.push_back(String("asdf"));
		arr.push_back(dic);

		Variant from = arr;
		CRASH_COND(from.get_type() != Variant::ARRAY);
		NS::VarData vd_from;
		GdNetworkInterface::convert(vd_from, from);

		NS::VarData vd_from_copy;
		vd_from_copy.copy(vd_from);

		Variant to;
		GdNetworkInterface::convert(to, vd_from_copy);

		CRASH_COND(from != to);
		CRASH_COND(vd_from.shared_buffer != vd_from_copy.shared_buffer);
	}

	// Test Dictionary
	{
		Array arr;
		arr.push_back(1);
		arr.push_back(String("asdf"));

		Dictionary dic;
		dic["Test"] = "www";
		dic["Arr"] = arr;

		Variant from = dic;
		CRASH_COND(from.get_type() != Variant::DICTIONARY);
		NS::VarData vd_from;
		GdNetworkInterface::convert(vd_from, from);

		NS::VarData vd_from_copy;
		vd_from_copy.copy(vd_from);

		Variant to;
		GdNetworkInterface::convert(to, vd_from_copy);

		CRASH_COND(from != to);
		CRASH_COND(vd_from.shared_buffer != vd_from_copy.shared_buffer);
		CRASH_COND(!GdNetworkInterface::compare_static(vd_from, vd_from_copy));
		{
			NS::VarData vd;
			CRASH_COND(GdNetworkInterface::compare_static(vd_from, vd));
		}
	}
}

void GdNetworkInterface::rpc_send(int p_peer_recipient, bool p_reliable, DataBuffer &&p_buffer) {
	const Vector<uint8_t> &buffer = p_buffer.get_buffer().get_bytes();

	if (p_reliable) {
		owner->rpc_id(p_peer_recipient, SNAME("_rpc_net_sync_reliable"), buffer);
	} else {
		owner->rpc_id(p_peer_recipient, SNAME("_rpc_net_sync_unreliable"), buffer);
	}
}

void GdNetworkInterface::gd_rpc_receive(const Vector<uint8_t> &p_buffer) {
	DataBuffer db(p_buffer);
	db.begin_read();
	rpc_receive(
			owner->get_multiplayer()->get_remote_sender_id(),
			db);
}
