#include "peer_networked_controller.h"

#include "core/config/project_settings.h"
#include "core/io/marshalls.h"
#include "core/os/os.h"
#include "core/templates/vector.h"

#include "../scene_synchronizer.h"
#include "ensure.h"
#include "scene_synchronizer_debugger.h"
#include <algorithm>
#include <string>

#define METADATA_SIZE 1

NS_NAMESPACE_BEGIN

PeerNetworkedController::PeerNetworkedController() {
	inputs_buffer = memnew(DataBuffer);
}

PeerNetworkedController::~PeerNetworkedController() {
	_sorted_controllable_objects.clear();

	memdelete(inputs_buffer);
	inputs_buffer = nullptr;

	if (controller != nullptr) {
		memdelete(controller);
		controller = nullptr;
		controller_type = CONTROLLER_TYPE_NULL;
	}

	remove_synchronizer();
}

void PeerNetworkedController::notify_controllable_objects_changed() {
	are_controllable_objects_sorted = false;
}

const std::vector<ObjectData *> &PeerNetworkedController::get_sorted_controllable_objects() {
	if (!are_controllable_objects_sorted) {
		are_controllable_objects_sorted = true;
		_sorted_controllable_objects.clear();

		const std::vector<ObjectData *> *controlled_objects = scene_synchronizer->get_peer_controlled_objects_data(get_authority_peer());
		if (controlled_objects) {
			for (ObjectData *controlled_object_data : *controlled_objects) {
				if (is_player_controller()) {
					if (controlled_object_data->realtime_sync_enabled_on_client) {
						// This client is simulating it.
						_sorted_controllable_objects.push_back(controlled_object_data);
					}
				} else {
					_sorted_controllable_objects.push_back(controlled_object_data);
				}
			}
		}
	}

	return _sorted_controllable_objects;
}

void PeerNetworkedController::set_server_controlled(bool p_server_controlled) {
	if (server_controlled == p_server_controlled) {
		// It's the same, nothing to do.
		return;
	}

	if (is_networking_initialized()) {
		if (is_server_controller()) {
			// This is the server, let's start the procedure to switch controll mode.

#ifdef DEBUG_ENABLED
			ASSERT_COND_MSG(scene_synchronizer, "When the `NetworkedController` is a server, the `scene_synchronizer` is always set.");
#endif

			// First update the variable.
			server_controlled = p_server_controlled;

			// Notify the `SceneSynchronizer` about it.
			scene_synchronizer->notify_controller_control_mode_changed(this);

			// Tell the client to do the switch too.
			if (authority_peer != 1) {
				scene_synchronizer->call_rpc_set_server_controlled(
						authority_peer,
						authority_peer,
						server_controlled);
			} else {
				SceneSynchronizerDebugger::singleton()->print(WARNING, "The peer_controller is owned by the server, there is no client that can control it; please assign the proper authority.", "CONTROLLER-" + std::to_string(authority_peer));
			}

		} else if (is_player_controller() || is_doll_controller()) {
			SceneSynchronizerDebugger::singleton()->print(WARNING, "You should never call the function `set_server_controlled` on the client, this has an effect only if called on the server.", "CONTROLLER-" + std::to_string(authority_peer));

		} else if (is_nonet_controller()) {
			// There is no networking, the same instance is both the client and the
			// server already, nothing to do.
			server_controlled = p_server_controlled;

		} else {
#ifdef DEBUG_ENABLED
			ASSERT_NO_ENTRY_MSG("Unreachable, all the cases are handled.");
#endif
		}
	} else {
		// This called during initialization or on the editor, nothing special just
		// set it.
		server_controlled = p_server_controlled;
	}
}

bool PeerNetworkedController::get_server_controlled() const {
	return server_controlled;
}

void PeerNetworkedController::set_max_redundant_inputs(int p_max) {
	max_redundant_inputs = p_max;
}

int PeerNetworkedController::get_max_redundant_inputs() const {
	return max_redundant_inputs;
}

void PeerNetworkedController::set_network_traced_frames(int p_size) {
	network_traced_frames = p_size;
}

int PeerNetworkedController::get_network_traced_frames() const {
	return network_traced_frames;
}

void PeerNetworkedController::set_min_frames_delay(int p_val) {
	min_frames_delay = p_val;
}

int PeerNetworkedController::get_min_frames_delay() const {
	return min_frames_delay;
}

void PeerNetworkedController::set_max_frames_delay(int p_val) {
	max_frames_delay = p_val;
}

int PeerNetworkedController::get_max_frames_delay() const {
	return max_frames_delay;
}

FrameIndex PeerNetworkedController::get_current_frame_index() const {
	ENSURE_V(controller, FrameIndex::NONE);
	return controller->get_current_frame_index();
}

void PeerNetworkedController::server_set_peer_simulating_this_controller(int p_peer, bool p_simulating) {
	ENSURE_MSG(is_server_controller(), "This function can be called only on the server.");
	if (p_simulating) {
		VecFunc::insert_unique(get_server_controller()->peers_simulating_this_controller, p_peer);
	} else {
		VecFunc::remove(get_server_controller()->peers_simulating_this_controller, p_peer);
	}
}

bool PeerNetworkedController::server_is_peer_simulating_this_controller(int p_peer) const {
	ENSURE_V_MSG(is_server_controller(), false, "This function can be called only on the server.");
	return VecFunc::has(get_server_controller()->peers_simulating_this_controller, p_peer);
}

bool PeerNetworkedController::has_another_instant_to_process_after(int p_i) const {
	ENSURE_V_MSG(is_player_controller(), false, "Can be executed only on player controllers.");
	return static_cast<PlayerController *>(controller)->has_another_instant_to_process_after(p_i);
}

void PeerNetworkedController::process(double p_delta) {
	if make_likely (controller && can_simulate()) {
		// This function is registered as processed function, so it's called by the
		// `SceneSync` in sync with the scene processing.
		controller->process(p_delta);
	}
}

ServerController *PeerNetworkedController::get_server_controller() {
	ENSURE_V_MSG(is_server_controller(), nullptr, "This controller is not a server controller.");
	return static_cast<ServerController *>(controller);
}

const ServerController *PeerNetworkedController::get_server_controller() const {
	ENSURE_V_MSG(is_server_controller(), nullptr, "This controller is not a server controller.");
	return static_cast<const ServerController *>(controller);
}

ServerController *PeerNetworkedController::get_server_controller_unchecked() {
	return static_cast<ServerController *>(controller);
}

const ServerController *PeerNetworkedController::get_server_controller_unchecked() const {
	return static_cast<const ServerController *>(controller);
}

PlayerController *PeerNetworkedController::get_player_controller() {
	ENSURE_V_MSG(is_player_controller(), nullptr, "This controller is not a player controller.");
	return static_cast<PlayerController *>(controller);
}

const PlayerController *PeerNetworkedController::get_player_controller() const {
	ENSURE_V_MSG(is_player_controller(), nullptr, "This controller is not a player controller.");
	return static_cast<const PlayerController *>(controller);
}

DollController *PeerNetworkedController::get_doll_controller() {
	ENSURE_V_MSG(is_doll_controller(), nullptr, "This controller is not a doll controller.");
	return static_cast<DollController *>(controller);
}

const DollController *PeerNetworkedController::get_doll_controller() const {
	ENSURE_V_MSG(is_doll_controller(), nullptr, "This controller is not a doll controller.");
	return static_cast<const DollController *>(controller);
}

NoNetController *PeerNetworkedController::get_nonet_controller() {
	ENSURE_V_MSG(is_nonet_controller(), nullptr, "This controller is not a no net controller.");
	return static_cast<NoNetController *>(controller);
}

const NoNetController *PeerNetworkedController::get_nonet_controller() const {
	ENSURE_V_MSG(is_nonet_controller(), nullptr, "This controller is not a no net controller.");
	return static_cast<const NoNetController *>(controller);
}

bool PeerNetworkedController::is_networking_initialized() const {
	return controller_type != CONTROLLER_TYPE_NULL;
}

bool PeerNetworkedController::is_server_controller() const {
	return controller_type == CONTROLLER_TYPE_SERVER || controller_type == CONTROLLER_TYPE_AUTONOMOUS_SERVER;
}

bool PeerNetworkedController::is_player_controller() const {
	return controller_type == CONTROLLER_TYPE_PLAYER;
}

bool PeerNetworkedController::is_doll_controller() const {
	return controller_type == CONTROLLER_TYPE_DOLL;
}

bool PeerNetworkedController::is_nonet_controller() const {
	return controller_type == CONTROLLER_TYPE_NONETWORK;
}

void PeerNetworkedController::set_inputs_buffer(const BitArray &p_new_buffer, uint32_t p_metadata_size_in_bit, uint32_t p_size_in_bit) {
	inputs_buffer->get_buffer_mut().get_bytes_mut() = p_new_buffer.get_bytes();
	inputs_buffer->shrink_to(p_metadata_size_in_bit, p_size_in_bit);
}

void PeerNetworkedController::setup_synchronizer(NS::SceneSynchronizerBase &p_synchronizer, int p_peer) {
	ENSURE_MSG(scene_synchronizer == nullptr, "Cannot register with a new `SceneSynchronizer` because this controller is already registered with one. This is a bug, one controller should not be registered with two `SceneSynchronizer`s.");
	scene_synchronizer = &p_synchronizer;
	authority_peer = p_peer;

	event_handler_peer_status_updated =
			scene_synchronizer->event_peer_status_updated.bind(std::bind(&PeerNetworkedController::on_peer_status_updated, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void PeerNetworkedController::remove_synchronizer() {
	if (scene_synchronizer == nullptr) {
		// Nothing to unregister.
		return;
	}
	authority_peer = -1;

	// Unregister the event processors with the scene synchronizer.
	scene_synchronizer->event_peer_status_updated.unbind(event_handler_peer_status_updated);
	event_handler_peer_status_updated = NS::NullPHandler;
	scene_synchronizer = nullptr;
}

NS::SceneSynchronizerBase *PeerNetworkedController::get_scene_synchronizer() const {
	return scene_synchronizer;
}

bool PeerNetworkedController::has_scene_synchronizer() const {
	return scene_synchronizer;
}

void PeerNetworkedController::on_peer_status_updated(int p_peer_id, bool p_connected, bool p_enabled) {
	if (authority_peer == p_peer_id) {
		if (is_server_controller()) {
			get_server_controller()->on_peer_update(p_connected && p_enabled);
		}
	}
}

void PeerNetworkedController::controllable_collect_input(double p_delta, DataBuffer &r_data_buffer) {
	const std::vector<ObjectData *> &sorted_controllable_objects = get_sorted_controllable_objects();
	for (ObjectData *object_data : sorted_controllable_objects) {
		object_data->controller_funcs.collect_input(p_delta, r_data_buffer);
	}
}

int PeerNetworkedController::controllable_count_input_size(DataBuffer &p_data_buffer) {
	int size = 0;
	const std::vector<ObjectData *> &sorted_controllable_objects = get_sorted_controllable_objects();
	for (ObjectData *object_data : sorted_controllable_objects) {
		size += object_data->controller_funcs.count_input_size(p_data_buffer);
	}
	return size;
}

bool PeerNetworkedController::controllable_are_inputs_different(DataBuffer &p_data_buffer_A, DataBuffer &p_data_buffer_B) {
	const std::vector<ObjectData *> &sorted_controllable_objects = get_sorted_controllable_objects();
	for (ObjectData *object_data : sorted_controllable_objects) {
		if (object_data->controller_funcs.are_inputs_different(p_data_buffer_A, p_data_buffer_B)) {
			return true;
		}
	}
	return false;
}

void PeerNetworkedController::controllable_process(double p_delta, DataBuffer &p_data_buffer) {
	const std::vector<ObjectData *> &sorted_controllable_objects = get_sorted_controllable_objects();
	for (ObjectData *object_data : sorted_controllable_objects) {
		object_data->controller_funcs.process(p_delta, p_data_buffer);
	}
}

void PeerNetworkedController::notify_receive_inputs(const Vector<uint8_t> &p_data) {
	if (controller) {
		controller->receive_inputs(p_data);
	}
}

void PeerNetworkedController::notify_set_server_controlled(bool p_server_controlled) {
	ENSURE_MSG(is_player_controller(), "This function is supposed to be called on the server.");
	server_controlled = p_server_controlled;

	ENSURE_MSG(scene_synchronizer, "The server controller is supposed to be set on the client at this point.");
	scene_synchronizer->notify_controller_control_mode_changed(this);
}

void PeerNetworkedController::player_set_has_new_input(bool p_has) {
	has_player_new_input = p_has;
}

bool PeerNetworkedController::player_has_new_input() const {
	return has_player_new_input;
}

bool PeerNetworkedController::can_simulate() {
	NS_PROFILE

	const std::vector<ObjectData *> *controlled_objects = scene_synchronizer ? scene_synchronizer->get_peer_controlled_objects_data(get_authority_peer()) : nullptr;
	if (controlled_objects) {
		if (is_server_controller() || is_player_controller()) {
			return controlled_objects->size() > 0;
		} else {
			// TODO optimize by avoiding fetching the controlled objects in this way?
			for (const ObjectData *od : *controlled_objects) {
				if (od->realtime_sync_enabled_on_client) {
					return true;
				}
			}
		}
	}
	return false;
}

void PeerNetworkedController::notify_controller_reset() {
	event_controller_reset.broadcast();
}

bool PeerNetworkedController::__input_data_parse(
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

	ENSURE_V(data_len >= 4, false);
	const FrameIndex first_input_id = FrameIndex{ decode_uint32(p_data.ptr() + ofs) };
	ofs += 4;

	uint32_t inserted_input_count = 0;

	// Contains the entire packet and in turn it will be seek to specific location
	// so I will not need to copy chunk of the packet data.
	DataBuffer *pir = memnew(DataBuffer);
	pir->copy(p_data);
	pir->begin_read();

	while (ofs < data_len) {
		ENSURE_V_MSG(ofs + 1 <= data_len, false, "The arrived packet size doesn't meet the expected size.");
		// First byte is used for the duplication count.
		const uint8_t duplication = p_data[ofs];
		ofs += 1;

		// Validate input
		const int input_buffer_offset_bit = ofs * 8;
		pir->shrink_to(input_buffer_offset_bit, (data_len - ofs) * 8);
		pir->seek(input_buffer_offset_bit);
		// Read metadata
		const bool has_data = pir->read_bool();

		const int input_size_in_bits = (has_data ? int(controllable_count_input_size(*pir)) : 0) + METADATA_SIZE;

		// Pad to 8 bits.
		const int input_size_padded =
				Math::ceil(static_cast<float>(input_size_in_bits) / 8.0);
		ENSURE_V_MSG(ofs + input_size_padded <= data_len, false, "The arrived packet size doesn't meet the expected size.");

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

	ENSURE_V_MSG(ofs == data_len, false, "At the end was detected that the arrived packet has an unexpected size.");
	return true;
}

bool PeerNetworkedController::__input_data_get_first_input_id(
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

bool PeerNetworkedController::__input_data_set_first_input_id(
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

RemotelyControlledController::RemotelyControlledController(PeerNetworkedController *p_peer_controller) :
		Controller(p_peer_controller) {}

void RemotelyControlledController::on_peer_update(bool p_peer_enabled) {
	if (p_peer_enabled == peer_enabled) {
		// Nothing to updated.
		return;
	}

	peer_enabled = p_peer_enabled;

	// Client inputs reset.
	ghost_input_count = 0;
	frames_input.clear();
}

FrameIndex RemotelyControlledController::get_current_frame_index() const {
	return current_input_buffer_id;
}

int RemotelyControlledController::get_inputs_count() const {
	return frames_input.size();
}

FrameIndex RemotelyControlledController::last_known_frame_index() const {
	if (frames_input.size() > 0) {
		return frames_input.back().id;
	} else {
		return FrameIndex::NONE;
	}
}

bool RemotelyControlledController::fetch_next_input(double p_delta) {
	bool is_new_input = true;

	if (unlikely(current_input_buffer_id == FrameIndex::NONE)) {
		// As initial packet, anything is good.
		if (frames_input.empty() == false) {
			// First input arrived.
			set_frame_input(frames_input.front(), true);
			frames_input.pop_front();
			// Start tracing the packets from this moment on.
			SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] Input `" + current_input_buffer_id + "` selected as first input.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
		} else {
			is_new_input = false;
			SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] Still no inputs.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
		}
	} else {
		const FrameIndex next_input_id = current_input_buffer_id + 1;
		SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] The server is looking for: " + next_input_id, "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

		if (unlikely(streaming_paused)) {
			SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] The streaming is paused.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
			// Stream is paused.
			if (frames_input.empty() == false &&
					frames_input.front().id >= next_input_id) {
				// A new input has arrived while the stream is paused.
				const bool is_buffer_void = (frames_input.front().buffer_size_bit - METADATA_SIZE) == 0;
				streaming_paused = is_buffer_void;
				set_frame_input(frames_input.front(), true);
				frames_input.pop_front();
				is_new_input = true;
			} else {
				// No inputs, or we are not yet arrived to the client input,
				// so just pretend the next input is void.
				peer_controller->set_inputs_buffer(BitArray(METADATA_SIZE), METADATA_SIZE, 0);
				is_new_input = false;
			}
		} else if (unlikely(frames_input.empty() == true)) {
			// The input buffer is empty; a packet is missing.
			SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] Missing input: " + std::to_string(next_input_id.id) + " Input buffer is void, i'm using the previous one!", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

			is_new_input = false;
			ghost_input_count += 1;

		} else {
			SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] The input buffer is not empty, so looking for the next input. Hopefully `" + std::to_string(next_input_id.id) + "`", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

			// The input buffer is not empty, search the new input.
			if (next_input_id == frames_input.front().id) {
				SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] The input `" + std::to_string(next_input_id.id) + "` was found.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

				// Wow, the next input is perfect!
				set_frame_input(frames_input.front(), false);
				frames_input.pop_front();

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

				SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] The input `" + std::to_string(next_input_id.id) + "` was NOT found. Recovering process started.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
				SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] ghost_input_count: `" + std::to_string(ghost_input_count) + "`", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

				const int size = MIN(ghost_input_count, frames_input.size());
				const FrameIndex ghost_packet_id = next_input_id + ghost_input_count;

				bool recovered = false;
				FrameInput pi;

				DataBuffer *pir_A = memnew(DataBuffer);
				DataBuffer *pir_B = memnew(DataBuffer);
				pir_A->copy(peer_controller->get_inputs_buffer());

				for (int i = 0; i < size; i += 1) {
					SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] checking if `" + std::string(frames_input.front().id) + "` can be used to recover `" + std::string(next_input_id) + "`.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

					if (ghost_packet_id < frames_input.front().id) {
						SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] The input `" + frames_input.front().id + "` can't be used as the ghost_packet_id (`" + std::string(ghost_packet_id) + "`) is more than the input.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
						break;
					} else {
						const FrameIndex input_id = frames_input.front().id;
						SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] The input `" + std::string(input_id) + "` is eligible as next frame.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

						pi = frames_input.front();
						frames_input.pop_front();
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

						const bool are_different = peer_controller->controllable_are_inputs_different(*pir_A, *pir_B);
						if (are_different) {
							SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::fetch_next_input] The input `" + input_id + "` is different from the one executed so far, so better to execute it.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
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
					SceneSynchronizerDebugger::singleton()->print(INFO, "Packet recovered. The new InputID is: `" + current_input_buffer_id + "`", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
				} else {
					ghost_input_count += 1;
					is_new_input = false;
					SceneSynchronizerDebugger::singleton()->print(INFO, "Packet still missing, the server is still using the old input.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
				}
			}
		}
	}

#ifdef DEBUG_ENABLED
	if (frames_input.empty() == false && current_input_buffer_id != FrameIndex::NONE) {
		// At this point is guaranteed that the current_input_buffer_id is never
		// greater than the first item contained by `snapshots`.
		ASSERT_COND(current_input_buffer_id < frames_input.front().id);
	}
#endif
	return is_new_input;
}

void RemotelyControlledController::set_frame_input(const FrameInput &p_frame_snapshot, bool p_first_input) {
	peer_controller->set_inputs_buffer(
			p_frame_snapshot.inputs_buffer,
			METADATA_SIZE,
			p_frame_snapshot.buffer_size_bit - METADATA_SIZE);
	current_input_buffer_id = p_frame_snapshot.id;
}

void RemotelyControlledController::process(double p_delta) {
	const bool is_new_input = fetch_next_input(p_delta);

	if (unlikely(current_input_buffer_id == FrameIndex::NONE)) {
		// Skip this until the first input arrive.
		SceneSynchronizerDebugger::singleton()->print(INFO, "Server skips this frame as the current_input_buffer_id == UINT32_MAX", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
		return;
	}

#ifdef DEBUG_ENABLED
	if (!is_new_input) {
		peer_controller->event_input_missed.broadcast(current_input_buffer_id + 1);
	}
#endif

	SceneSynchronizerDebugger::singleton()->print(INFO, "RemotelyControlled process index: " + current_input_buffer_id, "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

	peer_controller->get_inputs_buffer_mut().begin_read();
	peer_controller->get_inputs_buffer_mut().seek(METADATA_SIZE);
	SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(peer_controller->authority_peer, SceneSynchronizerDebugger::READ);
	peer_controller->controllable_process(
			p_delta,
			peer_controller->get_inputs_buffer_mut());
	SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
}

bool is_remote_frame_A_older(const FrameInput &p_snap_a, const FrameInput &p_snap_b) {
	return p_snap_a.id < p_snap_b.id;
}

bool RemotelyControlledController::receive_inputs(const Vector<uint8_t> &p_data) {
	const uint32_t now = OS::get_singleton()->get_ticks_msec();
	struct SCParseTmpData {
		RemotelyControlledController &controller;
		uint32_t now;
	} tmp = {
		*this,
		now
	};

	const bool success = peer_controller->__input_data_parse(
			p_data,
			&tmp,

			// Parse the Input:
			[](void *p_user_pointer, FrameIndex p_input_id, int p_input_size_in_bits, const BitArray &p_bit_array) -> void {
				SCParseTmpData *pd = static_cast<SCParseTmpData *>(p_user_pointer);

				if (unlikely(pd->controller.current_input_buffer_id != FrameIndex::NONE && pd->controller.current_input_buffer_id >= p_input_id)) {
					// We already have this input, so we don't need it anymore.
					return;
				}

				FrameInput rfs;
				rfs.id = p_input_id;

				const bool found = std::binary_search(
						pd->controller.frames_input.begin(),
						pd->controller.frames_input.end(),
						rfs,
						is_remote_frame_A_older);

				if (!found) {
					rfs.buffer_size_bit = p_input_size_in_bits;
					rfs.inputs_buffer = p_bit_array;
					rfs.received_timestamp = pd->now;

					pd->controller.frames_input.push_back(rfs);

					// Sort the added frame input.
					std::sort(
							pd->controller.frames_input.begin(),
							pd->controller.frames_input.end(),
							is_remote_frame_A_older);
				}
			});

#ifdef DEBUG_ENABLED
	if (frames_input.empty() == false && current_input_buffer_id != FrameIndex::NONE) {
		// At this point is guaranteed that the current_input_buffer_id is never
		// greater than the first item contained by `snapshots`.
		ASSERT_COND(current_input_buffer_id < frames_input.front().id);
	}
#endif

	if (!success) {
		SceneSynchronizerDebugger::singleton()->print(INFO, "[RemotelyControlledController::receive_input] Failed.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer), true);
	}

	return success;
}

ServerController::ServerController(
		PeerNetworkedController *p_peer_controller,
		int p_traced_frames) :
		RemotelyControlledController(p_peer_controller),
		network_watcher(p_traced_frames, 0),
		consecutive_input_watcher(p_traced_frames, 0) {
}

void ServerController::process(double p_delta) {
	RemotelyControlledController::process(p_delta);

	if (!streaming_paused) {
		// Update the consecutive inputs.
		int consecutive_inputs = 0;
		for (std::size_t i = 0; i < frames_input.size(); i += 1) {
			if (frames_input[i].id == (current_input_buffer_id + consecutive_inputs + 1)) {
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

void ServerController::set_frame_input(const FrameInput &p_frame_snapshot, bool p_first_input) {
	// If `previous_frame_received_timestamp` is bigger: the controller was
	// disabled, so nothing to do.
	if (previous_frame_received_timestamp < p_frame_snapshot.received_timestamp) {
		const uint32_t frame_delta_ms = peer_controller->scene_synchronizer->get_fixed_frame_delta() * 1000.0;

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
	if (current_input_buffer_id != FrameIndex::NONE && peer_controller->get_inputs_buffer().size() == 0) {
		streaming_paused = true;
	}
}

bool ServerController::receive_inputs(const Vector<uint8_t> &p_data) {
	Vector<uint8_t> data = p_data;

	const bool success = RemotelyControlledController::receive_inputs(data);

	if (success) {
		uint32_t input_id;
		const bool extraction_success = peer_controller->__input_data_get_first_input_id(data, input_id);
		ASSERT_COND(extraction_success);

		// The input parsing succeded on the server, now ping pong this to all the dolls.
		for (int peer_id : peers_simulating_this_controller) {
			if (peer_id == peer_controller->authority_peer) {
				continue;
			}

			// Convert the `input_id` to peer_id :: input_id.
			// So the peer can properly read the data.
			const uint32_t peer_input_id = convert_input_id_to(peer_id, input_id);

			if (peer_input_id == UINT32_MAX) {
				SceneSynchronizerDebugger::singleton()->print(INFO, "The `input_id` conversion failed for the peer `" + std::to_string(peer_id) + "`. This is expected untill the client is fully initialized.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
				continue;
			}

			peer_controller->__input_data_set_first_input_id(data, peer_input_id);

			peer_controller->scene_synchronizer->call_rpc_receive_inputs(
					peer_id,
					peer_controller->authority_peer,
					data);
		}
	}

	return success;
}

uint32_t ServerController::convert_input_id_to(int p_other_peer, uint32_t p_input_id) const {
	ENSURE_V(p_input_id != UINT32_MAX, UINT32_MAX);
	// This function must never be called for the same peer controlling this Character.
	ASSERT_COND(peer_controller->authority_peer != p_other_peer);
	const FrameIndex current = get_current_frame_index();
	const int64_t diff = int64_t(p_input_id) - int64_t(current.id);

	// Now find the other peer current_input_id to do the conversion.
	const PeerNetworkedController *controller = peer_controller->get_scene_synchronizer()->get_controller_for_peer(p_other_peer, false);
	if (controller == nullptr || controller->get_current_frame_index() == FrameIndex::NONE) {
		return UINT32_MAX;
	}
	return MAX(int64_t(controller->get_current_frame_index().id) + diff, 0);
}

int ceil_with_tolerance(double p_value, double p_tolerance) {
	return std::ceil(p_value - p_tolerance);
}

std::int8_t ServerController::compute_client_tick_rate_distance_to_optimal() {
	const float min_frames_delay = peer_controller->get_min_frames_delay();
	const float max_frames_delay = peer_controller->get_max_frames_delay();
	const double fixed_frame_delta = peer_controller->scene_synchronizer->get_fixed_frame_delta();

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
		SceneSynchronizerDebugger::singleton()->print(
				INFO,
				"Worst receival time (ms): `" + std::to_string(worst_receival_time_ms) +
						"` Optimal frame delay: `" + std::to_string(optimal_frame_delay) +
						"` Current frame delay: `" + std::to_string(current_frame_delay) +
						"` Distance to optimal: `" + std::to_string(distance_to_optimal) +
						"`",
				"NetController",
				true);
	}
	peer_controller->event_client_speedup_adjusted.broadcast(worst_receival_time_ms, optimal_frame_delay, current_frame_delay, distance_to_optimal);
#endif

	return distance_to_optimal;
}

AutonomousServerController::AutonomousServerController(
		PeerNetworkedController *p_peer_controller) :
		ServerController(p_peer_controller, 1) {
}

bool AutonomousServerController::receive_inputs(const Vector<uint8_t> &p_data) {
	SceneSynchronizerDebugger::singleton()->print(WARNING, "`receive_input` called on the `AutonomousServerController` - If this is called just after `set_server_controlled(true)` is called, you can ignore this warning, as the client is not aware about the switch for a really small window after this function call.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
	return false;
}

int AutonomousServerController::get_inputs_count() const {
	// No input collected by this class.
	return 0;
}

bool AutonomousServerController::fetch_next_input(double p_delta) {
	SceneSynchronizerDebugger::singleton()->print(INFO, "Autonomous server fetch input.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

	peer_controller->get_inputs_buffer_mut().begin_write(METADATA_SIZE);
	peer_controller->get_inputs_buffer_mut().seek(METADATA_SIZE);
	SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(peer_controller->authority_peer, SceneSynchronizerDebugger::WRITE);
	peer_controller->controllable_collect_input(p_delta, peer_controller->get_inputs_buffer_mut());
	SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
	peer_controller->get_inputs_buffer_mut().dry();

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

PlayerController::PlayerController(PeerNetworkedController *p_peer_controller) :
		Controller(p_peer_controller),
		current_input_id(FrameIndex::NONE),
		input_buffers_counter(0) {
	event_handler_rewind_frame_begin =
			peer_controller->scene_synchronizer->event_rewind_frame_begin.bind(std::bind(&PlayerController::on_rewind_frame_begin, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	event_handler_state_validated =
			peer_controller->scene_synchronizer->event_state_validated.bind(std::bind(&PlayerController::on_state_validated, this, std::placeholders::_1, std::placeholders::_2));
}

PlayerController::~PlayerController() {
	peer_controller->scene_synchronizer->event_rewind_frame_begin.unbind(event_handler_rewind_frame_begin);
	event_handler_rewind_frame_begin = NS::NullPHandler;

	peer_controller->scene_synchronizer->event_state_validated.unbind(event_handler_state_validated);
	event_handler_state_validated = NS::NullPHandler;
}

void PlayerController::notify_frame_checked(FrameIndex p_frame_index) {
	if (p_frame_index == FrameIndex::NONE) {
		// Nothing to do.
		return;
	}

	// Remove inputs prior to the known one. We may still need the known one
	// when the stream is paused.
	while (frames_input.empty() == false && frames_input.front().id <= p_frame_index) {
		if (frames_input.front().id == p_frame_index) {
			streaming_paused = (frames_input.front().buffer_size_bit - METADATA_SIZE) <= 0;
		}
		frames_input.pop_front();
	}

#ifdef DEBUG_ENABLED
	// Unreachable, because the next frame have always the next `p_frame_index` or empty.
	ASSERT_COND(frames_input.empty() || (p_frame_index + 1) == frames_input.front().id);
#endif

	// Make sure the remaining inputs are 0 sized, if not streaming can't be paused.
	if (streaming_paused) {
		for (auto it = frames_input.begin(); it != frames_input.end(); it += 1) {
			if ((it->buffer_size_bit - METADATA_SIZE) > 0) {
				// Streaming can't be paused.
				streaming_paused = false;
				break;
			}
		}
	}
}

int PlayerController::get_frames_count() const {
	return frames_input.size();
}

int PlayerController::count_frames_after(FrameIndex p_frame_index) const {
	NS_PROFILE

	int count = 0;

	for (const FrameInput &frame : frames_input) {
		if (frame.id > p_frame_index) {
			count += 1;
		}
	}

	return count;
}

FrameIndex PlayerController::last_known_frame_index() const {
	return get_stored_frame_index(-1);
}

FrameIndex PlayerController::get_stored_frame_index(int p_i) const {
	if (p_i < 0) {
		if (frames_input.empty() == false) {
			return frames_input.back().id;
		} else {
			return FrameIndex::NONE;
		}
	} else {
		const size_t i = p_i;
		if (i < frames_input.size()) {
			return frames_input[i].id;
		} else {
			return FrameIndex::NONE;
		}
	}
}

void PlayerController::on_rewind_frame_begin(FrameIndex p_frame_index, int p_index, int p_count) {
	if (!peer_controller->can_simulate()) {
		return;
	}

	if (p_index >= 0 && p_index < int(frames_input.size())) {
		queued_instant_to_process = p_index;
#ifdef DEBUG_ENABLED
		// IMPOSSIBLE to trigger - without bugs.
		ASSERT_COND(frames_input[p_index].id == p_frame_index);
#endif
	} else {
		queued_instant_to_process = -1;
	}
}

bool PlayerController::has_another_instant_to_process_after(int p_i) const {
	if (p_i >= 0 && p_i < int(frames_input.size())) {
		return (p_i + 1) < int(frames_input.size());
	} else {
		return false;
	}
}

void PlayerController::process(double p_delta) {
	if (unlikely(queued_instant_to_process >= 0)) {
		// There is a queued instant. It means the SceneSync is rewinding:
		// instead to fetch a new input, read it from the stored snapshots.
		DataBuffer ib(frames_input[queued_instant_to_process].inputs_buffer);
		ib.shrink_to(METADATA_SIZE, frames_input[queued_instant_to_process].buffer_size_bit - METADATA_SIZE);
		ib.begin_read();
		ib.seek(METADATA_SIZE);
		peer_controller->controllable_process(p_delta, ib);
		queued_instant_to_process = -1;
	} else {
		// Process a new frame.
		// This handles: 1. Read input 2. Process 3. Store the input

		// We need to know if we can accept a new input because in case of bad
		// internet connection we can't keep accumulating inputs forever
		// otherwise the server will differ too much from the client and we
		// introduce virtual lag.
		notify_frame_checked(peer_controller->scene_synchronizer->client_get_last_checked_frame_index());
		const bool accept_new_inputs = can_accept_new_inputs();

		if (accept_new_inputs) {
			current_input_id = FrameIndex{ input_buffers_counter };

			SceneSynchronizerDebugger::singleton()->print(INFO, "Player process index: " + std::string(current_input_id), "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

			peer_controller->get_inputs_buffer_mut().begin_write(METADATA_SIZE);

			peer_controller->get_inputs_buffer_mut().seek(METADATA_SIZE);

			SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(peer_controller->authority_peer, SceneSynchronizerDebugger::WRITE);
			peer_controller->controllable_collect_input(p_delta, peer_controller->get_inputs_buffer_mut());
			SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();

			// Set metadata data.
			peer_controller->get_inputs_buffer_mut().seek(0);
			if (peer_controller->get_inputs_buffer().size() > 0) {
				peer_controller->get_inputs_buffer_mut().add_bool(true);
				streaming_paused = false;
			} else {
				peer_controller->get_inputs_buffer_mut().add_bool(false);
			}
		} else {
			SceneSynchronizerDebugger::singleton()->print(WARNING, "It's not possible to accept new inputs. Is this lagging?", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
		}

		peer_controller->get_inputs_buffer_mut().dry();
		peer_controller->get_inputs_buffer_mut().begin_read();
		peer_controller->get_inputs_buffer_mut().seek(METADATA_SIZE); // Skip meta.

		SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(peer_controller->authority_peer, SceneSynchronizerDebugger::READ);
		// The physics process is always emitted, because we still need to simulate
		// the character motion even if we don't store the player inputs.
		peer_controller->controllable_process(p_delta, peer_controller->get_inputs_buffer_mut());
		SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();

		peer_controller->player_set_has_new_input(false);
		if (!streaming_paused) {
			if (accept_new_inputs) {
				input_buffers_counter += 1;
				store_input_buffer(current_input_id);
				peer_controller->player_set_has_new_input(true);
			}

			// Keep sending inputs, despite the server seems not responding properly,
			// to make sure the server becomes up to date at some point.
			send_frame_input_buffer_to_server();
		}
	}
}

void PlayerController::on_state_validated(FrameIndex p_frame_index, bool p_detected_desync) {
	notify_frame_checked(p_frame_index);
}

FrameIndex PlayerController::get_current_frame_index() const {
	return current_input_id;
}

bool PlayerController::receive_inputs(const Vector<uint8_t> &p_data) {
	SceneSynchronizerDebugger::singleton()->print(NS::ERROR, "`receive_input` called on the `PlayerServerController` -This function is not supposed to be called on the player controller. Only the server and the doll should receive this.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
	return false;
}

void PlayerController::store_input_buffer(FrameIndex p_frame_index) {
	FrameInput inputs;
	inputs.id = p_frame_index;
	inputs.inputs_buffer = peer_controller->get_inputs_buffer().get_buffer();
	inputs.buffer_size_bit = peer_controller->get_inputs_buffer().size() + METADATA_SIZE;
	inputs.similarity = FrameIndex::NONE;
	inputs.received_timestamp = UINT32_MAX;
	frames_input.push_back(inputs);
}

void PlayerController::send_frame_input_buffer_to_server() {
	// The packet is composed as follow:
	// - The following four bytes for the first input ID.
	// - Array of inputs:
	// |-- First byte the amount of times this input is duplicated in the packet.
	// |-- Input buffer.

	const size_t inputs_count = MIN(frames_input.size(), static_cast<size_t>(peer_controller->get_max_redundant_inputs() + 1));
	// This is unreachable because `can_accept_new_inputs()`, used just before
	// this function, checks the `frames_input` array to definite
	// whether the client can collects new inputs and make sure it always contains
	// at least 1 input.
	// It means that, unless the streaming is paused, the `frames_inputs`
	// is never going to be empty at this point.
	ASSERT_COND(inputs_count >= 1);

#define MAKE_ROOM(p_size)                                              \
	if (cached_packet_data.size() < static_cast<size_t>(ofs + p_size)) \
		cached_packet_data.resize(ofs + p_size);

	int ofs = 0;

	// Let's store the ID of the first snapshot.
	MAKE_ROOM(4);
	const FrameIndex first_input_id = frames_input[frames_input.size() - inputs_count].id;
	ofs += encode_uint32(first_input_id.id, cached_packet_data.ptr() + ofs);

	FrameIndex previous_input_id = FrameIndex::NONE;
	FrameIndex previous_input_similarity = FrameIndex::NONE;
	int previous_buffer_size = 0;
	uint8_t duplication_count = 0;

	DataBuffer *pir_A = memnew(DataBuffer);
	DataBuffer *pir_B = memnew(DataBuffer);
	pir_A->copy(peer_controller->get_inputs_buffer().get_buffer());

	// Compose the packets
	for (size_t i = frames_input.size() - inputs_count; i < frames_input.size(); i += 1) {
		bool is_similar = false;

		if (previous_input_id == FrameIndex::NONE) {
			// This happens for the first input of the packet.
			// Just write it.
			is_similar = false;
		} else if (duplication_count == UINT8_MAX) {
			// Prevent to overflow the `uint8_t`.
			is_similar = false;
		} else {
			if (frames_input[i].similarity != previous_input_id) {
				if (frames_input[i].similarity == FrameIndex::NONE) {
					// This input was never compared, let's do it now.
					pir_B->copy(frames_input[i].inputs_buffer);
					pir_B->shrink_to(METADATA_SIZE, frames_input[i].buffer_size_bit - METADATA_SIZE);

					pir_A->begin_read();
					pir_A->seek(METADATA_SIZE);
					pir_B->begin_read();
					pir_B->seek(METADATA_SIZE);

					const bool are_different = peer_controller->controllable_are_inputs_different(*pir_A, *pir_B);
					is_similar = !are_different;

				} else if (frames_input[i].similarity == previous_input_similarity) {
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
			SceneSynchronizerDebugger::singleton()->notify_are_inputs_different_result(peer_controller->authority_peer, frames_input[i].id.id, is_similar);
		} else if (current_input_id == frames_input[i].id) {
			SceneSynchronizerDebugger::singleton()->notify_are_inputs_different_result(peer_controller->authority_peer, previous_input_id.id, is_similar);
		}

		if (is_similar) {
			// This input is similar to the previous one, so just duplicate it.
			duplication_count += 1;
			// In this way, we don't need to compare these frames again.
			frames_input[i].similarity = previous_input_id;

			SceneSynchronizerDebugger::singleton()->notify_input_sent_to_server(peer_controller->authority_peer, frames_input[i].id.id, previous_input_id.id);

		} else {
			// This input is different from the previous one, so let's
			// finalize the previous and start another one.

			SceneSynchronizerDebugger::singleton()->notify_input_sent_to_server(peer_controller->authority_peer, frames_input[i].id.id, frames_input[i].id.id);

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
			const int buffer_size = frames_input[i].inputs_buffer.get_bytes().size();
			MAKE_ROOM(buffer_size);
			memcpy(
					cached_packet_data.ptr() + ofs,
					frames_input[i].inputs_buffer.get_bytes().ptr(),
					buffer_size);
			ofs += buffer_size;

			// Let's see if we can duplicate this input.
			previous_input_id = frames_input[i].id;
			previous_input_similarity = frames_input[i].similarity;
			previous_buffer_size = buffer_size;

			pir_A->get_buffer_mut() = frames_input[i].inputs_buffer;
			pir_A->shrink_to(METADATA_SIZE, frames_input[i].buffer_size_bit - METADATA_SIZE);
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

	peer_controller->scene_synchronizer->call_rpc_receive_inputs(
			peer_controller->scene_synchronizer->get_network_interface().get_server_peer(),
			peer_controller->authority_peer,
			packet_data);
}

bool PlayerController::can_accept_new_inputs() const {
	return frames_input.size() < peer_controller->scene_synchronizer->get_client_max_frames_storage_size();
}

DollController::DollController(PeerNetworkedController *p_peer_controller) :
		RemotelyControlledController(p_peer_controller) {
	event_handler_received_snapshot =
			peer_controller->scene_synchronizer->event_received_server_snapshot.bind(std::bind(&DollController::on_received_server_snapshot, this, std::placeholders::_1));

	event_handler_client_snapshot_updated =
			peer_controller->scene_synchronizer->event_snapshot_update_finished.bind(std::bind(&DollController::on_snapshot_update_finished, this, std::placeholders::_1));

	event_handler_rewind_frame_begin =
			peer_controller->scene_synchronizer->event_rewind_frame_begin.bind(std::bind(&DollController::on_rewind_frame_begin, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	event_handler_state_validated =
			peer_controller->scene_synchronizer->event_state_validated.bind(std::bind(&DollController::on_state_validated, this, std::placeholders::_1, std::placeholders::_2));

	event_handler_state_validated =
			peer_controller->scene_synchronizer->event_snapshot_applied.bind(std::bind(&DollController::on_snapshot_applied, this, std::placeholders::_1));
}

DollController::~DollController() {
	peer_controller->scene_synchronizer->event_received_server_snapshot.unbind(event_handler_received_snapshot);
	event_handler_received_snapshot = NS::NullPHandler;

	peer_controller->scene_synchronizer->event_snapshot_update_finished.unbind(event_handler_client_snapshot_updated);
	event_handler_client_snapshot_updated = NS::NullPHandler;

	peer_controller->scene_synchronizer->event_rewind_frame_begin.unbind(event_handler_rewind_frame_begin);
	event_handler_rewind_frame_begin = NS::NullPHandler;

	peer_controller->scene_synchronizer->event_state_validated.unbind(event_handler_state_validated);
	event_handler_state_validated = NS::NullPHandler;

	peer_controller->scene_synchronizer->event_snapshot_applied.unbind(event_handler_snapshot_applied);
	event_handler_snapshot_applied = NS::NullPHandler;
}

bool DollController::receive_inputs(const Vector<uint8_t> &p_data) {
	const uint32_t now = OS::get_singleton()->get_ticks_msec();
	struct SCParseTmpData {
		DollController &controller;
		uint32_t now;
	} tmp = {
		*this,
		now
	};

	const bool success = peer_controller->__input_data_parse(
			p_data,
			&tmp,

			// Parse the Input:
			[](void *p_user_pointer, FrameIndex p_frame_index, int p_input_size_in_bits, const BitArray &p_bit_array) -> void {
				SCParseTmpData *pd = static_cast<SCParseTmpData *>(p_user_pointer);

				ASSERT_COND(p_frame_index != FrameIndex::NONE);
				if (pd->controller.last_checked_input != FrameIndex::NONE && pd->controller.last_checked_input >= p_frame_index) {
					// This input is already processed.
					return;
				}

				FrameInput rfs;
				rfs.id = p_frame_index;

				const bool found = std::binary_search(
						pd->controller.frames_input.begin(),
						pd->controller.frames_input.end(),
						rfs,
						is_remote_frame_A_older);

				if (!found) {
					rfs.buffer_size_bit = p_input_size_in_bits;
					rfs.inputs_buffer = p_bit_array;
					rfs.received_timestamp = pd->now;

					pd->controller.frames_input.push_back(std::move(rfs));

					// Sort the added frame input.
					std::sort(
							pd->controller.frames_input.begin(),
							pd->controller.frames_input.end(),
							is_remote_frame_A_older);
				}
			});

	if (!success) {
		SceneSynchronizerDebugger::singleton()->print(INFO, "[DollController::receive_input] Failed.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
	}

	return success;
}

bool is_doll_snap_A_older(const DollController::DollSnapshot &p_snap_a, const DollController::DollSnapshot &p_snap_b) {
	return p_snap_a.data.input_id < p_snap_b.data.input_id;
}

void DollController::on_rewind_frame_begin(FrameIndex p_frame_index, int p_index, int p_count) {
	if (!peer_controller->can_simulate()) {
		return;
	}

	if (streaming_paused) {
		return;
	}

	for (size_t i = 0; i < frames_input.size(); ++i) {
		if (frames_input[i].id == p_frame_index) {
			queued_instant_to_process = i;
			return;
		}
	}

	SceneSynchronizerDebugger::singleton()->print(WARNING, "DollController was uable to find the input: " + std::string(p_frame_index) + " maybe it was never received?", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
	queued_instant_to_process = frames_input.size();
	return;
}

int DollController::count_queued_inputs() const {
	int queued_inputs_count = 0;
	for (const FrameInput &frame : frames_input) {
		if (frame.id > current_input_buffer_id) {
			queued_inputs_count += 1;
		}
	}
	return queued_inputs_count;
}

int DollController::fetch_optimal_queued_inputs() const {
	// The optimal virtual delay is a number that refers to the amount of queued
	// frames the DollController should try to have on each frame to avoid
	// remaining without inputs.
	// This delay should increase when the internet connection is bad (packet loss)
	// and decrease otherwise, allowing the inputs more time to be received.
	//
	// TODO: At the moment this value is fixed to the min_frame_delay, but at some
	// point we will want to change this value dynamically depending on packet loss.
	return peer_controller->get_min_frames_delay();
}

bool DollController::fetch_next_input(double p_delta) {
	if (queued_instant_to_process >= 0) {
		if (queued_instant_to_process < int(frames_input.size())) {
			// The SceneSync is rewinding the scene, so let's find the
			set_frame_input(frames_input[queued_instant_to_process], false);
			return true;
		}
		return false;
	}

	if make_unlikely (current_input_buffer_id == FrameIndex::NONE) {
		if (frames_input.size() > 0) {
			// Anything, as first input is good.
			set_frame_input(frames_input.front(), true);
			return true;
		}
		return false;
	}

	// -------------------------------------------------------- Lag compensation
	// The following code performs a lag compensation check to ensure it never
	// run out of inputs.

	// This parameter is used to enstablish the percentage of the lag compensation
	// algorithm to be activated.
	// This percentage is increased linearly by the distance the queue_inputs_count is relative to optimal_queued_inputs.
	// So, the bigger the distance is, the likely an action can happen.
	const float lag_compensation_base_percentage = 0.3;

	// 1. Count the queued inputs (the inputs that are not being processed yet).
	const int queued_inputs_count = count_queued_inputs();
	// 2. Fetch the optimal queued inputs (how many inputs should be queued based
	//    on the current connection).
	//    NOTE: The `+1` is needed to ensure the `queued_inputs_count` tend toward
	//    optmal AFTER the input is consumed, that is about to happen just after
	//    this function is executed.
	const int optimal_queued_inputs = fetch_optimal_queued_inputs() + 1;

	// The likeliness is used to scale the `lag_compensation_percentage`
	// to make it likely something to happen.
	const float factor = std::abs(float(optimal_queued_inputs) - float(queued_inputs_count)) / float(optimal_queued_inputs);

	const float lag_compensation_percentage = lag_compensation_base_percentage * factor;

	// 3. Based on the the virtual delay and queue_inputs_count it fetches the next input id.
	FrameIndex next_input_id = current_input_buffer_id + 1;
	const float r = float(rand()) / float(RAND_MAX);
	if (r < lag_compensation_percentage) {
		// Lag compensation activated.
		if (queued_inputs_count > optimal_queued_inputs) {
			// It has more queued inputs than the established optimal.
			// so let's skip 1 input so we can tend toward the optimal queued inputs.
			next_input_id += 1;
		} else if (queued_inputs_count == optimal_queued_inputs) {
			// It has the exact number of queued inputs.
			// Nothing to do.
		} else {
			// It has less queued input than the established optimal,
			// so let's use the old input for an extra frame, so we can
			// build the queue again.
			next_input_id -= 1;
		}
	}

	// -------------------------------------------------------- Search the input
	for (size_t i = 0; i < frames_input.size(); ++i) {
		if (frames_input[i].id >= next_input_id) {
			set_frame_input(frames_input[i], false);
			return true;
		}
	}

	if (frames_input.size() > 0) {
		set_frame_input(frames_input.back(), false);
		// It was impossible to find the input, so just pick the oldest one:
		// it's better than to stop the processing.
		return true;
	}

	return false;
}

void DollController::process(double p_delta) {
	notify_frame_checked(peer_controller->scene_synchronizer->client_get_last_checked_frame_index());

	const bool is_new_input = fetch_next_input(p_delta);

	if (is_new_input) {
		SceneSynchronizerDebugger::singleton()->print(INFO, "Doll process index: " + std::string(current_input_buffer_id), "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

		peer_controller->get_inputs_buffer_mut().begin_read();
		peer_controller->get_inputs_buffer_mut().seek(METADATA_SIZE);
		SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(peer_controller->authority_peer, SceneSynchronizerDebugger::READ);
		peer_controller->controllable_process(
				p_delta,
				peer_controller->get_inputs_buffer_mut());
		SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
	}

	queued_instant_to_process = -1;
}

void DollController::on_state_validated(FrameIndex p_frame_index, bool p_detected_desync) {
	notify_frame_checked(p_frame_index);
}

void DollController::notify_frame_checked(FrameIndex p_frame_index) {
	if (p_frame_index == FrameIndex::NONE) {
		// Nothing to do.
		return;
	}

	// Removes all the inputs older than the known one (included).
	while (frames_input.empty() == false && frames_input.front().id <= p_frame_index) {
		if (frames_input.front().id == p_frame_index) {
			// Pause the streaming if the last frame is empty.
			streaming_paused = (frames_input.front().buffer_size_bit - METADATA_SIZE) <= 0;
		}
		frames_input.pop_front();
	}

	last_checked_input = p_frame_index;
}

void DollController::on_received_server_snapshot(const Snapshot &p_snapshot) {
	if (last_checked_input != FrameIndex::NONE && last_checked_input >= p_snapshot.input_id) {
		// Snapshot already checked, no need to store this.
		return;
	}

	copy_controlled_objects_snapshot(p_snapshot, p_snapshot.input_id, server_snapshots);
}

void DollController::on_snapshot_update_finished(const Snapshot &p_snapshot) {
	copy_controlled_objects_snapshot(p_snapshot, current_input_buffer_id, client_snapshots);
}

void DollController::copy_controlled_objects_snapshot(
		const Snapshot &p_snapshot,
		FrameIndex p_doll_executed_input,
		std::vector<DollSnapshot> &r_snapshots) {
	const std::vector<ObjectData *> *controlled_objects = peer_controller->scene_synchronizer->get_peer_controlled_objects_data(peer_controller->get_authority_peer());
	if (!controlled_objects || controlled_objects->size() <= 0) {
		// Nothing to store.
		return;
	}

	DollSnapshot *snap;
	{
		std::vector<DollSnapshot>::iterator it = VecFunc::find(r_snapshots, DollSnapshot(p_snapshot.input_id));
		if (r_snapshots.end() != it) {
			snap = &*it;
		} else {
			r_snapshots.push_back(DollSnapshot(p_snapshot.input_id));
			snap = &r_snapshots.back();
		}
	}

	// This can't trigger because of the above check.
	ASSERT_COND(snap->data.input_id == p_snapshot.input_id);
	snap->doll_executed_input = p_doll_executed_input;

	// Find the biggest ID to initialize the snapshot.
	{
		ObjectNetId biggest_id = { 0 };
		for (ObjectData *object_data : *controlled_objects) {
			if (object_data->get_net_id() > biggest_id) {
				biggest_id = object_data->get_net_id();
			}
		}
		snap->data.object_vars.resize(biggest_id.id + 1);
	}

	// Now store the vars info.
	for (ObjectData *object_data : *controlled_objects) {
		if (!VecFunc::has(p_snapshot.simulated_objects, object_data->get_net_id())) {
			// This object was not simulated.
			continue;
		}

		const std::vector<NameAndVar> *vars = p_snapshot.get_object_vars(object_data->get_net_id());
		ENSURE_CONTINUE_MSG(vars, "The snapshot didn't contain the object: " + object_data->get_net_id() + ". If this error spams for a long period (5/10 seconds), it's a bug.");

		snap->data.simulated_objects.push_back(object_data->get_net_id());

		for (const NameAndVar &nav : *vars) {
			snap->data.object_vars[object_data->get_net_id().id].push_back(NameAndVar::make_copy(nav));
		}
	}

	// This array must be always sorted to ensure the snapshots order.
	std::sort(
			r_snapshots.begin(),
			r_snapshots.end(),
			is_doll_snap_A_older);
}

bool DollController::__pcr__fetch_recovery_info(
		FrameIndex p_checking_frame_index,
		Snapshot *r_no_rewind_recover,
		// The frames to process afterward.
		int p_predicted_frames,
		std::vector<std::string> *r_differences_info
#ifdef DEBUG_ENABLED
		,
		std::vector<ObjectNetId> *r_different_node_data
#endif
) const {

	// 1. Fetch the server snapshot.
	//    The server snapshot can be fetched, normally, using the index.
	std::vector<DollSnapshot>::const_iterator server_snap_it = VecFunc::find(server_snapshots, DollSnapshot(p_checking_frame_index));
	// The server snapshot was not found, this is impossible because we store
	// all the snapshots.
	ENSURE_V_MSG(server_snapshots.end() != server_snap_it, false, "Doll fetch recovery info failed because the snapshot was not found, though this should be impossible to trigger. checking_frame_index: " + p_checking_frame_index);

	const DollSnapshot *server_snapshot = &*server_snap_it;
	ENSURE_V(server_snapshot, false);

	// 2. Now fetch the client snapshot.
	//    Since the doll is following a a different timeline, we need to fetch the
	//    client frame by checking the `doll_executed_index` instead.
	const DollSnapshot *client_snapshot = nullptr;
	for (const DollSnapshot &snapshot : client_snapshots) {
		if (snapshot.doll_executed_input == p_checking_frame_index) {
			client_snapshot = &snapshot;
		}
	}
	ENSURE_V_MSG(client_snapshot, false, "Doll fetch recovery info failed because the client snapshot was not found. checking_frame: " + p_checking_frame_index)

	// Now just compare the two snapshots.
	return Snapshot::compare(
			*peer_controller->scene_synchronizer,
			server_snapshot->data,
			client_snapshot->data,
			-1,
			r_no_rewind_recover,
			r_differences_info
#ifdef DEBUG_ENABLED
			,
			r_different_node_data
#endif
	);
}

void DollController::on_snapshot_applied(const Snapshot &p_snapshot) {
	// Since this doll is executing on a different timeline, we can't apply
	// the received snapshot.
	// 1. Search which input was executed by this doll when this snapshot was executed.
	FrameIndex doll_executed_input;
	{
		const auto client_snap_it = VecFunc::find(client_snapshots, DollSnapshot(p_snapshot.input_id));
		ENSURE_MSG(client_snap_it != client_snapshots.end(), "The doll was unable to set the snapshot because it was unable to find the client snapshot with ID: " + p_snapshot.input_id);

		doll_executed_input = client_snap_it->doll_executed_input;
	}

	// Now it has the executed input so, fetch the snapshot to apply.
	// 2. Fetch the server snapshot.
	const auto server_snap_it = VecFunc::find(server_snapshots, DollSnapshot(doll_executed_input));
	if (server_snap_it ==)
}

NoNetController::NoNetController(PeerNetworkedController *p_peer_controller) :
		Controller(p_peer_controller),
		frame_id(FrameIndex{ 0 }) {
}

void NoNetController::process(double p_delta) {
	peer_controller->get_inputs_buffer_mut().begin_write(0); // No need of meta in this case.
	SceneSynchronizerDebugger::singleton()->print(INFO, "Nonet process index: " + std::string(frame_id), "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
	SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(peer_controller->authority_peer, SceneSynchronizerDebugger::WRITE);
	peer_controller->controllable_collect_input(p_delta, peer_controller->get_inputs_buffer_mut());
	SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
	peer_controller->get_inputs_buffer_mut().dry();
	peer_controller->get_inputs_buffer_mut().begin_read();
	SceneSynchronizerDebugger::singleton()->databuffer_operation_begin_record(peer_controller->authority_peer, SceneSynchronizerDebugger::READ);
	peer_controller->controllable_process(p_delta, peer_controller->get_inputs_buffer_mut());
	SceneSynchronizerDebugger::singleton()->databuffer_operation_end_record();
	frame_id += 1;
}

FrameIndex NoNetController::get_current_frame_index() const {
	return frame_id;
}

NS_NAMESPACE_END
