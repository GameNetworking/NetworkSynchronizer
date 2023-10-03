#include "gd_network_interface.h"

#include "core/error/error_macros.h"
#include "core/object/callable_method_pointer.h"
#include "core/variant/callable.h"
#include "core/variant/variant.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/node.h"

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
