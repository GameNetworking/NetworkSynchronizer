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
#include "modules/network_synchronizer/godot4/gd_scene_synchronizer.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/node.h"
#include <cfloat>
#include <functional>
#include <memory>

GdNetworkInterface::GdNetworkInterface() {
}

GdNetworkInterface::~GdNetworkInterface() {
}

std::string GdNetworkInterface::get_owner_name() const {
	return String(owner->get_path()).utf8().ptr();
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
	if (owner && owner->get_multiplayer().is_valid()) {
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

void NS_GD_Test::test_var_data_conversin() {
	// Test Transform
	{
		Basis b;
		b.set_axis_angle(Vector3(1, 0, 0), 0.12);
		Vector3 o(1, 2, 3);

		Transform3D initial_transform(b, o);
		Variant variant = initial_transform;

		NS::VarData vd;
		GdSceneSynchronizer::convert(vd, variant);

		Variant final_variant;
		GdSceneSynchronizer::convert(final_variant, vd);

		Transform3D final_transform = final_variant;

		CRASH_COND(!final_transform.is_equal_approx(initial_transform));
	}

	// Test bool
	{
		Variant from = true;
		NS::VarData vd_from;
		GdSceneSynchronizer::convert(vd_from, from);

		Variant to;
		GdSceneSynchronizer::convert(to, vd_from);

		CRASH_COND(from != to);

		// Test compare.
		{
			Variant from2 = true;
			NS::VarData vd_from2;
			GdSceneSynchronizer::convert(vd_from2, from2);

			CRASH_COND(!GdSceneSynchronizer::compare(vd_from, vd_from2));
			NS::VarData vd;
			CRASH_COND(GdSceneSynchronizer::compare(vd_from, vd));
			CRASH_COND(GdSceneSynchronizer::compare(vd_from2, vd));
		}
	}

	// Test StringName
	{
		Variant from = StringName("GHUEIAiasfjasdfkadjfak");
		CRASH_COND(from.get_type() != Variant::STRING_NAME);
		NS::VarData vd_from;
		GdSceneSynchronizer::convert(vd_from, from);

		NS::VarData vd_from_copy;
		vd_from_copy.copy(vd_from);

		Variant to;
		GdSceneSynchronizer::convert(to, vd_from_copy);

		CRASH_COND(from != to);
		CRASH_COND(vd_from.shared_buffer != vd_from_copy.shared_buffer);
	}

	// Test NodePath
	{
		Variant from = NodePath("/root/asdf/fieae");
		CRASH_COND(from.get_type() != Variant::NODE_PATH);
		NS::VarData vd_from;
		GdSceneSynchronizer::convert(vd_from, from);

		NS::VarData vd_from_copy;
		vd_from_copy.copy(vd_from);

		Variant to;
		GdSceneSynchronizer::convert(to, vd_from_copy);

		CRASH_COND(from != to);
		CRASH_COND(vd_from.shared_buffer != vd_from_copy.shared_buffer);
	}

	// Test String
	{
		Variant from = "GHUEIAiasfjasdfkadjfak";
		CRASH_COND(from.get_type() != Variant::STRING);
		NS::VarData vd_from;
		GdSceneSynchronizer::convert(vd_from, from);

		NS::VarData vd_from_copy;
		vd_from_copy.copy(vd_from);

		Variant to;
		GdSceneSynchronizer::convert(to, vd_from_copy);

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
		GdSceneSynchronizer::convert(vd_from, from);

		NS::VarData vd_from_copy;
		vd_from_copy.copy(vd_from);

		Variant to;
		GdSceneSynchronizer::convert(to, vd_from_copy);

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
		GdSceneSynchronizer::convert(vd_from, from);

		NS::VarData vd_from_copy;
		vd_from_copy.copy(vd_from);

		Variant to;
		GdSceneSynchronizer::convert(to, vd_from_copy);

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
		GdSceneSynchronizer::convert(vd_from, from);

		NS::VarData vd_from_copy;
		vd_from_copy.copy(vd_from);

		Variant to;
		GdSceneSynchronizer::convert(to, vd_from_copy);

		CRASH_COND(from != to);
		CRASH_COND(vd_from.shared_buffer != vd_from_copy.shared_buffer);
		CRASH_COND(!GdSceneSynchronizer::compare(vd_from, vd_from_copy));
		{
			NS::VarData vd;
			CRASH_COND(GdSceneSynchronizer::compare(vd_from, vd));
		}
	}
}

void GdNetworkInterface::rpc_send(int p_peer_recipient, bool p_reliable, DataBuffer &&p_buffer) {
	const std::vector<std::uint8_t> &buffer = p_buffer.get_buffer().get_bytes();

	// TODO use RPC directly from MultiPlayerPeer that allows to sent raw buffers. This would avoid this conversion to Vector.
	Vector<uint8_t> gd_buffer;
	for (auto b : buffer) {
		gd_buffer.push_back(b);
	}

	if (p_reliable) {
		owner->rpc_id(p_peer_recipient, SNAME("_rpc_net_sync_reliable"), gd_buffer);
	} else {
		owner->rpc_id(p_peer_recipient, SNAME("_rpc_net_sync_unreliable"), gd_buffer);
	}
}

void GdNetworkInterface::gd_rpc_receive(const Vector<uint8_t> &p_gd_buffer) {
	DataBuffer db;
	db.get_buffer_mut().get_bytes_mut().reserve(p_gd_buffer.size());
	for (auto b : p_gd_buffer) {
		db.get_buffer_mut().get_bytes_mut().push_back(b);
	}

	db.begin_read();
	rpc_receive(
			owner->get_multiplayer()->get_remote_sender_id(),
			db);
}

// Copied from `enet_packet_peer.h` to avoid including it, which is complex due to its dependency with enet.
enum PeerStatistic {
	PEER_PACKET_LOSS,
	PEER_PACKET_LOSS_VARIANCE,
	PEER_PACKET_LOSS_EPOCH,
	PEER_ROUND_TRIP_TIME,
	PEER_ROUND_TRIP_TIME_VARIANCE,
	PEER_LAST_ROUND_TRIP_TIME,
	PEER_LAST_ROUND_TRIP_TIME_VARIANCE,
	PEER_PACKET_THROTTLE,
	PEER_PACKET_THROTTLE_LIMIT,
	PEER_PACKET_THROTTLE_COUNTER,
	PEER_PACKET_THROTTLE_EPOCH,
	PEER_PACKET_THROTTLE_ACCELERATION,
	PEER_PACKET_THROTTLE_DECELERATION,
	PEER_PACKET_THROTTLE_INTERVAL,
};

// Copied from enet.h to avoid including it.
uint64_t ENET_PEER_PACKET_LOSS_SCALE = (1 << 16);

void GdNetworkInterface::server_update_net_stats(int p_peer, NS::PeerData &r_peer_data) const {
	// This function is always called on the server.
	ASSERT_COND(is_local_peer_server());

	ERR_FAIL_COND(!owner);
	ERR_FAIL_COND(!owner->get_multiplayer().is_valid());
	ERR_FAIL_COND(!owner->get_multiplayer()->get_multiplayer_peer().is_valid());

	Ref<MultiplayerPeer> packet_peer = owner->get_multiplayer()->get_multiplayer_peer();
	Ref<PacketPeer> enet_peer = packet_peer->call("get_peer", p_peer);
	ERR_FAIL_COND(!enet_peer.is_valid());

	// Using the GDScript bindings to read these so to avoid including `enet_packet_peer.h`, which is complex due to its dependency with enet.
	r_peer_data.set_latency(enet_peer->call("get_statistic", PEER_ROUND_TRIP_TIME));
	r_peer_data.set_out_packet_loss_percentage(double(enet_peer->call("get_statistic", PEER_PACKET_LOSS)) / double(ENET_PEER_PACKET_LOSS_SCALE));
	r_peer_data.set_latency_jitter_ms(enet_peer->call("get_statistic", PEER_ROUND_TRIP_TIME_VARIANCE));
}
