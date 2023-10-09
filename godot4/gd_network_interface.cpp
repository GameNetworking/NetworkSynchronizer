#include "gd_network_interface.h"

#include "core/error/error_macros.h"
#include "core/math/basis.h"
#include "core/math/math_defs.h"
#include "core/math/projection.h"
#include "core/math/quaternion.h"
#include "core/math/transform_2d.h"
#include "core/math/transform_3d.h"
#include "core/math/vector2.h"
#include "core/math/vector3.h"
#include "core/object/callable_method_pointer.h"
#include "core/variant/callable.h"
#include "core/variant/variant.h"
#include "modules/network_synchronizer/core/var_data.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/node.h"
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
	// TODO
	CRASH_NOW();
}

void GdNetworkInterface::decode(NS::VarData &r_val, DataBuffer &p_buffer) const {
	// TODO
	CRASH_NOW();
}

void GdNetworkInterface::convert(Variant &r_variant, const NS::VarData &p_vd) {
	const Variant::Type t = static_cast<Variant::Type>(p_vd.type);
	switch (t) {
		case Variant::BOOL:
			break;
		case Variant::INT:
			break;
		case Variant::FLOAT:
			break;

		case Variant::VECTOR2: {
		} break;
		case Variant::VECTOR2I: {
		} break;
		case Variant::RECT2: {
		} break;
		case Variant::RECT2I: {
		} break;
		case Variant::VECTOR3: {
		} break;
		case Variant::VECTOR3I: {
		} break;
		case Variant::TRANSFORM2D: {
		} break;
		case Variant::VECTOR4: {
		} break;
		case Variant::VECTOR4I: {
		} break;
		case Variant::PLANE: {
		} break;
		case Variant::QUATERNION: {
		} break;
		case Variant::AABB: {
		} break;
		case Variant::BASIS: {
		} break;
		case Variant::TRANSFORM3D: {
			Transform3D v;
			std::memcpy((void *)&v, &p_vd.data.ptr, sizeof(v));
			r_variant = v;
		} break;
		case Variant::PROJECTION: {
		} break;

		case Variant::STRING: {
		} break;

		default:
			ERR_PRINT("This VarData can't be converted. Type: " + itos(p_vd.type));
			r_variant = Variant();
	}
}

void GdNetworkInterface::convert(NS::VarData &r_vd, const Variant &p_variant) {
	r_vd.type = static_cast<std::uint8_t>(p_variant.get_type());
	switch (p_variant.get_type()) {
		case Variant::BOOL:
			r_vd.data.boolean = p_variant;
			break;
		case Variant::INT:
			r_vd.data.i64 = p_variant;
			break;
		case Variant::FLOAT:
			r_vd.data.f64 = p_variant;
			break;

		case Variant::VECTOR2: {
			const Vector2 v2 = p_variant;
			r_vd.data.x = v2.x;
			r_vd.data.y = v2.y;
			break;
		}
		case Variant::VECTOR2I: {
			const Vector2i v2i = p_variant;
			r_vd.data.x = v2i.x;
			r_vd.data.y = v2i.y;
			break;
		}
		case Variant::RECT2: {
			const Rect2 v = p_variant;
			r_vd.data.x = v.position.x;
			r_vd.data.y = v.position.y;
			r_vd.data.z = v.size.x;
			r_vd.data.w = v.size.y;
			break;
		}
		case Variant::RECT2I: {
			const Rect2i v = p_variant;
			r_vd.data.x = v.position.x;
			r_vd.data.y = v.position.y;
			r_vd.data.z = v.size.x;
			r_vd.data.w = v.size.y;
			break;
		}
		case Variant::VECTOR3: {
			const Vector3 v = p_variant;
			r_vd.data.x = v.x;
			r_vd.data.y = v.y;
			r_vd.data.z = v.z;
			break;
		}
		case Variant::VECTOR3I: {
			const Vector3i v = p_variant;
			r_vd.data.x = v.x;
			r_vd.data.y = v.y;
			r_vd.data.z = v.z;
			break;
		}
		case Variant::TRANSFORM2D: {
			const Transform2D v = p_variant;
			r_vd.data.columns[0].x = v.columns[0].x;
			r_vd.data.columns[0].y = v.columns[0].y;
			r_vd.data.columns[1].x = v.columns[1].x;
			r_vd.data.columns[1].y = v.columns[1].y;
			r_vd.data.columns[2].x = v.columns[2].x;
			r_vd.data.columns[2].y = v.columns[2].y;
		} break;
		case Variant::VECTOR4: {
			const Vector4 v = p_variant;
			r_vd.data.x = v.x;
			r_vd.data.y = v.y;
			r_vd.data.z = v.z;
			r_vd.data.w = v.w;
		} break;
		case Variant::VECTOR4I: {
			const Vector4i v = p_variant;
			r_vd.data.x = v.x;
			r_vd.data.y = v.y;
			r_vd.data.z = v.z;
			r_vd.data.w = v.w;
		} break;
		case Variant::PLANE: {
			const Plane v = p_variant;
			r_vd.data.x = v.normal.x;
			r_vd.data.y = v.normal.y;
			r_vd.data.z = v.normal.z;
			r_vd.data.w = v.d;
		} break;
		case Variant::QUATERNION: {
			const Quaternion v = p_variant;
			r_vd.data.x = v.x;
			r_vd.data.y = v.y;
			r_vd.data.z = v.z;
			r_vd.data.w = v.w;
		} break;
		case Variant::AABB: {
			const AABB v = p_variant;
			r_vd.data.rows[0].x = v.position.x;
			r_vd.data.rows[0].y = v.position.y;
			r_vd.data.rows[0].z = v.position.z;
			r_vd.data.rows[1].x = v.size.x;
			r_vd.data.rows[1].y = v.size.y;
			r_vd.data.rows[1].z = v.size.z;
		} break;
		case Variant::BASIS: {
			const Basis v = p_variant;
			r_vd.data.rows[0].x = v.rows[0].x;
			r_vd.data.rows[0].y = v.rows[0].y;
			r_vd.data.rows[0].z = v.rows[0].z;
			r_vd.data.rows[1].x = v.rows[1].x;
			r_vd.data.rows[1].y = v.rows[1].y;
			r_vd.data.rows[1].z = v.rows[1].z;
			r_vd.data.rows[2].x = v.rows[2].x;
			r_vd.data.rows[2].y = v.rows[2].y;
			r_vd.data.rows[2].z = v.rows[2].z;
		} break;
		case Variant::TRANSFORM3D: {
			const Transform3D v = p_variant;
			std::memcpy(&r_vd.data.ptr, &v, sizeof(v));
		} break;
		case Variant::PROJECTION: {
			const Projection v = p_variant;
			memcpy(r_vd.data.ptr, &v, sizeof(v));
		} break;

		case Variant::STRING: {
			String s = p_variant;
			NS::Buffer b(s.size());
			memcpy(b.data, s.utf8(), s.size());
			r_vd.shared_buffer = std::make_shared<NS::Buffer>(b);
		} break;

		default:
			ERR_PRINT("This variant can't be converted: " + p_variant.stringify());
			r_vd.type = Variant::VARIANT_MAX;
	}
}

bool GdNetworkInterface::compare(const NS::VarData &p_A, const NS::VarData &p_B) const {
	return compare_static(p_A, p_B);
}

bool GdNetworkInterface::compare_static(const NS::VarData &p_A, const NS::VarData &p_B) {
	return true;
}

void NS_Test::test_var_data_conversin() {
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

	CRASH_COND(final_transform != initial_transform);
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
