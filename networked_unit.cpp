#include "networked_unit.h"

#include "core/object/callable_method_pointer.h"
#include "scene/main/multiplayer_api.h"

void NetworkedUnit::_bind_methods() {
}

void NetworkedUnit::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		if (Engine::get_singleton()->is_editor_hint()) {
			return;
		}

	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		if (Engine::get_singleton()->is_editor_hint()) {
			return;
		}

		ns_stop_listening_peer_connection();
	}
}

NetworkedUnit::NetworkedUnit() :
		Node() {
}

NetworkedUnit::~NetworkedUnit() {
}

void NetworkedUnit::ns_start_listening_peer_connection() {
	if (!get_multiplayer()->is_connected(SNAME("peer_connected"), callable_mp(this, &NetworkedUnit::on_peer_connected))) {
		get_multiplayer()->connect(SNAME("peer_connected"), callable_mp(this, &NetworkedUnit::on_peer_connected));
		get_multiplayer()->connect(SNAME("peer_disconnected"), callable_mp(this, &NetworkedUnit::on_peer_connected));
	}
}

void NetworkedUnit::ns_stop_listening_peer_connection() {
	if (get_multiplayer()->is_connected(SNAME("peer_connected"), callable_mp(this, &NetworkedUnit::on_peer_connected))) {
		get_multiplayer()->disconnect(SNAME("peer_connected"), callable_mp(this, &NetworkedUnit::on_peer_connected));
		get_multiplayer()->disconnect(SNAME("peer_disconnected"), callable_mp(this, &NetworkedUnit::on_peer_connected));
	}
}

int NetworkedUnit::ns_fetch_local_peer_id() const {
	if (get_multiplayer().is_valid()) {
		return get_multiplayer()->get_unique_id();
	}
	return 0;
}

Vector<int> NetworkedUnit::ns_fetch_connected_peers() const {
	if (
			get_tree() &&
			get_tree()->get_multiplayer().is_valid()) {
		return get_tree()->get_multiplayer()->get_peer_ids();
	}
	return Vector<int>();
}

int NetworkedUnit::ns_get_unit_authority() const {
	return get_multiplayer_authority();
}

bool NetworkedUnit::ns_is_local_peer_networked() const {
	return get_tree() &&
			get_tree()->get_multiplayer()->get_multiplayer_peer()->get_class_name() != "OfflineMultiplayerPeer";
}

bool NetworkedUnit::ns_is_local_peer_server() const {
	if (ns_is_local_peer_networked()) {
		return get_tree()->get_multiplayer()->is_server();
	} else {
		return false;
	}
}

bool NetworkedUnit::ns_is_local_peer_authority_of_this_unit() const {
	return is_multiplayer_authority();
}

void NetworkedUnit::ns_configure_rpc(
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

	rpc_config(p_func, rpc_config_dic);
}

int NetworkedUnit::ns_rpc_get_sender() const {
	if (get_tree() && get_tree()->get_multiplayer().is_valid()) {
		return get_tree()->get_multiplayer()->get_remote_sender_id();
	}
	return 0;
}

void NetworkedUnit::ns_rpcp(
		int p_peer_id,
		const StringName &p_method,
		const Variant **p_arg,
		int p_argcount) {
	rpcp(p_peer_id, p_method, p_arg, p_argcount);
}
