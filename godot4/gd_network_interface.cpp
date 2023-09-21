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

Vector<int> GdNetworkInterface::fetch_connected_peers() const {
	if (
			owner->get_tree() &&
			owner->get_tree()->get_multiplayer().is_valid()) {
		return owner->get_tree()->get_multiplayer()->get_peer_ids();
	}
	return Vector<int>();
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

void GdNetworkInterface::rpc_send(uint8_t p_rpc_id, int p_peer_recipient, const Variant *p_args, int p_count) {
	ERR_FAIL_COND(rpcs_info.size() <= p_rpc_id);
	if (rpcs_info[p_rpc_id].call_local) {
		rpc_last_sender = get_unit_authority();
		rpcs_info[p_rpc_id].func(p_args, p_count);
	}

	// TODO at some point here we should use the DataBuffer instead.
	Vector<Variant> args;
	args.resize(p_count + 1);
	args.write[0] = p_rpc_id;
	for (int i = 0; i < p_count; i++) {
		args.write[i + 1] = p_args[i];
	}

	if (rpcs_info[p_rpc_id].is_reliable) {
		owner->rpc_id(p_peer_recipient, SNAME("_rpc_net_sync_reliable"), args);
	} else {
		owner->rpc_id(p_peer_recipient, SNAME("_rpc_net_sync_unreliable"), args);
	}
}

void GdNetworkInterface::gd_rpc_receive(const Vector<Variant> &p_args) {
	ERR_FAIL_COND(p_args.size() < 1);
	rpc_receive(
			p_args[0],
			owner->get_multiplayer()->get_remote_sender_id(),
			p_args.size() == 1 ? nullptr : p_args.ptr() + 1,
			p_args.size() - 1);
}
