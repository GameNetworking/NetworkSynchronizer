#include "gd_network_interface.h"

#include "core/error/error_macros.h"
#include "core/object/callable_method_pointer.h"
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

void GdNetworkInterface::configure_rpc(
		const StringName &p_func,
		bool p_call_local,
		bool p_is_reliable) {
	Dictionary rpc_config_dic;
	rpc_config_dic["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
	rpc_config_dic["call_local"] = p_call_local;

	if (p_is_reliable) {
		rpc_config_dic["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
	} else {
		rpc_config_dic["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE;
	}

	owner->rpc_config(p_func, rpc_config_dic);
}

int GdNetworkInterface::rpc_get_sender() const {
	if (owner->get_tree() && owner->get_tree()->get_multiplayer().is_valid()) {
		return owner->get_tree()->get_multiplayer()->get_remote_sender_id();
	}
	return 0;
}

void GdNetworkInterface::rpc_array(
		int p_peer_id,
		const StringName &p_method,
		const Variant **p_arg,
		int p_argcount) {
	owner->rpcp(p_peer_id, p_method, p_arg, p_argcount);
}