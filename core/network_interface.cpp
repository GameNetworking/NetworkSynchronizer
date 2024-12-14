#include "network_interface.h"

#include "../scene_synchronizer.h"

NS_NAMESPACE_BEGIN
void NetworkInterface::set_scene_synchronizer(SceneSynchronizerBase *p_scene_sync) {
	scene_synchronizer = p_scene_sync;
}

SceneSynchronizerDebugger &NetworkInterface::get_debugger() const {
	return debugger;
}

bool NetworkInterface::validate_rpc_sender_receive(int p_sender_peer, const RPCInfo &p_rpc_info, const ObjectData *p_od) const {
	if (p_rpc_info.call_local && p_sender_peer == get_local_peer_id()) {
		return true;
	}

	return validate_rpc_sender(p_sender_peer, p_rpc_info, p_od);
}

bool NetworkInterface::validate_rpc_sender(int p_sender_peer, const RPCInfo &p_rpc_info, const ObjectData *p_od) const {
	if (p_rpc_info.call_local && p_sender_peer == get_local_peer_id()) {
		return true;
	}

	switch (p_rpc_info.allowed_sender) {
		case RpcAllowedSender::ALL:
			// Always true.
			return true;
		case RpcAllowedSender::DOLL:
			if (p_od) {
				if (p_od->get_controlled_by_peer() > 0) {
					return p_od->get_controlled_by_peer() != p_sender_peer;
				} else {
					// Always true when the object is not controlled
					return true;
				}
			} else {
				// Never allow for rpcs toward to SceneSynchronizer.
				return false;
			}
		case RpcAllowedSender::PLAYER:
			if (p_od) {
				return p_od->get_controlled_by_peer() == p_sender_peer;
			} else {
				// Never allow for rpcs toward to SceneSynchronizer.
				return false;
			}
		case RpcAllowedSender::SERVER:
			return p_sender_peer == get_server_peer();
	}

	// Please implement all.
	NS_ASSERT_NO_ENTRY();
	return false;
}

void NetworkInterface::rpc_receive(int p_sender_peer, DataBuffer &p_db) {
	rpc_last_sender = p_sender_peer;

	p_db.begin_read(get_debugger());

	bool target_object;
	p_db.read(target_object);

	ObjectNetId target_id = ObjectNetId::NONE;
	if (target_object) {
		p_db.read(target_id.id);
	}

	std::uint8_t rpc_id;
	p_db.read(rpc_id);

	if (target_id != ObjectNetId::NONE) {
		ObjectData *od = scene_synchronizer->get_object_data(target_id);
		if (od) {
			NS_ENSURE_MSG(rpc_id < od->rpcs_info.size(), "The received rpc of object "+std::to_string(target_id.id)+" contains a broken RPC ID: `" + std::to_string(rpc_id) + "`, the `rpcs_info` size is `" + std::to_string(rpcs_info.size()) + "`.");
			// This can't be triggered because the rpc always points to a valid
			// function at this point because as soon as the object is deregistered
			// the RPCs are deregistered.
			NS_ASSERT_COND(od->rpcs_info[rpc_id].func);
			NS_ENSURE_MSG(validate_rpc_sender_receive(p_sender_peer, od->rpcs_info[rpc_id], od), "The RPC `"+std::to_string(rpc_id)+"` validation failed for the Object `"+std::to_string(od->get_net_id().id)+"#"+od->get_object_name()+"`, is the peer `"+std::to_string(p_sender_peer)+"` cheating?");
			od->rpcs_info[rpc_id].func(p_db);
		} else {
			// The rpc was not delivered because the object is not spawned yet,
			// Notify the network synchronizer.
			scene_synchronizer->notify_undelivered_rpc(target_id, rpc_id, p_sender_peer, p_db);
		}
	} else {
		NS_ENSURE_MSG(rpc_id < rpcs_info.size(), "The received rpc contains a broken RPC ID: `" + std::to_string(rpc_id) + "`, the `rpcs_info` size is `" + std::to_string(rpcs_info.size()) + "`.");
		// This can't be triggered because the rpc always points to a valid
		// function at this point.
		NS_ASSERT_COND(rpcs_info[rpc_id].func);
		NS_ENSURE_MSG(validate_rpc_sender_receive(p_sender_peer, rpcs_info[rpc_id], nullptr), "The RPC `"+std::to_string(rpc_id)+"` validation failed for the SceneSynchronizer RPC, is the peer `"+std::to_string(p_sender_peer)+"` cheating?");
		rpcs_info[rpc_id].func(p_db);
	}
}

void NetworkInterface::__fetch_rpc_info_from_object(
		ObjectLocalId p_id,
		int p_rpc_index,
		ObjectNetId &r_net_id,
		RPCInfo *&r_rpc_info) const {
	r_net_id = ObjectNetId::NONE;

	if (ObjectData *od = scene_synchronizer->get_object_data(p_id)) {
		if (od->rpcs_info.size() > p_rpc_index) {
			r_net_id = od->get_net_id();
			r_rpc_info = &od->rpcs_info[p_rpc_index];
		}
	}
}

NS_NAMESPACE_END