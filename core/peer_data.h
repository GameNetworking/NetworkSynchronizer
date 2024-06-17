
#pragma once

#include "core.h"
#include "peer_networked_controller.h"

NS_NAMESPACE_BEGIN

// These data are used by the server and are never synchronized.
struct PeerAuthorityData {
	// Used to know if the peer is enabled.
	bool enabled = true;

	// The Sync group this peer is in.
	SyncGroupId sync_group_id = SyncGroupId::GLOBAL;
};

struct PeerData {
	std::unique_ptr<PeerNetworkedController> controller;

	PeerAuthorityData authority_data;

private:
	/// Get latency (ping): The round trip time a packet takes to go and return back.
	std::uint8_t compressed_latency = 0;

	/// Get OUT packetloss in %
	float out_packet_loss_percentage = 0.0f;

	/// Current jitter for this connection in milliseconds.
	/// Jitter represents the average time divergence of all sent packets.
	/// Ex:
	/// - If the time between the sending and the reception of packets is always
	///   100ms; the jitter will be 0.
	/// - If the time difference is either 150ms or 100ms, the jitter will tend
	///   towards 50ms.
	float latency_jitter_ms = 0.0f;

public:
	// These constructors were added to avoid the std::map to complain about the
	// PeerData inability to be copied. I was unable to figure out why std::move
	// stop working and didn't have the time to figure it out.
	// TODO consider to fix this --^
	PeerData() = default;
	PeerData(const PeerData &other) :
			authority_data(other.authority_data),
			compressed_latency(other.compressed_latency),
			out_packet_loss_percentage(other.out_packet_loss_percentage),
			latency_jitter_ms(other.latency_jitter_ms) {}
	PeerData &operator=(const PeerData &other) noexcept {
		authority_data = other.authority_data;
		compressed_latency = other.compressed_latency;
		out_packet_loss_percentage = other.out_packet_loss_percentage;
		latency_jitter_ms = other.latency_jitter_ms;
		return *this;
	}

public:
	// In ms
	void set_latency(float p_ping);

	// In ms
	float get_latency() const;

	void set_compressed_latency(std::uint8_t p_compressed_latency) { compressed_latency = p_compressed_latency; }
	std::uint8_t get_compressed_latency() const { return compressed_latency; }

	void set_out_packet_loss_percentage(float p_packet_loss);
	float get_out_packet_loss_percentage() const { return out_packet_loss_percentage; }

	void set_latency_jitter_ms(float p_jitter_ms) { latency_jitter_ms = p_jitter_ms; }
	float get_latency_jitter_ms() const { return latency_jitter_ms; }

	void make_controller();
	PeerNetworkedController *get_controller() {
		return controller.get();
	}
	const PeerNetworkedController *get_controller() const {
		return controller.get();
	}
};

NS_NAMESPACE_END