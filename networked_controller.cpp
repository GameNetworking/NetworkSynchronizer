#include "networked_controller.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/error/error_macros.h"
#include "core/io/marshalls.h"
#include "core/print.h"
#include "core/processor.h"
#include "core/templates/vector.h"
#include "godot4/gd_network_interface.h"
#include "modules/network_synchronizer/core/core.h"
#include "modules/network_synchronizer/core/print.h"
#include "modules/network_synchronizer/scene_synchronizer.h"
#include "net_utilities.h"
#include "networked_controller.h"
#include "scene/main/multiplayer_api.h"
#include "scene_synchronizer.h"
#include "scene_synchronizer_debugger.h"
#include <algorithm>
#include <functional>
#include <string>

#define METADATA_SIZE 1

NS_NAMESPACE_BEGIN

NetworkedControllerBase::NetworkedControllerBase(NetworkInterface *p_network_interface) :
		network_interface(p_network_interface) {
	inputs_buffer = memnew(DataBuffer);
}

NetworkedControllerBase::~NetworkedControllerBase() {
	memdelete(inputs_buffer);
	inputs_buffer = nullptr;

	if (controller != nullptr) {
		memdelete(controller);
		controller = nullptr;
		controller_type = CONTROLLER_TYPE_NULL;
	}
	network_interface = nullptr;
}

void NetworkedControllerBase::setup(NetworkedControllerManager &p_controller_manager) {
	networked_controller_manager = &p_controller_manager;

	rpc_handle_receive_input =
			network_interface->rpc_config(
					std::function<void(const Vector<uint8_t> &)>(std::bind(&NetworkedControllerBase::rpc_receive_inputs, this, std::placeholders::_1)),
					false,
					false);

	rpc_handle_set_server_controlled =
			network_interface->rpc_config(
					std::function<void(bool)>(std::bind(&NetworkedControllerBase::rpc_set_server_controlled, this, std::placeholders::_1)),
					true,
					false);
}

void NetworkedControllerBase::conclude() {
	network_interface->clear();
	networked_controller_manager = nullptr;

	rpc_handle_receive_input.reset();
	rpc_handle_set_server_controlled.reset();
}

void NetworkedControllerBase::set_server_controlled(bool p_server_controlled) {
	if (server_controlled == p_server_controlled) {
		// It's the same, nothing to do.
		return;
	}

	if (is_networking_initialized()) {
		if (is_server_controller()) {
			// This is the server, let's start the procedure to switch controll mode.

#ifdef DEBUG_ENABLED
			CRASH_COND_MSG(scene_synchronizer == nullptr, "When the `NetworkedController` is a server, the `scene_synchronizer` is always set.");
#endif

			// First update the variable.
			server_controlled = p_server_controlled;

			// Notify the `SceneSynchronizer` about it.
			scene_synchronizer->notify_controller_control_mode_changed(this);

			// Tell the client to do the switch too.
			if (network_interface->get_unit_authority() != 1) {
				rpc_handle_set_server_controlled.rpc(
						get_network_interface(),
						network_interface->get_unit_authority(),
						server_controlled);
			} else {
				SceneSynchronizerDebugger::singleton()->debug_warning(network_interface, "The node is owned by the server, there is no client that can control it; please assign the proper authority.");
			}

		} else if (is_player_controller() || is_doll_controller()) {
			SceneSynchronizerDebugger::singleton()->debug_warning(network_interface, "You should never call the function `set_server_controlled` on the client, this has an effect only if called on the server.");

		} else if (is_nonet_controller()) {
			// There is no networking, the same instance is both the client and the
			// server already, nothing to do.
			server_controlled = p_server_controlled;

		} else {
#ifdef DEBUG_ENABLED
			CRASH_NOW_MSG("Unreachable, all the cases are handled.");
#endif
		}
	} else {
		// This called during initialization or on the editor, nothing special just
		// set it.
		server_controlled = p_server_controlled;
	}
}

bool NetworkedControllerBase::get_server_controlled() const {
	return server_controlled;
}

void NetworkedControllerBase::set_max_redundant_inputs(int p_max) {
	max_redundant_inputs = p_max;
}

int NetworkedControllerBase::get_max_redundant_inputs() const {
	return max_redundant_inputs;
}

void NetworkedControllerBase::set_network_traced_frames(int p_size) {
	network_traced_frames = p_size;
}

int NetworkedControllerBase::get_network_traced_frames() const {
	return network_traced_frames;
}

void NetworkedControllerBase::set_min_frames_delay(int p_val) {
	min_frames_delay = p_val;
}

int NetworkedControllerBase::get_min_frames_delay() const {
	return min_frames_delay;
}

void NetworkedControllerBase::set_max_frames_delay(int p_val) {
	max_frames_delay = p_val;
}

int NetworkedControllerBase::get_max_frames_delay() const {
	return max_frames_delay;
}

FrameIndex NetworkedControllerBase::get_current_input_id() const {
	ERR_FAIL_NULL_V(controller, FrameIndex::NONE);
	return controller->get_current_input_id();
}

void NetworkedControllerBase::server_set_peer_simulating_this_controller(int p_peer, bool p_simulating) {
	ERR_FAIL_COND_MSG(!is_server_controller(), "This function can be called only on the server.");
	if (p_simulating) {
		VecFunc::insert_unique(get_server_controller()->peers_simulating_this_controller, p_peer);
	} else {
		VecFunc::remove(get_server_controller()->peers_simulating_this_controller, p_peer);
	}
}

bool NetworkedControllerBase::server_is_peer_simulating_this_controller(int p_peer) const {
	ERR_FAIL_COND_V_MSG(!is_server_controller(), false, "This function can be called only on the server.");
	return VecFunc::has(get_server_controller()->peers_simulating_this_controller, p_peer);
}

int NetworkedControllerBase::server_get_associated_peer() const {
	return network_interface->get_unit_authority();
}

bool NetworkedControllerBase::has_another_instant_to_process_after(int p_i) const {
	ERR_FAIL_COND_V_MSG(is_player_controller() == false, false, "Can be executed only on player controllers.");
	return static_cast<PlayerController *>(controller)->has_another_instant_to_process_after(p_i);
}

void NetworkedControllerBase::process(double p_delta) {
	// This function is registered as processed function, so it's called by the
	// `SceneSync` in sync with the scene processing.
	controller->process(p_delta);
}

ServerController *NetworkedControllerBase::get_server_controller() {
	ERR_FAIL_COND_V_MSG(is_server_controller() == false, nullptr, "This controller is not a server controller.");
	return static_cast<ServerController *>(controller);
}

const ServerController *NetworkedControllerBase::get_server_controller() const {
	ERR_FAIL_COND_V_MSG(is_server_controller() == false, nullptr, "This controller is not a server controller.");
	return static_cast<const ServerController *>(controller);
}

ServerController *NetworkedControllerBase::get_server_controller_unchecked() {
	return static_cast<ServerController *>(controller);
}

const ServerController *NetworkedControllerBase::get_server_controller_unchecked() const {
	return static_cast<const ServerController *>(controller);
}

PlayerController *NetworkedControllerBase::get_player_controller() {
	ERR_FAIL_COND_V_MSG(is_player_controller() == false, nullptr, "This controller is not a player controller.");
	return static_cast<PlayerController *>(controller);
}

const PlayerController *NetworkedControllerBase::get_player_controller() const {
	ERR_FAIL_COND_V_MSG(is_player_controller() == false, nullptr, "This controller is not a player controller.");
	return static_cast<const PlayerController *>(controller);
}

DollController *NetworkedControllerBase::get_doll_controller() {
	ERR_FAIL_COND_V_MSG(is_doll_controller() == false, nullptr, "This controller is not a doll controller.");
	return static_cast<DollController *>(controller);
}

const DollController *NetworkedControllerBase::get_doll_controller() const {
	ERR_FAIL_COND_V_MSG(is_doll_controller() == false, nullptr, "This controller is not a doll controller.");
	return static_cast<const DollController *>(controller);
}

NoNetController *NetworkedControllerBase::get_nonet_controller() {
	ERR_FAIL_COND_V_MSG(is_nonet_controller() == false, nullptr, "This controller is not a no net controller.");
	return static_cast<NoNetController *>(controller);
}

const NoNetController *NetworkedControllerBase::get_nonet_controller() const {
	ERR_FAIL_COND_V_MSG(is_nonet_controller() == false, nullptr, "This controller is not a no net controller.");
	return static_cast<const NoNetController *>(controller);
}

bool NetworkedControllerBase::is_networking_initialized() const {
	return controller_type != CONTROLLER_TYPE_NULL;
}

bool NetworkedControllerBase::is_server_controller() const {
	return controller_type == CONTROLLER_TYPE_SERVER || controller_type == CONTROLLER_TYPE_AUTONOMOUS_SERVER;
}

bool NetworkedControllerBase::is_player_controller() const {
	return controller_type == CONTROLLER_TYPE_PLAYER;
}

bool NetworkedControllerBase::is_doll_controller() const {
	return controller_type == CONTROLLER_TYPE_DOLL;
}

bool NetworkedControllerBase::is_nonet_controller() const {
	return controller_type == CONTROLLER_TYPE_NONETWORK;
}

void NetworkedControllerBase::set_inputs_buffer(const BitArray &p_new_buffer, uint32_t p_metadata_size_in_bit, uint32_t p_size_in_bit) {
	inputs_buffer->get_buffer_mut().get_bytes_mut() = p_new_buffer.get_bytes();
	inputs_buffer->shrink_to(p_metadata_size_in_bit, p_size_in_bit);
}

void NetworkedControllerBase::unregister_with_synchronizer(NS::SceneSynchronizerBase *p_synchronizer) {
	if (scene_synchronizer == nullptr) {
		// Nothing to unregister.
		return;
	}
	ERR_FAIL_COND_MSG(p_synchronizer != scene_synchronizer, "Cannot unregister because the given `SceneSynchronizer` is not the old one. This is a bug, one `SceneSynchronizer` should not try to unregister another one's controller.");
	// Unregister the event processors with the scene synchronizer.
	scene_synchronizer->event_peer_status_updated.unbind(event_handler_peer_status_updated);
	scene_synchronizer->event_state_validated.unbind(event_handler_state_validated);
	scene_synchronizer->event_rewind_frame_begin.unbind(event_handler_rewind_frame_begin);
	event_handler_rewind_frame_begin = NS::NullPHandler;
	event_handler_state_validated = NS::NullPHandler;
	event_handler_peer_status_updated = NS::NullPHandler;
	// Unregister the process handler with the scene synchronizer.
	NS::ObjectLocalId local_id = scene_synchronizer->find_object_local_id(*this);
	scene_synchronizer->unregister_process(local_id, PROCESSPHASE_PROCESS, process_handler_process);
	process_handler_process = NS::NullPHandler;
	// Empty the network controller variables.
	net_id = ObjectNetId::NONE;
	scene_synchronizer = nullptr;
}

void NetworkedControllerBase::notify_registered_with_synchronizer(NS::SceneSynchronizerBase *p_synchronizer, NS::ObjectData &p_nd) {
	ERR_FAIL_COND_MSG(scene_synchronizer != nullptr, "Cannot register with a new `SceneSynchronizer` because this controller is already registered with one. This is a bug, one controller should not be registered with two `SceneSynchronizer`s.");
	net_id = ObjectNetId::NONE;
	scene_synchronizer = p_synchronizer;

	process_handler_process =
			scene_synchronizer->register_process(
					p_nd.get_local_id(),
					PROCESSPHASE_PROCESS,
					[this](float p_delta) -> void { process(p_delta); });

	event_handler_peer_status_updated =
			scene_synchronizer->event_peer_status_updated.bind([this](const NS::ObjectData *p_object_data, int p_peer_id, bool p_connected, bool p_enabled) -> void {
				on_peer_status_updated(p_object_data, p_peer_id, p_connected, p_enabled);
			});

	event_handler_rewind_frame_begin =
			scene_synchronizer->event_rewind_frame_begin.bind([this](FrameIndex p_frame_index, int p_index, int p_count) -> void {
				on_rewind_frame_begin(p_frame_index, p_index, p_count);
			});
}

NS::SceneSynchronizerBase *NetworkedControllerBase::get_scene_synchronizer() const {
	return scene_synchronizer;
}

bool NetworkedControllerBase::has_scene_synchronizer() const {
	return scene_synchronizer;
}

void NetworkedControllerBase::on_peer_status_updated(const NS::ObjectData *p_object_data, int p_peer_id, bool p_connected, bool p_enabled) {
	if (!p_object_data) {
		return;
	}

	if (p_object_data->get_controller() == this) {
		if (is_server_controller()) {
			get_server_controller()->on_peer_update(p_connected && p_enabled);
		}
	}
}

void NetworkedControllerBase::on_rewind_frame_begin(FrameIndex p_input_id, int p_index, int p_count) {
	if (controller && is_realtime_enabled()) {
		controller->queue_instant_process(p_input_id, p_index, p_count);
	}
}

void NetworkedControllerBase::rpc_receive_inputs(const Vector<uint8_t> &p_data) {
	if (controller) {
		controller->receive_inputs(p_data);
	}
}

void NetworkedControllerBase::rpc_set_server_controlled(bool p_server_controlled) {
	ERR_FAIL_COND_MSG(is_player_controller() == false, "This function is supposed to be called on the server.");
	server_controlled = p_server_controlled;

	ERR_FAIL_COND_MSG(scene_synchronizer == nullptr, "The server controller is supposed to be set on the client at this point.");
	scene_synchronizer->notify_controller_control_mode_changed(this);
}

void NetworkedControllerBase::player_set_has_new_input(bool p_has) {
	has_player_new_input = p_has;
}

bool NetworkedControllerBase::player_has_new_input() const {
	return has_player_new_input;
}

bool NetworkedControllerBase::is_realtime_enabled() {
	if (net_id == ObjectNetId::NONE) {
		if (scene_synchronizer) {
			const ObjectLocalId lid = scene_synchronizer->find_object_local_id(*this);
			if (lid != ObjectLocalId::NONE) {
				net_id = scene_synchronizer->get_object_data(lid)->get_net_id();
			}
		}
	}
	if (net_id != ObjectNetId::NONE) {
		NS::ObjectData *nd = scene_synchronizer->get_object_data(net_id);
		if (nd) {
			return nd->realtime_sync_enabled_on_client;
		}
	}
	return false;
}

void NetworkedControllerBase::notify_controller_reset() {
	event_controller_reset.broadcast();
}

bool NetworkedControllerBase::__input_data_parse(
		const Vector<uint8_t> &p_data,
		void *p_user_pointer,
		void (*p_input_parse)(void *p_user_pointer, FrameIndex p_input_id, int p_input_size_in_bits, const BitArray &p_input)) {
	// The packet is composed as follow:
	// |- Four bytes for the first input ID.
	// \- Array of inputs:
	//      |-- First byte the amount of times this input is duplicated in the packet.
	//      |-- inputs buffer.
	//
	// Let's decode it!

	const int data_len = p_data.size();

	int ofs = 0;

	ERR_FAIL_COND_V(data_len < 4, false);
	const FrameIndex first_input_id = FrameIndex{ decode_uint32(p_data.ptr() + ofs) };
	ofs += 4;

	uint32_t inserted_input_count = 0;

	// Contains the entire packet and in turn it will be seek to specific location
	// so I will not need to copy chunk of the packet data.
	DataBuffer *pir = memnew(DataBuffer);
	pir->copy(p_data);
	pir->begin_read();

	while (ofs < data_len) {
		ERR_FAIL_COND_V_MSG(ofs + 1 > data_len, false, "The arrived packet size doesn't meet the expected size.");
		// First byte is used for the duplication count.
		const uint8_t duplication = p_data[ofs];
		ofs += 1;

		// Validate input
		const int input_buffer_offset_bit = ofs * 8;
		pir->shrink_to(input_buffer_offset_bit, (data_len - ofs) * 8);
		pir->seek(input_buffer_offset_bit);
		// Read metadata
		const bool has_data = pir->read_bool();

		const int input_size_in_bits = (has_data ? int(networked_controller_manager->count_input_size(*pir)) : 0) + METADATA_SIZE;

		// Pad to 8 bits.
		const int input_size_padded =
				Math::ceil((static_cast<float>(input_size_in_bits)) / 8.0);
		ERR_FAIL_COND_V_MSG(ofs + input_size_padded > data_len, false, "The arrived packet size doesn't meet the expected size.");

		// Extract the data and copy into a BitArray.
		BitArray bit_array;
		bit_array.get_bytes_mut().resize(input_size_padded);
		memcpy(
				bit_array.get_bytes_mut().ptrw(),
				p_data.ptr() + ofs,
				input_size_padded);

		// The input is valid, and the bit array is created: now execute the callback.
		for (int sub = 0; sub <= duplication; sub += 1) {
			const FrameIndex input_id = first_input_id + inserted_input_count;
			inserted_input_count += 1;

			p_input_parse(p_user_pointer, input_id, input_size_in_bits, bit_array);
		}

		// Advance the offset to parse the next input.
		ofs += input_size_padded;
	}

	memdelete(pir);
	pir = nullptr;

	ERR_FAIL_COND_V_MSG(ofs != data_len, false, "At the end was detected that the arrived packet has an unexpected size.");
	return true;
}

bool NetworkedControllerBase::__input_data_get_first_input_id(
		const Vector<uint8_t> &p_data,
		uint32_t &p_input_id) const {
	// The first four bytes are reserved for the input_id.
	if (p_data.size() < 4) {
		return false;
	}

	const uint8_t *ptrw = p_data.ptr();
	const uint32_t *ptrw_32bit = reinterpret_cast<const uint32_t *>(ptrw);
	p_input_id = ptrw_32bit[0];

	return true;
}

bool NetworkedControllerBase::__input_data_set_first_input_id(
		Vector<uint8_t> &p_data,
		uint32_t p_input_id) {
	// The first four bytes are reserved for the input_id.
	if (p_data.size() < 4) {
		return false;
	}

	uint8_t *ptrw = p_data.ptrw();
	uint32_t *ptrw_32bit = reinterpret_cast<uint32_t *>(ptrw);
	ptrw_32bit[0] = p_input_id;

	return true;
}

RemotelyControlledController::RemotelyControlledController(NetworkedControllerBase *p_node) :
		Controller(p_node) {}

void RemotelyControlledController::on_peer_update(bool p_peer_enabled) {
	if (p_peer_enabled == peer_enabled) {
		// Nothing to updated.
		return;
	}

	peer_enabled = p_peer_enabled;

	// Client inputs reset.
	ghost_input_count = 0;
	snapshots.clear();
}

FrameIndex RemotelyControlledController::get_current_input_id() const {
	return current_input_buffer_id;
}

int RemotelyControlledController::get_inputs_count() const {
	return snapshots.size();
}

FrameIndex RemotelyControlledController::last_known_input() const {
	if (snapshots.size() > 0) {
		return snapshots.back().id;
	} else {
		return FrameIndex::NONE;
	}
}

bool RemotelyControlledController::fetch_next_input(real_t p_delta) {
	bool is_new_input = true;

	if (unlikely(current_input_buffer_id == FrameIndex::NONE)) {
		// As initial packet, anything is good.
		if (snapshots.empty() == false) {
			// First input arrived.
			set_frame_input(snapshots.front(), true);
			snapshots.pop_front();
			// Start tracing the packets from this moment on.
			SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] Input `" + uitos(current_input_buffer_id.id) + "` selected as first input.", true);
		} else {
			is_new_input = false;
			SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] Still no inputs.", true);
		}
	} else {
		const FrameIndex next_input_id = current_input_buffer_id + 1;
		SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The server is looking for: " + uitos(next_input_id.id), true);

		if (unlikely(streaming_paused)) {
			SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The streaming is paused.", true);
			// Stream is paused.
			if (snapshots.empty() == false &&
					snapshots.front().id >= next_input_id) {
				// A new input has arrived while the stream is paused.
				const bool is_buffer_void = (snapshots.front().buffer_size_bit - METADATA_SIZE) == 0;
				streaming_paused = is_buffer_void;
				set_frame_input(snapshots.front(), true);
				snapshots.pop_front();
				is_new_input = true;
			} else {
				// No inputs, or we are not yet arrived to the client input,
				// so just pretend the next input is void.
				node->set_inputs_buffer(BitArray(METADATA_SIZE), METADATA_SIZE, 0);
				is_new_input = false;
			}
		} else if (unlikely(snapshots.empty() == true)) {
			// The input buffer is empty; a packet is missing.
			SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] Missing input: " + uitos(next_input_id.id) + " Input buffer is void, i'm using the previous one!");

			is_new_input = false;
			ghost_input_count += 1;

		} else {
			SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The input buffer is not empty, so looking for the next input. Hopefully `" + uitos(next_input_id.id) + "`", true);

			// The input buffer is not empty, search the new input.
			if (next_input_id == snapshots.front().id) {
				SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The input `" + uitos(next_input_id.id) + "` was found.", true);

				// Wow, the next input is perfect!
				set_frame_input(snapshots.front(), false);
				snapshots.pop_front();

				ghost_input_count = 0;
			} else {
				// The next packet is not here. This can happen when:
				// - The packet is lost or not yet arrived.
				// - The client for any reason desync with the server.
				//
				// In this cases, the server has the hard task to re-sync.
				//
				// # What it does, then?
				// Initially it see that only 1 packet is missing so it just use
				// the previous one and increase `ghost_inputs_count` to 1.
				//
				// The next iteration, if the packet is not yet arrived the
				// server trys to take the next packet with the `id` less or
				// equal to `next_packet_id + ghost_packet_id`.
				//
				// As you can see the server doesn't lose immediately the hope
				// to find the missing packets, but at the same time deals with
				// it so increases its search pool per each iteration.
				//
				// # Wise input search.
				// Let's consider the case when a set of inputs arrive at the
				// same time, while the server is struggling for the missing packets.
				//
				// In the meanwhile that the packets were chilling on the net,
				// the server were simulating by guessing on their data; this
				// mean that they don't have any longer room to be simulated
				// when they arrive, and the right thing would be just forget
				// about these.
				//
				// The thing is that these can still contain meaningful data, so
				// instead to jump directly to the newest we restart the inputs
				// from the next important packet.
				//
				// For this reason we keep track the amount of missing packets
				// using `ghost_input_count`.

				SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The input `" + uitos(next_input_id.id) + "` was NOT found. Recovering process started.", true);
				SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] ghost_input_count: `" + itos(ghost_input_count) + "`", true);

				const int size = MIN(ghost_input_count, snapshots.size());
				const FrameIndex ghost_packet_id = next_input_id + ghost_input_count;

				bool recovered = false;
				FrameSnapshot pi;

				DataBuffer *pir_A = memnew(DataBuffer);
				DataBuffer *pir_B = memnew(DataBuffer);
				pir_A->copy(node->get_inputs_buffer());

				for (int i = 0; i < size; i += 1) {
					SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] checking if `" + uitos(snapshots.front().id.id) + "` can be used to recover `" + uitos(next_input_id.id) + "`.", true);

					if (ghost_packet_id < snapshots.front().id) {
						SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The input `" + uitos(snapshots.front().id.id) + "` can't be used as the ghost_packet_id (`" + uitos(ghost_packet_id.id) + "`) is more than the input.", true);
						break;
					} else {
						const FrameIndex input_id = snapshots.front().id;
						SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The input `" + uitos(input_id.id) + "` is eligible as next frame.", true);

						pi = snapshots.front();
						snapshots.pop_front();
						recovered = true;

						// If this input has some important changes compared to the last
						// good input, let's recover to this point otherwise skip it
						// until the last one.
						// Useful to avoid that the server stay too much behind the
						// client.

						pir_B->copy(pi.inputs_buffer);
						pir_B->shrink_to(METADATA_SIZE, pi.buffer_size_bit - METADATA_SIZE);

						pir_A->begin_read();
						pir_A->seek(METADATA_SIZE);
						pir_B->begin_read();
						pir_B->seek(METADATA_SIZE);

						const bool are_different = node->networked_controller_manager->are_inputs_different(*pir_A, *pir_B);
						if (are_different) {
							SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::fetch_next_input] The input `" + uitos(input_id.id) + "` is different from the one executed so far, so better to execute it.", true);
							break;
						}
					}
				}

				memdelete(pir_A);
				pir_A = nullptr;
				memdelete(pir_B);
				pir_B = nullptr;

				if (recovered) {
					set_frame_input(pi, false);
					ghost_input_count = 0;
					SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "Packet recovered. The new InputID is: `" + uitos(current_input_buffer_id.id) + "`");
				} else {
					ghost_input_count += 1;
					is_new_input = false;
					SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "Packet still missing, the server is still using the old input.");
				}
			}
		}
	}

#ifdef DEBUG_ENABLED
	if (snapshots.empty() == false && current_input_buffer_id != FrameIndex::NONE) {
		// At this point is guaranteed that the current_input_buffer_id is never
		// greater than the first item contained by `snapshots`.
		CRASH_COND(current_input_buffer_id >= snapshots.front().id);
	}
#endif
	return is_new_input;
}

void RemotelyControlledController::set_frame_input(const FrameSnapshot &p_frame_snapshot, bool p_first_input) {
	node->set_inputs_buffer(
			p_frame_snapshot.inputs_buffer,
			METADATA_SIZE,
			p_frame_snapshot.buffer_size_bit - METADATA_SIZE);
	current_input_buffer_id = p_frame_snapshot.id;
}

void RemotelyControlledController::process(double p_delta) {
	const bool is_new_input = fetch_next_input(p_delta);

	if (unlikely(current_input_buffer_id == FrameIndex::NONE)) {
		// Skip this until the first input arrive.
		SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "Server skips this frame as the current_input_buffer_id == UINT32_MAX", true);
		return;
	}

#ifdef DEBUG_ENABLED
	if (!is_new_input) {
		node->event_input_missed.broadcast(current_input_buffer_id + 1);
	}
#endif

	SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "RemotelyControlled process index: " + uitos(current_input_buffer_id.id), true);

	node->get_inputs_buffer_mut().begin_read();
	node->get_inputs_buffer_mut().seek(METADATA_SIZE);
	SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::READ);
	node->networked_controller_manager->controller_process(
			p_delta,
			node->get_inputs_buffer_mut());
	SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
}

bool is_remote_frame_A_older(const FrameSnapshot &p_snap_a, const FrameSnapshot &p_snap_b) {
	return p_snap_a.id < p_snap_b.id;
}

bool RemotelyControlledController::receive_inputs(const Vector<uint8_t> &p_data) {
	const uint32_t now = OS::get_singleton()->get_ticks_msec();
	struct SCParseTmpData {
		RemotelyControlledController *controller;
		NetworkedControllerBase *node_controller;
		uint32_t now;
	} tmp = {
		this,
		node,
		now
	};

	const bool success = node->__input_data_parse(
			p_data,
			&tmp,

			// Parse the Input:
			[](void *p_user_pointer, FrameIndex p_input_id, int p_input_size_in_bits, const BitArray &p_bit_array) -> void {
				SCParseTmpData *pd = static_cast<SCParseTmpData *>(p_user_pointer);

				if (unlikely(pd->controller->current_input_buffer_id != FrameIndex::NONE && pd->controller->current_input_buffer_id >= p_input_id)) {
					// We already have this input, so we don't need it anymore.
					return;
				}

				FrameSnapshot rfs;
				rfs.id = p_input_id;

				const bool found = std::binary_search(
						pd->controller->snapshots.begin(),
						pd->controller->snapshots.end(),
						rfs,
						is_remote_frame_A_older);

				if (!found) {
					rfs.buffer_size_bit = p_input_size_in_bits;
					rfs.inputs_buffer = p_bit_array;
					rfs.received_timestamp = pd->now;

					pd->controller->snapshots.push_back(rfs);

					// Sort the new inserted snapshot.
					std::sort(
							pd->controller->snapshots.begin(),
							pd->controller->snapshots.end(),
							is_remote_frame_A_older);
				}
			});

#ifdef DEBUG_ENABLED
	if (snapshots.empty() == false && current_input_buffer_id != FrameIndex::NONE) {
		// At this point is guaranteed that the current_input_buffer_id is never
		// greater than the first item contained by `snapshots`.
		CRASH_COND(current_input_buffer_id >= snapshots.front().id);
	}
#endif

	if (!success) {
		SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "[RemotelyControlledController::receive_input] Failed.");
	}

	return success;
}

ServerController::ServerController(
		NetworkedControllerBase *p_node,
		int p_traced_frames) :
		RemotelyControlledController(p_node),
		network_watcher(p_traced_frames, 0),
		consecutive_input_watcher(p_traced_frames, 0) {
}

void ServerController::process(double p_delta) {
	RemotelyControlledController::process(p_delta);

	if (!streaming_paused) {
		// Update the consecutive inputs.
		int consecutive_inputs = 0;
		for (std::size_t i = 0; i < snapshots.size(); i += 1) {
			if (snapshots[i].id == (current_input_buffer_id + consecutive_inputs + 1)) {
				consecutive_inputs += 1;
			}
		}
		consecutive_input_watcher.push(consecutive_inputs);
	}
}

void ServerController::on_peer_update(bool p_peer_enabled) {
	if (p_peer_enabled == peer_enabled) {
		// Nothing to updated.
		return;
	}

	// ~~ Reset everything to avoid accumulate old data. ~~
	RemotelyControlledController::on_peer_update(p_peer_enabled);

	additional_fps_notif_timer = 0.0;
	previous_frame_received_timestamp = UINT32_MAX;
	network_watcher.reset(0.0);
	consecutive_input_watcher.reset(0.0);
}

void ServerController::set_frame_input(const FrameSnapshot &p_frame_snapshot, bool p_first_input) {
	// If `previous_frame_received_timestamp` is bigger: the controller was
	// disabled, so nothing to do.
	if (previous_frame_received_timestamp < p_frame_snapshot.received_timestamp) {
		const double physics_ticks_per_second = Engine::get_singleton()->get_physics_ticks_per_second();
		const uint32_t frame_delta_ms = (1.0 / physics_ticks_per_second) * 1000.0;

		const uint32_t receival_time = p_frame_snapshot.received_timestamp - previous_frame_received_timestamp;
		const uint32_t network_time = receival_time > frame_delta_ms ? receival_time - frame_delta_ms : 0;

		network_watcher.push(network_time);
	}

	RemotelyControlledController::set_frame_input(p_frame_snapshot, p_first_input);

	if (p_first_input) {
		// Reset the watcher, as this is the first input.
		network_watcher.reset(0);
		consecutive_input_watcher.reset(0.0);
		previous_frame_received_timestamp = UINT32_MAX;
	} else {
		previous_frame_received_timestamp = p_frame_snapshot.received_timestamp;
	}
}

void ServerController::notify_send_state() {
	// If the notified input is a void buffer, the client is allowed to pause
	// the input streaming. So missing packets are just handled as void inputs.
	if (current_input_buffer_id != FrameIndex::NONE && node->get_inputs_buffer().size() == 0) {
		streaming_paused = true;
	}
}

bool ServerController::receive_inputs(const Vector<uint8_t> &p_data) {
	Vector<uint8_t> data = p_data;

	const bool success = RemotelyControlledController::receive_inputs(data);

	if (success) {
		uint32_t input_id;
		const bool extraction_success = node->__input_data_get_first_input_id(data, input_id);
		CRASH_COND(!extraction_success);

		// The input parsing succeded on the server, now ping pong this to all the dolls.
		for (int peer_id : peers_simulating_this_controller) {
			if (peer_id == node->server_get_associated_peer()) {
				continue;
			}

			// Convert the `input_id` to peer_id :: input_id.
			// So the peer can properly read the data.
			const uint32_t peer_input_id = convert_input_id_to(peer_id, input_id);

			if (peer_input_id == UINT32_MAX) {
				SceneSynchronizerDebugger::singleton()->debug_print(node->network_interface, "The `input_id` conversion failed for the peer `" + itos(peer_id) + "`. This is expected untill the client is fully initialized.", true);
				continue;
			}

			node->__input_data_set_first_input_id(data, peer_input_id);

			node->rpc_handle_receive_input.rpc(
					node->get_network_interface(),
					peer_id,
					data);
		}
	}

	return success;
}

uint32_t ServerController::convert_input_id_to(int p_other_peer, uint32_t p_input_id) const {
	ERR_FAIL_COND_V(p_input_id == UINT32_MAX, UINT32_MAX);
	CRASH_COND(node->server_get_associated_peer() == p_other_peer); // This function must never be called for the same peer controlling this Character.
	const FrameIndex current = get_current_input_id();
	const int64_t diff = int64_t(p_input_id) - int64_t(current.id);

	// Now find the other peer current_input_id to do the conversion.
	const NetworkedControllerBase *controller = node->get_scene_synchronizer()->get_controller_for_peer(p_other_peer, false);
	if (controller == nullptr || controller->get_current_input_id() == FrameIndex::NONE) {
		return UINT32_MAX;
	}
	return MAX(int64_t(controller->get_current_input_id().id) + diff, 0);
}

int ceil_with_tolerance(double p_value, double p_tolerance) {
	return std::ceil(p_value - p_tolerance);
}

std::int8_t ServerController::compute_client_tick_rate_distance_to_optimal() {
	const float min_frames_delay = node->get_min_frames_delay();
	const float max_frames_delay = node->get_max_frames_delay();
	const double fixed_frame_delta = node->scene_synchronizer->get_fixed_frame_delta();

	// `worst_receival_time` is in ms and indicates the maximum time passed to receive a consecutive
	// input in the last `network_traced_frames` frames.
	const std::uint32_t worst_receival_time_ms = network_watcher.max();

	const double worst_receival_time = double(worst_receival_time_ms) / 1000.0;

	const int optimal_frame_delay_unclamped = ceil_with_tolerance(
			worst_receival_time / fixed_frame_delta,
			fixed_frame_delta * 0.05); // Tolerance of 5% of frame time.

	const int optimal_frame_delay = CLAMP(optimal_frame_delay_unclamped, min_frames_delay, max_frames_delay);

	const int consecutive_inputs = consecutive_input_watcher.average_rounded();

	const std::int8_t distance_to_optimal = CLAMP(optimal_frame_delay - consecutive_inputs, INT8_MIN, INT8_MAX);

#ifdef DEBUG_ENABLED
	const bool debug = ProjectSettings::get_singleton()->get_setting("NetworkSynchronizer/debug_server_speedup");
	const int current_frame_delay = consecutive_inputs;
	if (debug) {
		NS::print_line(
				"Worst receival time (ms): `" + std::to_string(worst_receival_time_ms) +
				"` Optimal frame delay: `" + std::to_string(optimal_frame_delay) +
				"` Current frame delay: `" + std::to_string(current_frame_delay) +
				"` Distance to optimal: `" + std::to_string(distance_to_optimal) +
				"`");
	}
	node->event_client_speedup_adjusted.broadcast(worst_receival_time_ms, optimal_frame_delay, current_frame_delay, distance_to_optimal);
#endif

	return distance_to_optimal;
}

AutonomousServerController::AutonomousServerController(
		NetworkedControllerBase *p_node) :
		ServerController(p_node, 1) {
}

bool AutonomousServerController::receive_inputs(const Vector<uint8_t> &p_data) {
	SceneSynchronizerDebugger::singleton()->debug_warning(&node->get_network_interface(), "`receive_input` called on the `AutonomousServerController` - If this is called just after `set_server_controlled(true)` is called, you can ignore this warning, as the client is not aware about the switch for a really small window after this function call.");
	return false;
}

int AutonomousServerController::get_inputs_count() const {
	// No input collected by this class.
	return 0;
}

bool AutonomousServerController::fetch_next_input(real_t p_delta) {
	SceneSynchronizerDebugger::singleton()->debug_print(&node->get_network_interface(), "Autonomous server fetch input.", true);

	node->get_inputs_buffer_mut().begin_write(METADATA_SIZE);
	node->get_inputs_buffer_mut().seek(METADATA_SIZE);
	SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::WRITE);
	node->networked_controller_manager->collect_inputs(p_delta, node->get_inputs_buffer_mut());
	SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
	node->get_inputs_buffer_mut().dry();

	if (unlikely(current_input_buffer_id == FrameIndex::NONE)) {
		// This is the first input.
		current_input_buffer_id = { 0 };
	} else {
		// Just advance from now on.
		current_input_buffer_id += 1;
	}

	// The input is always new.
	return true;
}

PlayerController::PlayerController(NetworkedControllerBase *p_node) :
		Controller(p_node),
		current_input_id(FrameIndex::NONE),
		input_buffers_counter(0) {
}

void PlayerController::notify_input_checked(FrameIndex p_frame_index) {
	if (p_frame_index == FrameIndex::NONE) {
		// Nothing to do.
		return;
	}

	// Remove inputs prior to the known one. We may still need the known one
	// when the stream is paused.
	while (frames_snapshot.empty() == false && frames_snapshot.front().id <= p_frame_index) {
		if (frames_snapshot.front().id == p_frame_index) {
			streaming_paused = (frames_snapshot.front().buffer_size_bit - METADATA_SIZE) <= 0;
		}
		frames_snapshot.pop_front();
	}

#ifdef DEBUG_ENABLED
	// Unreachable, because the next input have always the next `p_input_id` or empty.
	CRASH_COND(frames_snapshot.empty() == false && (p_frame_index + 1) != frames_snapshot.front().id);
#endif

	// Make sure the remaining inputs are 0 sized, if not streaming can't be paused.
	if (streaming_paused) {
		for (auto it = frames_snapshot.begin(); it != frames_snapshot.end(); it += 1) {
			if ((it->buffer_size_bit - METADATA_SIZE) > 0) {
				// Streaming can't be paused.
				streaming_paused = false;
				break;
			}
		}
	}
}

int PlayerController::get_frames_input_count() const {
	return frames_snapshot.size();
}

FrameIndex PlayerController::last_known_input() const {
	return get_stored_input_id(-1);
}

FrameIndex PlayerController::get_stored_input_id(int p_i) const {
	if (p_i < 0) {
		if (frames_snapshot.empty() == false) {
			return frames_snapshot.back().id;
		} else {
			return FrameIndex::NONE;
		}
	} else {
		const size_t i = p_i;
		if (i < frames_snapshot.size()) {
			return frames_snapshot[i].id;
		} else {
			return FrameIndex::NONE;
		}
	}
}

void PlayerController::queue_instant_process(FrameIndex p_frame_index, int p_index, int p_count) {
	if (p_index >= 0 && p_index < int(frames_snapshot.size())) {
		queued_instant_to_process = p_index;
#ifdef DEBUG_ENABLED
		CRASH_COND(frames_snapshot[p_index].id != p_frame_index); // IMPOSSIBLE to trigger - without bugs.
#endif
	} else {
		queued_instant_to_process = -1;
	}
}

bool PlayerController::has_another_instant_to_process_after(int p_i) const {
	if (p_i >= 0 && p_i < int(frames_snapshot.size())) {
		return (p_i + 1) < int(frames_snapshot.size());
	} else {
		return false;
	}
}

void PlayerController::process(double p_delta) {
	if (unlikely(queued_instant_to_process >= 0)) {
		// There is a queued instant. It means the SceneSync is rewinding:
		// instead to fetch a new input, read it from the stored snapshots.
		DataBuffer ib(frames_snapshot[queued_instant_to_process].inputs_buffer);
		ib.shrink_to(METADATA_SIZE, frames_snapshot[queued_instant_to_process].buffer_size_bit - METADATA_SIZE);
		ib.begin_read();
		ib.seek(METADATA_SIZE);
		node->networked_controller_manager->controller_process(p_delta, ib);
		queued_instant_to_process = -1;
	} else {
		// Process a new frame.
		// This handles: 1. Read input 2. Process 3. Store the input

		// We need to know if we can accept a new input because in case of bad
		// internet connection we can't keep accumulating inputs forever
		// otherwise the server will differ too much from the client and we
		// introduce virtual lag.
		notify_input_checked(node->scene_synchronizer->client_get_last_checked_frame_index());
		const bool accept_new_inputs = can_accept_new_inputs();

		if (accept_new_inputs) {
			current_input_id = FrameIndex{ input_buffers_counter };

			SceneSynchronizerDebugger::singleton()->debug_print(&node->get_network_interface(), "Player process index: " + uitos(current_input_id.id), true);

			node->get_inputs_buffer_mut().begin_write(METADATA_SIZE);

			node->get_inputs_buffer_mut().seek(METADATA_SIZE);

			SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::WRITE);
			node->networked_controller_manager->collect_inputs(p_delta, node->get_inputs_buffer_mut());
			SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();

			// Set metadata data.
			node->get_inputs_buffer_mut().seek(0);
			if (node->get_inputs_buffer().size() > 0) {
				node->get_inputs_buffer_mut().add_bool(true);
				streaming_paused = false;
			} else {
				node->get_inputs_buffer_mut().add_bool(false);
			}
		} else {
			SceneSynchronizerDebugger::singleton()->debug_warning(&node->get_network_interface(), "It's not possible to accept new inputs. Is this lagging?");
		}

		node->get_inputs_buffer_mut().dry();
		node->get_inputs_buffer_mut().begin_read();
		node->get_inputs_buffer_mut().seek(METADATA_SIZE); // Skip meta.

		SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::READ);
		// The physics process is always emitted, because we still need to simulate
		// the character motion even if we don't store the player inputs.
		node->networked_controller_manager->controller_process(p_delta, node->get_inputs_buffer_mut());
		SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();

		node->player_set_has_new_input(false);
		if (!streaming_paused) {
			if (accept_new_inputs) {
				input_buffers_counter += 1;
				store_input_buffer(current_input_id);
				node->player_set_has_new_input(true);
			}

			// Keep sending inputs, despite the server seems not responding properly,
			// to make sure the server becomes up to date at some point.
			send_frame_input_buffer_to_server();
		}
	}
}

FrameIndex PlayerController::get_current_input_id() const {
	return current_input_id;
}

bool PlayerController::receive_inputs(const Vector<uint8_t> &p_data) {
	SceneSynchronizerDebugger::singleton()->debug_error(&node->get_network_interface(), "`receive_input` called on the `PlayerServerController` -This function is not supposed to be called on the player controller. Only the server and the doll should receive this.");
	return false;
}

void PlayerController::store_input_buffer(FrameIndex p_frame_index) {
	FrameSnapshot inputs;
	inputs.id = p_frame_index;
	inputs.inputs_buffer = node->get_inputs_buffer().get_buffer();
	inputs.buffer_size_bit = node->get_inputs_buffer().size() + METADATA_SIZE;
	inputs.similarity = FrameIndex::NONE;
	inputs.received_timestamp = UINT32_MAX;
	frames_snapshot.push_back(inputs);
}

void PlayerController::send_frame_input_buffer_to_server() {
	// The packet is composed as follow:
	// - The following four bytes for the first input ID.
	// - Array of inputs:
	// |-- First byte the amount of times this input is duplicated in the packet.
	// |-- Input buffer.

	const size_t inputs_count = MIN(frames_snapshot.size(), static_cast<size_t>(node->get_max_redundant_inputs() + 1));
	// This is unreachable because `can_accept_new_inputs()`, used just before
	// this function, checks the `frames_snapshot` array to definite
	// whether the client can collects new inputs and make sure it always contains
	// at least 1 input.
	// It means that, unless the streaming is paused, the `frames_snapshots`
	// is never going to be empty at this point.
	CRASH_COND(inputs_count < 1);

#define MAKE_ROOM(p_size)                                              \
	if (cached_packet_data.size() < static_cast<size_t>(ofs + p_size)) \
		cached_packet_data.resize(ofs + p_size);

	int ofs = 0;

	// Let's store the ID of the first snapshot.
	MAKE_ROOM(4);
	const FrameIndex first_input_id = frames_snapshot[frames_snapshot.size() - inputs_count].id;
	ofs += encode_uint32(first_input_id.id, cached_packet_data.ptr() + ofs);

	FrameIndex previous_input_id = FrameIndex::NONE;
	FrameIndex previous_input_similarity = FrameIndex::NONE;
	int previous_buffer_size = 0;
	uint8_t duplication_count = 0;

	DataBuffer *pir_A = memnew(DataBuffer);
	DataBuffer *pir_B = memnew(DataBuffer);
	pir_A->copy(node->get_inputs_buffer().get_buffer());

	// Compose the packets
	for (size_t i = frames_snapshot.size() - inputs_count; i < frames_snapshot.size(); i += 1) {
		bool is_similar = false;

		if (previous_input_id == FrameIndex::NONE) {
			// This happens for the first input of the packet.
			// Just write it.
			is_similar = false;
		} else if (duplication_count == UINT8_MAX) {
			// Prevent to overflow the `uint8_t`.
			is_similar = false;
		} else {
			if (frames_snapshot[i].similarity != previous_input_id) {
				if (frames_snapshot[i].similarity == FrameIndex::NONE) {
					// This input was never compared, let's do it now.
					pir_B->copy(frames_snapshot[i].inputs_buffer);
					pir_B->shrink_to(METADATA_SIZE, frames_snapshot[i].buffer_size_bit - METADATA_SIZE);

					pir_A->begin_read();
					pir_A->seek(METADATA_SIZE);
					pir_B->begin_read();
					pir_B->seek(METADATA_SIZE);

					const bool are_different = node->networked_controller_manager->are_inputs_different(*pir_A, *pir_B);
					is_similar = !are_different;

				} else if (frames_snapshot[i].similarity == previous_input_similarity) {
					// This input is similar to the previous one, the thing is
					// that the similarity check was done on an older input.
					// Fortunatelly we are able to compare the similarity id
					// and detect its similarity correctly.
					is_similar = true;
				} else {
					// This input is simply different from the previous one.
					is_similar = false;
				}
			} else {
				// These are the same, let's save some space.
				is_similar = true;
			}
		}

		if (current_input_id == previous_input_id) {
			SceneSynchronizerDebugger::singleton()->notify_are_inputs_different_result(&node->get_network_interface(), frames_snapshot[i].id.id, is_similar);
		} else if (current_input_id == frames_snapshot[i].id) {
			SceneSynchronizerDebugger::singleton()->notify_are_inputs_different_result(&node->get_network_interface(), previous_input_id.id, is_similar);
		}

		if (is_similar) {
			// This input is similar to the previous one, so just duplicate it.
			duplication_count += 1;
			// In this way, we don't need to compare these frames again.
			frames_snapshot[i].similarity = previous_input_id;

			SceneSynchronizerDebugger::singleton()->notify_input_sent_to_server(&node->get_network_interface(), frames_snapshot[i].id.id, previous_input_id.id);

		} else {
			// This input is different from the previous one, so let's
			// finalize the previous and start another one.

			SceneSynchronizerDebugger::singleton()->notify_input_sent_to_server(&node->get_network_interface(), frames_snapshot[i].id.id, frames_snapshot[i].id.id);

			if (previous_input_id != FrameIndex::NONE) {
				// We can finally finalize the previous input
				cached_packet_data[ofs - previous_buffer_size - 1] = duplication_count;
			}

			// Resets the duplication count.
			duplication_count = 0;

			// Writes the duplication_count for this new input
			MAKE_ROOM(1);
			cached_packet_data[ofs] = 0;
			ofs += 1;

			// Write the inputs
			const int buffer_size = frames_snapshot[i].inputs_buffer.get_bytes().size();
			MAKE_ROOM(buffer_size);
			memcpy(
					cached_packet_data.ptr() + ofs,
					frames_snapshot[i].inputs_buffer.get_bytes().ptr(),
					buffer_size);
			ofs += buffer_size;

			// Let's see if we can duplicate this input.
			previous_input_id = frames_snapshot[i].id;
			previous_input_similarity = frames_snapshot[i].similarity;
			previous_buffer_size = buffer_size;

			pir_A->get_buffer_mut() = frames_snapshot[i].inputs_buffer;
			pir_A->shrink_to(METADATA_SIZE, frames_snapshot[i].buffer_size_bit - METADATA_SIZE);
		}
	}

	memdelete(pir_A);
	pir_A = nullptr;
	memdelete(pir_B);
	pir_B = nullptr;

	// Finalize the last added input_buffer.
	cached_packet_data[ofs - previous_buffer_size - 1] = duplication_count;

	// Make the packet data.
	Vector<uint8_t> packet_data;
	packet_data.resize(ofs);

	memcpy(
			packet_data.ptrw(),
			cached_packet_data.ptr(),
			ofs);

	node->rpc_handle_receive_input.rpc(
			node->get_network_interface(),
			node->network_interface->get_server_peer(),
			packet_data);
}

bool PlayerController::can_accept_new_inputs() const {
	return frames_snapshot.size() < node->scene_synchronizer->get_client_max_frames_storage_size();
}

DollController::DollController(NetworkedControllerBase *p_node) :
		RemotelyControlledController(p_node) {
}

bool DollController::receive_inputs(const Vector<uint8_t> &p_data) {
	const uint32_t now = OS::get_singleton()->get_ticks_msec();
	struct SCParseTmpData {
		DollController *controller;
		NetworkedControllerBase *node_controller;
		uint32_t now;
	} tmp = {
		this,
		node,
		now
	};

	const bool success = node->__input_data_parse(
			p_data,
			&tmp,

			// Parse the Input:
			[](void *p_user_pointer, FrameIndex p_frame_index, int p_input_size_in_bits, const BitArray &p_bit_array) -> void {
				SCParseTmpData *pd = static_cast<SCParseTmpData *>(p_user_pointer);

				CRASH_COND(p_frame_index == FrameIndex::NONE);
				if (pd->controller->last_checked_input >= p_frame_index) {
					// This input is already processed.
					return;
				}

				FrameSnapshot rfs;
				rfs.id = p_frame_index;

				const bool found = std::binary_search(
						pd->controller->snapshots.begin(),
						pd->controller->snapshots.end(),
						rfs,
						is_remote_frame_A_older);

				if (!found) {
					rfs.buffer_size_bit = p_input_size_in_bits;
					rfs.inputs_buffer = p_bit_array;
					rfs.received_timestamp = pd->now;

					pd->controller->snapshots.push_back(rfs);

					// Sort the new inserted snapshots.
					std::sort(
							pd->controller->snapshots.begin(),
							pd->controller->snapshots.end(),
							is_remote_frame_A_older);
				}
			});

	if (!success) {
		SceneSynchronizerDebugger::singleton()->debug_print(&node->get_network_interface(), "[DollController::receive_input] Failed.");
	}

	return success;
}

void DollController::queue_instant_process(FrameIndex p_frame_index, int p_index, int p_count) {
	if (streaming_paused) {
		return;
	}

	for (size_t i = 0; i < snapshots.size(); ++i) {
		if (snapshots[i].id == p_frame_index) {
			queued_instant_to_process = i;
			return;
		}
	}

	SceneSynchronizerDebugger::singleton()->debug_warning(&node->get_network_interface(), "DollController was uable to find the input: " + uitos(p_frame_index.id) + " maybe it was never received?", true);
	queued_instant_to_process = snapshots.size();
	return;
}

bool DollController::fetch_next_input(real_t p_delta) {
	if (queued_instant_to_process >= 0) {
		if (queued_instant_to_process >= int(snapshots.size())) {
			return false;
		} else {
			// The SceneSync is rewinding the scene, so let's find the
			set_frame_input(snapshots[queued_instant_to_process], false);
			return true;
		}

	} else {
		if (current_input_buffer_id == FrameIndex::NONE) {
			if (snapshots.size() > 0) {
				// Anything, as first input is good.
				set_frame_input(snapshots.front(), true);
				return true;
			} else {
				return false;
			}
		} else {
			const FrameIndex next_input_id = current_input_buffer_id + 1;
			// Loop the snapshots.
			for (size_t i = 0; i < snapshots.size(); ++i) {
				// Take any NEXT snapshot. Eventually the rewind will fix this.
				// NOTE: the snapshots are sorted.
				if (snapshots[i].id >= next_input_id) {
					set_frame_input(snapshots[i], false);
					return true;
				}
			}
			if (snapshots.size() > 0) {
				set_frame_input(snapshots.back(), false);
				// true anyway, don't stop the processing, just use the input.
				return true;
			}
		}
	}
	return false;
}

void DollController::process(double p_delta) {
	notify_input_checked(node->scene_synchronizer->client_get_last_checked_frame_index());
	const bool is_new_input = fetch_next_input(p_delta);

	if (is_new_input) {
		SceneSynchronizerDebugger::singleton()->debug_print(&node->get_network_interface(), "Doll process index: " + uitos(current_input_buffer_id.id), true);

		node->get_inputs_buffer_mut().begin_read();
		node->get_inputs_buffer_mut().seek(METADATA_SIZE);
		SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::READ);
		node->networked_controller_manager->controller_process(
				p_delta,
				node->get_inputs_buffer_mut());
		SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
	}

	queued_instant_to_process = -1;
}

void DollController::notify_input_checked(FrameIndex p_frame_index) {
	if (p_frame_index == FrameIndex::NONE) {
		// Nothing to do.
		return;
	}

	// Remove inputs prior to the known one. We may still need the known one
	// when the stream is paused.
	while (snapshots.empty() == false && snapshots.front().id <= p_frame_index) {
		if (snapshots.front().id == p_frame_index) {
			streaming_paused = (snapshots.front().buffer_size_bit - METADATA_SIZE) <= 0;
		}
		snapshots.pop_front();
	}

	last_checked_input = p_frame_index;
}

NoNetController::NoNetController(NetworkedControllerBase *p_node) :
		Controller(p_node),
		frame_id(FrameIndex{ 0 }) {
}

void NoNetController::process(double p_delta) {
	node->get_inputs_buffer_mut().begin_write(0); // No need of meta in this case.
	SceneSynchronizerDebugger::singleton()->debug_print(&node->get_network_interface(), "Nonet process index: " + uitos(frame_id.id), true);
	SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::WRITE);
	node->networked_controller_manager->collect_inputs(p_delta, node->get_inputs_buffer_mut());
	SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
	node->get_inputs_buffer_mut().dry();
	node->get_inputs_buffer_mut().begin_read();
	SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(&node->get_network_interface(), SceneSynchronizerDebugger::READ);
	node->networked_controller_manager->controller_process(p_delta, node->get_inputs_buffer_mut());
	SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
	frame_id += 1;
}

FrameIndex NoNetController::get_current_input_id() const {
	return frame_id;
}

NS_NAMESPACE_END
