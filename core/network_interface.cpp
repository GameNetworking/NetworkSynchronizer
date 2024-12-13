#include "network_interface.h"

#include "../scene_synchronizer.h"

NS_NAMESPACE_BEGIN
void NetworkInterface::set_scene_synchronizer(SceneSynchronizerBase *p_scene_sync) {
	scene_synchronizer = p_scene_sync;
}

SceneSynchronizerDebugger &NetworkInterface::get_debugger() const {
	return debugger;
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