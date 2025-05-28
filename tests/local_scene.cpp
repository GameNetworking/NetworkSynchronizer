#include "local_scene.h"

#include <memory>
#include <string>
#include <cstring>

NS_NAMESPACE_BEGIN
LocalSceneObject::~LocalSceneObject() {
	const NS::ObjectLocalId id = find_local_id();
	if (get_scene() && get_scene()->scene_sync && id != NS::ObjectLocalId::NONE) {
		get_scene()->scene_sync->unregister_app_object(id);
	}
}

NS::ObjectLocalId LocalSceneObject::find_local_id() const {
	if (get_scene()) {
		return get_scene()->scene_sync->find_object_local_id(get_scene()->scene_sync->to_handle(this));
	}
	return NS::ObjectLocalId::NONE;
}

LocalSceneSynchronizer::LocalSceneSynchronizer(bool p_disable_sub_ticking) :
	SceneSynchronizer<LocalSceneObject, LocalNetworkInterface>(true, p_disable_sub_ticking),
	LocalSceneObject("LocalSceneSynchronizer") {
}

LocalSceneSynchronizer::~LocalSceneSynchronizer() {
}

void (*prev_var_data_encode_func)(NS::DataBuffer &r_buffer, const NS::VarData &p_val) = nullptr;
void (*prev_var_data_decode_func)(NS::VarData &r_val, NS::DataBuffer &p_buffer, std::uint8_t p_variable_type) = nullptr;
bool (*prev_var_data_compare_func)(const VarData &p_A, const VarData &p_B) = nullptr;
std::string (*prev_var_data_stringify_func)(const VarData &p_var_data, bool p_verbose) = nullptr;

void LocalSceneSynchronizer::install_local_scene_sync() {
	// Store the already set functions, so we can restore it again after the tests are done.
	prev_var_data_encode_func = SceneSynchronizerBase::var_data_encode_func;
	prev_var_data_decode_func = SceneSynchronizerBase::var_data_decode_func;
	prev_var_data_compare_func = SceneSynchronizerBase::var_data_compare_func;
	prev_var_data_stringify_func = SceneSynchronizerBase::var_data_stringify_func;

	install_synchronizer(
			[](NS::DataBuffer &r_buffer, const NS::VarData &p_val) {
				r_buffer.add(p_val.type);
				if (p_val.type == 3) {
					// Shared buffer array of integers
					const std::vector<int> &val = *std::static_pointer_cast<std::vector<int>>(p_val.shared_buffer);
					r_buffer.add(int(val.size()));
					for (int v : val) {
						r_buffer.add(v);
					}
				} else {
					r_buffer.add_bits(reinterpret_cast<const uint8_t *>(&p_val.data), sizeof(p_val.data) * 8);
					// The shared buffer must have the type set to 3.
					NS_ASSERT_COND(!p_val.shared_buffer);
				}
			},
			[](NS::VarData &r_val, NS::DataBuffer &p_buffer, std::uint8_t p_variable_type) {
				p_buffer.read(r_val.type);
				if (r_val.type == 3) {
					int size;
					p_buffer.read(size);
					std::vector<int> array;
					for (int i = 0; i < size; i++) {
						int v;
						p_buffer.read(v);
						array.push_back(v);
					}
					r_val.shared_buffer = std::make_shared<std::vector<int>>(array);
				} else {
					p_buffer.read_bits(reinterpret_cast<uint8_t *>(&r_val.data), sizeof(r_val.data) * 8);
				}
			},
			[](const NS::VarData &p_A, const NS::VarData &p_B) -> bool {
				if (p_A.type == 3 && p_B.type == 3) {
					const std::vector<int> &A = *std::static_pointer_cast<std::vector<int>>(p_A.shared_buffer);
					const std::vector<int> &B = *std::static_pointer_cast<std::vector<int>>(p_B.shared_buffer);
					if (A.size() == B.size()) {
						for (int i = 0; i < A.size(); i++) {
							if (A[i] != B[i]) {
								return false;
							}
						}
						return true;
					}
					return false;
				} else {
					return memcmp(&p_A.data, &p_B.data, sizeof(p_A.data)) == 0;
				}
			},
			[](const NS::VarData &p_var_data, bool p_verbose) -> std::string {
				if (p_var_data.type == 1) {
					return std::to_string(p_var_data.data.f32);
				} else if (p_var_data.type == 2) {
					return std::string("[" + std::to_string(p_var_data.data.vec.x) + ", " + std::to_string(p_var_data.data.vec.y) + ", " + std::to_string(p_var_data.data.vec.z) + "]");
				} else if (p_var_data.type == 3) {
					return std::string("[Array of ints]");
				} else {
					return std::string("[No stringify supported for this VarData type: `" + std::to_string(p_var_data.type) + "`]");
				}
			},
			print_line_func,
			print_code_message_func,
			print_flush_stdout_func);
}

void LocalSceneSynchronizer::uninstall_local_scene_sync() {
	// Restore the previously set functions.
	install_synchronizer(
			prev_var_data_encode_func,
			prev_var_data_decode_func,
			prev_var_data_compare_func,
			prev_var_data_stringify_func,
			print_line_func,
			print_code_message_func,
			print_flush_stdout_func);

	prev_var_data_encode_func = nullptr;
	prev_var_data_decode_func = nullptr;
	prev_var_data_compare_func = nullptr;
	prev_var_data_stringify_func = nullptr;
}

void LocalSceneSynchronizer::on_scene_entry() {
	get_network_interface().init(get_scene()->get_network(), name, authoritative_peer_id);
	setup(*this);
	register_app_object(to_handle(this));
}

void LocalSceneSynchronizer::setup_synchronizer(LocalSceneSynchronizer &p_scene_sync, ObjectLocalId p_id) {
	local_id = p_id;
}

void LocalSceneSynchronizer::on_scene_exit() {
	on_app_object_removed(to_handle(this));
}

ObjectHandle LocalSceneSynchronizer::fetch_app_object(const std::string &p_object_name) {
	LocalSceneObject *lso = get_scene()->fetch_object<LocalSceneObject>(p_object_name.c_str());
	if (lso) {
		return to_handle(lso);
	}
	return ObjectHandle::NONE;
}

uint64_t LocalSceneSynchronizer::debug_only_get_object_id(ObjectHandle p_app_object_handle) const {
	// No Object ID.
	return 0;
}

std::string LocalSceneSynchronizer::fetch_object_name(ObjectHandle p_app_object_handle) const {
	return from_handle(p_app_object_handle)->name;
}

void LocalSceneSynchronizer::setup_synchronizer_for(ObjectHandle p_app_object_handle, ObjectLocalId p_id, std::uint16_t p_scheme_id) {
	from_handle(p_app_object_handle)->scheme_id = p_scheme_id;
	from_handle(p_app_object_handle)->setup_synchronizer(*get_scene()->scene_sync, p_id);
}

void LocalSceneSynchronizer::clear_scene() {
	scene_owner = nullptr;
}

LocalScene *LocalSceneObject::get_scene() const {
	return scene_owner;
}

void LocalScene::start_as_no_net() {
	network.start_as_no_net();
}

void LocalScene::start_as_server() {
	network.start_as_server();
}

void LocalScene::start_as_client(LocalScene &p_server) {
	network.start_as_client(p_server.network);
}

int LocalScene::get_peer() const {
	return network.get_peer();
}

int LocalScene::fetch_object_index(const char *p_object_name) const {
	int i = 0;
	for (auto o : objects) {
		if (o->name == p_object_name) {
			return i;
		}
		i++;
	}
	return -1;
}

void LocalScene::remove_object(const char *p_object_name) {
	const int obj_index = fetch_object_index(p_object_name);
	if (obj_index >= 0) {
		std::shared_ptr<LocalSceneObject> obj = objects[obj_index];
		obj->on_scene_exit();
		obj->scene_owner = nullptr;
		VecFunc::remove_at_unordered(objects, obj_index);
	}
}

void LocalScene::process(float p_delta) {
	scene_sync->process(p_delta);
	// Clear any pending RPC.
	// NOTE: The network process is executed after the scene_sync so any pending
	// 			rpc is dispatched right away. When the rpc is sent, it's received
	// 			right away, so it's not needed to process it before the scene_sync.
	network.process(p_delta);
}

void LocalScene::process_only_network(float p_delta) {
	network.process(p_delta);
}

NS_NAMESPACE_END