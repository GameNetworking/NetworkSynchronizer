#include "net_action.h"

#include "core/error/error_macros.h"
#include "core/os/os.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/node.h"
#include "scene_synchronizer.h"
#include "scene_synchronizer_debugger.h"

void SenderNetAction::prepare_processor(
		NetUtility::NodeData *p_nd,
		NetActionId p_action_id,
		const Array &p_vars) {
	action_processor.action_id = p_action_id;
	action_processor.nd = p_nd;

	// Compress the vars so that locally we use the compressed version like remotely.
	const NetActionInfo &info = p_nd->net_actions[p_action_id];

	LocalVector<Variant> raw_vars;
	raw_vars.resize(p_vars.size());
	for (int i = 0; i < p_vars.size(); i += 1) {
		raw_vars[i] = p_vars[i];
	}

	DataBuffer db;
	db.begin_write(0);
	info.network_encoder->encode(raw_vars, db);
	db.begin_read();
	info.network_encoder->decode(db, raw_vars);

	action_processor.vars.resize(raw_vars.size());
	for (int i = 0; i < p_vars.size(); i += 1) {
		action_processor.vars[i] = raw_vars[i];
	}
}
const NetActionInfo &SenderNetAction::get_action_info() const {
	return action_processor.nd->net_actions[action_processor.action_id];
}

void SenderNetAction::client_set_executed_input_id(uint32_t p_input_id) {
	peers_executed_input_id[1] = p_input_id;
}

uint32_t SenderNetAction::client_get_executed_input_id() const {
	const uint32_t *input_id = peers_executed_input_id.getptr(1);
	return input_id == nullptr ? UINT32_MAX : *input_id;
}

uint32_t SenderNetAction::peer_get_executed_input_id(int p_peer) const {
	const uint32_t *input_id = peers_executed_input_id.getptr(p_peer);
	return input_id == nullptr ? UINT32_MAX : *input_id;
}

void net_action::encode_net_action(
		const LocalVector<SenderNetAction *> &p_actions,
		int p_peer,
		DataBuffer &r_data_buffer) {
	for (uint32_t i = 0; i < p_actions.size(); i++) {
		// ---------------------------------------------------------- Add a boolean to note a new action
		const bool has_anotherone = true;
		r_data_buffer.add_bool(has_anotherone);

		// ----------------------------------------------------------------- Add the sender action token
		r_data_buffer.add_uint(p_actions[i]->action_token, DataBuffer::COMPRESSION_LEVEL_1);

		// ----------------------------------------------------------------------------- Add the node id
		const bool uses_node_id = p_actions[i]->action_processor.nd->id != UINT32_MAX;
		r_data_buffer.add_bool(uses_node_id);

		if (uses_node_id) {
			r_data_buffer.add_uint(p_actions[i]->action_processor.nd->id, DataBuffer::COMPRESSION_LEVEL_2);
		} else {
			r_data_buffer.add_variant(p_actions[i]->action_processor.nd->node->get_path());
		}

		// --------------------------------------------------------------------------- Add the action_id
		const NetActionId action_id = p_actions[i]->action_processor.action_id;
		r_data_buffer.add_uint(action_id, DataBuffer::COMPRESSION_LEVEL_2);

		// -------------------------------------------------------------------------- Add executed frame
		const uint32_t *executed_frame = p_actions[i]->peers_executed_input_id.getptr(p_peer);
		const bool has_executed_frame = executed_frame != nullptr;
		r_data_buffer.add_bool(has_executed_frame);
		if (has_executed_frame) {
			r_data_buffer.add_uint(*executed_frame, DataBuffer::COMPRESSION_LEVEL_1);
		}

		// --------------------------------------------------------------- Add the executed time changed
		const bool sender_executed_time_changed =
				p_actions[i]->sender_executed_time_changed &&
				p_peer == p_actions[i]->sender_peer;

		r_data_buffer.add_bool(sender_executed_time_changed);
		if (sender_executed_time_changed) {
			r_data_buffer.add_uint(p_actions[i]->triggerer_action_token, DataBuffer::COMPRESSION_LEVEL_1);
		}

		// --------------------------------------------------------------------------- Add the variables
		LocalVector<Variant> inputs;
		inputs.resize(p_actions[i]->action_processor.vars.size());
		for (uint32_t u = 0; u < inputs.size(); u++) {
			inputs[u] = p_actions[i]->action_processor.vars[u];
		}
		p_actions[i]->action_processor.nd->net_actions[action_id].network_encoder->encode(inputs, r_data_buffer);
	}

	const bool has_anotherone = false;
	r_data_buffer.add_bool(has_anotherone);
}

void net_action::decode_net_action(
		SceneSynchronizer *synchronizer,
		DataBuffer &p_data_buffer,
		int p_peer,
		LocalVector<SenderNetAction> &r_actions) {
	const int sender_peer = synchronizer->get_tree()->get_multiplayer()->get_remote_sender_id();

	LocalVector<Variant> variables;

	while (p_data_buffer.get_bit_offset() < p_data_buffer.total_size()) {
		// ---------------------------------------------------------- Fetch the boolean `has_anotherone`
		const bool has_anotherone = p_data_buffer.read_bool();
		if (!has_anotherone) {
			break;
		}

		// --------------------------------------------------------------- Fetch the sender action token
		const uint32_t action_token = p_data_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_1);

		// --------------------------------------------------------------------------- Fetch the node id
		const bool uses_node_id = p_data_buffer.read_bool();

		// Fetch the node_data.
		NetUtility::NodeData *node_data;
		if (uses_node_id) {
			const uint32_t node_data_id = p_data_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_2);
			node_data = synchronizer->get_node_data(node_data_id);
			if (node_data == nullptr) {
				SceneSynchronizerDebugger::singleton()->debug_error(synchronizer, "The received action data contains a node which is not registered on this peer. NodeDataId: `" + itos(node_data_id) + "`");
				continue;
			}
		} else {
			Variant node_path = p_data_buffer.read_variant();
			ERR_FAIL_COND_MSG(node_path.get_type() != Variant::NODE_PATH, "The received acts data is malformed, expected NodePath at this point.");

			Node *node = synchronizer->get_node(node_path);
			if (node == nullptr) {
				SceneSynchronizerDebugger::singleton()->debug_error(synchronizer, String("The received action data contains a node path which is unknown: `") + node_path.stringify() + "`");
				continue;
			}
			node_data = synchronizer->find_node_data(node);
			if (node_data == nullptr) {
				SceneSynchronizerDebugger::singleton()->debug_error(synchronizer, String("The received action data contains a node which is not registered on this peer. NodePath: `") + node_path.stringify() + "`");
				continue;
			}
		}

		// ------------------------------------------------------------------------- Fetch the action_id
		const NetActionId action_id = p_data_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_2);
		if (node_data->net_actions.size() <= action_id) {
			SceneSynchronizerDebugger::singleton()->debug_error(synchronizer, "The received action data is malformed. This peer doesn't have the action_id (`" + itos(action_id) + "`) for the node `" + node_data->node->get_path() + "`");
			continue;
		}

		// ------------------------------------------------------------------------ Fetch executed frame
		uint32_t executed_frame = UINT32_MAX;
		const bool has_executed_frame = p_data_buffer.read_bool();
		if (has_executed_frame) {
			executed_frame = p_data_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_1);
		}

		// ------------------------------------------------------------- Fetch the executed time changed
		const bool sender_executed_time_changed = p_data_buffer.read_bool();
		uint32_t triggerer_action_token = action_token;
		if (sender_executed_time_changed) {
			triggerer_action_token = p_data_buffer.read_uint(DataBuffer::COMPRESSION_LEVEL_1);
		}

		// ------------------------------------------------------------------------- Fetch the variables
		variables.clear();
		node_data->net_actions[action_id].network_encoder->decode(p_data_buffer, variables);

		// This should never be triggered because the `has_anotherone` is meant to be false and stop the loop.
		ERR_FAIL_COND_MSG(p_data_buffer.get_bit_offset() >= p_data_buffer.total_size(), "The received action data is malformed.");

		Array arguments;
		arguments.resize(variables.size());
		for (uint32_t i = 0; i < variables.size(); i += 1) {
			arguments[i] = variables[i];
		}

		const uint32_t index = r_actions.size();
		r_actions.resize(index + 1);

		r_actions[index].action_token = action_token;
		r_actions[index].triggerer_action_token = triggerer_action_token;
		r_actions[index].sender_executed_time_changed = sender_executed_time_changed;
		r_actions[index].sender_peer = sender_peer;
		r_actions[index].peers_executed_input_id[p_peer] = executed_frame;
		r_actions[index].action_processor.nd = node_data;
		r_actions[index].action_processor.action_id = action_id;
		r_actions[index].action_processor.vars = arguments;
	}
}

bool NetActionSenderInfo::process_received_action(uint32_t p_action_index) {
	const uint64_t now = OS::get_singleton()->get_ticks_msec();

	bool already_received = true;

	if (last_received_action_id != UINT32_MAX) {
		if (last_received_action_id < p_action_index) {
			// Add all the in between ids as missing.
			for (uint32_t missing_action_id = last_received_action_id + 1; missing_action_id < p_action_index; missing_action_id += 1) {
				missing_actions.push_back({ missing_action_id, now });
			}

			last_received_action_id = p_action_index;
			already_received = false;

		} else if (last_received_action_id == p_action_index) {
			// Already known, drop it.
			already_received = true;

		} else {
			// Old act, check if it's a missing act.
			const int64_t index = missing_actions.find({ p_action_index, 0 });
			const bool known = index == -1;
			if (known) {
				already_received = true;
			} else {
				already_received = false;
				missing_actions.remove_at_unordered(index);
			}
		}
	} else {
		last_received_action_id = p_action_index;
		already_received = false;
	}

	return already_received;
}

void NetActionSenderInfo::check_missing_actions_and_clean_up(Node *p_owner) {
	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	const uint64_t one_second = 1000;

	for (int64_t i = int64_t(missing_actions.size()) - 1; i >= 0; i -= 1) {
		if ((missing_actions[i].timestamp + one_second) <= now) {
			// After more than 1 second the action is still missing.
			SceneSynchronizerDebugger::singleton()->debug_warning(p_owner, "The action with ID: `" + itos(missing_actions[i].id) + "` was never received.");
			// Remove it from missing actions, this will:
			// 1. From now on this action will be discarded if received.
			// 2. Reduce the `missing_actions` array size.
			missing_actions.remove_at_unordered(i);
		}
	}
}
