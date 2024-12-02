#include "peer_data.h"

#include <algorithm>
#include <cmath>

void NS::PeerData::set_latency(float p_latency) {
	compressed_latency = (std::uint8_t)std::round(std::clamp(p_latency, 0.f, 1000.0f) / 4.0f);
}

float NS::PeerData::get_latency() const {
	return compressed_latency * 4.0f;
}

void NS::PeerData::set_out_packet_loss_percentage(float p_packet_loss) {
	out_packet_loss_percentage = std::clamp(p_packet_loss, 0.0f, 1.0f);
}

void NS::PeerData::make_controller(SceneSynchronizerBase &p_scene_synchronizer) {
	controller = std::make_unique<PeerNetworkedController>(p_scene_synchronizer);
}
