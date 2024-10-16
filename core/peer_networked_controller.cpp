#include "peer_networked_controller.h"

#include "../scene_synchronizer.h"
#include "ensure.h"
#include "scene_synchronizer_debugger.h"
#include <algorithm>
#include <string>
#include <cmath>
#include <cstring>

// The INPUT METADATA stores the input buffer size uint16_t.
#define METADATA_SIZE (16)

NS_NAMESPACE_BEGIN
static inline unsigned int ns_encode_uint32(std::uint32_t p_uint, std::uint8_t *p_arr) {
	for (int i = 0; i < 4; i++) {
		*p_arr = p_uint & 0xFF;
		p_arr++;
		p_uint >>= 8;
	}

	return sizeof(std::uint32_t);
}

static inline std::uint32_t ns_decode_uint32(const std::uint8_t *p_arr) {
	std::uint32_t u = 0;

	for (int i = 0; i < 4; i++) {
		std::uint32_t b = *p_arr;
		b <<= (i * 8);
		u |= b;
		p_arr++;
	}

	return u;
}

PeerNetworkedController::PeerNetworkedController(SceneSynchronizerBase &p_scene_synchronizer):
	scene_synchronizer(&p_scene_synchronizer),
	inputs_buffer(p_scene_synchronizer.get_debugger()) {
}

PeerNetworkedController::~PeerNetworkedController() {
	_sorted_controllable_objects.clear();

	if (controller != nullptr) {
		delete controller;
		controller = nullptr;
		controller_type = CONTROLLER_TYPE_NULL;
	}

	remove_synchronizer();
}

SceneSynchronizerDebugger &PeerNetworkedController::get_debugger() const {
	return scene_synchronizer->get_debugger();
};

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
				if (controlled_object_data->controller_funcs.collect_input &&
					controlled_object_data->controller_funcs.are_inputs_different &&
					controlled_object_data->controller_funcs.process) {
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
	}

	return _sorted_controllable_objects;
}

int PeerNetworkedController::get_max_redundant_inputs() const {
	return scene_synchronizer ? scene_synchronizer->get_max_redundant_inputs() : 0;
}

FrameIndex PeerNetworkedController::get_current_frame_index() const {
	NS_ENSURE_V(controller, FrameIndex::NONE);
	return controller->get_current_frame_index();
}

void PeerNetworkedController::server_set_peer_simulating_this_controller(int p_peer, bool p_simulating) {
	NS_ENSURE_MSG(is_server_controller(), "This function can be called only on the server.");
	if (p_simulating) {
		VecFunc::insert_unique(get_server_controller()->peers_simulating_this_controller, p_peer);
	} else {
		VecFunc::remove(get_server_controller()->peers_simulating_this_controller, p_peer);
	}
}

bool PeerNetworkedController::server_is_peer_simulating_this_controller(int p_peer) const {
	NS_ENSURE_V_MSG(is_server_controller(), false, "This function can be called only on the server.");
	return VecFunc::has(get_server_controller()->peers_simulating_this_controller, p_peer);
}

bool PeerNetworkedController::has_another_instant_to_process_after(int p_i) const {
	NS_ENSURE_V_MSG(is_player_controller(), false, "Can be executed only on player controllers.");
	return static_cast<PlayerController *>(controller)->has_another_instant_to_process_after(p_i);
}

void PeerNetworkedController::process(float p_delta) {
	if make_likely(controller && can_simulate()) {
		// This function is registered as processed function, so it's called by the
		// `SceneSync` in sync with the scene processing.
		controller->process(p_delta);
	}
}

ServerController *PeerNetworkedController::get_server_controller() {
	NS_ENSURE_V_MSG(is_server_controller(), nullptr, "This controller is not a server controller.");
	return static_cast<ServerController *>(controller);
}

const ServerController *PeerNetworkedController::get_server_controller() const {
	NS_ENSURE_V_MSG(is_server_controller(), nullptr, "This controller is not a server controller.");
	return static_cast<const ServerController *>(controller);
}

ServerController *PeerNetworkedController::get_server_controller_unchecked() {
	return static_cast<ServerController *>(controller);
}

const ServerController *PeerNetworkedController::get_server_controller_unchecked() const {
	return static_cast<const ServerController *>(controller);
}

PlayerController *PeerNetworkedController::get_player_controller() {
	NS_ENSURE_V_MSG(is_player_controller(), nullptr, "This controller is not a player controller.");
	return static_cast<PlayerController *>(controller);
}

const PlayerController *PeerNetworkedController::get_player_controller() const {
	NS_ENSURE_V_MSG(is_player_controller(), nullptr, "This controller is not a player controller.");
	return static_cast<const PlayerController *>(controller);
}

DollController *PeerNetworkedController::get_doll_controller() {
	NS_ENSURE_V_MSG(is_doll_controller(), nullptr, "This controller is not a doll controller.");
	return static_cast<DollController *>(controller);
}

const DollController *PeerNetworkedController::get_doll_controller() const {
	NS_ENSURE_V_MSG(is_doll_controller(), nullptr, "This controller is not a doll controller.");
	return static_cast<const DollController *>(controller);
}

NoNetController *PeerNetworkedController::get_nonet_controller() {
	NS_ENSURE_V_MSG(is_nonet_controller(), nullptr, "This controller is not a no net controller.");
	return static_cast<NoNetController *>(controller);
}

const NoNetController *PeerNetworkedController::get_nonet_controller() const {
	NS_ENSURE_V_MSG(is_nonet_controller(), nullptr, "This controller is not a no net controller.");
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
	inputs_buffer.get_buffer_mut().get_bytes_mut() = p_new_buffer.get_bytes();
	inputs_buffer.shrink_to(p_metadata_size_in_bit, p_size_in_bit);
}

void PeerNetworkedController::setup_synchronizer(int p_peer) {
	// This is set by the constructor.
	NS_ASSERT_COND(scene_synchronizer);
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

SceneSynchronizerBase *PeerNetworkedController::get_scene_synchronizer() const {
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

void PeerNetworkedController::controllable_collect_input(float p_delta, DataBuffer &r_data_buffer) {
	r_data_buffer.begin_write(get_debugger(), METADATA_SIZE);
	r_data_buffer.seek(METADATA_SIZE);

	get_debugger().databuffer_operation_begin_record(authority_peer, SceneSynchronizerDebugger::WRITE);

	const std::vector<ObjectData *> &sorted_controllable_objects = get_sorted_controllable_objects();
	for (ObjectData *object_data : sorted_controllable_objects) {
		object_data->controller_funcs.collect_input(p_delta, r_data_buffer);
#ifdef NS_DEBUG_ENABLED
		if (scene_synchronizer->pedantic_checks) {
			NS_ASSERT_COND_MSG(!r_data_buffer.is_buffer_failed(), "[NetID: " + std::to_string(object_data->get_net_id().id) + " ObjectName: " + object_data->get_object_name() + "] The collecte_input failed adding data into the DataBuffer. This should never happen!");
		} else {
			NS_ENSURE_MSG(!r_data_buffer.is_buffer_failed(), "[FATAL] [NetID: " + std::to_string(object_data->get_net_id().id) + " ObjectName: " + object_data->get_object_name() + "] The collecte_input failed adding data into the DataBuffer. This should never happen!");
		}
#endif
	}

	get_debugger().databuffer_operation_end_record();

	// Set the metadata which is used to store the buffer size.
	const std::uint16_t buffer_size_bits = get_inputs_buffer().size() + METADATA_SIZE;
	r_data_buffer.seek(0);
	r_data_buffer.add(buffer_size_bits);
}

bool PeerNetworkedController::controllable_are_inputs_different(DataBuffer &p_data_buffer_A, DataBuffer &p_data_buffer_B) {
	const std::vector<ObjectData *> &sorted_controllable_objects = get_sorted_controllable_objects();
	for (ObjectData *object_data : sorted_controllable_objects) {
		const bool are_inputs_different = object_data->controller_funcs.are_inputs_different(p_data_buffer_A, p_data_buffer_B);
#ifdef NS_DEBUG_ENABLED
		if (scene_synchronizer->pedantic_checks) {
			NS_ASSERT_COND_MSG(!p_data_buffer_A.is_buffer_failed(), "[NetID: " + std::to_string(object_data->get_net_id().id) + " ObjectName: " + object_data->get_object_name() + "] The are_inputs_different failed reading from the DataBufferA. This should never happen!");
			NS_ASSERT_COND_MSG(!p_data_buffer_B.is_buffer_failed(), "[NetID: " + std::to_string(object_data->get_net_id().id) + " ObjectName: " + object_data->get_object_name() + "] The are_inputs_different failed reading from the DataBufferB. This should never happen!");
		} else {
			NS_ENSURE_V_MSG(!p_data_buffer_A.is_buffer_failed(), true, "[FATAL] [NetID: " + std::to_string(object_data->get_net_id().id) + " ObjectName: " + object_data->get_object_name() + "] The are_inputs_different failed reading from the DataBufferA. This should never happen!");
			NS_ENSURE_V_MSG(!p_data_buffer_B.is_buffer_failed(), true, "[FATAL] [NetID: " + std::to_string(object_data->get_net_id().id) + " ObjectName: " + object_data->get_object_name() + "] The are_inputs_different failed reading from the DataBufferB. This should never happen!");
		}
#endif
		if (are_inputs_different) {
			return true;
		}
	}
	return false;
}

void PeerNetworkedController::controllable_process(float p_delta, DataBuffer &p_data_buffer) {
	const std::vector<ObjectData *> &sorted_controllable_objects = get_sorted_controllable_objects();
	for (ObjectData *object_data : sorted_controllable_objects) {
		object_data->controller_funcs.process(p_delta, p_data_buffer);
#ifdef NS_DEBUG_ENABLED
		if (scene_synchronizer->pedantic_checks) {
			NS_ASSERT_COND_MSG(!p_data_buffer.is_buffer_failed(), "[NetID: " + std::to_string(object_data->get_net_id().id) + " ObjectName: " + object_data->get_object_name() + "] The process failed reading from the DataBuffer. This should never happen!");
		} else {
			NS_ENSURE_MSG(!p_data_buffer.is_buffer_failed(), "[FATAL] [NetID: " + std::to_string(object_data->get_net_id().id) + " ObjectName: " + object_data->get_object_name() + "] The process failed reading from the DataBuffer. This should never happen!");
		}
#endif
	}
}

void PeerNetworkedController::notify_receive_inputs(const std::vector<std::uint8_t> &p_data) {
	if (controller) {
		controller->receive_inputs(p_data);
	}
}

void PeerNetworkedController::store_input_buffer(std::deque<FrameInput> &r_frames_input, FrameIndex p_frame_index) {
	const std::uint16_t buffer_size_bits = get_inputs_buffer().size() + METADATA_SIZE;

#ifdef NS_DEBUG_ENABLED
	if (scene_synchronizer->pedantic_checks) {
		get_inputs_buffer_mut().begin_read(get_debugger());
		std::uint16_t from_buffer__buffer_size_bits;
		get_inputs_buffer_mut().begin_read(get_debugger());
		get_inputs_buffer_mut().read(from_buffer__buffer_size_bits);
		NS_ASSERT_COND_MSG(from_buffer__buffer_size_bits == buffer_size_bits, "The buffer size must be the same between the one just calculated and the one inside the buffer");
	}

	NS_ASSERT_COND_MSG(buffer_size_bits>=METADATA_SIZE, "The buffer size can't be less than the metadata.");
#endif

	FrameInput inputs(get_debugger());
	inputs.id = p_frame_index;
	inputs.inputs_buffer = get_inputs_buffer().get_buffer();
	inputs.buffer_size_bit = buffer_size_bits;
	inputs.similarity = FrameIndex::NONE;
	r_frames_input.push_back(inputs);
}

void PeerNetworkedController::encode_inputs(std::deque<FrameInput> &p_frames_input, std::vector<std::uint8_t> &r_buffer) {
	// The inputs buffer is composed as follows:
	// - The following four bytes for the first input ID.
	// - Array of inputs:
	// |-- First byte the amount of times this input is duplicated in the packet.
	// |-- Input buffer.

	const size_t inputs_count = std::min(p_frames_input.size(), std::max(static_cast<size_t>(1), static_cast<size_t>(get_max_redundant_inputs())));
	if make_unlikely(inputs_count <= 0) {
		// Nothing to send.
		return;
	}

#define MAKE_ROOM(p_size)                                              \
	if (r_buffer.size() < static_cast<size_t>(ofs + p_size)) \
		r_buffer.resize(ofs + p_size);

	int ofs = 0;
	r_buffer.clear();
	// At this point both the cached_packet_data and ofs are the same.
	NS_ASSERT_COND(ofs == r_buffer.size());

	// Let's store the ID of the first snapshot.
	MAKE_ROOM(4);
	const FrameIndex first_input_id = p_frames_input[p_frames_input.size() - inputs_count].id;
	ofs += ns_encode_uint32(first_input_id.id, r_buffer.data() + ofs);

	FrameIndex previous_input_id = FrameIndex::NONE;
	FrameIndex previous_input_similarity = FrameIndex::NONE;
	int previous_buffer_size = 0;
	uint8_t duplication_count = 0;

	DataBuffer pir_A(get_debugger());
	DataBuffer pir_B(get_debugger());
	pir_A.copy(get_inputs_buffer().get_buffer());

	// Compose the packets
	for (size_t i = p_frames_input.size() - inputs_count; i < p_frames_input.size(); i += 1) {
		bool is_similar = false;

		if (previous_input_id == FrameIndex::NONE) {
			// This happens for the first input of the packet.
			// Just write it.
			is_similar = false;
		} else if (duplication_count == UINT8_MAX) {
			// Prevent to overflow the `uint8_t`.
			is_similar = false;
		} else {
			if (p_frames_input[i].similarity != previous_input_id) {
				if (p_frames_input[i].similarity == FrameIndex::NONE) {
					// This input was never compared, let's do it now.
					pir_B.copy(p_frames_input[i].inputs_buffer);
					pir_B.shrink_to(METADATA_SIZE, p_frames_input[i].buffer_size_bit - METADATA_SIZE);

					pir_A.begin_read(get_debugger());
					pir_A.seek(METADATA_SIZE);
					pir_B.begin_read(get_debugger());
					pir_B.seek(METADATA_SIZE);

					const bool are_different = controllable_are_inputs_different(pir_A, pir_B);
					is_similar = !are_different;
				} else if (p_frames_input[i].similarity == previous_input_similarity) {
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

		if (get_current_frame_index() == previous_input_id) {
			get_debugger().notify_are_inputs_different_result(authority_peer, p_frames_input[i].id.id, is_similar);
		} else if (get_current_frame_index() == p_frames_input[i].id) {
			get_debugger().notify_are_inputs_different_result(authority_peer, previous_input_id.id, is_similar);
		}

		if (is_similar) {
			// This input is similar to the previous one, so just duplicate it.
			duplication_count += 1;
			// In this way, we don't need to compare these frames again.
			p_frames_input[i].similarity = previous_input_id;

			get_debugger().notify_input_sent_to_server(authority_peer, p_frames_input[i].id.id, previous_input_id.id);
		} else {
			// This input is different from the previous one, so let's
			// finalize the previous and start another one.

			get_debugger().notify_input_sent_to_server(authority_peer, p_frames_input[i].id.id, p_frames_input[i].id.id);

			if (previous_input_id != FrameIndex::NONE) {
				// We can finally finalize the previous input
				r_buffer[ofs - previous_buffer_size - 1] = duplication_count;
			}

			// Resets the duplication count.
			duplication_count = 0;

			// Writes the duplication_count for this new input
			MAKE_ROOM(1);
			r_buffer[ofs] = 0;
			ofs += 1;

			// Write the inputs
			const int buffer_size = (int)p_frames_input[i].inputs_buffer.get_bytes().size();
			MAKE_ROOM(buffer_size);
			memcpy(
					r_buffer.data() + ofs,
					p_frames_input[i].inputs_buffer.get_bytes().data(),
					buffer_size);
			ofs += buffer_size;

			// Let's see if we can duplicate this input.
			previous_input_id = p_frames_input[i].id;
			previous_input_similarity = p_frames_input[i].similarity;
			previous_buffer_size = buffer_size;

			pir_A.get_buffer_mut() = p_frames_input[i].inputs_buffer;
			pir_A.shrink_to(METADATA_SIZE, p_frames_input[i].buffer_size_bit - METADATA_SIZE);
		}
	}

	// Finalize the last added input_buffer.
	r_buffer[ofs - previous_buffer_size - 1] = duplication_count;

	// At this point both the cached_packet_data.size() and ofs MUST be the same.
	NS_ASSERT_COND(ofs == r_buffer.size());

#undef MAKE_ROOM
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
		if (is_server_controller() || is_player_controller() || is_nonet_controller()) {
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
		const std::vector<std::uint8_t> &p_data,
		void *p_user_pointer,
		void (*p_input_parse)(void *p_user_pointer, FrameIndex p_input_id, std::uint16_t p_input_size_in_bits, const BitArray &p_input)) {
	// The packet is composed as follow:
	// |- Four bytes for the first input ID.
	// \- Array of inputs:
	//      |-- First byte the amount of times this input is duplicated in the packet.
	//      |-- inputs buffer.
	//
	// Let's decode it!

	const int data_len = (int)p_data.size();

	int ofs = 0;

	NS_ENSURE_V(data_len >= 4, false);
	const FrameIndex first_input_id = FrameIndex{ { ns_decode_uint32(p_data.data() + ofs) } };
	ofs += 4;

	uint32_t inserted_input_count = 0;

	// Contains the entire packet and in turn it will be seek to specific location
	// so I will not need to copy chunk of the packet data.
	DataBuffer pir(get_debugger());
	pir.copy(BitArray(get_debugger(), p_data));
	pir.begin_read(get_debugger());

	while (ofs < data_len) {
		NS_ENSURE_V_MSG(ofs + 1 <= data_len, false, "The arrived packet size doesn't meet the expected size.");
		// First byte is used for the duplication count.
		const uint8_t duplication = p_data[ofs];
		ofs += 1;

		// Validate input
		const int input_buffer_offset_bit = ofs * 8;
		pir.shrink_to(input_buffer_offset_bit, (data_len - ofs) * 8);
		pir.seek(input_buffer_offset_bit);
		NS_ENSURE_V(!pir.is_buffer_failed(), false);

		// Read metadata
		std::uint16_t input_size_in_bits;
		pir.read(input_size_in_bits);
		NS_ENSURE_V(!pir.is_buffer_failed(), false);

		// Pad to 8 bits.
		const int input_size_padded =
				int(std::ceil(static_cast<float>(input_size_in_bits) / 8.0f));
		NS_ENSURE_V_MSG(ofs + input_size_padded <= data_len, false, "The arrived packet size doesn't meet the expected size.");

		// Extract the data and copy into a BitArray.
		BitArray bit_array;
		bit_array.get_bytes_mut().resize(input_size_padded);
		memcpy(
				bit_array.get_bytes_mut().data(),
				p_data.data() + ofs,
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

	NS_ENSURE_V_MSG(ofs == data_len, false, "At the end was detected that the arrived packet has an unexpected size.");
	return true;
}

RemotelyControlledController::RemotelyControlledController(PeerNetworkedController *p_peer_controller) :
	Controller(p_peer_controller) {
}

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
	return (int)frames_input.size();
}

FrameIndex RemotelyControlledController::last_known_frame_index() const {
	if (frames_input.size() > 0) {
		return frames_input.back().id;
	} else {
		return FrameIndex::NONE;
	}
}

bool RemotelyControlledController::fetch_next_input(float p_delta) {
	bool is_new_input = true;

	if make_unlikely(current_input_buffer_id == FrameIndex::NONE) {
		// As initial packet, anything is good.
		if (frames_input.empty() == false) {
			// First input arrived.
			set_frame_input(frames_input.front(), true);
			frames_input.pop_front();
			// Start tracing the packets from this moment on.
			peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] Input `" + current_input_buffer_id + "` selected as first input.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
		} else {
			is_new_input = false;
			peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] Still no inputs.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
		}
	} else {
		const FrameIndex next_input_id = current_input_buffer_id + 1;
		peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] The server is looking for: " + next_input_id, "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

		if make_unlikely(streaming_paused) {
			peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] The streaming is paused.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
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
				peer_controller->set_inputs_buffer(BitArray(get_debugger(),METADATA_SIZE), METADATA_SIZE, 0);
				is_new_input = false;
			}
		} else if make_unlikely(frames_input.empty() == true) {
			// The input buffer is empty; a packet is missing.
			peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] Missing input: " + std::to_string(next_input_id.id) + " Input buffer is void, i'm using the previous one!", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

			is_new_input = false;
			ghost_input_count += 1;
		} else {
			peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] The input buffer is not empty, so looking for the next input. Hopefully `" + std::to_string(next_input_id.id) + "`", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

			// The input buffer is not empty, search the new input.
			if (next_input_id == frames_input.front().id) {
				peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] The input `" + std::to_string(next_input_id.id) + "` was found.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

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

				peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] The input `" + std::to_string(next_input_id.id) + "` was NOT found. Recovering process started.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
				peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] ghost_input_count: `" + std::to_string(ghost_input_count) + "`", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

				const int size = std::min(ghost_input_count, std::uint32_t(frames_input.size()));
				const FrameIndex ghost_packet_id = next_input_id + ghost_input_count;

				bool recovered = false;
				FrameInput pi(get_debugger());

				DataBuffer pir_A(get_debugger());
				DataBuffer pir_B(get_debugger());
				pir_A.copy(peer_controller->get_inputs_buffer());

				for (int i = 0; i < size; i += 1) {
					peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] checking if `" + std::string(frames_input.front().id) + "` can be used to recover `" + std::string(next_input_id) + "`.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

					if (ghost_packet_id < frames_input.front().id) {
						peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] The input `" + frames_input.front().id + "` can't be used as the ghost_packet_id (`" + std::string(ghost_packet_id) + "`) is more than the input.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
						break;
					} else {
						const FrameIndex input_id = frames_input.front().id;
						peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] The input `" + std::string(input_id) + "` is eligible as next frame.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

						pi = frames_input.front();
						frames_input.pop_front();
						recovered = true;

						// If this input has some important changes compared to the last
						// good input, let's recover to this point otherwise skip it
						// until the last one.
						// Useful to avoid that the server stay too much behind the
						// client.

						pir_B.copy(pi.inputs_buffer);
						pir_B.shrink_to(METADATA_SIZE, pi.buffer_size_bit - METADATA_SIZE);

						pir_A.begin_read(get_debugger());
						pir_A.seek(METADATA_SIZE);
						pir_B.begin_read(get_debugger());
						pir_B.seek(METADATA_SIZE);

						const bool are_different = peer_controller->controllable_are_inputs_different(pir_A, pir_B);
						if (are_different) {
							peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::fetch_next_input] The input `" + input_id + "` is different from the one executed so far, so better to execute it.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
							break;
						}
					}
				}

				if (recovered) {
					set_frame_input(pi, false);
					ghost_input_count = 0;
					peer_controller->get_debugger().print(INFO, "Packet recovered. The new InputID is: `" + current_input_buffer_id + "`", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
				} else {
					ghost_input_count += 1;
					is_new_input = false;
					peer_controller->get_debugger().print(INFO, "Packet still missing, the server is still using the old input.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
				}
			}
		}
	}

#ifdef NS_DEBUG_ENABLED
	if (frames_input.empty() == false && current_input_buffer_id != FrameIndex::NONE) {
		// At this point is guaranteed that the current_input_buffer_id is never
		// greater than the first item contained by `snapshots`.
		NS_ASSERT_COND(current_input_buffer_id < frames_input.front().id);
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

void RemotelyControlledController::process(float p_delta) {
#ifdef NS_DEBUG_ENABLED
	const bool is_new_input =
#endif
			fetch_next_input(p_delta);

	if make_unlikely(current_input_buffer_id == FrameIndex::NONE) {
		// Skip this until the first input arrive.
		peer_controller->get_debugger().print(INFO, "Server skips this frame as the current_input_buffer_id == FrameIndex::NONE", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
		return;
	}

#ifdef NS_DEBUG_ENABLED
	if (!is_new_input) {
		peer_controller->event_input_missed.broadcast(current_input_buffer_id + 1);
	}
#endif

	peer_controller->get_debugger().print(INFO, "RemotelyControlled process index: " + current_input_buffer_id, "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

	peer_controller->get_inputs_buffer_mut().begin_read(get_debugger());
	peer_controller->get_inputs_buffer_mut().seek(METADATA_SIZE);
	peer_controller->get_debugger().databuffer_operation_begin_record(peer_controller->authority_peer, SceneSynchronizerDebugger::READ);
	peer_controller->controllable_process(
			p_delta,
			peer_controller->get_inputs_buffer_mut());
	peer_controller->get_debugger().databuffer_operation_end_record();
}

bool is_remote_frame_A_older(const FrameInput &p_snap_a, const FrameInput &p_snap_b) {
	return p_snap_a.id < p_snap_b.id;
}

bool RemotelyControlledController::receive_inputs(const std::vector<std::uint8_t> &p_data) {
	struct SCParseTmpData {
		RemotelyControlledController &controller;
	} tmp = {
				*this,
			};

	const bool success = peer_controller->__input_data_parse(
			p_data,
			&tmp,

			// Parse the Input:
			[](void *p_user_pointer, FrameIndex p_input_id, std::uint16_t p_input_size_in_bits, const BitArray &p_bit_array) -> void {
				SCParseTmpData *pd = static_cast<SCParseTmpData *>(p_user_pointer);

				if make_unlikely(pd->controller.current_input_buffer_id != FrameIndex::NONE && pd->controller.current_input_buffer_id >= p_input_id) {
					// We already have this input, so we don't need it anymore.
					return;
				}

				FrameInput rfs(pd->controller.get_debugger());
				rfs.id = p_input_id;

				const bool found = std::binary_search(
						pd->controller.frames_input.begin(),
						pd->controller.frames_input.end(),
						rfs,
						is_remote_frame_A_older);

				if (!found) {
					rfs.buffer_size_bit = p_input_size_in_bits;
					rfs.inputs_buffer = p_bit_array;

					pd->controller.frames_input.push_back(rfs);

					// Sort the added frame input.
					std::sort(
							pd->controller.frames_input.begin(),
							pd->controller.frames_input.end(),
							is_remote_frame_A_older);
				}
			});

#ifdef NS_DEBUG_ENABLED
	if (frames_input.empty() == false && current_input_buffer_id != FrameIndex::NONE) {
		// At this point is guaranteed that the current_input_buffer_id is never
		// greater than the first item contained by `snapshots`.
		NS_ASSERT_COND(current_input_buffer_id < frames_input.front().id);
	}
#endif

	if (!success) {
		peer_controller->get_debugger().print(INFO, "[RemotelyControlledController::receive_input] Failed.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer), true);
	}

	return success;
}

ServerController::ServerController(
		PeerNetworkedController *p_peer_controller) :
	RemotelyControlledController(p_peer_controller) {
}

void ServerController::process(float p_delta) {
	RemotelyControlledController::process(p_delta);

	if (!streaming_paused) {
		// Update the consecutive inputs.
		int consecutive_inputs = 0;
		for (std::size_t i = 0; i < frames_input.size(); i += 1) {
			if (frames_input[i].id == (current_input_buffer_id + consecutive_inputs + 1)) {
				consecutive_inputs += 1;
			}
		}
	}
}

void ServerController::on_peer_update(bool p_peer_enabled) {
	if (p_peer_enabled == peer_enabled) {
		// Nothing to updated.
		return;
	}

	// ~~ Reset everything to avoid accumulate old data. ~~
	RemotelyControlledController::on_peer_update(p_peer_enabled);
}

void ServerController::set_frame_input(const FrameInput &p_frame_snapshot, bool p_first_input) {
	RemotelyControlledController::set_frame_input(p_frame_snapshot, p_first_input);
}

void ServerController::notify_send_state() {
	// If the notified input is a void buffer, the client is allowed to pause
	// the input streaming. So missing packets are just handled as void inputs.
	if (current_input_buffer_id != FrameIndex::NONE && peer_controller->get_inputs_buffer().size() == 0) {
		streaming_paused = true;
	}
}

bool ServerController::receive_inputs(const std::vector<std::uint8_t> &p_data) {
	const bool success = RemotelyControlledController::receive_inputs(p_data);

	if (success) {
		// The input parsing succeded on the server, now ping pong this to all the dolls.
		for (int peer_id : peers_simulating_this_controller) {
			if (peer_id == peer_controller->authority_peer || peer_id == peer_controller->scene_synchronizer->get_network_interface().get_server_peer()) {
				continue;
			}

			peer_controller->scene_synchronizer->call_rpc_receive_inputs(
					peer_id,
					peer_controller->authority_peer,
					p_data);
		}
	}

	return success;
}

AutonomousServerController::AutonomousServerController(
		PeerNetworkedController *p_peer_controller) :
	ServerController(p_peer_controller) {
	event_handler_on_app_process_end =
			peer_controller->scene_synchronizer->event_app_process_end.bind(std::bind(&AutonomousServerController::on_app_process_end, this, std::placeholders::_1));
}

AutonomousServerController::~AutonomousServerController() {
	peer_controller->scene_synchronizer->event_app_process_end.unbind(event_handler_on_app_process_end);
	event_handler_on_app_process_end = NullPHandler;
}

bool AutonomousServerController::receive_inputs(const std::vector<std::uint8_t> &p_data) {
	peer_controller->get_debugger().print(ERROR, "`receive_input` called on the `AutonomousServerController` it should not happen by design. This is a bug.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
	return false;
}

int AutonomousServerController::get_inputs_count() const {
	// No input collected by this class.
	return 0;
}

bool AutonomousServerController::fetch_next_input(float p_delta) {
	peer_controller->get_debugger().print(INFO, "Autonomous server fetch input.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

	peer_controller->controllable_collect_input(p_delta, peer_controller->get_inputs_buffer_mut());

	peer_controller->get_inputs_buffer_mut().dry();

	if make_unlikely(current_input_buffer_id == FrameIndex::NONE) {
		// This is the first input.
		current_input_buffer_id = FrameIndex{ { 0 } };
	} else {
		// Just advance from now on.
		current_input_buffer_id += 1;
	}

	peer_controller->store_input_buffer(frames_input, current_input_buffer_id);

	// The input is always new.
	return true;
}

void AutonomousServerController::on_app_process_end(float p_delta_seconds) {
	// Removes all the old inputs
	while (frames_input.size() > peer_controller->get_max_redundant_inputs()) {
		frames_input.pop_front();
	}

	// Send inputs to clients.
	if (frames_input.size() == 0) {
		return;
	}

	peer_controller->encode_inputs(frames_input, cached_packet_data);

	for (int peer_id : peers_simulating_this_controller) {
		if (peer_id != peer_controller->authority_peer) {
			peer_controller->scene_synchronizer->call_rpc_receive_inputs(
					peer_id,
					peer_controller->authority_peer,
					cached_packet_data);
		}
	}
}

PlayerController::PlayerController(PeerNetworkedController *p_peer_controller) :
	Controller(p_peer_controller),
	current_input_id(FrameIndex::NONE),
	input_buffers_counter(0) {
	event_handler_rewind_frame_begin =
			peer_controller->scene_synchronizer->event_rewind_frame_begin.bind(std::bind(&PlayerController::on_rewind_frame_begin, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	event_handler_state_validated =
			peer_controller->scene_synchronizer->event_state_validated.bind(std::bind(&PlayerController::on_state_validated, this, std::placeholders::_1, std::placeholders::_2));

	event_handler_on_app_process_end =
			peer_controller->scene_synchronizer->event_app_process_end.bind(std::bind(&PlayerController::on_app_process_end, this, std::placeholders::_1));
}

PlayerController::~PlayerController() {
	peer_controller->scene_synchronizer->event_app_process_end.unbind(event_handler_on_app_process_end);
	event_handler_on_app_process_end = NullPHandler;

	peer_controller->scene_synchronizer->event_rewind_frame_begin.unbind(event_handler_rewind_frame_begin);
	event_handler_rewind_frame_begin = NullPHandler;

	peer_controller->scene_synchronizer->event_state_validated.unbind(event_handler_state_validated);
	event_handler_state_validated = NullPHandler;
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

#ifdef NS_DEBUG_ENABLED
	// Unreachable, because the next frame have always the next `p_frame_index` or empty.
	NS_ASSERT_COND(frames_input.empty() || (p_frame_index + 1) == frames_input.front().id);
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
	return int(frames_input.size());
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

void PlayerController::on_rewind_frame_begin(FrameIndex p_frame_index, int p_rewinding_index, int p_rewinding_frame_count) {
	NS_PROFILE
	if (!peer_controller->can_simulate()) {
		return;
	}

	if (p_rewinding_index >= 0 && p_rewinding_index < int(frames_input.size())) {
		queued_instant_to_process = p_rewinding_index;
#ifdef NS_DEBUG_ENABLED
		// IMPOSSIBLE to trigger - without bugs.
		NS_ASSERT_COND(frames_input[p_rewinding_index].id == p_frame_index);
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

void PlayerController::process(float p_delta) {
	if make_unlikely(queued_instant_to_process >= 0) {
		// There is a queued instant. It means the SceneSync is rewinding:
		// instead to fetch a new input, read it from the stored snapshots.
		DataBuffer ib(frames_input[queued_instant_to_process].inputs_buffer);
		ib.shrink_to(METADATA_SIZE, frames_input[queued_instant_to_process].buffer_size_bit - METADATA_SIZE);
		ib.begin_read(get_debugger());
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
			current_input_id = FrameIndex{ { input_buffers_counter } };

			peer_controller->get_debugger().print(INFO, "Player process index: " + std::string(current_input_id), "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

			peer_controller->controllable_collect_input(p_delta, peer_controller->get_inputs_buffer_mut());

			// Unpause streaming?
			if (peer_controller->get_inputs_buffer().size() > 0) {
				streaming_paused = false;
			}
		} else {
			peer_controller->get_debugger().print(WARNING, "It's not possible to accept new inputs. Is this lagging?", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
		}

		peer_controller->get_inputs_buffer_mut().dry();
		peer_controller->get_inputs_buffer_mut().begin_read(get_debugger());
		peer_controller->get_inputs_buffer_mut().seek(METADATA_SIZE); // Skip meta.

		peer_controller->get_debugger().databuffer_operation_begin_record(peer_controller->authority_peer, SceneSynchronizerDebugger::READ);
		// The physics process is always emitted, because we still need to simulate
		// the character motion even if we don't store the player inputs.
		peer_controller->controllable_process(p_delta, peer_controller->get_inputs_buffer_mut());
		peer_controller->get_debugger().databuffer_operation_end_record();

		peer_controller->player_set_has_new_input(false);
		if (!streaming_paused) {
			if (accept_new_inputs) {
				input_buffers_counter += 1;
				peer_controller->store_input_buffer(frames_input, current_input_id);
				peer_controller->player_set_has_new_input(true);
			}

			// Keep sending inputs, despite the server seems not responding properly,
			// to make sure the server becomes up to date at some point.
			has_pending_inputs_sent = true;
		}
	}
}

void PlayerController::on_state_validated(FrameIndex p_frame_index, bool p_detected_desync) {
	notify_frame_checked(p_frame_index);
}

void PlayerController::on_app_process_end(float p_delta_seconds) {
	send_frame_input_buffer_to_server();
}

FrameIndex PlayerController::get_current_frame_index() const {
	return current_input_id;
}

bool PlayerController::receive_inputs(const std::vector<std::uint8_t> &p_data) {
	peer_controller->get_debugger().print(NS::ERROR, "`receive_input` called on the `PlayerServerController` -This function is not supposed to be called on the player controller. Only the server and the doll should receive this.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
	return false;
}

void PlayerController::send_frame_input_buffer_to_server() {
	if (!has_pending_inputs_sent) {
		return;
	}
	has_pending_inputs_sent = false;

	peer_controller->encode_inputs(frames_input, cached_packet_data);

	peer_controller->scene_synchronizer->call_rpc_receive_inputs(
			peer_controller->scene_synchronizer->get_network_interface().get_server_peer(),
			peer_controller->authority_peer,
			cached_packet_data);
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

	event_handler_state_validated =
			peer_controller->scene_synchronizer->event_state_validated.bind(std::bind(&DollController::on_state_validated, this, std::placeholders::_1, std::placeholders::_2));

	event_handler_rewind_frame_begin =
			peer_controller->scene_synchronizer->event_rewind_frame_begin.bind(std::bind(&DollController::on_rewind_frame_begin, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	event_handler_snapshot_applied =
			peer_controller->scene_synchronizer->event_snapshot_applied.bind(std::bind(&DollController::on_snapshot_applied, this, std::placeholders::_1, std::placeholders::_2));
}

DollController::~DollController() {
	peer_controller->scene_synchronizer->event_received_server_snapshot.unbind(event_handler_received_snapshot);
	event_handler_received_snapshot = NS::NullPHandler;

	peer_controller->scene_synchronizer->event_snapshot_update_finished.unbind(event_handler_client_snapshot_updated);
	event_handler_client_snapshot_updated = NS::NullPHandler;

	peer_controller->scene_synchronizer->event_state_validated.unbind(event_handler_state_validated);
	event_handler_state_validated = NS::NullPHandler;

	peer_controller->scene_synchronizer->event_rewind_frame_begin.unbind(event_handler_rewind_frame_begin);
	event_handler_rewind_frame_begin = NS::NullPHandler;

	peer_controller->scene_synchronizer->event_snapshot_applied.unbind(event_handler_snapshot_applied);
	event_handler_snapshot_applied = NS::NullPHandler;
}

bool DollController::receive_inputs(const std::vector<uint8_t> &p_data) {
	struct SCParseTmpData {
		DollController &controller;
	} tmp = {
				*this,
			};

	const bool success = peer_controller->__input_data_parse(
			p_data,
			&tmp,

			// Parse the Input:
			[](void *p_user_pointer, FrameIndex p_frame_index, std::uint16_t p_input_size_in_bits, const BitArray &p_bit_array) -> void {
				SCParseTmpData *pd = static_cast<SCParseTmpData *>(p_user_pointer);
				NS_ASSERT_COND(p_frame_index != FrameIndex::NONE);
				if (pd->controller.last_doll_validated_input != FrameIndex::NONE && pd->controller.last_doll_validated_input >= p_frame_index) {
					// This input is already processed.
					return;
				}

				FrameInput rfs(pd->controller.get_debugger());
				rfs.id = p_frame_index;

				const bool found = std::binary_search(
						pd->controller.frames_input.begin(),
						pd->controller.frames_input.end(),
						rfs,
						is_remote_frame_A_older);

				if (!found) {
					rfs.buffer_size_bit = p_input_size_in_bits;
					rfs.inputs_buffer = p_bit_array;

					pd->controller.frames_input.push_back(std::move(rfs));

					// Sort the added frame input.
					std::sort(
							pd->controller.frames_input.begin(),
							pd->controller.frames_input.end(),
							is_remote_frame_A_older);
				}
			});

	if (!success) {
		peer_controller->get_debugger().print(ERROR, "[DollController::receive_input] Failed.", "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
	}

	return success;
}

bool is_doll_snap_A_older(const DollController::DollSnapshot &p_snap_a, const DollController::DollSnapshot &p_snap_b) {
	return p_snap_a.doll_executed_input < p_snap_b.doll_executed_input;
}

void DollController::on_rewind_frame_begin(FrameIndex p_frame_index, int p_rewinding_index, int p_rewinding_frame_count) {
	NS_PROFILE

	if (!peer_controller->can_simulate()) {
		return;
	}

	if (streaming_paused) {
		return;
	}

	// Just set the rewinding frame count, the fetch_next_input will
	// validate it anyway.
	queued_instant_to_process = p_rewinding_index;
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
	return peer_controller->scene_synchronizer->get_min_doll_input_buffer_size();
}

bool DollController::fetch_next_input(float p_delta) {
	if (queued_instant_to_process >= 0) {
		if make_unlikely(queued_frame_index_to_process == FrameIndex::NONE) {
			// This happens when the server didn't start to process this doll yet.
			return false;
		}

		// This offset is defined by the lag compensation algorithm inside the
		// `on_snapshot_applied`, and is used to compensate the lag by
		// getting rid or introduce inputs, during the reconciliation (rewinding)
		// phase.
		const FrameIndex frame_to_process = queued_frame_index_to_process + queued_instant_to_process;
		// Search the input.
		for (const FrameInput &frame : frames_input) {
			if (frame.id == frame_to_process) {
				set_frame_input(frame, false);
				return true;
			} else if (frame_to_process < frame.id) {
				// The frames are sorted, so it's impossible we find the frame in this case.
				break;
			}
		}
		// The doll controller is compensating for missing inputs, so return
		// false, on this frame to stop processing untill then.
		current_input_buffer_id = frame_to_process;
		return false;
	}

	if make_unlikely(current_input_buffer_id == FrameIndex::NONE) {
		if (frames_input.size() > 0) {
			// Anything, as first input is good.
			set_frame_input(frames_input.front(), true);
			return true;
		}
		return false;
	}

	const FrameIndex next_input_id = current_input_buffer_id + 1;

	// -------------------------------------------------------- Search the input
	int closest_frame_index = -1;
	int closest_frame_distance = std::numeric_limits<int>::max();
	// NOTE: Iterating in reverse order since I've noticed it's likely to find
	// the input at the end of this vector.
	for (int i = (int)frames_input.size() - 1; i >= 0; i--) {
		if (frames_input[i].id == next_input_id) {
			set_frame_input(frames_input[i], false);
			return true;
		}

		const int distance = (int)std::abs(std::int64_t(frames_input[i].id.id) - std::int64_t(next_input_id.id));
		if (distance < closest_frame_distance) {
			closest_frame_index = i;
			closest_frame_distance = distance;
		} else {
			// The frames_input is a sorted vector, when the distance to the
			// searched input increases it means we can't find it anylonger.
			// So interrupt the loop.
			break;
		}
	}

	if (!peer_controller->scene_synchronizer->get_settings().lag_compensation.doll_allow_guess_input_when_missing) {
		// It was not possible to find the input, and the doll is not allowed to guess,
		// so just return false.
		return false;
	}

	if (closest_frame_index >= 0) {
		// It was impossible to find the input, so just pick the closest one and
		// assume it's the one we are executing.
		FrameInput guessed_fi = frames_input[closest_frame_index];
		guessed_fi.id = next_input_id;
		set_frame_input(guessed_fi, false);
		peer_controller->get_debugger().print(INFO, "The input " + next_input_id + " is missing. Copying it from " + std::string(frames_input[closest_frame_index].id));
		return true;
	} else {
		// The input is not set and there is no suitable one.
		return false;
	}
}

void DollController::process(float p_delta) {
	const bool is_new_input = fetch_next_input(p_delta);

	if make_likely(current_input_buffer_id > FrameIndex{ { 0 } }) {
		// This operation is done here, because the doll process on a different
		// timeline than the one processed by the client.
		// Whenever it found a server snapshot, it's applied.
		// 1. Try fetching the previous server snapshot.
		auto server_snap_it = VecFunc::find(server_snapshots, DollSnapshot(current_input_buffer_id - 1));
		if (server_snap_it != server_snapshots.end()) {
			// 2. The snapshot was found, so apply it.
			static_cast<ClientSynchronizer *>(peer_controller->scene_synchronizer->get_synchronizer_internal())->apply_snapshot(server_snap_it->data, 0, 0, nullptr, true, true, true, true, true);
		}
	}

	if (is_new_input) {
		peer_controller->get_debugger().print(INFO, "Doll process index: " + std::string(current_input_buffer_id), "CONTROLLER-" + std::to_string(peer_controller->authority_peer));

		peer_controller->get_inputs_buffer_mut().begin_read(get_debugger());
		peer_controller->get_inputs_buffer_mut().seek(METADATA_SIZE);
		peer_controller->get_debugger().databuffer_operation_begin_record(peer_controller->authority_peer, SceneSynchronizerDebugger::READ);
		peer_controller->controllable_process(
				p_delta,
				peer_controller->get_inputs_buffer_mut());
		peer_controller->get_debugger().databuffer_operation_end_record();
	}

	queued_instant_to_process = -1;
}

void DollController::on_state_validated(FrameIndex p_frame_index, bool p_detected_desync) {
	if (!skip_snapshot_validation) {
		notify_frame_checked(last_doll_compared_input);
		clear_previously_generated_client_snapshots();
	}
}

void DollController::notify_frame_checked(FrameIndex p_doll_frame_index) {
	if (last_doll_validated_input != FrameIndex::NONE && last_doll_validated_input >= p_doll_frame_index) {
		// Already checked.
		return;
	}

	if make_likely(p_doll_frame_index != FrameIndex::NONE) {
		// Removes all the inputs older than the known one (included).
		while (!frames_input.empty() && frames_input.front().id <= p_doll_frame_index) {
			if (frames_input.front().id == p_doll_frame_index) {
				// Pause the streaming if the last frame is empty.
				streaming_paused = (frames_input.front().buffer_size_bit - METADATA_SIZE) <= 0;
			}
			frames_input.pop_front();
		}

		// Remove all the server snapshots which doll frame was already executed.
		// NOTE: This logic is removing all the snapshots older than the specified
		//       frame index while is not removing the specified frame index.
		//       It's quite important to keep that snapshot to ensure the function
		//       `apply_snapshot_instant_input_reconciliation` can work properly.
		//       It needs the snapshot the doll is at, to safely apply the reconciliation.
		while (!server_snapshots.empty() && server_snapshots.front().doll_executed_input < p_doll_frame_index) {
			VecFunc::remove_at(server_snapshots, 0);
		}

		// Removed all the checked doll frame snapshots.
		// NOTE: This logic is removing all the snapshots older than the specified
		//       frame index while is not removing the specified frame index.
		//       It's quite important to keep that snapshot to ensure the function
		//       `apply_snapshot_instant_input_reconciliation` can work properly.
		//       It needs the snapshot the doll is at, to safely apply the reconciliation.
		while (!client_snapshots.empty() && client_snapshots.front().doll_executed_input < p_doll_frame_index) {
			VecFunc::remove_at(client_snapshots, 0);
		}
	} else {
		VecFunc::remove(server_snapshots, FrameIndex::NONE);
		VecFunc::remove(client_snapshots, FrameIndex::NONE);
	}

	last_doll_validated_input = p_doll_frame_index;
}

void DollController::clear_previously_generated_client_snapshots() {
	if make_likely(current_input_buffer_id != FrameIndex::NONE) {
		// Removed all the client snapshots which input is more than the specified one
		// to ensure the function `__pcr__fetch_recovery_info` works properly.
		for (int i = int(client_snapshots.size()) - 1; i >= 0; i--) {
			if (client_snapshots[i].doll_executed_input > current_input_buffer_id) {
				VecFunc::remove_at(client_snapshots, i);
			} else {
				break;
			}
		}
	}
}

void DollController::on_received_server_snapshot(const Snapshot &p_snapshot) {
	NS_PROFILE
	const FrameIndexWithMeta doll_executed_input_meta = MapFunc::at(p_snapshot.peers_frames_index, peer_controller->get_authority_peer(), FrameIndexWithMeta());
	if (last_doll_validated_input != FrameIndex::NONE && last_doll_validated_input >= doll_executed_input_meta.frame_index) {
		// Snapshot already checked, no need to store this.
		return;
	}

	// This check ensure that the server_snapshots contains just a single FrameIndex::NONE
	// snapshot or a bunch of indexed one.
	if (p_snapshot.input_id == FrameIndex::NONE || doll_executed_input_meta.frame_index == FrameIndex::NONE) {
		// The received snapshot doesn't have a FrameIndex set, it means there is no controller
		// so assume this is the most up-to-date snapshot.
		server_snapshots.clear();
	} else {
		// Make sure to remove all the snapshots with FrameIndex::NONE received before this one.
		VecFunc::remove(server_snapshots, DollSnapshot(FrameIndex::NONE));
	}

	copy_controlled_objects_snapshot(p_snapshot, server_snapshots, true);
}

void DollController::on_snapshot_update_finished(const Snapshot &p_snapshot) {
#ifdef NS_DEBUG_ENABLED
	// The SceneSync set the correct input, and here it checks it.
	const FrameIndexWithMeta doll_executed_input = MapFunc::at(p_snapshot.peers_frames_index, peer_controller->get_authority_peer(), FrameIndexWithMeta());
	NS_ASSERT_COND(doll_executed_input.frame_index == current_input_buffer_id);
	// NOTE: This function is called on client, so is_server_validated is expected to be false at this point.
	NS_ASSERT_COND(doll_executed_input.is_server_validated== false);
#endif
	copy_controlled_objects_snapshot(p_snapshot, client_snapshots, false);
}

void DollController::copy_controlled_objects_snapshot(
		const Snapshot &p_snapshot,
		std::vector<DollSnapshot> &r_snapshots,
		bool p_store_even_when_doll_is_not_processing) {
	NS_PROFILE
	const FrameIndexWithMeta doll_executed_input_meta = MapFunc::at(p_snapshot.peers_frames_index, peer_controller->get_authority_peer(), FrameIndexWithMeta());

	std::vector<ObjectData *> controlled_objects;
	for (const SimulatedObjectInfo &sim_object : p_snapshot.simulated_objects) {
		if (sim_object.controlled_by_peer == peer_controller->get_authority_peer()) {
			ObjectData *object_data = peer_controller->scene_synchronizer->get_object_data(sim_object.net_id);
			if (object_data) {
				controlled_objects.push_back(peer_controller->scene_synchronizer->get_object_data(sim_object.net_id));
			} else {
				get_debugger().print(WARNING, "The object data with ID `" + sim_object.net_id + "` was not found, but it's expected to be found as this peer is simulating and controlling it. If this happens too many times and the game miss behave, this might be something to investigate.");
			}
		}
	}

	if (!p_store_even_when_doll_is_not_processing) {
		if (doll_executed_input_meta.frame_index == FrameIndex::NONE) {
			// Nothing to store.
			return;
		}
		if (controlled_objects.size() <= 0) {
			// Nothing to store for this doll.
			return;
		}
	}

	DollSnapshot *snap;
	{
		auto it = VecFunc::find(r_snapshots, DollSnapshot(doll_executed_input_meta.frame_index));
		if (it == r_snapshots.end()) {
			r_snapshots.push_back(DollSnapshot(FrameIndex::NONE));
			snap = &r_snapshots.back();
			snap->doll_executed_input = doll_executed_input_meta.frame_index;
		} else {
			snap = &*it;
		}
	}

	NS_ASSERT_COND(snap->doll_executed_input == doll_executed_input_meta.frame_index);
	snap->is_server_validated = doll_executed_input_meta.is_server_validated;
	snap->data.input_id = p_snapshot.input_id;

	// Extracts the data from the snapshot.
	MapFunc::assign(snap->data.peers_frames_index, peer_controller->get_authority_peer(), doll_executed_input_meta);

	if (controlled_objects.size() <= 0) {
		// Nothing to store for this doll.
		return;
	}

	// Find the biggest ID to initialize the snapshot.
	{
		ObjectNetId biggest_id = ObjectNetId{ { 0 } };
		for (ObjectData *object_data : controlled_objects) {
			if (object_data->get_net_id() > biggest_id) {
				biggest_id = object_data->get_net_id();
			}
		}
		snap->data.object_vars.resize(biggest_id.id + 1);
	}

	snap->data.simulated_objects.clear();

	// Now store the vars info.
	for (ObjectData *object_data : controlled_objects) {
		if (!VecFunc::has(p_snapshot.simulated_objects, object_data->get_net_id())) {
			// This object was not simulated.
			continue;
		}

		const std::vector<std::optional<VarData>> *vars = p_snapshot.get_object_vars(object_data->get_net_id());
		NS_ENSURE_CONTINUE_MSG(vars, "[FATAL] The snapshot didn't contain the object: " + object_data->get_net_id() + ". If this error spams for a long period (1/2 seconds) or never recover, it's a bug since.");

		snap->data.simulated_objects.push_back(object_data->get_net_id());

		snap->data.object_vars[object_data->get_net_id().id].clear();
		for (const std::optional<VarData> &nav : *vars) {
			if (nav.has_value()) {
				snap->data.object_vars[object_data->get_net_id().id].push_back(VarData::make_copy(nav.value()));
			} else {
				snap->data.object_vars[object_data->get_net_id().id].push_back(std::optional<VarData>());
			}
		}
	}

	// This array must be always sorted to ensure the snapshots order.
	std::sort(
			r_snapshots.begin(),
			r_snapshots.end(),
			is_doll_snap_A_older);
}

FrameIndex DollController::fetch_checkable_snapshot(DollSnapshot *&r_client_snapshot, DollSnapshot *&r_server_snapshot) {
	clear_previously_generated_client_snapshots();

	for (auto client_snap_it = client_snapshots.rbegin(); client_snap_it != client_snapshots.rend(); client_snap_it++) {
		if (client_snap_it->doll_executed_input != FrameIndex::NONE) {
			NS_ASSERT_COND_MSG(client_snap_it->doll_executed_input <= current_input_buffer_id, "All the client snapshots are properly cleared when the `current_input_id` is manipulated. So this function is impossible to trigger. If it does, there is a bug on the `clear_previously_generated_client_snapshots`.");

			auto server_snap_it = VecFunc::find(server_snapshots, client_snap_it->doll_executed_input);
			if (server_snap_it != server_snapshots.end()) {
				r_client_snapshot = &(*client_snap_it);
				r_server_snapshot = &*server_snap_it;
				return client_snap_it->doll_executed_input;
			}
		}
	}
	return FrameIndex::NONE;
}

bool DollController::__pcr__fetch_recovery_info(
		const FrameIndex p_checking_frame_index,
		const int p_frame_count_to_rewind,
		Snapshot *r_no_rewind_recover,
		std::vector<std::string> *r_differences_info
#ifdef NS_DEBUG_ENABLED
		,
		std::vector<ObjectNetId> *r_different_node_data
#endif
		) {
	// ---------------------------------------------- Force input reconciliation
	const Settings &settings = peer_controller->scene_synchronizer->get_settings();
	if (p_frame_count_to_rewind >= settings.lag_compensation.doll_force_input_reconciliation_min_frames) {
		const int optimal_queued_inputs = fetch_optimal_queued_inputs();
		const float optimal_input_count = float(p_frame_count_to_rewind + optimal_queued_inputs);
		const int input_count = (int)frames_input.size();
		if (input_count > (optimal_input_count + settings.lag_compensation.doll_force_input_reconciliation)) {
			return false;
		}
	}

	// ---------------------------------------------------- Snapshot comparation
	// Since the doll is processing a parallel timeline, we can't simply use
	// the `p_checking_frame_index` provided.

	// 1. Find the last processed client snapshot for which a server snapshot is
	//    available.
	DollSnapshot *client_snapshot;
	DollSnapshot *server_snapshot;

	const FrameIndex checkable_input = fetch_checkable_snapshot(client_snapshot, server_snapshot);
	if (checkable_input == FrameIndex::NONE) {
		// Nothing to check.
		return true;
	}

	last_doll_compared_input = checkable_input;

	// Now just compare the two snapshots.
	const bool compare = Snapshot::compare(
			*peer_controller->scene_synchronizer,
			server_snapshot->data,
			client_snapshot->data,
			peer_controller->get_authority_peer(),
			r_no_rewind_recover,
			r_differences_info
#ifdef NS_DEBUG_ENABLED
			,
			r_different_node_data
#endif
			);

	return compare;
}

void DollController::on_snapshot_applied(
		const Snapshot &p_global_server_snapshot,
		const int p_frame_count_to_rewind) {
#ifdef NS_DEBUG_ENABLED
	// The `DollController` is never created on the server, and the below
	// assertion is always satisfied.
	NS_ASSERT_COND(peer_controller->scene_synchronizer->is_client());
	NS_ASSERT_COND(p_frame_count_to_rewind >= 0);
#endif

	// This function is executed when the SceneSynchronizer apply the server
	// snapshot to reconcile the PlayerController.
	// The doll, which timeline is detached from the main SceneSync (which follows the PlayerController) timeline,
	// is still processed together with the SceneSync so it uses this event to
	// Apply the doll server snapshots and compensate the doll input.
	// NOTE: The input compensation is the act of:
	//       - Delaying the input processing when the input buffer is small (with the goal of growing the buffer)
	//       - Discarding part of the input buffer, if the buffer grown too much, to remain up-to-dated with the server.

	skip_snapshot_validation = false;

	if make_unlikely(!server_snapshots.empty() && server_snapshots.back().doll_executed_input == FrameIndex::NONE) {
		// This controller is not simulating on the server. This function handles this case.
		apply_snapshot_no_simulation(p_global_server_snapshot);
	}

	const FrameIndexWithMeta doll_executed_input_meta = MapFunc::at(p_global_server_snapshot.peers_frames_index, peer_controller->get_authority_peer(), FrameIndexWithMeta());
	if (doll_executed_input_meta.frame_index != FrameIndex::NONE && !doll_executed_input_meta.is_server_validated) {
		// This snapshot is a partially updated one that contains a state
		// generated locally, so it's not good for processing the input reconciliation.

		skip_snapshot_validation = true;

		apply_snapshot_no_input_reconciliation(p_global_server_snapshot, doll_executed_input_meta.frame_index);
		return;
	}

	if make_likely(current_input_buffer_id != FrameIndex::NONE) {
		if (p_frame_count_to_rewind == 0) {
			apply_snapshot_instant_input_reconciliation(p_global_server_snapshot, p_frame_count_to_rewind);
		} else {
			apply_snapshot_rewinding_input_reconciliation(p_global_server_snapshot, p_frame_count_to_rewind);
		}
	}
}

void DollController::apply_snapshot_no_simulation(const Snapshot &p_global_server_snapshot) {
	// Apply the latest received server snapshot right away since the doll is not
	// yet still processing on the server.

	NS_ASSERT_COND(server_snapshots.back().doll_executed_input == FrameIndex::NONE);

	static_cast<ClientSynchronizer *>(peer_controller->scene_synchronizer->get_synchronizer_internal())->apply_snapshot(server_snapshots.back().data, 0, 0, nullptr, true, true, true, true, true);
	last_doll_compared_input = FrameIndex::NONE;
	current_input_buffer_id = FrameIndex::NONE;
	queued_frame_index_to_process = FrameIndex::NONE;
}

void DollController::apply_snapshot_no_input_reconciliation(const Snapshot &p_global_server_snapshot, FrameIndex p_frame_index) {
	static_cast<ClientSynchronizer *>(peer_controller->scene_synchronizer->get_synchronizer_internal())->apply_snapshot(
			p_global_server_snapshot,
			0,
			0,
			nullptr,
			true,
			true,
			true,
			true,
			true);
	current_input_buffer_id = p_frame_index;
	queued_frame_index_to_process = current_input_buffer_id + 1;
	skip_snapshot_validation = true;
}

void DollController::apply_snapshot_instant_input_reconciliation(const Snapshot &p_global_server_snapshot, const int p_frame_count_to_rewind) {
	// This function assume the "frame count to rewind" is always 0.
	NS_ASSERT_COND(p_frame_count_to_rewind == 0);

	const int input_count = (int)frames_input.size();
	if make_unlikely(input_count == 0) {
		// When there are no inputs to process, it's much better not to apply
		// any snapshot.
		// The reason is that at some point it will receive inputs, and then
		// this algorithm will do much better job applying the snapshot and
		// avoid jittering.
		// NOTE: This logic is extremly important to avoid start discarding
		//       the inputs even before processing them, that could happen
		//       when the received server snapshot is ahead the received inputs.
		return;
	}

	// 1. Fetch the optimal queued inputs (how many inputs should be queued based
	//    on the current connection).
	const int optimal_queued_inputs = fetch_optimal_queued_inputs();

	// 2. Then, find the ideal input to restore. Notice that this logic is used
	//    mainly to alter the input buffering size:
	//    If the input buffer `frames_input` is too big it discards the superflous inputs.
	//    If the input buffer is too small adds some fake inputs to delay the execution.
	if make_likely(frames_input.back().id.id >= std::uint32_t(optimal_queued_inputs)) {
		last_doll_compared_input = frames_input.back().id - optimal_queued_inputs;
	} else {
		last_doll_compared_input = FrameIndex{ { 0 } };
	}

	// 3. Once the ideal input to restore is found, it's necessary to find the
	//    nearest server snapshot to apply.
	//    Notice that this logic is build so to prefer building a bigger input buffer
	//    than needed, while keeping the scene consistent, rather than breaking
	//    the synchronization.
	const DollSnapshot *snapshot_to_apply = nullptr;
	for (const DollSnapshot &snapshot : server_snapshots) {
		if (snapshot.doll_executed_input <= last_doll_compared_input) {
			snapshot_to_apply = &snapshot;
		} else {
			break;
		}
	}

	// 4. Just apply the snapshot.
	if (snapshot_to_apply) {
		static_cast<ClientSynchronizer *>(peer_controller->scene_synchronizer->get_synchronizer_internal())->apply_snapshot(snapshot_to_apply->data, 0, 0, nullptr, true, true, true, true, true);
		// Bring everything back to this point.
		last_doll_compared_input = snapshot_to_apply->doll_executed_input;
		current_input_buffer_id = last_doll_compared_input;
	}
}

void DollController::apply_snapshot_rewinding_input_reconciliation(const Snapshot &p_global_server_snapshot, const int p_frame_count_to_rewind) {
	// This function applies the snapshot and handles the reconciliation mechanism
	// during the rewinding process.
	// The input reconciliation performed during the rewinding is the best because
	// the timeline manipulations are much less visible.

	// This function assume the "frame count to rewind" is never 0.
	NS_ASSERT_COND(p_frame_count_to_rewind > 0);

	// 1. Fetch the optimal queued inputs (how many inputs should be queued based
	//    on the current connection).
	const int optimal_queued_inputs = fetch_optimal_queued_inputs();

	const int input_count = (int)frames_input.size();
	const DollSnapshot *server_snapshot = nullptr;
	FrameIndex new_last_doll_compared_input = FrameIndex::NONE;
	if make_likely(input_count > 0) {
		// 2. Fetch the best input to start processing.
		const int optimal_input_count = p_frame_count_to_rewind + optimal_queued_inputs;

		// The lag compensation algorithm offsets the available
		// inputs so that the `input_count` equals to `optimal_queued_inputs`
		// at the end of the reconciliation (rewinding) operation.

		// 3. Fetch the ideal frame to reset.
		if make_likely(frames_input.back().id.id >= std::uint32_t(optimal_input_count)) {
			new_last_doll_compared_input = frames_input.back().id - optimal_input_count;
		} else {
			new_last_doll_compared_input = FrameIndex{ { 0 } };
		}

		// 4. Ensure there is a server snapshot at some point, in between the new
		//    rewinding process queue or return and wait until there is a
		//    server snapshot.
		bool server_snapshot_found = false;
		for (auto it = server_snapshots.rbegin(); it != server_snapshots.rend(); it++) {
			if (it->doll_executed_input < (new_last_doll_compared_input + optimal_input_count)) {
				if make_likely(it->doll_executed_input > new_last_doll_compared_input) {
					// This is the most common case: The server snapshot is in between the rewinding.
					// Nothing to do here.
				} else if (it->doll_executed_input == new_last_doll_compared_input) {
					// In this case the rewinding is still in between the rewinding
					// though as an optimization we just assign the snapshot to apply
					// to avoid searching it.
					server_snapshot = &*it;
				} else {
					// In this case the server snapshot ISN'T part of the rewinding
					// so it brings the rewinding back a bit, to ensure the server
					// snapshot is applied.
					new_last_doll_compared_input = it->doll_executed_input;
					server_snapshot = &*it;
				}
				server_snapshot_found = true;
				break;
			}
		}

		if (!server_snapshot_found) {
			// Server snapshot not found: Set this to none to signal that this
			// rewind should not be performed.
			new_last_doll_compared_input = FrameIndex::NONE;
		}
	}

	if make_unlikely(input_count == 0 || new_last_doll_compared_input == FrameIndex::NONE) {
		// There are no inputs or there were no server snapshots to apply during
		// the rewinding phase, so it's preferable to wait more inputs and snapshots
		// so to safely apply the reconciliation without introducing any desynchronizations.
		//
		// The follow logic make sure that the rewinding is about to happen
		// doesn't alter this doll timeline: At the end of the rewinding this
		// doll will be exactly as is right now.
		const FrameIndex frames_to_travel = FrameIndex{ { std::uint32_t(p_frame_count_to_rewind + optimal_queued_inputs) } };
		if make_likely(current_input_buffer_id > frames_to_travel) {
			last_doll_compared_input = current_input_buffer_id - frames_to_travel;
		} else {
			last_doll_compared_input = FrameIndex{ { 0 } };
		}
	} else {
		last_doll_compared_input = new_last_doll_compared_input;
	}

	// 5. Now it's time to prepare the doll for the next rewinding that is about to:
	//    - Reconcile the client
	//    - Resize the input buffer.
	current_input_buffer_id = last_doll_compared_input;
	queued_frame_index_to_process = last_doll_compared_input + 1;

	if make_unlikely(server_snapshot) {
		// 6. Apply the server snapshot found during the point `4`.
		//    That logic detected that this controller has the server snapshot
		//    for the input we have to reset.
		//    In this case, it's mandatory to apply that, to ensure the scene
		//    reconciliation.
		static_cast<ClientSynchronizer *>(peer_controller->scene_synchronizer->get_synchronizer_internal())->apply_snapshot(server_snapshots.back().data, 0, 0, nullptr, true, true, true, true, true);
	} else if make_likely(!client_snapshots.empty()) {
		// 7. Get the closest available snapshot, and apply it, no need to be
		//    precise here, since the process will apply the server snapshot
		//    when available.
		int distance = std::numeric_limits<int>::max();
		DollSnapshot *best = nullptr;
		for (auto &snap : client_snapshots) {
			const int delta = (int)std::abs(std::int64_t(last_doll_compared_input.id) - std::int64_t(snap.doll_executed_input.id));
			if (delta < distance) {
				best = &snap;
				distance = delta;
			} else {
				// Since the snapshots are sorted, it can interrupt the
				// processing right after the distance start increasing.
				break;
			}
		}

		if (best) {
			static_cast<ClientSynchronizer *>(peer_controller->scene_synchronizer->get_synchronizer_internal())->apply_snapshot(best->data, 0, 0, nullptr, true, true, true, true, true);
		}
	}
}

NoNetController::NoNetController(PeerNetworkedController *p_peer_controller) :
	Controller(p_peer_controller),
	frame_id(FrameIndex{ { 0 } }) {
}

void NoNetController::process(float p_delta) {
	peer_controller->get_inputs_buffer_mut().begin_write(get_debugger(), 0); // No need of meta in this case.
	peer_controller->get_debugger().print(INFO, "Nonet process index: " + std::string(frame_id), "CONTROLLER-" + std::to_string(peer_controller->authority_peer));
	peer_controller->controllable_collect_input(p_delta, peer_controller->get_inputs_buffer_mut());
	peer_controller->get_inputs_buffer_mut().dry();
	peer_controller->get_inputs_buffer_mut().begin_read(get_debugger());
	peer_controller->get_inputs_buffer_mut().seek(METADATA_SIZE); // Skip meta.
	peer_controller->get_debugger().databuffer_operation_begin_record(peer_controller->authority_peer, SceneSynchronizerDebugger::READ);
	peer_controller->controllable_process(p_delta, peer_controller->get_inputs_buffer_mut());
	peer_controller->get_debugger().databuffer_operation_end_record();
	frame_id += 1;
}

FrameIndex NoNetController::get_current_frame_index() const {
	return frame_id;
}

NS_NAMESPACE_END