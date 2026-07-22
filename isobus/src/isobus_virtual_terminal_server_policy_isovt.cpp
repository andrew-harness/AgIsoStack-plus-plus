//================================================================================================
/// @file isobus_virtual_terminal_server_policy_isovt.cpp
///
/// @brief Implements the fork's VT server policy: the active working set arbitration (ISO 11783-6
/// clause 4.6.14) and the operator selection (clause 4.6.8) that feeds it, the alarm activation
/// sequence stamping the arbitration ranks by, the VT Status change tracking and visible-mask
/// field refresh, the repaint dispatch, the macro execution queue drain, the F.46 mask-lock
/// timeout release and the clause 4.6.9 / C.2.6 working-set loss teardown passes run from
/// update(), the macro-suppression response gate, and the Pointing Event message.
///
/// This translation unit is isovt-owned and has no upstream counterpart. Per ADR-0008, the
/// fork's additive VT-server code lives here rather than interleaved in
/// isobus_virtual_terminal_server.cpp, to keep that upstream file's merge surface small. These
/// definitions remain members of VirtualTerminalServer, declared in the upstream header.
//================================================================================================

#include "isobus/isobus/isobus_virtual_terminal_server.hpp"
#include "isobus/isobus/can_general_parameter_group_numbers.hpp"
#include "isobus/isobus/can_network_manager.hpp"
#include "isobus/isobus/can_stack_logger.hpp"
#include "isobus/isobus/isobus_virtual_terminal_objects.hpp"
#include "isobus/utility/system_timing.hpp"

namespace isobus
{
	/// @brief Extracts the low byte from a 16-bit value.
	/// @param value The value to extract the low byte from.
	/// @returns The low byte of the value.
	static constexpr std::uint8_t get_low_byte(std::uint16_t value) noexcept
	{
		return static_cast<std::uint8_t>(value & 0xFFU);
	}

	/// @brief Extracts the high byte from a 16-bit value.
	/// @param value The value to extract the high byte from.
	/// @returns The high byte of the value.
	static constexpr std::uint8_t get_high_byte(std::uint16_t value) noexcept
	{
		return static_cast<std::uint8_t>((value >> 8) & 0xFFU);
	}

	/// @brief Creates a mask for a bit in an 8-bit value.
	/// @param bitIndex The zero-based bit index.
	/// @returns A mask with the requested bit set, or zero if the index is invalid.
	static constexpr std::uint8_t get_bit(std::uint8_t bitIndex) noexcept
	{
		return (bitIndex < 8U) ? static_cast<std::uint8_t>(1U << bitIndex) : static_cast<std::uint8_t>(0U);
	}

	void VirtualTerminalServer::set_operator_selected_working_set(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> workingSet)
	{
		operatorSelectedWorkingSet = workingSet;

		// ISO 11783-6 clause 4.6.8 gives the operator the means to select the active working set, so the
		// choice takes effect at once rather than at the next event that happens to arbitrate. Going
		// through the arbitration rather than assigning activeWorkingSet directly is what subordinates
		// the choice to clause 4.6.14: with an alarm raised the screen does not move, and the choice is
		// remembered for when that alarm clears.
		apply_active_working_set_arbitration();
	}

	bool VirtualTerminalServer::active_working_set_candidate_outranks(const ActiveWorkingSetCandidate &candidate,
	                                                                 const ActiveWorkingSetCandidate &incumbent)
	{
		// The tiers, best first. Only the alarm tier has an ordering of its own; every other tier is a
		// plain preference, so ties in them are refused and the caller's first candidate keeps the
		// display.
		enum : std::uint8_t
		{
			TIER_ALARM_RAISED = 0,
			TIER_OPERATOR_SELECTION = 1,
			TIER_LAST_DATA_MASK = 2,
			TIER_CURRENTLY_ACTIVE = 3,
			TIER_ELIGIBLE = 4
		};
		auto tier_of = [](const ActiveWorkingSetCandidate &entry) -> std::uint8_t {
			if (entry.hasAlarmRaised)
			{
				return TIER_ALARM_RAISED;
			}
			if (entry.isOperatorSelection)
			{
				return TIER_OPERATOR_SELECTION;
			}
			if (entry.isLastDataMaskHolder)
			{
				return TIER_LAST_DATA_MASK;
			}
			if (entry.isCurrentlyActive)
			{
				return TIER_CURRENTLY_ACTIVE;
			}
			return TIER_ELIGIBLE;
		};
		const std::uint8_t candidateTier = tier_of(candidate);
		const std::uint8_t incumbentTier = tier_of(incumbent);
		bool retVal = false;

		if (candidateTier != incumbentTier)
		{
			retVal = (candidateTier < incumbentTier);
		}
		else if (TIER_ALARM_RAISED == candidateTier)
		{
			// 4.6.14 ranks alarms first by the Alarm Mask's priority attribute and second by
			// chronological order of activation. The priority attribute encodes High as 0, Medium as 1
			// and Low as 2, so a NUMERICALLY LOWER value is a HIGHER priority and the winner is the
			// minimum. Equal priorities go to the lowest activation sequence, which is the alarm raised
			// first -- "the first of these, as processed by the VT, shall become the active mask".
			if (candidate.alarmPriority != incumbent.alarmPriority)
			{
				retVal = (candidate.alarmPriority < incumbent.alarmPriority);
			}
			else
			{
				retVal = (candidate.alarmActivationSequence < incumbent.alarmActivationSequence);
			}
		}
		return retVal;
	}

	void VirtualTerminalServer::drain_macro_execution_queue()
	{
		// Re-entrancy guard: a macro command that triggers another macro enqueues it and calls this
		// function again. The already-active drain will reach the new entry, so this call returns and
		// there is only ever one drain running -- which is what flattens all macro nesting to one level
		// and makes the FIFO order the trigger order (ISO 11783-6 4.6.11.4 b/c).
		if (macroQueueDraining)
		{
			return;
		}
		macroQueueDraining = true;

		// 4.6.11.4 f): withhold the responses of every macro-replayed command for the whole drain. One
		// 0/1 window brackets the drain rather than a per-macro depth, because the queue does not nest.
		macroExecutionDepth = 1;

		while (!macroExecutionQueue.empty())
		{
			const QueuedMacro queued = macroExecutionQueue.front();
			macroExecutionQueue.pop_front();
			execute_macro(queued.macroID, queued.workingSet);

			if (macroExecutionBudgetExhausted)
			{
				// The budget stopped a runaway (e.g. a self-referencing macro that re-enqueues itself).
				// Drop the rest so the drain terminates; execute_macro already logged the one refusal.
				macroExecutionQueue.clear();
				break;
			}
		}

		macroExecutionDepth = 0;
		macroQueueDraining = false;
	}

	std::vector<std::array<std::uint8_t, 32>> VirtualTerminalServer::get_extended_versions(NAME)
	{
		return {};
	}

	bool VirtualTerminalServer::send_response(const std::uint8_t *buffer, std::uint32_t length, std::shared_ptr<ControlFunction> destination) const
	{
		// 4.6.11.4 f): the VT sends no response on the bus for a command message contained in a macro.
		// The command itself still executes, and any VT Status it causes is still sent.
		if (0 != macroExecutionDepth)
		{
			return true;
		}
		return CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
		                                                      buffer,
		                                                      length,
		                                                      serverInternalControlFunction,
		                                                      destination,
		                                                      get_priority());
	}

	bool VirtualTerminalServer::send_pointing_event_message(std::uint16_t xPosition, std::uint16_t yPosition, std::uint8_t touchState, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::PointingEventMessage),
				get_low_byte(xPosition),
				get_high_byte(xPosition),
				get_low_byte(yPosition),
				get_high_byte(yPosition),
				touchState,
				0xFF, // Reserved
				0xFF // Reserved
			};

			// This goes onto the bus directly rather than through send_response, which is the choke
			// point that withholds the VT's response to a command contained in a macro (clause
			// 4.6.11.4 f). A Pointing Event is an operator input event the VT originates, not a
			// response to any command, so passing it through that choke point would silently drop
			// operator input that happened to coincide with macro execution. The Button Activation
			// and Soft Key Activation messages send directly for the same reason.
			retVal = CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
			                                                        buffer.data(),
			                                                        CAN_DATA_LENGTH,
			                                                        serverInternalControlFunction,
			                                                        destination,
			                                                        get_priority());
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_vt_esc_message(std::uint16_t abortedObjectId, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::VTESCMessage),
				get_low_byte(abortedObjectId),
				get_high_byte(abortedObjectId),
				errorBitfield,
				0xFF, // Reserved
				0xFF, // Reserved
				0xFF, // Reserved
				0xFF // Reserved
			};

			// Like the Pointing Event, this goes onto the bus directly rather than through send_response,
			// the choke point that withholds the VT's response to a command contained in a macro (clause
			// 4.6.11.4 f). The VT ESC message is an operator input event the VT originates -- sent when the
			// operator presses the ESC means, and when a Change Active Mask command closes an open input
			// field (ISO 11783-6 H.10) -- not a response to any command, so it must not pass through that
			// choke point.
			retVal = CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
			                                                        buffer.data(),
			                                                        CAN_DATA_LENGTH,
			                                                        serverInternalControlFunction,
			                                                        destination,
			                                                        get_priority());
		}
		return retVal;
	}

	void VirtualTerminalServer::mark_status_message_changed()
	{
		statusMessagePending = true;
	}

	std::uint16_t VirtualTerminalServer::get_visible_soft_key_mask(const std::shared_ptr<VirtualTerminalServerManagedWorkingSet> &workingSet, std::uint16_t maskObjectId) const
	{
		std::uint16_t retVal = NULL_OBJECT_ID;

		if (nullptr != workingSet)
		{
			auto maskObject = workingSet->get_object_by_id(maskObjectId);

			if (nullptr != maskObject)
			{
				if (VirtualTerminalObjectType::DataMask == maskObject->get_object_type())
				{
					retVal = std::static_pointer_cast<DataMask>(maskObject)->get_soft_key_mask();
				}
				else if (VirtualTerminalObjectType::AlarmMask == maskObject->get_object_type())
				{
					retVal = std::static_pointer_cast<AlarmMask>(maskObject)->get_soft_key_mask();
				}
			}
		}
		return retVal;
	}

	void VirtualTerminalServer::refresh_active_mask_status_fields()
	{
		if (nullptr == activeWorkingSet)
		{
			return;
		}

		auto workingSetObject = activeWorkingSet->get_working_set_object();

		if (nullptr == workingSetObject)
		{
			return;
		}

		const std::uint16_t maskObjectId = std::static_pointer_cast<WorkingSet>(workingSetObject)->get_active_mask();
		const std::uint16_t softKeyMaskObjectId = get_visible_soft_key_mask(activeWorkingSet, maskObjectId);

		if ((maskObjectId != activeWorkingSetDataMaskObjectID) ||
		    (softKeyMaskObjectId != activeWorkingSetSoftkeyMaskObjectID))
		{
			activeWorkingSetDataMaskObjectID = maskObjectId;
			activeWorkingSetSoftkeyMaskObjectID = softKeyMaskObjectId;
			mark_status_message_changed();
		}
	}

	bool VirtualTerminalServer::is_any_object_pool_parsing() const
	{
		bool retVal = false;

		for (const auto &ws : managedWorkingSetList)
		{
			const auto processingState = ws->get_object_pool_processing_state();

			// Running is the parse itself; Success and Fail are parsed-but-not-yet-answered, and the
			// standard holds the bit until the response is queued.
			if ((VirtualTerminalServerManagedWorkingSet::ObjectPoolProcessingThreadState::Running == processingState) ||
			    (VirtualTerminalServerManagedWorkingSet::ObjectPoolProcessingThreadState::Success == processingState) ||
			    (VirtualTerminalServerManagedWorkingSet::ObjectPoolProcessingThreadState::Fail == processingState))
			{
				retVal = true;
				break;
			}
		}
		return retVal;
	}

	std::shared_ptr<VTObject> VirtualTerminalServer::get_active_mask_object(const std::shared_ptr<VirtualTerminalServerManagedWorkingSet> &workingSet) const
	{
		std::shared_ptr<VTObject> retVal;

		// This reads working sets other than the one being served, and both lookups it makes come from a
		// single snapshot, so the mask it reports is the one the Working Set object it read actually
		// names.
		//
		// The Running and Fail states are refused as a matter of policy, not of safety. Reading either
		// would be safe -- the snapshot is immutable and, during a run-time pool update, holds precisely
		// the pool that is on screen. The refusals are deliberate answers instead: a pool mid-parse is
		// reported as showing nothing because callers use this to decide what the terminal presents, and
		// a pool whose parse failed is not valid to display at all, so until the client re-uploads or
		// the working set is torn down it must not take the screen.
		const auto processingState = (nullptr != workingSet) ?
		  workingSet->get_object_pool_processing_state() :
		  VirtualTerminalServerManagedWorkingSet::ObjectPoolProcessingThreadState::Running;

		if ((VirtualTerminalServerManagedWorkingSet::ObjectPoolProcessingThreadState::Running != processingState) &&
		    (VirtualTerminalServerManagedWorkingSet::ObjectPoolProcessingThreadState::Fail != processingState))
		{
			const auto objectTree = workingSet->get_object_tree();
			auto workingSetEntry = objectTree->find(workingSet->get_working_set_object_id());

			if ((objectTree->end() != workingSetEntry) && (nullptr != workingSetEntry->second))
			{
				auto maskEntry = objectTree->find(std::static_pointer_cast<WorkingSet>(workingSetEntry->second)->get_active_mask());

				if ((objectTree->end() != maskEntry) && (nullptr != maskEntry->second))
				{
					retVal = maskEntry->second;
				}
			}
		}
		return retVal;
	}

	bool VirtualTerminalServer::is_any_alarm_mask_active() const
	{
		bool retVal = false;

		for (const auto &ws : managedWorkingSetList)
		{
			auto maskObject = get_active_mask_object(ws);

			if ((nullptr != maskObject) &&
			    (VirtualTerminalObjectType::AlarmMask == maskObject->get_object_type()))
			{
				retVal = true;
				break;
			}
		}
		return retVal;
	}

	bool VirtualTerminalServer::is_object_open_for_input(const std::shared_ptr<VirtualTerminalServerManagedWorkingSet> &workingSet, std::uint16_t objectID) const
	{
		return (nullptr != workingSet) &&
		  (NULL_OBJECT_ID != objectID) &&
		  (objectID == workingSet->get_object_open_for_input());
	}

	void VirtualTerminalServer::stamp_alarm_activation_sequences()
	{
		// ISO 11783-6 4.6.14 breaks a priority tie by "chronological order of activation", so the
		// sequence is stamped when a working set's active mask TRANSITIONS INTO being an Alarm Mask,
		// which a sequence of zero identifies, and is cleared when the mask is no longer an Alarm Mask.
		// Re-stamping a working set that already has an alarm asserted would make it lose the tie-break
		// to every later alarm even though it raised its own first. A working set whose mask cannot be
		// resolved keeps whatever it had, because its tree is mid-parse and says nothing about its alarm.
		for (const auto &ws : managedWorkingSetList)
		{
			auto maskObject = get_active_mask_object(ws);

			if (nullptr == maskObject)
			{
				continue;
			}

			if (VirtualTerminalObjectType::AlarmMask == maskObject->get_object_type())
			{
				if (0 == ws->get_alarm_activation_sequence())
				{
					// Zero is the "no alarm asserted" sentinel, so the counter skips it on wrap rather
					// than handing out a value that reads as unstamped and loses every tie-break.
					++alarmActivationSequenceCounter;

					if (0 == alarmActivationSequenceCounter)
					{
						++alarmActivationSequenceCounter;
					}
					ws->set_alarm_activation_sequence(alarmActivationSequenceCounter, {});
				}
			}
			else
			{
				ws->set_alarm_activation_sequence(0, {});
			}
		}
	}

	std::shared_ptr<VirtualTerminalServerManagedWorkingSet> VirtualTerminalServer::select_active_working_set() const
	{
		std::shared_ptr<VirtualTerminalServerManagedWorkingSet> winner;
		ActiveWorkingSetCandidate winningCandidate;
		auto lastDataMask = lastDataMaskWorkingSet.lock();
		auto operatorSelection = operatorSelectedWorkingSet.lock();

		// A working set is eligible only if its active mask resolves, so a pool that is mid-parse, that
		// failed to parse, or that names no mask cannot take the display. Everything else about who wins
		// is in active_working_set_candidate_outranks; this loop only scores the state it reads.
		for (const auto &ws : managedWorkingSetList)
		{
			auto maskObject = get_active_mask_object(ws);

			if (nullptr == maskObject)
			{
				continue;
			}

			ActiveWorkingSetCandidate candidate;
			candidate.hasAlarmRaised = (VirtualTerminalObjectType::AlarmMask == maskObject->get_object_type());

			if (candidate.hasAlarmRaised)
			{
				candidate.alarmPriority = static_cast<std::uint8_t>(std::static_pointer_cast<AlarmMask>(maskObject)->get_mask_priority());
				candidate.alarmActivationSequence = ws->get_alarm_activation_sequence();
			}
			candidate.isOperatorSelection = (ws == operatorSelection);
			candidate.isLastDataMaskHolder = (ws == lastDataMask);
			candidate.isCurrentlyActive = (ws == activeWorkingSet);

			if ((nullptr == winner) || active_working_set_candidate_outranks(candidate, winningCandidate))
			{
				winner = ws;
				winningCandidate = candidate;
			}
		}
		return winner;
	}

	void VirtualTerminalServer::apply_active_working_set_arbitration()
	{
		// An incumbent is never deselected merely because its object tree is momentarily unreadable.
		// A runtime object pool update leaves the working set Running for the length of the parse, and
		// during that window it reports no active mask, so every eligibility test below would reject
		// it -- including the fallbacks that exist to keep it. Deselecting here would hand the screen
		// to another working set permanently, because the tail of this function then records that one
		// as the last Data Mask holder. The parse-completion path re-arbitrates, and a working set
		// left in Fail is torn down within the same update() call, so this holds for at most one pass.
		//
		// The outstanding-parse test keys on the parse thread handle, not the processing state, because
		// the state trails the handle at the start of a parse: start_parsing_thread() returns on the CAN
		// thread before the worker records Running, so between those two points the parse is outstanding
		// while the state still reads None. A Load Version empties the published tree before its reparse,
		// so an incumbent caught in that None window resolves no active mask and the state test alone
		// would let it be deselected. The handle is written only on the CAN thread and is set the instant
		// the worker is spawned, so it has no such gap and closes that window.
		if (nullptr != activeWorkingSet)
		{
			const auto activeState = activeWorkingSet->get_object_pool_processing_state();

			if (activeWorkingSet->is_object_pool_parse_outstanding() ||
			    (VirtualTerminalServerManagedWorkingSet::ObjectPoolProcessingThreadState::Running == activeState) ||
			    (VirtualTerminalServerManagedWorkingSet::ObjectPoolProcessingThreadState::Fail == activeState))
			{
				return;
			}
		}
		stamp_alarm_activation_sequences();

		auto selectedWorkingSet = select_active_working_set();

		if (selectedWorkingSet != activeWorkingSet)
		{
			// The working set losing the screen, captured before the reassignment below overwrites it,
			// so its open input can be closed the way a mask change closes one.
			auto displacedWorkingSet = activeWorkingSet;

			activeWorkingSet = selectedWorkingSet;
			activeWorkingSetMasterAddress = ((nullptr != selectedWorkingSet) && (nullptr != selectedWorkingSet->get_control_function())) ?
			  selectedWorkingSet->get_control_function()->get_address() :
			  isobus::NULL_CAN_ADDRESS;

			if (nullptr == selectedWorkingSet)
			{
				activeWorkingSetDataMaskObjectID = NULL_OBJECT_ID;
				activeWorkingSetSoftkeyMaskObjectID = NULL_OBJECT_ID;
			}
			mark_status_message_changed();

			if (nullptr != selectedWorkingSet)
			{
				dispatch_repaint(selectedWorkingSet);
			}

			// ISO 11783-6 Table 5 has no row for arbitration moving the screen to another working set,
			// so this closes the displaced working set's open input BY ANALOGY to Table 5's mask-change
			// row rather than by quoting it: clause 4.2 says the VT Status during data input names the
			// working set and mask "which contains the input object", a state the VT can no longer
			// honour once a different working set holds the screen. So the displaced working set's input
			// is closed exactly as a mask change (Annex H.10) would close it -- the VT ESC message (its
			// open object, error 0), then the VT Select Input Object deselect, then focus and open state
			// cleared -- sent to THAT working set's own control function. A merely-focused (not open)
			// object gets the deselect alone.
			//
			// The still-managed guard is load-bearing: this function also runs right after the loss
			// teardown pass has erased lost working sets (update()), and a working set erased there has
			// a gone control function, so nothing must be sent for it. Identity against
			// managedWorkingSetList is the test -- an erased working set is no longer in it. The
			// commanding working set of a Change Active Mask has already had its own open input cleared
			// before this function runs (the same handler, carry 0058, ahead of its arbitration call),
			// so a mask change never double-sends here for the working set that issued it; this only
			// closes the DIFFERENT working set the arbitration displaces.
			if (nullptr != displacedWorkingSet)
			{
				bool displacedStillManaged = false;

				for (const auto &ws : managedWorkingSetList)
				{
					if (ws == displacedWorkingSet)
					{
						displacedStillManaged = true;
						break;
					}
				}

				if (displacedStillManaged)
				{
					const std::uint16_t displacedOpenObject = displacedWorkingSet->get_object_open_for_input();

					if (NULL_OBJECT_ID != displacedOpenObject)
					{
						send_vt_esc_message(displacedOpenObject, 0, displacedWorkingSet->get_control_function());
						send_select_input_object_message(displacedOpenObject, false, false, displacedWorkingSet->get_control_function());
						displacedWorkingSet->set_object_open_for_input(NULL_OBJECT_ID);
						displacedWorkingSet->set_object_focus(NULL_OBJECT_ID);
					}
					else if (NULL_OBJECT_ID != displacedWorkingSet->get_object_focus())
					{
						send_select_input_object_message(displacedWorkingSet->get_object_focus(), false, false, displacedWorkingSet->get_control_function());
						displacedWorkingSet->set_object_focus(NULL_OBJECT_ID);
					}
				}
			}
		}
		refresh_active_mask_status_fields();

		// 4.6.14 c) sounds the acoustic signal when a mask change causes an Alarm Mask to appear or
		// reappear, so the event is raised on the transition onto the screen and not while the same
		// Alarm Mask of the same working set simply stays there. Both the working set and the mask ID
		// are compared: two working sets can carry Alarm Masks that share an object ID.
		auto displayedMask = get_active_mask_object(activeWorkingSet);
		const bool alarmIsDisplayed = (nullptr != displayedMask) &&
		  (VirtualTerminalObjectType::AlarmMask == displayedMask->get_object_type());

		if (alarmIsDisplayed)
		{
			const std::uint16_t alarmMaskObjectID = displayedMask->get_id();

			if ((displayedAlarmWorkingSet.lock() != activeWorkingSet) ||
			    (displayedAlarmMaskObjectID != alarmMaskObjectID))
			{
				displayedAlarmWorkingSet = activeWorkingSet;
				displayedAlarmMaskObjectID = alarmMaskObjectID;
				onAlarmMaskDisplayedEventDispatcher.call(activeWorkingSet, alarmMaskObjectID);
			}
		}
		else
		{
			displayedAlarmWorkingSet.reset();
			displayedAlarmMaskObjectID = NULL_OBJECT_ID;

			if (nullptr != displayedMask)
			{
				lastDataMaskWorkingSet = activeWorkingSet;
			}
		}
	}

	void VirtualTerminalServer::dispatch_repaint(const std::shared_ptr<VirtualTerminalServerManagedWorkingSet> &workingSet)
	{
		// ISO 11783-6 F.46: while a mask is locked, its on screen presentation is not updated for any
		// reason. Commands, key presses, events and macros are still processed, so only the refresh is
		// withheld here and the object model behind it goes on changing.
		if (NULL_OBJECT_ID != workingSet->get_mask_lock_object_id())
		{
			return;
		}
		onRepaintEventDispatcher.call(workingSet);
	}

	void VirtualTerminalServer::release_expired_mask_locks()
	{
		// ISO 11783-6 F.46: a Lock Mask command may carry a timeout, and once it expires the VT releases
		// the lock itself if the working set has not, so a client that stops talking cannot freeze the
		// operator's screen indefinitely. F.46 requires the release to be announced with an unsolicited
		// Lock/Unlock Mask Response, which is the client's only notice that its lock is gone.
		for (const auto &ws : managedWorkingSetList)
		{
			const std::uint16_t lockTimeout_ms = ws->get_mask_lock_timeout_ms();

			if ((NULL_OBJECT_ID != ws->get_mask_lock_object_id()) &&
			    (0 != lockTimeout_ms) &&
			    (isobus::SystemTiming::time_expired_ms(ws->get_mask_lock_timestamp_ms(), lockTimeout_ms)))
			{
				ws->set_mask_lock(NULL_OBJECT_ID, 0, 0, {});
				dispatch_repaint(ws);
				send_lock_unlock_mask_response(0, get_bit(static_cast<std::uint8_t>(LockUnlockMaskErrorBit::UnsolicitedUnlockTimeoutOccurred)), true, ws->get_control_function());
				LOG_WARNING("[VT Server]: Mask lock held by the working set at address %u timed out and was released.", (nullptr != ws->get_control_function()) ? ws->get_control_function()->get_address() : isobus::NULL_CAN_ADDRESS);
			}
		}
	}

	EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, VirtualTerminalServer::WorkingSetLossReason> &VirtualTerminalServer::get_on_working_set_lost_event_dispatcher()
	{
		return onWorkingSetLostEventDispatcher;
	}

	bool VirtualTerminalServer::tear_down_lost_working_sets()
	{
		// Tear down a working set on either of two triggers. Both delete its object pool, drop it as
		// the active working set, alert the operator, and require the client to re-initialise:
		//   1. ISO 11783-6 clause 4.6.9, unexpected loss of a working set: a connected working set
		//      sends a Working Set Maintenance message about once per second, and if those stop
		//      arriving the VT shall consider it lost after a 3 s timeout. A maintenance timestamp of 0
		//      means no maintenance message has been received yet (e.g. a pool still uploading), which
		//      must not be timed out.
		//   2. ISO 11783-6 clause C.2.6, invalid runtime object pool update: the parse-completion loop
		//      above flagged an already-active working set whose runtime pool update failed to parse
		//      (request_deletion()), and here the entire pool -- including the pre-update version -- is
		//      deleted and the working set suspended. request_deletion() is set in the Fail branch of
		//      that loop and consumed here within the same update() call, so this teardown is immediate.
		// This is a separate pass from the parse-completion loop above so that erasing a working set
		// cannot invalidate that loop's iterator. The operator-facing alert UI is deferred with the SDL
		// window (like the acoustic alarm in clause 4.4 b), so each reason is logged here and raised on
		// the backend once that exists; no audio/UI dependency is added.
		// Out of scope here: NACK-until-reinitialise (re-initialisation happens naturally once the
		// working set is gone). Auxiliary-assignment removal rides the delete_object_pool call below:
		// the derived server's override drops the lost working set's AUX-N assignments (the AUX-N
		// engine lives in the derived server, ADR-0007).
		bool workingSetWasTornDown = false;

		for (auto workingSetIterator = managedWorkingSetList.begin(); managedWorkingSetList.end() != workingSetIterator;)
		{
			const auto &ws = *workingSetIterator;
			const std::uint32_t maintenanceTimestamp = ws->get_working_set_maintenance_message_timestamp_ms();

			const bool maintenanceTimedOut = (0 != maintenanceTimestamp) &&
			  (isobus::SystemTiming::time_expired_ms(maintenanceTimestamp, 3000));
			const bool poolInvalidated = ws->is_deletion_requested();

			// Invariant: the CAN thread never waits on an object pool parse. Erasing a working set drops
			// what is normally the last reference to it, and its destructor joins the parse worker to
			// hold the invariant that a joinable thread is never destroyed -- so erasing one whose worker
			// is still outstanding would block this thread inside that join until the parse finished.
			// Address claiming, the maintenance messages every other working set is timed out against,
			// and the rest of the stack all stall behind it. So a teardown that lands mid-parse is
			// deferred to a later update() instead. The stall is tens of milliseconds for a pool of
			// ordinary size; it is the slower MCU target where it would become long enough to break the
			// timings the rest of the stack depends on.
			//
			// The deferral is bounded and cannot strand a working set. A parse terminates, and the
			// parse-completion loop above -- which runs before this one, on every update() -- joins the
			// worker as soon as it observes Success or Fail, clearing this condition. Neither trigger is
			// consumed by being deferred: request_deletion() is sticky, and the maintenance timestamp is
			// refreshed only by a Working Set Maintenance message, which a working set being timed out
			// under clause 4.6.9 is by definition not sending (and if it resumes, the working set is no
			// longer lost and must not be torn down at all). The pass that follows the parse therefore
			// tears it down.
			//
			// Both of those rest on the parse terminating. Nothing enforces that: parse_iop_into_objects
			// loops on the remaining length and trusts parse_next_object either to consume bytes or to
			// fail. A worker that did not terminate would leave this working set undeletable and would
			// block shutdown in the destructor rather than aborting there.
			const bool parseOutstanding = ws->is_object_pool_parse_outstanding();

			if ((maintenanceTimedOut || poolInvalidated) && (!parseOutstanding))
			{
				const bool workingSetHasControlFunction = (nullptr != ws->get_control_function());
				std::uint8_t lostAddress = isobus::NULL_CAN_ADDRESS;
				if (workingSetHasControlFunction)
				{
					lostAddress = ws->get_control_function()->get_address();
				}

				// A working set could satisfy both triggers; the C.2.6 invalid-update message takes
				// precedence over the 4.6.9 timeout message.
				if (poolInvalidated)
				{
					LOG_ERROR("[VT Server]: Working set at address %u had an invalid object pool update - deleting the entire pool from volatile memory and suspending the working set per ISO 11783-6 C.2.6.", lostAddress);
				}
				else
				{
					LOG_ERROR("[VT Server]: Working set at address %u lost - no Working Set Maintenance message for over 3 s. Deleting its object pool per ISO 11783-6 4.6.9.", lostAddress);
				}

				// Clause 4.6.9 requires the VT to alert the operator on an unexpected working-set shutdown,
				// and C.2.6 on an invalid runtime pool update; the means is proprietary to the VT. The alert
				// itself is a display-layer concern, so this raises the event with the working set (still
				// valid here, before the erase below) and the reason, and the derived server's listener
				// surfaces it. The logging above is the diagnostic record; this is the operator-facing seam.
				onWorkingSetLostEventDispatcher.call(ws, poolInvalidated ? WorkingSetLossReason::InvalidObjectPoolUpdate : WorkingSetLossReason::MaintenanceTimeout);

				if ((ws == activeWorkingSet) ||
				    (workingSetHasControlFunction && (lostAddress == activeWorkingSetMasterAddress)))
				{
					activeWorkingSet = nullptr;
					activeWorkingSetMasterAddress = isobus::NULL_CAN_ADDRESS;
					activeWorkingSetDataMaskObjectID = NULL_OBJECT_ID;
					activeWorkingSetSoftkeyMaskObjectID = NULL_OBJECT_ID;
					mark_status_message_changed();
				}

				if ((workingSetHasControlFunction) &&
				    (!delete_object_pool(ws->get_control_function()->get_NAME())))
				{
					LOG_WARNING("[VT Server]: Failed to delete the object pool for the lost working set at address %u.", lostAddress);
				}

				workingSetIterator = managedWorkingSetList.erase(workingSetIterator);
				workingSetWasTornDown = true;
			}
			else
			{
				++workingSetIterator;
			}
		}
		return workingSetWasTornDown;
	}
}
