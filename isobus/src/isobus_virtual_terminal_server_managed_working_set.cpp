//================================================================================================
/// @file isobus_virtual_terminal_server_managed_working_set.cpp
///
/// @brief Defines a class that manages a VT server's active working sets.
/// @author Adrian Del Grosso
///
/// @copyright 2023 Adrian Del Grosso
//================================================================================================
#include "isobus/isobus/isobus_virtual_terminal_server_managed_working_set.hpp"

#include "isobus/isobus/can_network_manager.hpp"
#include "isobus/isobus/can_stack_logger.hpp"
#include "isobus/utility/to_string.hpp"

#include <cstring>

namespace isobus
{
	VirtualTerminalServerManagedWorkingSet::VirtualTerminalServerManagedWorkingSet()
	{
		LOG_INFO("[WS]: New VT Server Object Created with no associated control function");
	}

	VirtualTerminalServerManagedWorkingSet::VirtualTerminalServerManagedWorkingSet(std::shared_ptr<ControlFunction> associatedControlFunction) :
	  workingSetControlFunction(associatedControlFunction)
	{
		if (nullptr != associatedControlFunction)
		{
			LOG_INFO("[WS]: New VT Server Object Created for CF " + to_string(static_cast<int>(associatedControlFunction->get_NAME().get_full_name())));
		}
		else
		{
			LOG_INFO("[WS]: New VT Server Object Created with no associated control function");
		}
	}

	void VirtualTerminalServerManagedWorkingSet::start_parsing_thread()
	{
		if (nullptr == objectPoolProcessingThread)
		{
			objectPoolProcessingThread.reset(new std::thread([this]() { worker_thread_function(); }));
		}
	}

	void VirtualTerminalServerManagedWorkingSet::join_parsing_thread()
	{
		if ((nullptr != objectPoolProcessingThread) && (objectPoolProcessingThread->joinable()))
		{
			objectPoolProcessingThread->join();
			objectPoolProcessingThread = nullptr;
			set_object_pool_processing_state(ObjectPoolProcessingThreadState::Joined);
		}
	}

	bool VirtualTerminalServerManagedWorkingSet::get_any_object_pools() const
	{
		return (!iopFilesRawData.empty());
	}

	bool VirtualTerminalServerManagedWorkingSet::is_object_pool_parse_outstanding() const
	{
		return (nullptr != objectPoolProcessingThread);
	}

	bool VirtualTerminalServerManagedWorkingSet::reset_object_pool()
	{
		bool retVal = false;

		if (!is_object_pool_parse_outstanding())
		{
			reset_object_pool_storage();

			// Deleting the pool ends any input it had open, so ESC must not go on reporting a field of
			// the deleted pool as open and running its macros.
			objectOpenForInput = NULL_OBJECT_ID;
			focusedObject = NULL_OBJECT_ID;

			// The deleted pool declared whatever mask was locked, and F.46 lists "The pool is deleted"
			// among the mechanisms that release a lock, so no later repaint may be withheld on its
			// account. No unsolicited Lock/Unlock Mask Response accompanies this. F.46 does say
			// generally that "when one of the unlock mechanisms occurs, a response message is sent",
			// but its "shall" for an unsolicited response names only the timeout and the mask going
			// hidden, and it requires "appropriate error codes set" -- of which there is none for a
			// deleted pool. The client asked for this deletion and is answered by the F.45 response.
			maskLockObjectID = NULL_OBJECT_ID;
			maskLockTimeout_ms = 0;
			maskLockTimestamp_ms = 0;

			// The Colour Map selected by Select Colour Map (F.60) was an object of the deleted pool.
			// NULL_OBJECT_ID is the default palette, which is what a pool-less working set uses.
			activeColourMapObjectId = NULL_OBJECT_ID;

			// A non-zero sequence says an Alarm Mask of this working set is asserted, and the mask went
			// with the pool. It matters beyond tidiness: stamp_alarm_activation_sequences() re-stamps
			// only a working set whose sequence is zero, so a stale value would order a later pool's
			// first alarm as though it had been raised at the deleted pool's activation time and let it
			// win the clause 4.6.14 chronological tie-break against alarms raised in between.
			alarmActivationSequence = 0;

			// These select the message that completes the NEXT parse (a Load Version response rather
			// than an End of Object Pool response, and the extended variant of it). They describe the
			// pool that was just deleted, so a new upload must not inherit them.
			wasLoadedFromNonVolatileMemory = false;
			loadedViaExtendedVersionCommand = false;

			// None, not Joined: no parse has been run for the pool this working set now holds, which is
			// no pool at all. start_parsing_thread() keys off the thread handle rather than this state,
			// so a later upload still parses.
			set_object_pool_processing_state(ObjectPoolProcessingThreadState::None);
			retVal = true;
		}
		return retVal;
	}

	VirtualTerminalServerManagedWorkingSet::ObjectPoolProcessingThreadState VirtualTerminalServerManagedWorkingSet::get_object_pool_processing_state()
	{
		const std::lock_guard<std::mutex> lock(managedWorkingSetMutex);
		return processingState;
	}

	std::shared_ptr<ControlFunction> VirtualTerminalServerManagedWorkingSet::get_control_function() const
	{
		return workingSetControlFunction;
	}

	std::uint32_t VirtualTerminalServerManagedWorkingSet::get_working_set_maintenance_message_timestamp_ms() const
	{
		return workingSetMaintenanceMessageTimestamp_ms;
	}

	void VirtualTerminalServerManagedWorkingSet::set_working_set_maintenance_message_timestamp_ms(std::uint32_t value)
	{
		workingSetMaintenanceMessageTimestamp_ms = value;
	}

	void VirtualTerminalServerManagedWorkingSet::save_callback_handle(isobus::EventCallbackHandle callbackHandle)
	{
		callbackHandles.push_back(callbackHandle);
	}

	void VirtualTerminalServerManagedWorkingSet::clear_callback_handles()
	{
		callbackHandles.clear();
	}

	bool VirtualTerminalServerManagedWorkingSet::get_was_object_pool_loaded_from_non_volatile_memory() const
	{
		return wasLoadedFromNonVolatileMemory;
	}

	void VirtualTerminalServerManagedWorkingSet::set_was_object_pool_loaded_from_non_volatile_memory(bool value, CANLibBadge<VirtualTerminalServer>)
	{
		wasLoadedFromNonVolatileMemory = value;
	}

	bool VirtualTerminalServerManagedWorkingSet::get_loaded_via_extended_version_command() const
	{
		return loadedViaExtendedVersionCommand;
	}

	void VirtualTerminalServerManagedWorkingSet::set_loaded_via_extended_version_command(bool value, CANLibBadge<VirtualTerminalServer>)
	{
		loadedViaExtendedVersionCommand = value;
	}

	std::uint8_t VirtualTerminalServerManagedWorkingSet::get_working_set_maintenance_version() const
	{
		return workingSetMaintenanceVersion;
	}

	void VirtualTerminalServerManagedWorkingSet::set_working_set_maintenance_version(std::uint8_t value, CANLibBadge<VirtualTerminalServer>)
	{
		workingSetMaintenanceVersion = value;
	}

	std::uint16_t VirtualTerminalServerManagedWorkingSet::get_active_colour_map_object_id() const
	{
		return activeColourMapObjectId;
	}

	void VirtualTerminalServerManagedWorkingSet::set_active_colour_map_object_id(std::uint16_t value, CANLibBadge<VirtualTerminalServer>)
	{
		activeColourMapObjectId = value;
	}

	std::uint16_t VirtualTerminalServerManagedWorkingSet::get_mask_lock_object_id() const
	{
		return maskLockObjectID;
	}

	std::uint16_t VirtualTerminalServerManagedWorkingSet::get_mask_lock_timeout_ms() const
	{
		return maskLockTimeout_ms;
	}

	std::uint32_t VirtualTerminalServerManagedWorkingSet::get_mask_lock_timestamp_ms() const
	{
		return maskLockTimestamp_ms;
	}

	void VirtualTerminalServerManagedWorkingSet::set_mask_lock(std::uint16_t objectID, std::uint16_t timeout_ms, std::uint32_t timestamp_ms, CANLibBadge<VirtualTerminalServer>)
	{
		maskLockObjectID = objectID;
		maskLockTimeout_ms = timeout_ms;
		maskLockTimestamp_ms = timestamp_ms;
	}

	std::uint32_t VirtualTerminalServerManagedWorkingSet::get_alarm_activation_sequence() const
	{
		return alarmActivationSequence;
	}

	void VirtualTerminalServerManagedWorkingSet::set_alarm_activation_sequence(std::uint32_t value, CANLibBadge<VirtualTerminalServer>)
	{
		alarmActivationSequence = value;
	}

	void VirtualTerminalServerManagedWorkingSet::set_object_focus(std::uint16_t objectID)
	{
		focusedObject = objectID;
	}

	std::uint16_t VirtualTerminalServerManagedWorkingSet::get_object_focus() const
	{
		return focusedObject;
	}

	void VirtualTerminalServerManagedWorkingSet::set_object_open_for_input(std::uint16_t objectID)
	{
		objectOpenForInput = objectID;
	}

	std::uint16_t VirtualTerminalServerManagedWorkingSet::get_object_open_for_input() const
	{
		return objectOpenForInput;
	}

	void VirtualTerminalServerManagedWorkingSet::set_auxiliary_input_maintenance_timestamp_ms(std::uint32_t value)
	{
		auxiliaryInputMaintenanceMessageTimestamp_ms = value;
	}

	std::uint32_t VirtualTerminalServerManagedWorkingSet::get_auxiliary_input_maintenance_timestamp_ms() const
	{
		return auxiliaryInputMaintenanceMessageTimestamp_ms;
	}

	void VirtualTerminalServerManagedWorkingSet::request_deletion()
	{
		workingSetDeletionRequested = true;
	}

	bool VirtualTerminalServerManagedWorkingSet::is_deletion_requested() const
	{
		return workingSetDeletionRequested;
	}

	void VirtualTerminalServerManagedWorkingSet::set_iop_size(std::uint32_t newIopSize)
	{
		const std::lock_guard<std::mutex> lock(managedWorkingSetMutex);
		iopSize = newIopSize;
		// Each Get Memory (or Load Version) declares the size of a fresh transfer, so the
		// running transferred-byte count restarts here. This keeps transferredIopSize per
		// declared transfer rather than a monotonic lifetime total, so the overrun check stays
		// correct when a working set is reused (a Load Version or a runtime pool update).
		transferredIopSize = 0;
	}

	float VirtualTerminalServerManagedWorkingSet::iop_load_percentage() const
	{
		if (processingState != ObjectPoolProcessingThreadState::None || transferredIopSize > iopSize)
		{
			return 100.0f;
		}

		if (0 == iopSize)
		{
			return 0.0f;
		}

		// if IOP transfer is not completed check if there is an ongoing IOP transfer to us
		auto sessions = CANNetworkManager::CANNetwork.get_active_transport_protocol_sessions(0);
		auto currentTransferredIopSize = transferredIopSize;
		for (const auto &session : sessions)
		{
			if (session->get_source()->get_address() == get_control_function()->get_address() &&
			    (static_cast<std::uint32_t>(CANLibParameterGroupNumber::ECUtoVirtualTerminal) == session->get_parameter_group_number()) &&
			    (session->get_data().size() >= 1) &&
			    (session->get_data().get_byte(0) == 0x11)) // ObjectPoolTransferMessage
			{
				currentTransferredIopSize += session->get_total_bytes_transferred();
			}
		}

		if (currentTransferredIopSize > iopSize)
		{
			return 100.0f;
		}
		return (static_cast<float>(currentTransferredIopSize) / static_cast<float>(iopSize)) * 100.0f;
	}

	void VirtualTerminalServerManagedWorkingSet::set_object_pool_processing_state(ObjectPoolProcessingThreadState value)
	{
		const std::lock_guard<std::mutex> lock(managedWorkingSetMutex);
		processingState = value;
	}

	void VirtualTerminalServerManagedWorkingSet::worker_thread_function()
	{
		// Reject a pool that transferred more bytes than it declared in Get Memory (Annex D.3):
		// the declared size was already vetted against the memory budget, so honouring it bounds
		// the parse. transferredIopSize is reset per declared transfer (see set_iop_size), so this
		// holds equally for a reused working set (Load Version, runtime pool update).
		if (!is_object_pool_within_declared_iop_size())
		{
			LOG_ERROR("[WS]: Object pool transferred more than the declared memory; rejecting the pool.");
			clear_published_object_tree();
			set_object_pool_processing_state(ObjectPoolProcessingThreadState::Fail);
			return;
		}

		if (!iopFilesRawData.empty())
		{
			bool lSuccess = true;

			set_object_pool_processing_state(ObjectPoolProcessingThreadState::Running);
			LOG_INFO("[WS]: Beginning parsing of object pool. This pool has " +
			         isobus::to_string(static_cast<int>(iopFilesRawData.size())) +
			         " IOP components, " +
			         isobus::to_string(static_cast<int>(iopFilesRawData.size() - parsedIopFileCount)) +
			         " new this pass.");
			// Parse only the chunks at or after parsedIopFileCount. A runtime object pool update
			// (C.2.6) transfers only the added/replacement objects; add_or_replace_object merges
			// them into the existing tree (a re-sent ID overwrites, a new ID is added), so already
			// parsed objects -- and any runtime state on them -- survive rather than being rebuilt
			// from their authored bytes.
			for (std::size_t i = parsedIopFileCount; i < iopFilesRawData.size(); i++)
			{
				if (!parse_iop_into_objects(iopFilesRawData[i].data(), static_cast<std::uint32_t>(iopFilesRawData[i].size())))
				{
					lSuccess = false;
					break;
				}
			}

			if (lSuccess)
			{
				parsedIopFileCount = iopFilesRawData.size();
				LOG_INFO("[WS]: Object pool successfully parsed.");

				// The pool becomes visible to other threads here, once and as a whole -- every chunk of
				// it is parsed, so no reader can catch it part-built. Publication precedes the state
				// change, and must: a thread that observes Success is entitled to find the pool it
				// announces already published.
				publish_object_tree();
				set_object_pool_processing_state(ObjectPoolProcessingThreadState::Success);
			}
			else
			{
				LOG_ERROR("[WS]: Object pool failed to be parsed.");
				// A pool that did not parse is not valid to display, so nothing of it is published --
				// including whatever a failed run-time update (ISO 11783-6 C.2.6) had already merged into
				// the staging tree on top of the previous pool. The staging tree itself is kept, so an
				// initial upload that failed can still be completed by transferring the rest of it.
				clear_published_object_tree();
				set_object_pool_processing_state(ObjectPoolProcessingThreadState::Fail);
			}
		}
		else
		{
			LOG_ERROR("[WS]: Object pool failed to be parsed.");
			clear_published_object_tree();
			set_object_pool_processing_state(ObjectPoolProcessingThreadState::Fail);
		}
	}

	bool VirtualTerminalServerManagedWorkingSet::is_object_pool_transfer_in_progress() const
	{
		return iop_load_percentage() != 0.0f;
	}

} // namespace isobus
