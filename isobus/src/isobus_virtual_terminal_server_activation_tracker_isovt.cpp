//================================================================================================
/// @file isobus_virtual_terminal_server_activation_tracker_isovt.cpp
///
/// @brief Implements the fork's ISO 11783-6 Annex H activation tracker for a VT-version-6 pair:
/// Transaction Number (TAN) generation and stamping for the activation senders, response pairing by
/// TAN, the 300 ms retry (up to three), and the clause 4.6.9 unexpected-shutdown timeout the
/// loss-teardown pass reads. For a version-5-or-prior pair every activation frame is byte-identical
/// to before -- the TAN byte stays reserved 0xFF and nothing is tracked.
///
/// This translation unit is isovt-owned and has no upstream counterpart (ADR-0008): the tracker is
/// additive VT-server machinery, kept out of the upstream isobus_virtual_terminal_server.cpp to keep
/// that file's merge surface small. Its definitions remain members of VirtualTerminalServer, declared
/// in the upstream header.
//================================================================================================

#include "isobus/isobus/isobus_virtual_terminal_server.hpp"
#include "isobus/isobus/can_general_parameter_group_numbers.hpp"
#include "isobus/isobus/can_network_manager.hpp"
#include "isobus/isobus/can_stack_logger.hpp"
#include "isobus/utility/system_timing.hpp"

namespace isobus
{
	std::size_t VirtualTerminalServer::activation_tan_byte_offset(std::uint8_t functionCode)
	{
		// ISO 11783-6 Annex H: the TAN occupies bits 7-4 of one message-specific byte. Soft Key
		// Activation (H.2), Button Activation (H.4), VT Select Input Object (H.8) and VT ESC (H.10)
		// carry it in byte 8; VT Change Numeric Value (H.12) carries it in byte 4. For all of those the
		// byte's low nibble is reserved 0xF. The Pointing Event (H.6) carries the TAN in byte 6 bits 7-4,
		// but its low nibble is the Touch State (0/1/2), not reserved -- stamp_and_record_activation
		// preserves whatever low nibble the sender built there, so this byte offset is all the pairing
		// needs to distinguish. Byte offsets are zero-based here (byte 8 = index 7, byte 6 = index 5,
		// byte 4 = index 3).
		switch (static_cast<Function>(functionCode))
		{
			case Function::SoftKeyActivationMessage:
			case Function::ButtonActivationMessage:
			case Function::VTSelectInputObjectMessage:
			case Function::VTESCMessage:
				return 7;

			case Function::PointingEventMessage:
				return 5;

			case Function::VTChangeNumericValueMessage:
				return 3;

			default:
				return CAN_DATA_LENGTH;
		}
	}

	std::shared_ptr<VirtualTerminalServerManagedWorkingSet> VirtualTerminalServer::find_managed_working_set_for(const std::shared_ptr<ControlFunction> &destination) const
	{
		if (nullptr != destination)
		{
			for (const auto &ws : managedWorkingSetList)
			{
				if (ws->get_control_function() == destination)
				{
					return ws;
				}
			}
		}
		return nullptr;
	}

	bool VirtualTerminalServer::stamp_and_record_activation(std::shared_ptr<ControlFunction> destination, std::uint8_t functionCode, std::array<std::uint8_t, CAN_DATA_LENGTH> &frame) const
	{
		const std::size_t tanByte = activation_tan_byte_offset(functionCode);
		if (tanByte >= CAN_DATA_LENGTH)
		{
			return false; // Not a TAN-tracked activation message (e.g. the Pointing Event, deferred to a later slice)
		}

		auto workingSet = find_managed_working_set_for(destination);
		if (!is_version6_pair(workingSet))
		{
			// A version-5-or-prior pair keeps the legacy optional-response behaviour: the frame's TAN byte
			// is left at its reserved value, so the frame is byte-identical to before Annex H tracking.
			return false;
		}

		ActivationTrackerState &state = activationTrackers[workingSet.get()];
		const std::uint8_t tan = state.nextTan;
		state.nextTan = static_cast<std::uint8_t>((state.nextTan + 1u) & 0x0Fu);

		// ISO 11783-6 Annex H: TAN in bits 7-4. The low nibble is left as the sender built it: reserved
		// 0xF for Soft Key / Button / Select Input / ESC / Change Numeric Value (whose builders fill the
		// byte 0xFF), and the Touch State for the Pointing Event (H.6 byte 6 bits 3-0). Preserving it is
		// what lets a retry resend the same frame -- including the same touch state -- unchanged.
		frame[tanByte] = static_cast<std::uint8_t>((tan << 4) | (frame[tanByte] & 0x0Fu));

		// H.1's current-value-not-stale rule: a new activation for the same function supersedes the prior
		// outstanding one (a new state/value carries a fresh TAN), so only the latest is retried. operator[]
		// replaces any existing entry for this function code.
		OutstandingActivation &entry = state.outstanding[functionCode];
		entry.frame = frame;
		entry.sentAtMs = SystemTiming::get_timestamp_ms();
		entry.tan = tan;
		entry.retriesRemaining = ACTIVATION_MAX_RETRIES;
		return true;
	}

	void VirtualTerminalServer::handle_activation_response(const std::shared_ptr<VirtualTerminalServerManagedWorkingSet> &workingSet, std::uint8_t functionCode, std::uint8_t responseTan)
	{
		if (nullptr == workingSet)
		{
			return;
		}

		auto trackerIt = activationTrackers.find(workingSet.get());
		if (activationTrackers.end() == trackerIt)
		{
			return;
		}

		auto &outstanding = trackerIt->second.outstanding;
		auto entryIt = outstanding.find(functionCode);
		if (outstanding.end() == entryIt)
		{
			return; // No activation of this function is awaiting a response (already cleared, or never tracked)
		}

		if (entryIt->second.tan == responseTan)
		{
			// ISO 11783-6 H.1: the response's TAN matches the outstanding activation -- the pair is
			// complete, so stop retrying it.
			outstanding.erase(entryIt);
		}
		else
		{
			// The response answers a frame a newer activation already superseded (H.1: the TAN aligns
			// potentially overlapping message-response pairs). The current entry keeps waiting for its
			// own response.
			LOG_DEBUG("[VT Server]: Activation response for function 0x%02X from client 0x%02X carried TAN %u, but the outstanding activation has TAN %u; ignoring the superseded response.",
			          functionCode,
			          (nullptr != workingSet->get_control_function()) ? workingSet->get_control_function()->get_address() : NULL_CAN_ADDRESS,
			          responseTan,
			          entryIt->second.tan);
		}
	}

	void VirtualTerminalServer::update_activation_trackers()
	{
		for (const auto &ws : managedWorkingSetList)
		{
			auto trackerIt = activationTrackers.find(ws.get());
			if (activationTrackers.end() == trackerIt)
			{
				continue;
			}
			ActivationTrackerState &state = trackerIt->second;

			for (auto &functionEntry : state.outstanding)
			{
				OutstandingActivation &entry = functionEntry.second;
				if (!SystemTiming::time_expired_ms(entry.sentAtMs, ACTIVATION_RESPONSE_TIMEOUT_MS))
				{
					continue;
				}

				if (entry.retriesRemaining > 0)
				{
					// ISO 11783-6 H.1: no response within 300 ms -- retry the activation unchanged, keeping
					// the same TAN (the data are unchanged, so H.1 requires the prior TAN, e.g. a button
					// release's retry).
					CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
					                                               entry.frame.data(),
					                                               CAN_DATA_LENGTH,
					                                               serverInternalControlFunction,
					                                               ws->get_control_function(),
					                                               get_priority());
					entry.retriesRemaining--;
					entry.sentAtMs = SystemTiming::get_timestamp_ms();
				}
				else
				{
					// ISO 11783-6 H.1: the required response did not arrive after the third retry's 300 ms
					// window either. The VT shall perform as if an unexpected shutdown of the working set
					// occurred (clause 4.6.9). Flag it; the loss-teardown pass in update() (which runs after
					// this one) reads the flag and tears the working set down. Once it is no longer a managed
					// working set, its subsequent Working Set Maintenance messages are answered with the
					// Acknowledgement:NACK the generic unmanaged-source path already sends -- which is exactly
					// H.1's "send the Acknowledgement:NACK message in response to the Working Set Maintenance
					// message".
					state.responseTimedOut = true;
				}
			}
		}
	}

	bool VirtualTerminalServer::is_activation_response_timed_out(const std::shared_ptr<VirtualTerminalServerManagedWorkingSet> &workingSet) const
	{
		if (nullptr == workingSet)
		{
			return false;
		}
		auto trackerIt = activationTrackers.find(workingSet.get());
		return (activationTrackers.end() != trackerIt) && trackerIt->second.responseTimedOut;
	}

	void VirtualTerminalServer::clear_activation_tracker(const std::shared_ptr<VirtualTerminalServerManagedWorkingSet> &workingSet)
	{
		if (nullptr != workingSet)
		{
			activationTrackers.erase(workingSet.get());
		}
	}
}
