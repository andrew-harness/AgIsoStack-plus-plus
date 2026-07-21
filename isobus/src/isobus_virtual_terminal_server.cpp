//================================================================================================
/// @file isobus_virtual_terminal_server.cpp
///
/// @brief Implements portions of an abstract VT server.
/// @author Adrian Del Grosso
///
/// @copyright 2023 Adrian Del Grosso
//================================================================================================
#include "isobus/isobus/isobus_virtual_terminal_server.hpp"
#include "isobus/isobus/can_general_parameter_group_numbers.hpp"
#include "isobus/isobus/can_message.hpp"
#include "isobus/isobus/can_network_manager.hpp"
#include "isobus/isobus/can_stack_logger.hpp"
#include "isobus/utility/system_timing.hpp"
#include "isobus/utility/to_string.hpp"

#include <iomanip>
#include <sstream>

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

	/// @brief Extracts a byte from a 32-bit value.
	/// @param value The value to extract the byte from.
	/// @param byteIndex The zero-based byte index.
	/// @returns The requested byte, or zero if the index is invalid.
	static constexpr std::uint8_t get_byte(std::uint32_t value, std::uint8_t byteIndex) noexcept
	{
		return (byteIndex < 4U) ? static_cast<std::uint8_t>((value >> (byteIndex * 8U)) & 0xFFU) : static_cast<std::uint8_t>(0U);
	}

	/// @brief Extracts the low 16 bits from a 32-bit value.
	/// @param value The value to extract the low bits from.
	/// @returns The low 16 bits of the value.
	static constexpr std::uint16_t get_low_16_bits(std::uint32_t value) noexcept
	{
		return static_cast<std::uint16_t>(value & 0xFFFFU);
	}

	/// @brief Reads a little-endian 16-bit value from a byte vector.
	/// @param data The vector containing the encoded value.
	/// @param index The index of the first byte. The vector must contain at least two bytes beginning at this index.
	/// @returns The decoded 16-bit value.
	static std::uint16_t get_little_endian_uint16(const std::vector<std::uint8_t> &data, std::size_t index)
	{
		const auto lowByte = static_cast<std::uint16_t>(data[index]);
		const auto highByte = static_cast<std::uint16_t>(data[index + 1]);

		return static_cast<std::uint16_t>(lowByte | static_cast<std::uint16_t>(highByte << 8));
	}

	/// @brief Reads a little-endian 32-bit value from a byte vector.
	/// @param data The vector containing the encoded value.
	/// @param index The index of the first byte. The vector must contain at least four bytes beginning at this index.
	/// @returns The decoded 32-bit value.
	static std::uint32_t get_little_endian_uint32(const std::vector<std::uint8_t> &data, std::size_t index)
	{
		return static_cast<std::uint32_t>(data[index]) |
		  (static_cast<std::uint32_t>(data[index + 1]) << 8) |
		  (static_cast<std::uint32_t>(data[index + 2]) << 16) |
		  (static_cast<std::uint32_t>(data[index + 3]) << 24);
	}

	VirtualTerminalServer::VirtualTerminalServer(std::shared_ptr<InternalControlFunction> controlFunctionToUse) :
	  languageCommandInterface(controlFunctionToUse, true),
	  serverInternalControlFunction(controlFunctionToUse)
	{
	}

	VirtualTerminalServer ::~VirtualTerminalServer()
	{
		if (initialized)
		{
			CANNetworkManager::CANNetwork.remove_any_control_function_parameter_group_number_callback(static_cast<std::uint32_t>(CANLibParameterGroupNumber::ECUtoVirtualTerminal),
			                                                                                          process_rx_message,
			                                                                                          this);
		}
	}

	void VirtualTerminalServer::initialize()
	{
		if (!initialized)
		{
			languageCommandInterface.initialize();
			CANNetworkManager::CANNetwork.add_any_control_function_parameter_group_number_callback(static_cast<std::uint32_t>(CANLibParameterGroupNumber::ECUtoVirtualTerminal),
			                                                                                       process_rx_message,
			                                                                                       this);
			initialized = true;
		}
	}

	bool VirtualTerminalServer::get_initialized() const
	{
		return initialized;
	}

	std::shared_ptr<InternalControlFunction> VirtualTerminalServer::get_internal_control_function() const
	{
		return serverInternalControlFunction;
	}

	std::shared_ptr<VirtualTerminalServerManagedWorkingSet> VirtualTerminalServer::get_active_working_set() const
	{
		return activeWorkingSet;
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

	VirtualTerminalBase::GraphicMode VirtualTerminalServer::get_graphic_mode() const
	{
		return VirtualTerminalBase::GraphicMode::TwoHundredFiftySixColour;
	}

	std::uint8_t VirtualTerminalServer::get_powerup_time() const
	{
		return 0xFF;
	}

	std::uint8_t VirtualTerminalServer::get_supported_small_fonts_bitfield() const
	{
		return 0x7F;
	}

	std::uint8_t VirtualTerminalServer::get_supported_large_fonts_bitfield() const
	{
		return 0x7F;
	}

	void VirtualTerminalServer::identify_vt()
	{
		LOG_ERROR("[VT Server]: The Identify VT command is not implemented");
	}

	void VirtualTerminalServer::screen_capture(std::uint8_t item, std::uint8_t path, std::shared_ptr<ControlFunction> requestor)
	{
		(void)item;
		(void)path;
		(void)requestor;
		LOG_ERROR("[VT Server]: The Screen Capture command is not implemented");
	}

	std::uint8_t VirtualTerminalServer::get_user_layout_softkeymask_bg_color() const
	{
		LOG_ERROR("[VT Server]: The Get User Layout Softkeymask background color is not implemented, returning with black");
		return 0;
	}

	void VirtualTerminalServer::transferred_object_pool_parse_start(std::shared_ptr<isobus::VirtualTerminalServerManagedWorkingSet> &ws) const
	{
		(void)ws;
	}

	std::uint8_t VirtualTerminalServer::get_user_layout_datamask_bg_color() const
	{
		LOG_ERROR("[VT Server]: The Get User Layout Datamask background color is not implemented, returning with black");
		return 0;
	}

	EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>> &VirtualTerminalServer::get_on_repaint_event_dispatcher()
	{
		return onRepaintEventDispatcher;
	}

	EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, std::uint16_t, std::uint16_t> &VirtualTerminalServer::get_on_change_active_mask_event_dispatcher()
	{
		return onChangeActiveMaskEventDispatcher;
	}

	EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, std::uint16_t, std::uint16_t> &VirtualTerminalServer::get_on_change_active_softkey_mask_event_dispatcher()
	{
		return onChangeActiveSoftKeyMaskEventDispatcher;
	}

	EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, std::uint16_t, bool> &VirtualTerminalServer::get_on_focus_object_event_dispatcher()
	{
		return onFocusObjectEventDispatcher;
	}

	EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, std::uint16_t> &VirtualTerminalServer::get_on_alarm_mask_displayed_event_dispatcher()
	{
		return onAlarmMaskDisplayedEventDispatcher;
	}

	LanguageCommandInterface &VirtualTerminalServer::get_language_command_interface()
	{
		return languageCommandInterface;
	}

	bool VirtualTerminalServer::check_if_source_is_managed(const CANMessage &message)
	{
		// Check if we're managing this CF
		bool retVal = false;

		// This is the static callback for the instance.
		// See if we need to set up a new managed working set.
		for (const auto &cf : managedWorkingSetList)
		{
			if (cf->get_control_function() == message.get_source_control_function())
			{
				// Found a match
				retVal = true;
				break;
			}
		}

		if (!retVal)
		{
			const auto &data = message.get_data();
			if ((data[0] == static_cast<std::uint8_t>(Function::WorkingSetMaintenanceMessage)) &&
			    (data[1] & 0x01)) // Init bit is set
			{
				// This CF is probably trying to initiate communication with us.
				managedWorkingSetList.emplace_back(std::make_shared<VirtualTerminalServerManagedWorkingSet>(message.get_source_control_function()));

				LOG_INFO("[VT Server]: Client %u initiated working set maintenance messages with version %u", managedWorkingSetList.back()->get_control_function()->get_address(), data[2]);
				if (data[2] > get_vt_version_byte(get_version()))
				{
					LOG_WARNING("[VT Server]: Client %u version %u is higher than our reported version, which is %u", managedWorkingSetList.back()->get_control_function()->get_address(), data[2], get_vt_version_byte(get_version()));
				}
				managedWorkingSetList.back()->set_working_set_maintenance_message_timestamp_ms(SystemTiming::get_timestamp_ms());
				managedWorkingSetList.back()->set_working_set_maintenance_version(data[2], {});
				retVal = true;
			}
		}
		return retVal;
	}

	void VirtualTerminalServer::execute_macro_as_rx_message(const CANMessage &message)
	{
		if ((message.get_destination_control_function() == serverInternalControlFunction) &&
		    (message.get_source_control_function() != nullptr) &&
		    (CAN_DATA_LENGTH == message.get_data_length()))
		{
			process_rx_message(message, this);
		}
	}

	bool VirtualTerminalServer::execute_macro(std::uint16_t objectIDOfMacro, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> workingSet)
	{
		auto object = workingSet->get_object_by_id(objectIDOfMacro);
		bool retVal = false;

		if ((nullptr != object) && (VirtualTerminalObjectType::Macro == object->get_object_type()))
		{
			auto macro = std::static_pointer_cast<Macro>(object);

			if (macro->get_are_command_packets_valid())
			{
				// 4.6.11.4 places no ceiling on macro execution, but under the queue a self-referencing
				// macro re-enqueues itself on every run, so the drain would loop forever without this
				// budget. It is the sole bound now that the queue has flattened all nesting to one level.
				// Spent once, it is latched and logged once, and the drain clears the rest of the queue.
				// Reset per bus command (see process_rx_message).
				if (macroExecutionsThisCommand >= MAX_MACRO_EXECUTIONS_PER_COMMAND)
				{
					if (!macroExecutionBudgetExhausted)
					{
						macroExecutionBudgetExhausted = true;
						LOG_ERROR("[VT Server]: Refusing to execute macro %u: macro execution budget of %u executions for one command is spent. Abandoning the remaining macros.", objectIDOfMacro, static_cast<unsigned int>(MAX_MACRO_EXECUTIONS_PER_COMMAND));
					}
					return retVal;
				}

				macroExecutionsThisCommand++;
				LOG_DEBUG("[VT Server]: Executing macro %u", macro->get_id());
				retVal = true;

				// 4.6.11.4 f): the commands a macro contains execute, but the VT sends no response on the
				// bus for any of them. The suppression window (macroExecutionDepth) is opened by the drain
				// for the whole run, not here -- a command that is itself Execute Macro enqueues rather
				// than re-entering, so there is no per-macro depth to track.
				for (std::uint8_t j = 0; j < macro->get_number_of_commands(); j++)
				{
					std::vector<std::uint8_t> commandPacket;

					if (macro->get_command_packet(j, commandPacket))
					{
						isobus::CANMessage message(isobus::CANMessage::Type::Receive,
						                           isobus::CANIdentifier(0x14E70000),
						                           commandPacket,
						                           workingSet->get_control_function(),
						                           get_internal_control_function(),
						                           workingSet->get_control_function()->get_can_port());
						LOG_DEBUG("[VT Server]: Executing macro command %u", j);
						execute_macro_as_rx_message(message);
					}
				}
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

	CANIdentifier::CANPriority VirtualTerminalServer::get_priority() const
	{
		return (VTVersion::Version6 == get_version()) ? CANIdentifier::CANPriority::Priority5 : CANIdentifier::CANPriority::PriorityLowest7;
	}

	std::vector<std::array<std::uint8_t, 32>> VirtualTerminalServer::get_extended_versions(NAME)
	{
		return {};
	}

	std::uint8_t VirtualTerminalServer::get_vt_version_byte(VTVersion version)
	{
		std::uint8_t retVal = 2;

		switch (version)
		{
			case VTVersion::Version3:
			{
				retVal = 3;
			}
			break;

			case VTVersion::Version4:
			{
				retVal = 4;
			}
			break;

			case VTVersion::Version5:
			{
				retVal = 5;
			}
			break;

			case VTVersion::Version6:
			{
				retVal = 6;
			}
			break;

			default:
			{
				// Report version 2
			}
			break;
		}
		return retVal;
	}

	bool VirtualTerminalServer::process_stateless_messages(const CANMessage &message)
	{
		bool retVal = false;
		const auto &data = message.get_data();
		switch (static_cast<Function>(data.at(0)))
		{
			case Function::GetMemoryMessage:
			{
				const auto requiredMemory = get_little_endian_uint32(data, 2);
				bool isEnoughMemory = get_is_enough_memory(requiredMemory);
				LOG_INFO("[VT Server]: An ecu requested %u bytes of memory.", requiredMemory);

				if (!isEnoughMemory)
				{
					LOG_WARNING("[VT Server]: Callback indicated there is NOT enough memory.", requiredMemory);
				}
				else
				{
					LOG_DEBUG("[VT Server]: Callback indicated there may be enough memory, but since there is overhead associated to object storage it is impossible to be sure.", requiredMemory);
				}

				std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };
				buffer[0] = static_cast<std::uint8_t>(Function::GetMemoryMessage);
				buffer[1] = get_vt_version_byte(get_version());
				buffer[2] = static_cast<std::uint8_t>(!isEnoughMemory);
				buffer[3] = 0xFF; // Reserved
				buffer[4] = 0xFF; // Reserved
				buffer[5] = 0xFF; // Reserved
				buffer[6] = 0xFF; // Reserved
				buffer[7] = 0xFF; // Reserved
				CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
				                                               buffer.data(),
				                                               CAN_DATA_LENGTH,
				                                               serverInternalControlFunction,
				                                               message.get_source_control_function(),
				                                               get_priority());
				retVal = true;
			}
			break;

			case Function::GetNumberOfSoftKeysMessage:
			{
				std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };
				buffer[0] = static_cast<std::uint8_t>(Function::GetNumberOfSoftKeysMessage);
				buffer[1] = get_number_of_navigation_soft_keys(); // No navigation softkeys
				buffer[2] = 0xFF; // Reserved
				buffer[3] = 0xFF; // Reserved
				buffer[4] = get_soft_key_descriptor_x_pixel_width(); // Width of the softkey descriptor in pixels
				buffer[5] = get_soft_key_descriptor_y_pixel_height(); // Height of the softkey descriptor in pixels
				buffer[6] = get_number_of_possible_virtual_soft_keys_in_soft_key_mask(); // Number of possible virtual Soft Keys in a Soft Key Mask
				buffer[7] = get_number_of_physical_soft_keys(); // No physical softkeys

				CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
				                                               buffer.data(),
				                                               CAN_DATA_LENGTH,
				                                               serverInternalControlFunction,
				                                               message.get_source_control_function(),
				                                               get_priority());
				retVal = true;
			}
			break;

			case Function::GetTextFontDataMessage:
			{
				std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };
				buffer[0] = static_cast<std::uint8_t>(Function::GetTextFontDataMessage);
				buffer[1] = 0xFF; // Reserved
				buffer[2] = 0xFF; // Reserved
				buffer[3] = 0xFF; // Reserved
				buffer[4] = 0xFF; // Reserved
				buffer[5] = get_supported_small_fonts_bitfield(); // Say we support all small fonts
				buffer[6] = get_supported_large_fonts_bitfield(); // Say we support all large fonts
				buffer[7] = 0x8F; // Support normal, bold, italic, proportional
				CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
				                                               buffer.data(),
				                                               CAN_DATA_LENGTH,
				                                               serverInternalControlFunction,
				                                               message.get_source_control_function(),
				                                               get_priority());
				retVal = true;
			}
			break;

			case Function::GetHardwareMessage:
			{
				std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };
				buffer[0] = static_cast<std::uint8_t>(Function::GetHardwareMessage);
				buffer[1] = get_powerup_time();
				buffer[2] = static_cast<std::uint8_t>(get_graphic_mode()); // 256 Colour Mode by default
				buffer[3] = 0x0F; // Support pointing event message
				buffer[4] = get_low_byte(get_data_mask_area_size_x_pixels()); // X Pixels LSB
				buffer[5] = get_high_byte(get_data_mask_area_size_x_pixels()); // X Pixels MSB
				buffer[6] = get_low_byte(get_data_mask_area_size_y_pixels()); // Y Pixels LSB
				buffer[7] = get_high_byte(get_data_mask_area_size_y_pixels()); // Y Pixels MSB
				CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
				                                               buffer.data(),
				                                               CAN_DATA_LENGTH,
				                                               serverInternalControlFunction,
				                                               message.get_source_control_function(),
				                                               get_priority());
				retVal = true;
			}
			break;

			case Function::GetSupportedWidecharsMessage:
			{
				std::vector<std::uint8_t> wideCharRangeArray;
				std::uint8_t numberOfRanges = 0;
				std::uint8_t codePlane = data.at(1);
				std::uint16_t firstWideCharInInquiryRange = get_little_endian_uint16(data, 2);
				std::uint16_t lastWideCharInInquiryRange = get_little_endian_uint16(data, 4);
				auto errorCode = get_supported_wide_chars(codePlane, firstWideCharInInquiryRange, lastWideCharInInquiryRange, numberOfRanges, wideCharRangeArray);

				std::vector<std::uint8_t> buffer;
				buffer.push_back(static_cast<std::uint8_t>(Function::GetSupportedWidecharsMessage));
				buffer.push_back(codePlane);
				buffer.push_back(get_low_byte(firstWideCharInInquiryRange));
				buffer.push_back(get_high_byte(firstWideCharInInquiryRange));
				buffer.push_back(get_low_byte(lastWideCharInInquiryRange));
				buffer.push_back(get_high_byte(lastWideCharInInquiryRange));
				buffer.push_back(static_cast<std::uint8_t>(errorCode));
				buffer.push_back(numberOfRanges);

				for (const auto &range : wideCharRangeArray)
				{
					buffer.push_back(range);
				}
				CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
				                                               buffer.data(),
				                                               static_cast<std::uint32_t>(buffer.size()),
				                                               serverInternalControlFunction,
				                                               message.get_source_control_function(),
				                                               get_priority());
				retVal = true;
			}
			break;

			case Function::GetWindowMaskDataMessage:
			{
				send_get_window_mask_data_response(message.get_source_control_function());
				retVal = true;
			}
			break;

			case Function::AuxiliaryCapabilitiesRequest:
			{
				if (data.size() >= 2)
				{
					const std::uint8_t requestType = data.at(1);
					LOG_DEBUG("[VT Server]: Client at address %u requested Auxiliary Capabilities (request type %u).", message.get_identifier().get_source_address(), requestType);
					send_auxiliary_capabilities_response(requestType, message.get_source_control_function());
					retVal = true;
				}
			}
			break;

			case Function::GetSupportedObjectsMessage:
			{
				// D.14/D.15 is a Get Technical Data query about the VT itself, like Get Hardware and
				// Get Memory beside it, and its answer does not depend on any object pool. Answering
				// it here rather than from the connection-dependent path lets a control function ask
				// before it has uploaded anything -- which is when a working set actually wants to
				// know, since the answer decides what it puts in the pool.
				LOG_DEBUG("[VT Server]: Client at address %u requested the supported object list.", message.get_identifier().get_source_address());
				send_supported_objects(message.get_source_control_function());
				retVal = true;
			}
			break;

			default:
				break;
		}
		return retVal;
	}

	bool VirtualTerminalServer::process_connection_dependent_messages(const CANMessage &message, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
	{
		bool functionSupported = true;
		const auto &data = message.get_data();
		switch (static_cast<Function>(data.at(0)))
		{
			case Function::GetMemoryMessage:
			{
				// The response is handled in process_stateless_messages
				// but save the size requested for later use if we have a connected working set
				const auto requiredMemory = get_little_endian_uint32(data, 2);
				managedWorkingSet->set_iop_size(requiredMemory);
			}
			break;

			case Function::ObjectPoolTransferMessage:
			{
				if (managedWorkingSet->is_object_pool_parse_outstanding())
				{
					// The transferred data is dropped, because it cannot be stored and there is nothing
					// else to answer with. It cannot be stored because the parse worker iterates the raw
					// chunk buffer for the whole length of a parse, so appending to it can reallocate the
					// buffer out from under a live element reference. And there is nothing else to answer
					// with because ISO 11783-6 C.2.3 defines no response to this message at all ("Since
					// there is no response to this message, it is recommended to not send single objects
					// that fit within a single packet").
					//
					// Dropping is the right answer rather than a lesser evil. A working set that reaches
					// here is ignoring both of the mechanisms the standard gave it: C.2.2 g) requires it
					// to wait for the response to its previous Annex C command before sending another,
					// and C.2.5 gives it the VT Status busy-parsing bit to wait on while the VT parses.
					//
					// Nothing later in the transfer would reveal the gap on its own. The worker parses
					// each chunk from its own offset, so one dropped chunk does not desynchronise the
					// others: the chunks that did arrive parse cleanly, the End of Object Pool that ends
					// the transfer reports success, and the objects the dropped chunk carried are simply
					// absent from the pool the client is then told is loaded. Recording the drop is what
					// turns that silent success into the error response End of Object Pool sends below.
					managedWorkingSet->set_object_pool_transfer_data_dropped();
					LOG_WARNING("[VT Server]: Client at address %u transferred object pool data while its previous pool is still being parsed. Dropping the data; this client should be waiting on the VT Status busy-parsing bit. Its End of Object Pool will now be refused.", message.get_identifier().get_source_address());
					break;
				}

				std::vector<std::uint8_t> tempPool = data; // Make a copy of the data (ouch)
				tempPool.erase(tempPool.begin()); // Strip off the mux byte (double ouch, good thing this is rare)
				LOG_INFO("[VT Server]: An ecu at address %u transferred %u bytes of object pool data to us.", message.get_identifier().get_source_address(), static_cast<std::uint32_t>(tempPool.size()));
				managedWorkingSet->add_iop_raw_data(tempPool);
			}
			break;

			case Function::GetVersionsMessage:
			{
				auto versions = get_versions(message.get_source_control_function()->get_NAME());

				std::vector<std::uint8_t> buffer;
				buffer.push_back(static_cast<std::uint8_t>(Function::GetVersionsResponse));

				LOG_DEBUG("[VT Server]: Client %u requests stored versions", message.get_source_control_function()->get_address());

				if (versions.size() > 255)
				{
					LOG_WARNING("[VT Server]: get_versions returned too many versions! This client should really delete some.");
				}

				buffer.push_back(static_cast<std::uint8_t>(versions.size() & 0xFF));

				for (const auto &version : versions)
				{
					for (const auto &versionByte : version)
					{
						buffer.push_back(versionByte);
					}
				}

				while (buffer.size() < CAN_DATA_LENGTH)
				{
					buffer.push_back(0xFF);
				}
				CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
				                                               buffer.data(),
				                                               static_cast<std::uint32_t>(buffer.size()),
				                                               serverInternalControlFunction,
				                                               message.get_source_control_function(),
				                                               get_priority());
			}
			break;

			case Function::ExtendedGetVersionsMessage:
			{
				auto versions = get_extended_versions(message.get_source_control_function()->get_NAME());

				std::vector<std::uint8_t> buffer;
				buffer.push_back(static_cast<std::uint8_t>(Function::ExtendedGetVersionsMessage));

				LOG_DEBUG("[VT Server]: Client %u requests stored extended versions", message.get_source_control_function()->get_address());

				if (versions.size() > 255)
				{
					LOG_WARNING("[VT Server]: get_extended_versions returned too many versions! This client should really delete some.");
				}

				buffer.push_back(static_cast<std::uint8_t>(versions.size() & 0xFF));

				for (const auto &version : versions)
				{
					for (const auto &versionByte : version)
					{
						buffer.push_back(versionByte);
					}
				}

				while (buffer.size() < CAN_DATA_LENGTH)
				{
					buffer.push_back(0xFF);
				}
				CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
				                                               buffer.data(),
				                                               static_cast<std::uint32_t>(buffer.size()),
				                                               serverInternalControlFunction,
				                                               message.get_source_control_function(),
				                                               get_priority());
			}
			break;

			case Function::ExtendedStoreVersionCommand:
			{
				if (data.size() < static_cast<std::size_t>(EXTENDED_VERSION_LABEL_LENGTH) + 1u)
				{
					LOG_WARNING("[VT Server]: Received a malformed Extended Store Version command (too short to contain a 32 byte label)");
					break;
				}

				if (managedWorkingSet->get_any_object_pools())
				{
					std::ostringstream nameString;
					nameString << std::hex << std::setfill('0') << std::setw(16) << managedWorkingSet->get_control_function()->get_NAME().get_full_name();
					std::vector<std::uint8_t> versionLabel;
					bool allPoolsSaved = true;
					versionLabel.reserve(EXTENDED_VERSION_LABEL_LENGTH);

					for (std::uint_fast8_t i = 0; i < EXTENDED_VERSION_LABEL_LENGTH; i++)
					{
						versionLabel.push_back(data[i + 1]);
					}

					// The object pool arrives as one or more object-aligned components; concatenate them
					// into a single stream stored under one key. save_version names the file by the version
					// label, so saving each component separately would overwrite all but the last, and a
					// multi-component pool would reload incomplete -- objects referencing the dropped
					// components then dangle when the loaded pool is activated.
					std::vector<std::uint8_t> combinedPool;
					for (std::size_t i = 0; i < managedWorkingSet->get_number_iop_files(); i++)
					{
						const std::vector<std::uint8_t> &component = managedWorkingSet->get_iop_raw_data(i);
						combinedPool.insert(combinedPool.end(), component.begin(), component.end());
					}

					allPoolsSaved = save_version(combinedPool, versionLabel, message.get_source_control_function()->get_NAME());
					if (allPoolsSaved)
					{
						LOG_INFO("[VT Server]: Object pool for NAME " + nameString.str() + " was stored.");
					}
					else
					{
						LOG_ERROR("[VT Server]: Object pool for NAME " + nameString.str() + " could not be stored.");
					}

					std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };
					buffer[0] = static_cast<std::uint8_t>(Function::ExtendedStoreVersionCommand);
					buffer[1] = 0xFF; // Reserved
					buffer[2] = 0xFF; // Reserved
					buffer[3] = 0xFF; // Reserved
					buffer[4] = 0xFF; // Reserved
					if (allPoolsSaved)
					{
						buffer[5] = 0; // No error
					}
					else
					{
						// E.13 byte 6 bit 3, "Any other error": the generic error for a save_version write
						// failure, which is none of bit 1 (bad version label) or bit 2 (insufficient memory);
						// bit 0 is reserved.
						buffer[5] = get_bit(static_cast<std::uint8_t>(StoreVersionErrorBit::AnyOtherError));
					}
					buffer[6] = 0xFF; // Reserved
					buffer[7] = 0xFF; // Reserved
					CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
					                                               buffer.data(),
					                                               CAN_DATA_LENGTH,
					                                               serverInternalControlFunction,
					                                               message.get_source_control_function(),
					                                               get_priority());
				}
				else
				{
					// Whomever this is appears to be behaving badly, send them a NACK
					send_acknowledgement(AcknowledgementType::Negative, static_cast<std::uint32_t>(CANLibParameterGroupNumber::ECUtoVirtualTerminal), serverInternalControlFunction, managedWorkingSet->get_control_function());
				}
			}
			break;

			case Function::ExtendedDeleteVersionCommand:
			{
				if (data.size() < static_cast<std::size_t>(EXTENDED_VERSION_LABEL_LENGTH) + 1u)
				{
					LOG_WARNING("[VT Server]: Received a malformed Extended Delete Version command (too short to contain a 32 byte label)");
					break;
				}

				std::vector<std::uint8_t> versionLabel;
				std::ostringstream nameString;
				nameString << std::hex << std::setfill('0') << std::setw(16) << managedWorkingSet->get_control_function()->get_NAME().get_full_name();
				versionLabel.reserve(EXTENDED_VERSION_LABEL_LENGTH);

				for (std::uint_fast8_t i = 0; i < EXTENDED_VERSION_LABEL_LENGTH; i++)
				{
					versionLabel.push_back(data[i + 1]);
				}

				bool wasDeleted = delete_version(versionLabel, managedWorkingSet->get_control_function()->get_NAME());

				if (wasDeleted)
				{
					LOG_INFO("[VT Server]: Deleted an extended object pool version for client NAME %s", nameString.str().c_str());
				}
				else
				{
					LOG_WARNING("[VT Server]: Extended delete version failed for client NAME %s", nameString.str().c_str());
				}

				const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
					static_cast<std::uint8_t>(Function::ExtendedDeleteVersionCommand),
					0xFF, // Reserved
					0xFF, // Reserved
					0xFF, // Reserved
					0xFF, // Reserved
					static_cast<std::uint8_t>(wasDeleted ? 0 : get_bit(static_cast<std::uint8_t>(DeleteVersionErrorBit::VersionLabelNotCorrectOrUnknown))),
					0xFF, // Reserved
					0xFF // Reserved
				};
				CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
				                                               buffer.data(),
				                                               CAN_DATA_LENGTH,
				                                               serverInternalControlFunction,
				                                               message.get_source_control_function(),
				                                               get_priority());
			}
			break;

			case Function::ExtendedLoadVersionCommand:
			{
				if (data.size() < static_cast<std::size_t>(EXTENDED_VERSION_LABEL_LENGTH) + 1u)
				{
					LOG_WARNING("[VT Server]: Received a malformed Extended Load Version command (too short to contain a 32 byte label)");
					break;
				}

				if (managedWorkingSet->is_object_pool_parse_outstanding())
				{
					// Loading a version appends the stored pool to the raw chunk buffer the parse worker
					// is iterating, which can reallocate it under a live element reference, and it would
					// then have to start a parse that cannot be started while one is outstanding. So the
					// command is refused. E.15 byte 6 bit 3, "Any other error", is the bit for it: the VT
					// is busy, which is none of the file system, version label or memory faults the other
					// three bits name.
					//
					// A conformant client cannot see this. E.14 requires the working set master to wait
					// for the Extended Load Version response, and until then to watch the VT Status
					// busy-parsing bit, before assuming its command was lost.
					send_extended_load_version_response(get_bit(static_cast<std::uint8_t>(LoadVersionErrorBit::AnyOtherError)), managedWorkingSet->get_control_function());
					LOG_WARNING("[VT Server]: Client at address %u sent an Extended Load Version command while its object pool is still being parsed. Refusing it.", message.get_identifier().get_source_address());
					break;
				}

				std::vector<std::uint8_t> versionLabel;

				versionLabel.reserve(EXTENDED_VERSION_LABEL_LENGTH);

				for (std::uint_fast8_t i = 0; i < EXTENDED_VERSION_LABEL_LENGTH; i++)
				{
					versionLabel.push_back(data[i + 1]);
				}

				auto loadedVersion = load_version(versionLabel, message.get_source_control_function()->get_NAME());
				if (!loadedVersion.empty())
				{
					// E.6: "If an object pool is already loaded it is overwritten." Returning the working
					// set to its pre-upload state before loading makes the stored version parse into a
					// clean tree that replaces the current pool, rather than the parse worker merging it
					// into the live tree (merging is correct only for a runtime pool update, C.2.6). The
					// reset also discards the state derived from the pool being replaced -- the mask lock,
					// the Colour Map selection, the alarm activation sequence, open-for-input and focus --
					// which the new pool's parse repopulates. Nothing between here and that parse reads
					// them. The reset fails only while a parse is outstanding, and the guard at the top of
					// this case already returned in that state, so it cannot fail here; the false branch
					// refuses the load rather than let add_iop_raw_data race a live parse worker.
					if (managedWorkingSet->reset_object_pool())
					{
						managedWorkingSet->set_iop_size(static_cast<std::uint32_t>(loadedVersion.size()));
						managedWorkingSet->add_iop_raw_data(loadedVersion);

						// A parse belongs to a load that produced something to parse. Started outside this
						// branch it would also run when no version was found -- on a working set that still
						// holds an earlier pool, a parse of zero new chunks that succeeds and sends a second,
						// contradicting response saying the version loaded.
						//
						// The two flags select the message that completes this parse, so they describe a
						// parse this call actually started and nothing else. Set after a start that did not
						// happen, they would redirect the completion of whatever parse is already
						// outstanding, answering a client waiting on an End of Object Pool response with an
						// Extended Load Version response instead.
						if (managedWorkingSet->start_parsing_thread())
						{
							managedWorkingSet->set_was_object_pool_loaded_from_non_volatile_memory(true, {});
							managedWorkingSet->set_loaded_via_extended_version_command(true, {});
							LOG_DEBUG("[VT Server]: Starting parsing thread for loaded extended pool data.");
						}
					}
					else
					{
						send_extended_load_version_response(get_bit(static_cast<std::uint8_t>(LoadVersionErrorBit::AnyOtherError)), managedWorkingSet->get_control_function());
						LOG_ERROR("[VT Server]: Could not reset the object pool before an Extended Load Version; a parse is unexpectedly outstanding. Refusing the load.");
					}
				}
				else
				{
					const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
						static_cast<std::uint8_t>(Function::ExtendedLoadVersionCommand),
						0xFF, // Reserved
						0xFF, // Reserved
						0xFF, // Reserved
						0xFF, // Reserved
						get_bit(static_cast<std::uint8_t>(LoadVersionErrorBit::VersionLabelNotCorrectOrUnknown)), // E.15 byte 6 bit 1: requested version not in non-volatile storage
						0xFF, // Reserved
						0xFF // Reserved
					};
					CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
					                                               buffer.data(),
					                                               CAN_DATA_LENGTH,
					                                               serverInternalControlFunction,
					                                               message.get_source_control_function(),
					                                               get_priority());
					LOG_ERROR("[VT Server]: Failed to load requested extended object pool version");
				}
			}
			break;

			case Function::LoadVersionCommand:
			{
				if (managedWorkingSet->is_object_pool_parse_outstanding())
				{
					// Same reasoning as the Extended Load Version command above, against E.6 and E.7:
					// the raw chunk buffer belongs to the parse worker while it runs, and a second parse
					// cannot be started while one is outstanding. E.7 byte 6 bit 3 is "Any other error".
					send_load_version_response(get_bit(static_cast<std::uint8_t>(LoadVersionErrorBit::AnyOtherError)), managedWorkingSet->get_control_function());
					LOG_WARNING("[VT Server]: Client at address %u sent a Load Version command while its object pool is still being parsed. Refusing it.", message.get_identifier().get_source_address());
					break;
				}

				std::vector<std::uint8_t> versionLabel;

				versionLabel.reserve(VERSION_LABEL_LENGTH);

				for (std::uint_fast8_t i = 0; i < VERSION_LABEL_LENGTH; i++)
				{
					versionLabel.push_back(data[i + 1]);
				}

				auto loadedVersion = load_version(versionLabel, message.get_source_control_function()->get_NAME());
				if (!loadedVersion.empty())
				{
					// E.6: "If an object pool is already loaded it is overwritten." Returning the working
					// set to its pre-upload state before loading makes the stored version parse into a
					// clean tree that replaces the current pool, rather than the parse worker merging it
					// into the live tree (merging is correct only for a runtime pool update, C.2.6). The
					// reset also discards the state derived from the pool being replaced -- the mask lock,
					// the Colour Map selection, the alarm activation sequence, open-for-input and focus --
					// which the new pool's parse repopulates. Nothing between here and that parse reads
					// them. The reset fails only while a parse is outstanding, and the guard at the top of
					// this case already returned in that state, so it cannot fail here; the false branch
					// refuses the load rather than let add_iop_raw_data race a live parse worker.
					if (managedWorkingSet->reset_object_pool())
					{
						managedWorkingSet->set_iop_size(static_cast<std::uint32_t>(loadedVersion.size()));
						managedWorkingSet->add_iop_raw_data(loadedVersion);

						// The flag is set only when a worker actually started. See the extended path above
						// for what that prevents.
						if (managedWorkingSet->start_parsing_thread())
						{
							managedWorkingSet->set_was_object_pool_loaded_from_non_volatile_memory(true, {});
							LOG_DEBUG("[VT Server]: Starting parsing thread for loaded pool data.");
						}
					}
					else
					{
						send_load_version_response(get_bit(static_cast<std::uint8_t>(LoadVersionErrorBit::AnyOtherError)), managedWorkingSet->get_control_function());
						LOG_ERROR("[VT Server]: Could not reset the object pool before a Load Version; a parse is unexpectedly outstanding. Refusing the load.");
					}
				}
				else
				{
					// E.7 byte 6 bit 1, "Version label is not correct or Version label unknown": the
					// requested version is not in non-volatile storage. Bit 0 is the file system / pool
					// corruption bit, which exists only in VT version 4 and later, so it is wrong here.
					send_load_version_response(get_bit(static_cast<std::uint8_t>(LoadVersionErrorBit::VersionLabelNotCorrectOrUnknown)), managedWorkingSet->get_control_function());
					LOG_ERROR("[VT Server]: Failed to load requested object pool version");
				}
			}
			break;

			case Function::StoreVersionCommand:
			{
				if (managedWorkingSet->get_any_object_pools())
				{
					std::ostringstream nameString;
					nameString << std::hex << std::setfill('0') << std::setw(16) << managedWorkingSet->get_control_function()->get_NAME().get_full_name();
					std::vector<std::uint8_t> versionLabel;
					bool allPoolsSaved = true;
					versionLabel.reserve(VERSION_LABEL_LENGTH);

					for (std::uint_fast8_t i = 0; i < VERSION_LABEL_LENGTH; i++)
					{
						versionLabel.push_back(data[i + 1]);
					}

					// The object pool arrives as one or more object-aligned components; concatenate them
					// into a single stream stored under one key. save_version names the file by the version
					// label, so saving each component separately would overwrite all but the last, and a
					// multi-component pool would reload incomplete -- objects referencing the dropped
					// components then dangle when the loaded pool is activated.
					std::vector<std::uint8_t> combinedPool;
					for (std::size_t i = 0; i < managedWorkingSet->get_number_iop_files(); i++)
					{
						const std::vector<std::uint8_t> &component = managedWorkingSet->get_iop_raw_data(i);
						combinedPool.insert(combinedPool.end(), component.begin(), component.end());
					}

					allPoolsSaved = save_version(combinedPool, versionLabel, message.get_source_control_function()->get_NAME());
					if (allPoolsSaved)
					{
						LOG_INFO("[VT Server]: Object pool for NAME " + nameString.str() + " was stored.");
					}
					else
					{
						LOG_ERROR("[VT Server]: Object pool for NAME " + nameString.str() + " could not be stored.");
					}

					std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };
					buffer[0] = static_cast<std::uint8_t>(Function::StoreVersionCommand);
					buffer[1] = 0xFF; // Reserved
					buffer[2] = 0xFF; // Reserved
					buffer[3] = 0xFF; // Reserved
					buffer[4] = 0xFF; // Reserved
					if (allPoolsSaved)
					{
						buffer[5] = 0; // No error
					}
					else
					{
						// E.5 byte 6 bit 3, "Any other error": the generic error for a save_version write
						// failure, which is none of bit 1 (bad version label) or bit 2 (insufficient memory);
						// bit 0 is reserved.
						buffer[5] = get_bit(static_cast<std::uint8_t>(StoreVersionErrorBit::AnyOtherError));
					}
					buffer[6] = 0xFF; // Reserved
					buffer[7] = 0xFF; // Reserved
					CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
					                                               buffer.data(),
					                                               CAN_DATA_LENGTH,
					                                               serverInternalControlFunction,
					                                               message.get_source_control_function(),
					                                               get_priority());
				}
				else
				{
					// Whomever this is appears to be behaving badly, send them a NACK
					send_acknowledgement(AcknowledgementType::Negative, static_cast<std::uint32_t>(CANLibParameterGroupNumber::ECUtoVirtualTerminal), serverInternalControlFunction, managedWorkingSet->get_control_function());
				}
			}
			break;

			case Function::DeleteVersionCommand:
			{
				std::vector<std::uint8_t> versionLabel;
				std::ostringstream nameString;
				nameString << std::hex << std::setfill('0') << std::setw(16) << managedWorkingSet->get_control_function()->get_NAME().get_full_name();
				versionLabel.reserve(VERSION_LABEL_LENGTH);

				for (std::uint_fast8_t i = 0; i < VERSION_LABEL_LENGTH; i++)
				{
					versionLabel.push_back(data[i + 1]);
				}

				bool wasDeleted = delete_version(versionLabel, managedWorkingSet->get_control_function()->get_NAME());

				if (wasDeleted)
				{
					LOG_INFO("[VT Server]: Deleted an object pool version for client NAME %s", nameString.str().c_str());
					send_delete_version_response(0, managedWorkingSet->get_control_function());
				}
				else
				{
					LOG_WARNING("[VT Server]: Delete version failed for client NAME %s", nameString.str().c_str());
					send_delete_version_response(get_bit(static_cast<std::uint8_t>(DeleteVersionErrorBit::VersionLabelNotCorrectOrUnknown)), managedWorkingSet->get_control_function());
				}
			}
			break;

			case Function::EndOfObjectPoolMessage:
			{
				if (managedWorkingSet->get_object_pool_transfer_data_dropped())
				{
					// Object Pool Transfer data for this working set was discarded, so the pool is
					// missing objects the client believes it sent. Parsing what did arrive would parse
					// cleanly and report success, telling the client a pool loaded that is not the pool
					// it transferred -- so the pool is refused instead.
					//
					// C.2.5 byte 2 bit 0, "There are errors in the Object Pool, refer to Bytes 3 to 8",
					// with byte 7 bit 2, "any other error". No single object is at fault -- the fault is
					// a hole in the transfer -- so the parent and faulting object IDs stay NULL, and byte
					// 7 genuinely carries an error, which is what byte 2 bit 0 promises the client it
					// will find there.
					//
					// This is refused for as long as the pool lives, not just once: the discarded bytes
					// are unrecoverable and C.2.3 defines no response that could have told the client
					// which ones to resend, so a second End of Object Pool would report success on the
					// same incomplete pool. A Delete Object Pool (F.44) clears the record along with the
					// pool, which is how a client that wants to start over does it.
					//
					// The consequences match every other End of Object Pool failure rather than
					// inventing new ones, because this is one: an incomplete pool on an already-active
					// working set is a failed runtime object pool update, and C.2.6 has the VT delete
					// the entire pool -- the pre-update version included -- and suspend the working set.
					// So the same test the parse-failure path uses, the same byte 7 bit 3 telling the
					// client the pool was deleted from volatile memory, and the same deletion request.
					// An incomplete pool on a working set that never became active is an initial-upload
					// failure and is left in place for the client to retry, again as there.
					const bool poolWasActive = (managedWorkingSet == activeWorkingSet);

					LOG_ERROR("[VT Server]: Refusing the object pool of the working set at address %u: transfer data for it was dropped while an earlier parse was outstanding, so the pool is incomplete.", message.get_identifier().get_source_address());
					send_end_of_object_pool_response(false, NULL_OBJECT_ID, NULL_OBJECT_ID, static_cast<std::uint8_t>(get_bit(2) | (poolWasActive ? get_bit(3) : 0)), managedWorkingSet->get_control_function());

					if (poolWasActive)
					{
						managedWorkingSet->request_deletion();
					}
				}
				else if (managedWorkingSet->get_any_object_pools())
				{
					managedWorkingSet->start_parsing_thread();
				}
				else
				{
					LOG_WARNING("[VT Server]: End of object pool message ignored - no object pools are loaded for the source control function");
				}
			}
			break;

			case Function::WorkingSetMaintenanceMessage:
			{
				if (0 != managedWorkingSet->get_working_set_maintenance_message_timestamp_ms())
				{
					managedWorkingSet->set_working_set_maintenance_message_timestamp_ms(SystemTiming::get_timestamp_ms());
				}
			}
			break;

			case Function::ChangeNumericValueCommand:
			{
				const auto value = get_little_endian_uint32(data, 4);
				auto objectId = get_little_endian_uint16(data, 1);
				auto lTargetObject = managedWorkingSet->get_object_by_id(objectId);
				bool logSuccess = true;

				if (nullptr != lTargetObject)
				{
					switch (lTargetObject->get_object_type())
					{
						case VirtualTerminalObjectType::InputBoolean:
						{
							std::static_pointer_cast<InputBoolean>(lTargetObject)->set_value(get_byte(value, 0));
							dispatch_repaint(managedWorkingSet);
							send_change_numeric_value_response(objectId, 0, value, managedWorkingSet->get_control_function());
						}
						break;

						case VirtualTerminalObjectType::InputNumber:
						{
							std::static_pointer_cast<InputNumber>(lTargetObject)->set_value(value);
							dispatch_repaint(managedWorkingSet);
							send_change_numeric_value_response(objectId, 0, value, managedWorkingSet->get_control_function());
						}
						break;

						case VirtualTerminalObjectType::InputList:
						{
							std::static_pointer_cast<InputList>(lTargetObject)->set_value(get_byte(value, 0));
							dispatch_repaint(managedWorkingSet);
							send_change_numeric_value_response(objectId, 0, value, managedWorkingSet->get_control_function());
						}
						break;

						case VirtualTerminalObjectType::OutputNumber:
						{
							std::static_pointer_cast<OutputNumber>(lTargetObject)->set_value(value);
							dispatch_repaint(managedWorkingSet);
							send_change_numeric_value_response(objectId, 0, value, managedWorkingSet->get_control_function());
						}
						break;

						case VirtualTerminalObjectType::OutputList:
						{
							std::static_pointer_cast<OutputList>(lTargetObject)->set_value(get_byte(value, 0));
							dispatch_repaint(managedWorkingSet);
							send_change_numeric_value_response(objectId, 0, value, managedWorkingSet->get_control_function());
						}
						break;

						case VirtualTerminalObjectType::OutputMeter:
						{
							std::static_pointer_cast<OutputMeter>(lTargetObject)->set_value(get_low_16_bits(value));
							dispatch_repaint(managedWorkingSet);
							send_change_numeric_value_response(objectId, 0, value, managedWorkingSet->get_control_function());
						}
						break;

						case VirtualTerminalObjectType::OutputLinearBarGraph:
						{
							std::static_pointer_cast<OutputLinearBarGraph>(lTargetObject)->set_value(get_low_16_bits(value));
							dispatch_repaint(managedWorkingSet);
							send_change_numeric_value_response(objectId, 0, value, managedWorkingSet->get_control_function());
						}
						break;

						case VirtualTerminalObjectType::OutputArchedBarGraph:
						{
							std::static_pointer_cast<OutputArchedBarGraph>(lTargetObject)->set_value(get_low_16_bits(value));
							dispatch_repaint(managedWorkingSet);
							send_change_numeric_value_response(objectId, 0, value, managedWorkingSet->get_control_function());
						}
						break;

						case VirtualTerminalObjectType::NumberVariable:
						{
							std::static_pointer_cast<NumberVariable>(lTargetObject)->set_value(value);
							dispatch_repaint(managedWorkingSet);
							send_change_numeric_value_response(objectId, 0, value, managedWorkingSet->get_control_function());
						}
						break;

						case VirtualTerminalObjectType::ObjectPointer:
						{
							std::static_pointer_cast<ObjectPointer>(lTargetObject)->set_value(get_low_16_bits(value));
							dispatch_repaint(managedWorkingSet);
							send_change_numeric_value_response(objectId, 0, value, managedWorkingSet->get_control_function());
						}
						break;

						case VirtualTerminalObjectType::ExternalObjectPointer:
						{
							std::uint16_t externalReferenceNAMEObjectIdD = get_little_endian_uint16(data, 4);
							std::uint16_t referencedObjectID = get_little_endian_uint16(data, 6);
							std::static_pointer_cast<ExternalObjectPointer>(lTargetObject)->set_external_reference_name_id(externalReferenceNAMEObjectIdD);
							std::static_pointer_cast<ExternalObjectPointer>(lTargetObject)->set_external_object_id(referencedObjectID);
							send_change_numeric_value_response(objectId, 0, value, managedWorkingSet->get_control_function());
							// Todo: event dispatcher
						}
						break;

						case VirtualTerminalObjectType::Animation:
						{
							// Todo std::static_pointer_cast<Animation>(lTargetObject)->set_value(value);
							// onChangeNumericValueEventDispatcher.call(objectId, value);
							send_change_numeric_value_response(objectId, get_bit(static_cast<std::uint8_t>(ChangeNumericValueErrorBit::AnyOtherError)), value, managedWorkingSet->get_control_function());
							LOG_WARNING("[VT Server]: Client %u change numeric value for animation not implemented yet", managedWorkingSet->get_control_function()->get_address());
							logSuccess = false;
						}
						break;

						default:
						{
							send_change_numeric_value_response(objectId, get_bit(static_cast<std::uint8_t>(ChangeNumericValueErrorBit::InvalidObjectID)), value, managedWorkingSet->get_control_function());
							LOG_WARNING("[VT Server]: Client %u change numeric value invalid object type. ID: %u", managedWorkingSet->get_control_function()->get_address(), objectId);
							logSuccess = false;
						}
						break;
					}

					if (logSuccess)
					{
						LOG_DEBUG("[VT Server]: Client %u change numeric value command: change object ID %u to be %u", managedWorkingSet->get_control_function()->get_address(), objectId, value);
						process_macro(lTargetObject, isobus::EventID::OnChangeValue, lTargetObject->get_object_type(), managedWorkingSet);
					}
				}
				else
				{
					send_change_numeric_value_response(objectId, get_bit(static_cast<std::uint8_t>(ChangeNumericValueErrorBit::InvalidObjectID)), value, managedWorkingSet->get_control_function());
					LOG_WARNING("[VT Server]: Client %u change numeric value invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), objectId);
				}
			}
			break;

			case Function::HideShowObjectCommand:
			{
				auto objectId = get_little_endian_uint16(data, 1);
				auto targetObject = managedWorkingSet->get_object_by_id(objectId);

				if ((nullptr != targetObject) && (VirtualTerminalObjectType::Container == targetObject->get_object_type()))
				{
					std::static_pointer_cast<Container>(targetObject)->set_hidden(0 == data[3]);
					send_hide_show_object_response(objectId, 0, (0 != data[3]), managedWorkingSet->get_control_function());
					dispatch_repaint(managedWorkingSet);

					if (0 == data[3])
					{
						LOG_DEBUG("[VT Server]: Client %u hide object command %u", managedWorkingSet->get_control_function()->get_address(), objectId);
						process_macro(targetObject, EventID::OnHide, targetObject->get_object_type(), managedWorkingSet);
					}
					else
					{
						LOG_DEBUG("[VT Server]: Client %u show object command %u", managedWorkingSet->get_control_function()->get_address(), objectId);
						process_macro(targetObject, EventID::OnShow, targetObject->get_object_type(), managedWorkingSet);
					}
				}
				else
				{
					send_hide_show_object_response(objectId, get_bit(static_cast<std::uint8_t>(HideShowObjectErrorBit::InvalidObjectID)), (0 != data[3]), managedWorkingSet->get_control_function());
					LOG_WARNING("[VT Server]: Client %u hide/show object command failed. It can only affect containers! ID: %u", managedWorkingSet->get_control_function()->get_address(), objectId);
				}
			}
			break;

			case Function::EnableDisableObjectCommand:
			{
				auto objectId = get_little_endian_uint16(data, 1);
				auto lTargetObject = managedWorkingSet->get_object_by_id(objectId);

				if (nullptr != lTargetObject)
				{
					if (data[3] <= 1)
					{
						switch (lTargetObject->get_object_type())
						{
							case VirtualTerminalObjectType::InputBoolean:
							{
								std::static_pointer_cast<InputBoolean>(lTargetObject)->set_enabled(0 != data[3]);
								send_enable_disable_object_response(objectId, 0, (0 != data[3]), managedWorkingSet->get_control_function());
								dispatch_repaint(managedWorkingSet);
							}
							break;

							case VirtualTerminalObjectType::InputList:
							{
								std::static_pointer_cast<InputList>(lTargetObject)->set_option(InputList::Options::Enabled, (0 != data[3]));
								send_enable_disable_object_response(objectId, 0, (0 != data[3]), managedWorkingSet->get_control_function());
								dispatch_repaint(managedWorkingSet);
							}
							break;

							case VirtualTerminalObjectType::InputString:
							{
								std::static_pointer_cast<InputString>(lTargetObject)->set_enabled((0 != data[3]));
								send_enable_disable_object_response(objectId, 0, (0 != data[3]), managedWorkingSet->get_control_function());
								dispatch_repaint(managedWorkingSet);
							}
							break;

							case VirtualTerminalObjectType::InputNumber:
							{
								std::static_pointer_cast<InputNumber>(lTargetObject)->set_option2(InputNumber::Options2::Enabled, (0 != data[3]));
								send_enable_disable_object_response(objectId, 0, (0 != data[3]), managedWorkingSet->get_control_function());
								dispatch_repaint(managedWorkingSet);
							}
							break;

							case VirtualTerminalObjectType::Button:
							{
								std::static_pointer_cast<Button>(lTargetObject)->set_option(Button::Options::Disabled, (0 == data[3]));
								send_enable_disable_object_response(objectId, 0, (0 != data[3]), managedWorkingSet->get_control_function());
								dispatch_repaint(managedWorkingSet);
							}
							break;

							default:
							{
								send_enable_disable_object_response(objectId, get_bit(static_cast<std::uint8_t>(EnableDisableObjectErrorBit::InvalidObjectID)), (0 != data[3]), managedWorkingSet->get_control_function());
							}
							break;
						}
					}
					else
					{
						send_enable_disable_object_response(objectId, get_bit(static_cast<std::uint8_t>(EnableDisableObjectErrorBit::InvalidEnableDisableCommandValue)), (0 != data[3]), managedWorkingSet->get_control_function());
					}
				}
				else
				{
					send_enable_disable_object_response(objectId, get_bit(static_cast<std::uint8_t>(EnableDisableObjectErrorBit::InvalidObjectID)), (0 != data[3]), managedWorkingSet->get_control_function());
				}
			}
			break;

			case Function::ChangeChildLocationCommand:
			{
				auto parentObjectId = get_little_endian_uint16(data, 1);
				auto objectID = get_little_endian_uint16(data, 3);
				auto parentObject = managedWorkingSet->get_object_by_id(parentObjectId);

				if (nullptr != parentObject)
				{
					auto lTargetObject = managedWorkingSet->get_object_by_id(objectID);

					if (nullptr != lTargetObject)
					{
						auto xRelativeChange = static_cast<std::int8_t>(static_cast<std::int16_t>(data[5]) - 127);
						auto yRelativeChange = static_cast<std::int8_t>(static_cast<std::int16_t>(data[6]) - 127);
						bool anyObjectMatched = parentObject->offset_all_children_with_id(objectID, xRelativeChange, yRelativeChange);

						dispatch_repaint(managedWorkingSet);

						if (anyObjectMatched)
						{
							send_change_child_location_response(parentObjectId, objectID, 0, managedWorkingSet->get_control_function());
							LOG_DEBUG("[VT Server]: Client %u change child location command. Parent: %u, Target: %u, X-Offset: %d, Y-Offset: %d", managedWorkingSet->get_control_function()->get_address(), parentObjectId, objectID, xRelativeChange, yRelativeChange);
							process_macro(parentObject, EventID::ChangeChildLocation, parentObject->get_object_type(), managedWorkingSet);
						}
						else
						{
							send_change_child_location_response(parentObjectId, objectID, get_bit(static_cast<std::uint8_t>(ChangeChildLocationorPositionErrorBit::TargetObjectDoesNotExistOrIsNotApplicable)), managedWorkingSet->get_control_function());
							LOG_WARNING("[VT Server]: Client %u change child location failed because the target object with ID %u isn't applicable", managedWorkingSet->get_control_function()->get_address(), objectID);
						}
					}
					else
					{
						send_change_child_location_response(parentObjectId, objectID, get_bit(static_cast<std::uint8_t>(ChangeChildLocationorPositionErrorBit::TargetObjectDoesNotExistOrIsNotApplicable)), managedWorkingSet->get_control_function());
						LOG_WARNING("[VT Server]: Client %u change child location failed because the target object with ID %u doesn't exist", managedWorkingSet->get_control_function()->get_address(), objectID);
					}
				}
				else
				{
					send_change_child_location_response(parentObjectId, objectID, get_bit(static_cast<std::uint8_t>(ChangeChildLocationorPositionErrorBit::ParentObjectDoesntExistOrIsNotAParentOfSpecifiedObject)), managedWorkingSet->get_control_function());
					LOG_WARNING("[VT Server]: Client %u change child location failed because the parent object with ID %u doesn't exist", managedWorkingSet->get_control_function()->get_address(), parentObjectId);
				}
			}
			break;

			case Function::ChangeActiveMaskCommand:
			{
				auto workingSetObjectId = get_little_endian_uint16(data, 1);
				auto newActiveMaskObjectId = get_little_endian_uint16(data, 3);
				// Both IDs resolve against ONE snapshot, so the mask that gets set belongs to the same
				// object tree as the Working Set object it is set on. Resolving them separately would
				// take two snapshots, which the parse worker can publish between.
				const auto objectTree = managedWorkingSet->get_object_tree();
				auto workingSetObject = VTObject::get_object_by_id(workingSetObjectId, *objectTree);
				auto newActiveMaskObject = VTObject::get_object_by_id(newActiveMaskObjectId, *objectTree);

				// F.34 restricts this command to "the active mask of a Working Set" and to a new mask that is
				// "either a Data Mask object or an Alarm Mask object", so both IDs are checked for type and not
				// merely for existence. The Working Set check is what makes the downcast below defined: this
				// build has RTTI off, so the cast is unchecked, and WorkingSet::set_active_mask writes a member
				// that only WorkingSet declares. An object of any other type would be written past the storage
				// it actually has.
				const bool workingSetObjectIsValid = (nullptr != workingSetObject) &&
				  (VirtualTerminalObjectType::WorkingSet == workingSetObject->get_object_type());
				const bool newActiveMaskObjectIsValid = (nullptr != newActiveMaskObject) &&
				  ((VirtualTerminalObjectType::DataMask == newActiveMaskObject->get_object_type()) ||
				   (VirtualTerminalObjectType::AlarmMask == newActiveMaskObject->get_object_type()));

				// Both IDs are validated before the first side effect, so a rejected command changes nothing:
				// no active mask, no open-for-input field, no mask lock release, no arbitration and no event
				// dispatch. Only the error response leaves the server.
				if (workingSetObjectIsValid)
				{
					if (newActiveMaskObjectIsValid)
					{
						std::static_pointer_cast<WorkingSet>(workingSetObject)->set_active_mask(newActiveMaskObjectId);

						// An input field open for input belongs to the mask that was visible. Moving to
						// another mask ends that input, so ESC must not report the field as still open.
						managedWorkingSet->set_object_open_for_input(NULL_OBJECT_ID);

						// F.46 releases a mask lock when the locked mask goes from visible to hidden, and
						// requires an unsolicited response saying so. The release is conditioned on the
						// locked mask actually being the one leaving the screen: a client that re-selects
						// the mask it already has visible, which some use as a refresh idiom, hides
						// nothing and keeps its lock.
						if ((NULL_OBJECT_ID != managedWorkingSet->get_mask_lock_object_id()) &&
						    (newActiveMaskObjectId != managedWorkingSet->get_mask_lock_object_id()))
						{
							managedWorkingSet->set_mask_lock(NULL_OBJECT_ID, 0, 0, {});
							send_lock_unlock_mask_response(0, get_bit(static_cast<std::uint8_t>(LockUnlockMaskErrorBit::UnsolicitedUnlockMaskIsHidden)), true, managedWorkingSet->get_control_function());
							LOG_DEBUG("[VT Server]: Client %u mask lock released because the locked mask is no longer visible", managedWorkingSet->get_control_function()->get_address());
						}
						send_change_active_mask_response(newActiveMaskObjectId, 0, managedWorkingSet->get_control_function());

						// A mask change can move the screen between working sets under the priority rules of
						// 4.6.14, so which working set is displayed is recomputed rather than left with whoever
						// held it. The arbitration also refreshes the status' visible-mask fields (G.2 bytes
						// 3-6), which report the ACTIVE working set's mask and the soft key mask it carries.
						apply_active_working_set_arbitration();
						onChangeActiveMaskEventDispatcher.call(managedWorkingSet, workingSetObjectId, newActiveMaskObjectId);
						LOG_DEBUG("[VT Server]: Client %u changed active mask to object %u for working set object %u", managedWorkingSet->get_control_function()->get_address(), newActiveMaskObjectId, workingSetObjectId);
					}
					else
					{
						send_change_active_mask_response(newActiveMaskObjectId, get_bit(static_cast<std::uint8_t>(ChangeActiveMaskErrorBit::InvalidMaskObjectID)), managedWorkingSet->get_control_function());
						LOG_WARNING("[VT Server]: Client %u change active mask failed because the new mask object ID %u was not valid.", managedWorkingSet->get_control_function()->get_address(), newActiveMaskObjectId);
					}
				}
				else
				{
					send_change_active_mask_response(newActiveMaskObjectId, get_bit(static_cast<std::uint8_t>(ChangeActiveMaskErrorBit::InvalidWorkingSetObjectID)), managedWorkingSet->get_control_function());
					LOG_WARNING("[VT Server]: Client %u change active mask failed because the working set object ID %u was not valid.", managedWorkingSet->get_control_function()->get_address(), workingSetObjectId);
				}
			}
			break;

			case Function::SelectColourMapCommand:
			{
				auto objectId = get_little_endian_uint16(data, 1);

				if (NULL_OBJECT_ID == objectId)
				{
					managedWorkingSet->set_active_colour_map_object_id(NULL_OBJECT_ID, {});
					send_select_colour_map_response(objectId, 0, managedWorkingSet->get_control_function());
					dispatch_repaint(managedWorkingSet);
					LOG_DEBUG("[VT Server]: Client %u select colour map command restored the default palette", managedWorkingSet->get_control_function()->get_address());
				}
				else
				{
					auto object = managedWorkingSet->get_object_by_id(objectId);

					if (nullptr == object)
					{
						send_select_colour_map_response(objectId, get_bit(static_cast<std::uint8_t>(SelectColourMapErrorBit::InvalidObjectID)), managedWorkingSet->get_control_function());
						LOG_WARNING("[VT Server]: Client %u select colour map failed because the object ID %u doesn't exist", managedWorkingSet->get_control_function()->get_address(), objectId);
					}
					else if (VirtualTerminalObjectType::ColourMap != object->get_object_type())
					{
						send_select_colour_map_response(objectId, get_bit(static_cast<std::uint8_t>(SelectColourMapErrorBit::InvalidColourMap)), managedWorkingSet->get_control_function());
						LOG_WARNING("[VT Server]: Client %u select colour map failed because the object ID %u is not a Colour Map", managedWorkingSet->get_control_function()->get_address(), objectId);
					}
					else
					{
						managedWorkingSet->set_active_colour_map_object_id(objectId, {});
						send_select_colour_map_response(objectId, 0, managedWorkingSet->get_control_function());
						dispatch_repaint(managedWorkingSet);
						LOG_DEBUG("[VT Server]: Client %u selected colour map object %u", managedWorkingSet->get_control_function()->get_address(), objectId);
					}
				}
			}
			break;

			case Function::GetAttributeValueMessage:
			{
				auto objectId = get_little_endian_uint16(data, 1);
				std::uint8_t attributeId = data[3];
				auto object = managedWorkingSet->get_object_by_id(objectId);

				if (nullptr == object)
				{
					send_get_attribute_value_response(objectId, attributeId, 0, get_bit(static_cast<std::uint8_t>(GetAttributeValueErrorBit::InvalidObjectID)), managedWorkingSet->get_control_function());
					LOG_WARNING("[VT Server]: Client %u get attribute value failed because the object ID %u doesn't exist", managedWorkingSet->get_control_function()->get_address(), objectId);
				}
				else
				{
					std::uint32_t attributeValue = 0;

					if (object->get_attribute(attributeId, attributeValue))
					{
						send_get_attribute_value_response(objectId, attributeId, attributeValue, 0, managedWorkingSet->get_control_function());
						LOG_DEBUG("[VT Server]: Client %u read attribute %u of object %u as %u", managedWorkingSet->get_control_function()->get_address(), attributeId, objectId, attributeValue);
					}
					else
					{
						send_get_attribute_value_response(objectId, attributeId, 0, get_bit(static_cast<std::uint8_t>(GetAttributeValueErrorBit::InvalidAttributeID)), managedWorkingSet->get_control_function());
						LOG_WARNING("[VT Server]: Client %u get attribute value failed because object %u has no attribute %u", managedWorkingSet->get_control_function()->get_address(), objectId, attributeId);
					}
				}
			}
			break;

			case Function::ChangeStringValueCommand:
			{
				auto objectIdToChange = get_little_endian_uint16(data, 1);
				auto numberOfBytesInString = get_little_endian_uint16(data, 3);
				auto stringObject = managedWorkingSet->get_object_by_id(objectIdToChange);

				if (message.get_data_length() >= static_cast<std::uint32_t>(numberOfBytesInString + 5))
				{
					if (nullptr != stringObject)
					{
						std::string newStringValue;

						for (std::uint32_t i = 0; i < numberOfBytesInString; i++)
						{
							newStringValue.push_back(static_cast<char>(data.at(5 + i)));
						}

						switch (stringObject->get_object_type())
						{
							case VirtualTerminalObjectType::StringVariable:
							{
								auto stringVariable = std::static_pointer_cast<StringVariable>(stringObject);

								// The transferred string is allowed to be smaller than the length of the value attribute of the target object
								// and in this case the VT shall pad the value attribute with space characters.
								while (newStringValue.length() < stringVariable->get_value().length())
								{
									newStringValue.push_back(' ');
								}
								stringVariable->set_value(newStringValue);
								send_change_string_value_response(objectIdToChange, 0, message.get_source_control_function());
								dispatch_repaint(managedWorkingSet);
								LOG_DEBUG("[VT Server]: Client %u change string value command for string variable object %u. Value: " + newStringValue, managedWorkingSet->get_control_function()->get_address(), objectIdToChange);
							}
							break;

							case VirtualTerminalObjectType::OutputString:
							{
								auto outputString = std::static_pointer_cast<OutputString>(stringObject);

								// The transferred string is allowed to be smaller than the length of the value attribute of the target object
								// and in this case the VT shall pad the value attribute with space characters.
								while (newStringValue.length() < outputString->get_value().length())
								{
									newStringValue.push_back(' ');
								}
								outputString->set_value(newStringValue);
								send_change_string_value_response(objectIdToChange, 0, message.get_source_control_function());
								dispatch_repaint(managedWorkingSet);
								LOG_DEBUG("[VT Server]: Client %u change string value command for output string object %u. Value: " + newStringValue, managedWorkingSet->get_control_function()->get_address(), objectIdToChange);
							}
							break;

							case VirtualTerminalObjectType::InputString:
							{
								auto inputString = std::static_pointer_cast<InputString>(stringObject);

								// The transferred string is allowed to be smaller than the length of the value attribute of the target object
								// and in this case the VT shall pad the value attribute with space characters.
								while (newStringValue.length() < inputString->get_value().length())
								{
									newStringValue.push_back(' ');
								}
								inputString->set_value(newStringValue);
								send_change_string_value_response(objectIdToChange, 0, message.get_source_control_function());
								dispatch_repaint(managedWorkingSet);
								LOG_DEBUG("[VT Server]: Client %u change string value command for input string object %u. Value: " + newStringValue, managedWorkingSet->get_control_function()->get_address(), objectIdToChange);
							}
							break;

							default:
							{
								send_change_string_value_response(objectIdToChange, get_bit(static_cast<std::uint8_t>(ChangeStringValueErrorBit::InvalidObjectID)), message.get_source_control_function());
								LOG_WARNING("[VT Server]: Client %u change string value command for object %u failed because the object ID was for an object that isn't a string.", managedWorkingSet->get_control_function()->get_address(), objectIdToChange);
							}
							break;
						}
					}
					else
					{
						send_change_string_value_response(objectIdToChange, get_bit(static_cast<std::uint8_t>(ChangeStringValueErrorBit::InvalidObjectID)), message.get_source_control_function());
						LOG_WARNING("[VT Server]: Client %u change string value command for object %u failed because the object ID was invalid.", managedWorkingSet->get_control_function()->get_address(), objectIdToChange);
					}
				}
				else
				{
					send_change_string_value_response(objectIdToChange, get_bit(static_cast<std::uint8_t>(ChangeStringValueErrorBit::AnyOtherError)), message.get_source_control_function());
					LOG_WARNING("[VT Server]: Client %u change string value command for object %u failed because data length is not valid when compared to the amount sent.", managedWorkingSet->get_control_function()->get_address(), objectIdToChange);
				}
			}
			break;

			case Function::ChangeFillAttributesCommand:
			{
				auto objectIdToChange = get_little_endian_uint16(data, 1);
				auto fillPatternID = get_little_endian_uint16(data, 5);
				auto object = managedWorkingSet->get_object_by_id(objectIdToChange);
				auto fillPatternObject = managedWorkingSet->get_object_by_id(fillPatternID);

				if ((nullptr != object) && (VirtualTerminalObjectType::FillAttributes == object->get_object_type()))
				{
					auto fillObject = std::static_pointer_cast<FillAttributes>(object);

					if (((nullptr != fillPatternObject) && (VirtualTerminalObjectType::PictureGraphic == fillPatternObject->get_object_type())) || (NULL_OBJECT_ID == fillPatternID))
					{
						if (data[3] <= static_cast<std::uint8_t>(FillAttributes::FillType::FillWithPatternGivenByFillPatternAttribute))
						{
							fillObject->set_fill_pattern(fillPatternID);
							fillObject->set_type(static_cast<FillAttributes::FillType>(data[3]));
							fillObject->set_background_color(data[4]);
							send_change_fill_attributes_response(objectIdToChange, 0, message.get_source_control_function());
							dispatch_repaint(managedWorkingSet);
							LOG_DEBUG("[VT Server]: Client %u change fill attributes command for object %u", managedWorkingSet->get_control_function()->get_address(), objectIdToChange);
						}
						else
						{
							send_change_fill_attributes_response(objectIdToChange, get_bit(static_cast<std::uint8_t>(ChangeFillAttributesErrorBit::InvalidType)), message.get_source_control_function());
							LOG_WARNING("[VT Server]: Client %u change fill attributes of object %u invalid fill object type. Must be a picture graphic.", managedWorkingSet->get_control_function()->get_address(), objectIdToChange);
						}
					}
					else
					{
						send_change_fill_attributes_response(objectIdToChange, get_bit(static_cast<std::uint8_t>(ChangeFillAttributesErrorBit::InvalidPatternObjectID)), message.get_source_control_function());
						LOG_WARNING("[VT Server]: Client %u change fill attributes invalid pattern object ID of %u for object %u", managedWorkingSet->get_control_function()->get_address(), fillPatternID, objectIdToChange);
					}
				}
				else
				{
					send_change_fill_attributes_response(objectIdToChange, get_bit(static_cast<std::uint8_t>(ChangeFillAttributesErrorBit::InvalidObjectID)), message.get_source_control_function());
					LOG_WARNING("[VT Server]: Client %u change fill attributes invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), objectIdToChange);
				}
			}
			break;

			case Function::ChangeChildPositionCommand:
			{
				auto parentObjectId = get_little_endian_uint16(data, 1);
				auto objectID = get_little_endian_uint16(data, 3);
				if (message.get_data_length() > CAN_DATA_LENGTH) // Must be at least 9 bytes
				{
					std::uint16_t newXPosition = get_little_endian_uint16(data, 5);
					std::uint16_t newYPosition = get_little_endian_uint16(data, 7);
					auto parentObject = managedWorkingSet->get_object_by_id(parentObjectId);
					auto targetObject = managedWorkingSet->get_object_by_id(objectID);

					if (nullptr != parentObject)
					{
						if (nullptr != targetObject)
						{
							switch (parentObject->get_object_type())
							{
								case VirtualTerminalObjectType::Button:
								case VirtualTerminalObjectType::Container:
								case VirtualTerminalObjectType::AlarmMask:
								case VirtualTerminalObjectType::DataMask:
								case VirtualTerminalObjectType::Key:
								case VirtualTerminalObjectType::WorkingSet:
								case VirtualTerminalObjectType::AuxiliaryInputType2:
								case VirtualTerminalObjectType::WindowMask:
								{
									bool wasFound = false;

									// If a parent object includes the child object multiple times, then each instance will be moved
									for (std::uint16_t i = 0; i < parentObject->get_number_children(); i++)
									{
										if (objectID == parentObject->get_child_id(i))
										{
											wasFound = true;
											parentObject->set_child_x(i, newXPosition);
											parentObject->set_child_y(i, newYPosition);
											dispatch_repaint(managedWorkingSet);
										}
									}

									if (wasFound)
									{
										LOG_DEBUG("[VT Server]: Client %u changed child position: object %u of parent object %u, x: %u, y: %u", managedWorkingSet->get_control_function()->get_address(), objectID, parentObjectId, newXPosition, newYPosition);
										send_change_child_position_response(parentObjectId, objectID, 0, message.get_source_control_function());
										process_macro(parentObject, EventID::OnChangeChildPosition, parentObject->get_object_type(), managedWorkingSet);
									}
									else
									{
										LOG_WARNING("[VT Server]: Client %u change child position error. Target object does not exist or is not applicable: object %u of parent object %u, x: %u, y: %u", managedWorkingSet->get_control_function()->get_address(), objectID, parentObjectId, newXPosition, newYPosition);
										send_change_child_position_response(parentObjectId, objectID, get_bit(static_cast<std::uint8_t>(ChangeChildLocationorPositionErrorBit::TargetObjectDoesNotExistOrIsNotApplicable)), message.get_source_control_function());
									}
								}
								break;

								default:
								{
									LOG_WARNING("[VT Server]: Client %u change child position error. Parent object type cannot be targeted by this command: object %u of parent object %u, x: %u, y: %u", managedWorkingSet->get_control_function()->get_address(), objectID, parentObjectId, newXPosition, newYPosition);
									send_change_child_position_response(parentObjectId, objectID, get_bit(static_cast<std::uint8_t>(ChangeChildLocationorPositionErrorBit::AnyOtherError)), message.get_source_control_function());
								}
								break;
							}
						}
						else
						{
							LOG_WARNING("[VT Server]: Client %u change child position error. Target object does not exist or is not applicable: object %u of parent object %u, x: %u, y: %u", managedWorkingSet->get_control_function()->get_address(), objectID, parentObjectId, newXPosition, newYPosition);
							send_change_child_position_response(parentObjectId, objectID, get_bit(static_cast<std::uint8_t>(ChangeChildLocationorPositionErrorBit::TargetObjectDoesNotExistOrIsNotApplicable)), message.get_source_control_function());
						}
					}
					else
					{
						LOG_WARNING("[VT Server]: Client %u change child position error. Parent object does not exist or is not applicable: object %u of parent object %u, x: %u, y: %u", managedWorkingSet->get_control_function()->get_address(), objectID, parentObjectId, newXPosition, newYPosition);
						send_change_child_position_response(parentObjectId, objectID, get_bit(static_cast<std::uint8_t>(ChangeChildLocationorPositionErrorBit::ParentObjectDoesntExistOrIsNotAParentOfSpecifiedObject)), message.get_source_control_function());
					}
				}
				else
				{
					LOG_WARNING("[VT Server]: Client %u change child position error. DLC must be 9 bytes for the message to be valid.");
					send_change_child_position_response(parentObjectId, objectID, get_bit(static_cast<std::uint8_t>(ChangeChildLocationorPositionErrorBit::AnyOtherError)), message.get_source_control_function());
				}
			}
			break;

			case Function::ChangeAttributeCommand:
			{
				auto objectID = get_little_endian_uint16(data, 1);
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);
				std::uint8_t attributeID = data[3];
				const auto attributeData = get_little_endian_uint32(data, 4);
				VTObject::AttributeError errorCode = VTObject::AttributeError::AnyOtherError;

				if ((NULL_OBJECT_ID != objectID) && (nullptr != targetObject))
				{
					// The snapshot is held in a named local for the duration of the call that reads it,
					// so the tree reference passed below outlives the call rather than dangling on a
					// temporary that expired at the end of the argument list.
					const auto objectTree = managedWorkingSet->get_object_tree();

					if (targetObject->set_attribute(attributeID, attributeData, *objectTree, errorCode)) // 0 Is always the read-only "type" attribute
					{
						send_change_attribute_response(objectID, 0, data.at(3), message.get_source_control_function());
						LOG_DEBUG("[VT Server]: Client %u changed object %u attribute %u to %u", managedWorkingSet->get_control_function()->get_address(), objectID, attributeID, attributeData);

						// Change Attribute reaches the same state the dedicated commands do, including both keys
						// 4.6.14 ranks alarms by: attribute 3 on a Working Set retargets the active mask, exactly as
						// Change Active Mask does, and attribute 3 on an Alarm Mask sets its priority, exactly as
						// Change Priority does. Arbitrating here is what stamps the activation sequence at the moment
						// an alarm is raised this way, and what lets an alarm raised by a non-active working set take
						// the screen and sound. Arbitration subsumes the status refresh, and is idempotent when the
						// attribute changed nothing the screen depends on.
						apply_active_working_set_arbitration();
						dispatch_repaint(managedWorkingSet);
						process_macro(targetObject, EventID::OnChangeAttribute, targetObject->get_object_type(), managedWorkingSet);
					}
					else
					{
						send_change_attribute_response(objectID, get_bit(static_cast<std::uint8_t>(errorCode)), data.at(3), message.get_source_control_function());
						LOG_WARNING("[VT Server]: Client %u change object %u attribute %u to %ul error %u", managedWorkingSet->get_control_function()->get_address(), objectID, attributeID, attributeData, static_cast<std::uint8_t>(errorCode));
					}
				}
				else
				{
					send_change_attribute_response(objectID, get_bit(static_cast<std::uint8_t>(VTObject::AttributeError::InvalidObjectID)), data.at(3), message.get_source_control_function());
					LOG_WARNING("[VT Server]: Client %u change attribute %u invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), attributeID, objectID);
				}
			}
			break;

			case Function::ChangeSizeCommand:
			{
				auto objectID = get_little_endian_uint16(data, 1);
				auto newWidth = get_little_endian_uint16(data, 3);
				auto newHeight = get_little_endian_uint16(data, 5);
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);

				if (nullptr != targetObject)
				{
					bool success = false;

					switch (targetObject->get_object_type())
					{
						case VirtualTerminalObjectType::OutputMeter:
						{
							if (newWidth == newHeight) // Output meter must be square!
							{
								targetObject->set_width(newWidth);
								targetObject->set_height(newHeight);
								success = true;
								LOG_DEBUG("[VT Server]: Client %u change size command: Object: %u, Width: %u, Height: %u", managedWorkingSet->get_control_function()->get_address(), objectID, newWidth, newHeight);
								dispatch_repaint(managedWorkingSet);
							}
							else
							{
								LOG_WARNING("[VT Server]: Client %u change size command: invalid new size. Meter must be square! Object: %u", managedWorkingSet->get_control_function()->get_address(), objectID);
								send_change_size_response(objectID, get_bit(static_cast<std::uint8_t>(ChangeSizeErrorBit::AnyOtherError)), message.get_source_control_function());
							}
						}
						break;

						case VirtualTerminalObjectType::Animation:
						case VirtualTerminalObjectType::Button:
						case VirtualTerminalObjectType::Container:
						case VirtualTerminalObjectType::InputBoolean:
						case VirtualTerminalObjectType::InputList:
						case VirtualTerminalObjectType::InputString:
						case VirtualTerminalObjectType::InputNumber:
						case VirtualTerminalObjectType::OutputArchedBarGraph:
						case VirtualTerminalObjectType::OutputEllipse:
						case VirtualTerminalObjectType::OutputLine:
						case VirtualTerminalObjectType::OutputLinearBarGraph:
						case VirtualTerminalObjectType::OutputList:
						case VirtualTerminalObjectType::OutputNumber:
						case VirtualTerminalObjectType::OutputPolygon:
						case VirtualTerminalObjectType::OutputRectangle:
						case VirtualTerminalObjectType::OutputString:
						{
							targetObject->set_width(newWidth);
							targetObject->set_height(newHeight);
							success = true;
							LOG_DEBUG("[VT Server]: Client %u change size command: Object: %u, Width: %u, Height: %u", managedWorkingSet->get_control_function()->get_address(), objectID, newWidth, newHeight);
							dispatch_repaint(managedWorkingSet);
						}
						break;

						default:
						{
							LOG_WARNING("[VT Server]: Client %u change size command: invalid object type for object %u", managedWorkingSet->get_control_function()->get_address(), objectID);
							send_change_size_response(objectID, get_bit(static_cast<std::uint8_t>(ChangeSizeErrorBit::AnyOtherError)), message.get_source_control_function());
						}
						break;
					}

					if (success)
					{
						send_change_size_response(objectID, 0, message.get_source_control_function());
						process_macro(targetObject, EventID::OnChangeSize, targetObject->get_object_type(), managedWorkingSet);
					}
				}
				else
				{
					LOG_WARNING("[VT Server]: Client %u change size command: invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), objectID);
					send_change_size_response(objectID, get_bit(static_cast<std::uint8_t>(ChangeSizeErrorBit::InvalidObjectID)), message.get_source_control_function());
				}
			}
			break;

			case Function::ChangeListItemCommand:
			{
				auto objectID = get_little_endian_uint16(data, 1);
				auto newObjectID = get_little_endian_uint16(data, 4);
				auto listIndex = data[3];
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);
				auto newObject = managedWorkingSet->get_object_by_id(newObjectID);

				if (nullptr != targetObject)
				{
					// Named local rather than a temporary in the argument list, so the tree reference the
					// change_list_item calls below take stays alive for the whole of each call.
					const auto objectTree = managedWorkingSet->get_object_tree();

					if ((NULL_OBJECT_ID == newObjectID) || (nullptr != newObject))
					{
						switch (targetObject->get_object_type())
						{
							case VirtualTerminalObjectType::InputList:
							{
								if (std::static_pointer_cast<InputList>(targetObject)->change_list_item(listIndex, newObjectID, *objectTree))
								{
									send_change_list_item_response(objectID, newObjectID, 0, listIndex, message.get_source_control_function());
									LOG_DEBUG("[VT Server]: Client %u change list item command: Object ID: %u, New Object ID: %u, Index: %u", managedWorkingSet->get_control_function()->get_address(), objectID, newObjectID, listIndex);
									dispatch_repaint(managedWorkingSet);
								}
								else
								{
									send_change_list_item_response(objectID, newObjectID, get_bit(static_cast<std::uint8_t>(ChangeListItemErrorBit::AnyOtherError)), listIndex, message.get_source_control_function());
									LOG_WARNING("[VT Server]: Client %u change list item command failed. Object ID: %u, New Object ID: %u, Index: %u", managedWorkingSet->get_control_function()->get_address(), objectID, newObjectID, listIndex);
								}
							}
							break;

							case VirtualTerminalObjectType::Animation:
							case VirtualTerminalObjectType::ExternalObjectDefinition:
							{
								// @todo
								send_change_list_item_response(objectID, newObjectID, get_bit(static_cast<std::uint8_t>(ChangeListItemErrorBit::AnyOtherError)), listIndex, message.get_source_control_function());
								LOG_WARNING("[VT Server]: Client %u change list item command: TODO object type", managedWorkingSet->get_control_function()->get_address());
							}
							break;

							case VirtualTerminalObjectType::OutputList:
							{
								if (std::static_pointer_cast<OutputList>(targetObject)->change_list_item(listIndex, newObjectID, *objectTree))
								{
									send_change_list_item_response(objectID, newObjectID, 0, listIndex, message.get_source_control_function());
									LOG_DEBUG("[VT Server]: Client %u change list item command: Object ID: %u, New Object ID: %u, Index: %u", managedWorkingSet->get_control_function()->get_address(), objectID, newObjectID, listIndex);
									dispatch_repaint(managedWorkingSet);
								}
								else
								{
									send_change_list_item_response(objectID, newObjectID, get_bit(static_cast<std::uint8_t>(ChangeListItemErrorBit::AnyOtherError)), listIndex, message.get_source_control_function());
									LOG_WARNING("[VT Server]: Client %u change list item command failed. Object ID: %u, New Object ID: %u, Index: %u", managedWorkingSet->get_control_function()->get_address(), objectID, newObjectID, listIndex);
								}
							}
							break;

							default:
							{
								LOG_WARNING("[VT Server]: Client %u change list item command: invalid object type. Object: %u", managedWorkingSet->get_control_function()->get_address(), objectID);
								send_change_list_item_response(objectID, newObjectID, get_bit(static_cast<std::uint8_t>(ChangeListItemErrorBit::AnyOtherError)), listIndex, message.get_source_control_function());
							}
							break;
						}
					}
					else
					{
						LOG_WARNING("[VT Server]: Client %u change list item command: invalid new object ID of %u", managedWorkingSet->get_control_function()->get_address(), newObjectID);
						send_change_list_item_response(objectID, newObjectID, get_bit(static_cast<std::uint8_t>(ChangeListItemErrorBit::InvalidNewListItemObjectID)), listIndex, message.get_source_control_function());
					}
				}
				else
				{
					LOG_WARNING("[VT Server]: Client %u change list item command: invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), objectID);
					send_change_list_item_response(objectID, newObjectID, get_bit(static_cast<std::uint8_t>(ChangeListItemErrorBit::InvalidObjectID)), listIndex, message.get_source_control_function());
				}
			}
			break;

			case Function::ChangeFontAttributesCommand:
			{
				auto objectID = get_little_endian_uint16(data, 1);
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);
				std::uint8_t fontColour = data[3];
				std::uint8_t fontSize = data[4];
				std::uint8_t fontType = data[5];
				std::uint8_t fontStyle = data[6];

				if ((nullptr != targetObject) &&
				    (VirtualTerminalObjectType::FontAttributes == targetObject->get_object_type()))
				{
					if (fontSize <= static_cast<std::uint8_t>(FontAttributes::FontSize::Size128x192))
					{
						auto font = std::static_pointer_cast<FontAttributes>(targetObject);
						font->set_colour(fontColour);
						font->set_size(static_cast<FontAttributes::FontSize>(fontSize));
						font->set_type(static_cast<FontAttributes::FontType>(fontType));
						font->set_style(fontStyle);
						LOG_DEBUG("[VT Server]: Client %u change font attributes command: ObjectID: %u", managedWorkingSet->get_control_function()->get_address(), objectID);
						send_change_font_attributes_response(objectID, 0, message.get_source_control_function());
						dispatch_repaint(managedWorkingSet);
					}
					else
					{
						LOG_WARNING("[VT Server]: Client %u change font attributes command: invalid font size %u. ObjectID: %u", managedWorkingSet->get_control_function()->get_address(), fontSize, objectID);
						send_change_font_attributes_response(objectID, get_bit(static_cast<std::uint8_t>(ChangeFontAttributesErrorBit::InvalidSize)), message.get_source_control_function());
					}
				}
				else
				{
					LOG_WARNING("[VT Server]: Client %u change font attributes command: invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), objectID);
					send_change_font_attributes_response(objectID, get_bit(static_cast<std::uint8_t>(ChangeFontAttributesErrorBit::InvalidObjectID)), message.get_source_control_function());
				}
			}
			break;

			case Function::ChangeLineAttributesCommand:
			{
				auto objectID = get_little_endian_uint16(data, 1);
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);
				std::uint8_t lineColour = data[3];
				std::uint8_t lineWidth = data[4];
				std::uint16_t lineArt = get_little_endian_uint16(data, 5);

				if ((nullptr != targetObject) &&
				    (VirtualTerminalObjectType::LineAttributes == targetObject->get_object_type()))
				{
					auto line = std::static_pointer_cast<LineAttributes>(targetObject);
					line->set_background_color(lineColour);
					line->set_width(lineWidth);
					line->set_line_art_bit_pattern(lineArt);
					LOG_DEBUG("[VT Server]: Client %u change line attributes command: ObjectID: %u", managedWorkingSet->get_control_function()->get_address(), objectID);
					send_change_line_attributes_response(objectID, 0, message.get_source_control_function());
					dispatch_repaint(managedWorkingSet);
				}
				else
				{
					LOG_WARNING("[VT Server]: Client %u change line attributes command: invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), objectID);
					send_change_line_attributes_response(objectID, get_bit(static_cast<std::uint8_t>(ChangeFontAttributesErrorBit::InvalidObjectID)), message.get_source_control_function());
				}
			}
			break;

			case Function::ChangeSoftKeyMaskCommand:
			{
				auto dataOrAlarmMaskId = get_little_endian_uint16(data, 2);
				auto newSoftKeyMaskId = get_little_endian_uint16(data, 4);
				auto targetMask = managedWorkingSet->get_object_by_id(dataOrAlarmMaskId);
				auto newSoftKeyMask = managedWorkingSet->get_object_by_id(newSoftKeyMaskId);

				if (nullptr != targetMask)
				{
					// Named local rather than a temporary in the argument list, so the tree reference the
					// change_soft_key_mask calls below take stays alive for the whole of each call.
					const auto objectTree = managedWorkingSet->get_object_tree();

					if ((NULL_OBJECT_ID == newSoftKeyMaskId) || (nullptr != newSoftKeyMask))
					{
						switch (targetMask->get_object_type())
						{
							case VirtualTerminalObjectType::AlarmMask:
							{
								if (std::static_pointer_cast<AlarmMask>(targetMask)->change_soft_key_mask(newSoftKeyMaskId, *objectTree))
								{
									LOG_DEBUG("[VT Server]: Client %u change soft key mask command: alarm mask object %u to %u", managedWorkingSet->get_control_function()->get_address(), dataOrAlarmMaskId, newSoftKeyMaskId);
									send_change_soft_key_mask_response(dataOrAlarmMaskId, newSoftKeyMaskId, 0, message.get_source_control_function());

									if (activeWorkingSet == managedWorkingSet)
									{
										// The status' soft key mask field (G.2 bytes 5-6) tracks the mask the active
										// working set currently shows, so a change to a mask it does not show recomputes
										// the same value and moves nothing.
										refresh_active_mask_status_fields();
									}
									onChangeActiveSoftKeyMaskEventDispatcher.call(managedWorkingSet, dataOrAlarmMaskId, newSoftKeyMaskId);
									process_macro(targetMask, EventID::OnChangeSoftKeyMask, VirtualTerminalObjectType::AlarmMask, managedWorkingSet);
								}
								else
								{
									LOG_WARNING("[VT Server]: Client %u change soft key mask command: failed to set mask for alarm mask object %u to %u", managedWorkingSet->get_control_function()->get_address(), dataOrAlarmMaskId, newSoftKeyMaskId);
									send_change_soft_key_mask_response(dataOrAlarmMaskId, newSoftKeyMaskId, get_bit(static_cast<std::uint8_t>(ChangeSoftKeyMaskErrorBit::AnyOtherError)), message.get_source_control_function());
								}
							}
							break;

							case VirtualTerminalObjectType::DataMask:
							{
								if (std::static_pointer_cast<DataMask>(targetMask)->change_soft_key_mask(newSoftKeyMaskId, *objectTree))
								{
									LOG_DEBUG("[VT Server]: Client %u change soft key mask command: data mask object %u to %u", managedWorkingSet->get_control_function()->get_address(), dataOrAlarmMaskId, newSoftKeyMaskId);
									send_change_soft_key_mask_response(dataOrAlarmMaskId, newSoftKeyMaskId, 0, message.get_source_control_function());

									if (activeWorkingSet == managedWorkingSet)
									{
										// The status' soft key mask field (G.2 bytes 5-6) tracks the mask the active
										// working set currently shows, so a change to a mask it does not show recomputes
										// the same value and moves nothing.
										refresh_active_mask_status_fields();
									}
									onChangeActiveSoftKeyMaskEventDispatcher.call(managedWorkingSet, dataOrAlarmMaskId, newSoftKeyMaskId);
									process_macro(targetMask, EventID::OnChangeSoftKeyMask, VirtualTerminalObjectType::DataMask, managedWorkingSet);
								}
								else
								{
									LOG_WARNING("[VT Server]: Client %u change soft key mask command: failed to set mask for data mask object %u to %u", managedWorkingSet->get_control_function()->get_address(), dataOrAlarmMaskId, newSoftKeyMaskId);
									send_change_soft_key_mask_response(dataOrAlarmMaskId, newSoftKeyMaskId, get_bit(static_cast<std::uint8_t>(ChangeSoftKeyMaskErrorBit::AnyOtherError)), message.get_source_control_function());
								}
							}
							break;

							default:
							{
								LOG_WARNING("[VT Server]: Client %u change soft key mask command: invalid object type for object %u", managedWorkingSet->get_control_function()->get_address(), dataOrAlarmMaskId);
								send_change_soft_key_mask_response(dataOrAlarmMaskId, newSoftKeyMaskId, get_bit(static_cast<std::uint8_t>(ChangeSoftKeyMaskErrorBit::AnyOtherError)), message.get_source_control_function());
							}
							break;
						}
					}
					else
					{
						LOG_WARNING("[VT Server]: Client %u change soft key mask command: invalid soft key object ID of %u", managedWorkingSet->get_control_function()->get_address(), newSoftKeyMaskId);
						send_change_soft_key_mask_response(dataOrAlarmMaskId, newSoftKeyMaskId, get_bit(static_cast<std::uint8_t>(ChangeSoftKeyMaskErrorBit::InvalidSoftKeyMaskObjectID)), message.get_source_control_function());
					}
				}
				else
				{
					LOG_WARNING("[VT Server]: Client %u change soft key mask command: invalid data mask or alarm mask object ID of %u", managedWorkingSet->get_control_function()->get_address(), dataOrAlarmMaskId);
					send_change_soft_key_mask_response(dataOrAlarmMaskId, newSoftKeyMaskId, get_bit(static_cast<std::uint8_t>(ChangeSoftKeyMaskErrorBit::InvalidDataOrAlarmMaskObjectID)), message.get_source_control_function());
				}
			}
			break;

			case Function::ChangeBackgroundColourCommand:
			{
				auto objectID = get_little_endian_uint16(data, 1);
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);
				std::uint8_t backgroundColour = data[3];

				if (nullptr != targetObject)
				{
					switch (targetObject->get_object_type())
					{
						case VirtualTerminalObjectType::AuxiliaryInputType2:
						case VirtualTerminalObjectType::WorkingSet:
						case VirtualTerminalObjectType::DataMask:
						case VirtualTerminalObjectType::AlarmMask:
						case VirtualTerminalObjectType::SoftKeyMask:
						case VirtualTerminalObjectType::Key:
						case VirtualTerminalObjectType::Button:
						case VirtualTerminalObjectType::InputNumber:
						case VirtualTerminalObjectType::InputBoolean:
						case VirtualTerminalObjectType::InputString:
						case VirtualTerminalObjectType::OutputString:
						case VirtualTerminalObjectType::OutputNumber:
						case VirtualTerminalObjectType::GraphicsContext:
						case VirtualTerminalObjectType::WindowMask:
						{
							targetObject->set_background_color(backgroundColour);
							LOG_DEBUG("[VT Server]: Client %u change background colour command: colour = %u", managedWorkingSet->get_control_function()->get_address(), objectID, backgroundColour);
							send_change_background_colour_response(objectID, 0, backgroundColour, message.get_source_control_function());
							process_macro(targetObject, EventID::OnChangeBackgroundColour, targetObject->get_object_type(), managedWorkingSet);
							dispatch_repaint(managedWorkingSet);
						}
						break;

						default:
						{
							LOG_WARNING("[VT Server]: Client %u change background colour command: invalid object type for object %u", managedWorkingSet->get_control_function()->get_address(), objectID);
							send_change_background_colour_response(objectID, get_bit(static_cast<std::uint8_t>(ChangeBackgroundColourErrorBit::AnyOtherError)), backgroundColour, message.get_source_control_function());
						}
						break;
					}
				}
				else
				{
					LOG_WARNING("[VT Server]: Client %u change background colour command: invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), objectID);
					send_change_background_colour_response(objectID, get_bit(static_cast<std::uint8_t>(ChangeBackgroundColourErrorBit::InvalidObjectID)), backgroundColour, message.get_source_control_function());
				}
			}
			break;

			case Function::ChangePriorityCommand:
			{
				auto objectID = get_little_endian_uint16(data, 1);
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);
				std::uint8_t newPriority = data[3];

				if (nullptr != targetObject)
				{
					if (VirtualTerminalObjectType::AlarmMask == targetObject->get_object_type())
					{
						if (newPriority <= static_cast<std::uint8_t>(AlarmMaskPriority::Low))
						{
							std::static_pointer_cast<AlarmMask>(targetObject)->set_mask_priority(static_cast<AlarmMask::Priority>(newPriority));
							send_change_priority_response(objectID, 0, newPriority, message.get_source_control_function());
							LOG_DEBUG("[VT Server]: Client %u change priority command: New Priority %u", managedWorkingSet->get_control_function()->get_address(), newPriority);
							process_macro(targetObject, EventID::OnChangePriority, VirtualTerminalObjectType::AlarmMask, managedWorkingSet);

							// The priority attribute is the first key 4.6.14 ranks alarms by, so changing it can
							// reorder which working set's alarm belongs on screen.
							apply_active_working_set_arbitration();
						}
						else
						{
							send_change_priority_response(objectID, get_bit(static_cast<std::uint8_t>(ChangePriorityErrorBit::InvalidPriority)), newPriority, message.get_source_control_function());
							LOG_WARNING("[VT Server]: Client %u change priority command: Invalid Priority %u. Must be 2 or less.", managedWorkingSet->get_control_function()->get_address(), newPriority);
						}
					}
					else
					{
						send_change_priority_response(objectID, get_bit(static_cast<std::uint8_t>(ChangePriorityErrorBit::AnyOtherError)), newPriority, message.get_source_control_function());
						LOG_WARNING("[VT Server]: Client %u change priority command: invalid object ID of %u - the object must be an alarm mask.", managedWorkingSet->get_control_function()->get_address(), objectID);
					}
				}
				else
				{
					send_change_priority_response(objectID, get_bit(static_cast<std::uint8_t>(ChangePriorityErrorBit::InvalidObjectID)), newPriority, message.get_source_control_function());
					LOG_WARNING("[VT Server]: Client %u change priority command: invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), objectID);
				}
			}
			break;

			case Function::SelectInputObjectCommand:
			{
				auto objectID = get_little_endian_uint16(data, 1);
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);

				if (nullptr != targetObject)
				{
					switch (targetObject->get_object_type())
					{
						case VirtualTerminalObjectType::Button:
						case VirtualTerminalObjectType::Key:
						{
							if (get_vt_version_byte(get_version()) > 3)
							{
								if (0 == data[3])
								{
									// 0 in Version 4+ means to activate the object for input
									managedWorkingSet->set_object_focus(objectID);
									managedWorkingSet->set_object_open_for_input(objectID);
									LOG_DEBUG("[VT Server]: Client %u select input object %u and open for input", managedWorkingSet->get_control_function()->get_address(), objectID);
									onFocusObjectEventDispatcher.call(managedWorkingSet, objectID, true);
									send_select_input_object_response(objectID, 0, NULL_OBJECT_ID == objectID ? SelectInputObjectResponse::ObjectIsNotSelectedOrIsNullOrError : SelectInputObjectResponse::ObjectIsOpenedForEdit, message.get_source_control_function());
									process_macro(targetObject, NULL_OBJECT_ID == objectID ? EventID::OnInputFieldDeselection : EventID::OnInputFieldSelection, targetObject->get_object_type(), managedWorkingSet);
								}
								else if (0xFF == data[3])
								{
									// This removes focus if the ID is NULL_OBJECT_ID, or sets focus if not
									managedWorkingSet->set_object_focus(objectID);

									// F.7 answers this option with ObjectIsSelected rather than ObjectIsOpenedForEdit,
									// so a selection ends any edit in progress and leaves no input field open.
									managedWorkingSet->set_object_open_for_input(NULL_OBJECT_ID);
									LOG_DEBUG("[VT Server]: Client %u select input object %u", managedWorkingSet->get_control_function()->get_address(), objectID);
									onFocusObjectEventDispatcher.call(managedWorkingSet, objectID, false);
									send_select_input_object_response(objectID, 0, NULL_OBJECT_ID == objectID ? SelectInputObjectResponse::ObjectIsNotSelectedOrIsNullOrError : SelectInputObjectResponse::ObjectIsSelected, message.get_source_control_function());
									process_macro(targetObject, NULL_OBJECT_ID == objectID ? EventID::OnInputFieldDeselection : EventID::OnInputFieldSelection, targetObject->get_object_type(), managedWorkingSet);
								}
								else
								{
									LOG_WARNING("[VT Server]: Client %u select input object command: Illegal option byte", managedWorkingSet->get_control_function()->get_address(), objectID);
									send_select_input_object_response(objectID, get_bit(static_cast<std::uint8_t>(SelectInputObjectErrorBit::InvalidOptionValue)), SelectInputObjectResponse::ObjectIsNotSelectedOrIsNullOrError, message.get_source_control_function());
								}
							}
							else
							{
								send_select_input_object_response(objectID, get_bit(static_cast<std::uint8_t>(SelectInputObjectErrorBit::AnyOtherError)), SelectInputObjectResponse::ObjectIsNotSelectedOrIsNullOrError, message.get_source_control_function());
								LOG_WARNING("[VT Server]: Client %u select input object command: buttons and keys can only be selected when the server is version 4 or higher.", managedWorkingSet->get_control_function()->get_address(), objectID);
							}
						}
						break;

						case VirtualTerminalObjectType::InputNumber:
						case VirtualTerminalObjectType::InputString:
						case VirtualTerminalObjectType::InputList:
						{
							if (0 == data[3])
							{
								// 0 in Version 4+ means to activate the object for input
								managedWorkingSet->set_object_focus(objectID);
								managedWorkingSet->set_object_open_for_input(objectID);
								LOG_DEBUG("[VT Server]: Client %u select input object %u and open for input", managedWorkingSet->get_control_function()->get_address(), objectID);
								onFocusObjectEventDispatcher.call(managedWorkingSet, objectID, true);
								send_select_input_object_response(objectID, 0, NULL_OBJECT_ID == objectID ? SelectInputObjectResponse::ObjectIsNotSelectedOrIsNullOrError : SelectInputObjectResponse::ObjectIsOpenedForEdit, message.get_source_control_function());
								process_macro(targetObject, NULL_OBJECT_ID == objectID ? EventID::OnInputFieldDeselection : EventID::OnInputFieldSelection, targetObject->get_object_type(), managedWorkingSet);
							}
							else if (0xFF == data[3])
							{
								// This removes focus if the ID is NULL_OBJECT_ID, or sets focus if not
								managedWorkingSet->set_object_focus(objectID);

								// F.7 answers this option with ObjectIsSelected rather than ObjectIsOpenedForEdit,
								// so a selection ends any edit in progress and leaves no input field open.
								managedWorkingSet->set_object_open_for_input(NULL_OBJECT_ID);
								LOG_DEBUG("[VT Server]: Client %u select input object %u", managedWorkingSet->get_control_function()->get_address(), objectID);
								onFocusObjectEventDispatcher.call(managedWorkingSet, objectID, false);
								send_select_input_object_response(objectID, 0, NULL_OBJECT_ID == objectID ? SelectInputObjectResponse::ObjectIsNotSelectedOrIsNullOrError : SelectInputObjectResponse::ObjectIsSelected, message.get_source_control_function());
								process_macro(targetObject, NULL_OBJECT_ID == objectID ? EventID::OnInputFieldDeselection : EventID::OnInputFieldSelection, targetObject->get_object_type(), managedWorkingSet);
							}
							else
							{
								LOG_WARNING("[VT Server]: Client %u select input object command: Illegal option byte", managedWorkingSet->get_control_function()->get_address(), objectID);
								send_select_input_object_response(objectID, get_bit(static_cast<std::uint8_t>(SelectInputObjectErrorBit::InvalidOptionValue)), SelectInputObjectResponse::ObjectIsNotSelectedOrIsNullOrError, message.get_source_control_function());
							}
						}
						break;

						default:
						{
							LOG_WARNING("[VT Server]: Client %u select input object command: invalid object type", managedWorkingSet->get_control_function()->get_address(), objectID);
							send_select_input_object_response(objectID, get_bit(static_cast<std::uint8_t>(SelectInputObjectErrorBit::AnyOtherError)), SelectInputObjectResponse::ObjectIsNotSelectedOrIsNullOrError, message.get_source_control_function());
						}
						break;
					}
				}
				else
				{
					send_select_input_object_response(objectID, get_bit(static_cast<std::uint8_t>(SelectInputObjectErrorBit::InvalidObjectID)), SelectInputObjectResponse::ObjectIsNotSelectedOrIsNullOrError, message.get_source_control_function());
					LOG_WARNING("[VT Server]: Client %u select input object command: invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), objectID);
				}
			}
			break;

			case Function::PreferredAssignmentCommand:
			{
				// Variable-length command (J.7.7), possibly reassembled from the transport protocol. Every field is
				// bounds-checked against the message length; a truncated/malformed buffer routes an empty list.
				std::vector<AuxiliaryPreferredAssignmentEntry> entries;
				const std::uint32_t dataLength = message.get_data_length();
				bool malformed = (dataLength < 2);

				if (!malformed)
				{
					const std::uint8_t numberOfInputUnits = message.get_uint8_at(1);
					std::uint32_t offset = 2;

					for (std::uint8_t unitIndex = 0; (unitIndex < numberOfInputUnits) && !malformed; unitIndex++)
					{
						// Each input unit header is NAME (8) + Model Identification Code (2) + number of functions (1).
						if (offset + 11 > dataLength)
						{
							malformed = true;
							break;
						}

						const std::uint64_t inputUnitName = message.get_uint64_at(offset);
						const std::uint16_t modelIdentificationCode = message.get_uint16_at(offset + 8);
						const std::uint8_t numberOfFunctions = message.get_uint8_at(offset + 10);
						offset += 11;

						for (std::uint8_t functionIndex = 0; functionIndex < numberOfFunctions; functionIndex++)
						{
							// Each function record is Function OID (2) + Input OID (2).
							if (offset + 4 > dataLength)
							{
								malformed = true;
								break;
							}

							AuxiliaryPreferredAssignmentEntry entry;
							entry.inputUnitName = inputUnitName;
							entry.modelIdentificationCode = modelIdentificationCode;
							entry.functionObjectId = message.get_uint16_at(offset);
							entry.inputObjectId = message.get_uint16_at(offset + 2);
							entries.push_back(entry);
							offset += 4;
						}
					}
				}

				if (malformed)
				{
					entries.clear();
					LOG_DEBUG("[VT Server]: Client %u sent a malformed Preferred Assignment command; routing an empty assignment list.", managedWorkingSet->get_control_function()->get_address());
				}
				on_auxiliary_preferred_assignment_received(managedWorkingSet, entries);
			}
			break;

			case Function::AuxiliaryInputTypeTwoMaintenanceMessage:
			{
				managedWorkingSet->set_auxiliary_input_maintenance_timestamp_ms(SystemTiming::get_timestamp_ms());

				if (message.get_data_length() >= 4)
				{
					const std::uint16_t modelIdentificationCode = message.get_uint16_at(1);
					const bool ready = (1 == message.get_uint8_at(3));
					on_auxiliary_input_maintenance_received(managedWorkingSet, modelIdentificationCode, ready);
				}
			}
			break;

			case Function::AuxiliaryAssignmentTypeTwoCommand:
			{
				// Mux 0x24 is shared: an incoming message at the VT is always the 8-byte response (J.7.6);
				// the 14-byte form is the VT's own outgoing command, so only the short form is a response.
				if ((message.get_data_length() >= 4) && (message.get_data_length() < 14))
				{
					const std::uint16_t functionObjectId = message.get_uint16_at(1);
					const std::uint8_t errorCode = message.get_uint8_at(3);
					on_auxiliary_assignment_response_received(managedWorkingSet, functionObjectId, errorCode);
				}
			}
			break;

			case Function::AuxiliaryInputStatusTypeTwoEnableCommand:
			{
				// An incoming message at the VT is the 8-byte enable response (J.7.12).
				if (message.get_data_length() >= 5)
				{
					const std::uint16_t inputObjectId = message.get_uint16_at(1);
					const std::uint8_t status = message.get_uint8_at(3);
					const std::uint8_t errorCode = message.get_uint8_at(4);
					on_auxiliary_input_status_enable_response_received(managedWorkingSet, inputObjectId, status, errorCode);
				}
			}
			break;

			case Function::AuxiliaryInputTypeTwoStatusMessage:
			{
				// In normal operation this status is broadcast for the assigned function working set;
				// in learn mode the input unit sends it destination-specific to the VT (J.7.9).
				// Whichever form reaches the server is surfaced to the subclass hook.
				if (message.get_data_length() >= 8)
				{
					const std::uint16_t inputObjectId = message.get_uint16_at(1);
					const std::uint16_t value1 = message.get_uint16_at(3);
					const std::uint16_t value2 = message.get_uint16_at(5);
					const std::uint8_t operatingState = message.get_uint8_at(7);
					on_auxiliary_input_status_received(managedWorkingSet, inputObjectId, value1, value2, operatingState);
				}
			}
			break;

			case Function::ExecuteMacroCommand:
			{
				auto objectID = static_cast<std::uint16_t>(data[1]);
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);

				if (nullptr != targetObject)
				{
					if (VirtualTerminalObjectType::Macro == targetObject->get_object_type())
					{
						if (std::static_pointer_cast<Macro>(targetObject)->get_are_command_packets_valid())
						{
							// 4.6.11.4 b/c/d: queue the macro, do not run it inline; the drain runs it (and
							// any it triggers) FIFO to completion within this bus command. The response is
							// sent AFTER the drain, so at bus level the suppression window is already closed
							// (depth 0) and the client's own Execute Macro response is not withheld -- only
							// the macro's contained commands, replayed inside the drain at depth 1, are
							// (4.6.11.4 f). A nested Execute Macro (this handler reached from a replayed
							// packet) is at depth 1 throughout, so its response is withheld and its drain
							// call is the re-entrant no-op.
							macroExecutionQueue.push_back({ objectID, managedWorkingSet });
							drain_macro_execution_queue();
							LOG_DEBUG("[VT Server]: Client %u execute macro command %u: completed.", managedWorkingSet->get_control_function()->get_address(), objectID);
							send_execute_macro_or_extended_macro_response(objectID, 0, message.get_source_control_function(), false);
						}
						else
						{
							LOG_ERROR("[VT Server]: Client %u execute macro command: failed. Macro contains invalid commands.", managedWorkingSet->get_control_function()->get_address(), objectID);
							send_execute_macro_or_extended_macro_response(objectID, get_bit(static_cast<std::uint8_t>(ExecuteMacroResponseErrorBit::AnyOtherError)), message.get_source_control_function(), false);
						}
					}
					else
					{
						LOG_WARNING("[VT Server]: Client %u execute macro command: object ID %u is not a macro!", managedWorkingSet->get_control_function()->get_address(), objectID);
						send_execute_macro_or_extended_macro_response(objectID, get_bit(static_cast<std::uint8_t>(ExecuteMacroResponseErrorBit::ObjectIsNotAMacro)), message.get_source_control_function(), false);
					}
				}
				else
				{
					LOG_WARNING("[VT Server]: Client %u execute macro command: invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), objectID);
					send_execute_macro_or_extended_macro_response(objectID, get_bit(static_cast<std::uint8_t>(ExecuteMacroResponseErrorBit::ObjectDoesntExist)), message.get_source_control_function(), false);
				}
			}
			break;

			case Function::ExecuteExtendedMacroCommand:
			{
				auto objectID = get_little_endian_uint16(data, 1);
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);

				if (nullptr != targetObject)
				{
					if (VirtualTerminalObjectType::Macro == targetObject->get_object_type())
					{
						if (std::static_pointer_cast<Macro>(targetObject)->get_are_command_packets_valid())
						{
							// See the ExecuteMacroCommand case: queue then drain, response after the drain
							// so it is not suppressed at bus level (4.6.11.4 b/c/d/f).
							macroExecutionQueue.push_back({ objectID, managedWorkingSet });
							drain_macro_execution_queue();
							LOG_DEBUG("[VT Server]: Client %u execute extended macro command %u: completed.", managedWorkingSet->get_control_function()->get_address(), objectID);
							send_execute_macro_or_extended_macro_response(objectID, 0, message.get_source_control_function(), true);
						}
						else
						{
							LOG_ERROR("[VT Server]: Client %u execute extended macro command: failed. Macro contains invalid commands.", managedWorkingSet->get_control_function()->get_address(), objectID);
							send_execute_macro_or_extended_macro_response(objectID, get_bit(static_cast<std::uint8_t>(ExecuteMacroResponseErrorBit::AnyOtherError)), message.get_source_control_function(), true);
						}
					}
					else
					{
						LOG_WARNING("[VT Server]: Client %u execute extended macro command: object ID %u is not a macro!", managedWorkingSet->get_control_function()->get_address(), objectID);
						send_execute_macro_or_extended_macro_response(objectID, get_bit(static_cast<std::uint8_t>(ExecuteMacroResponseErrorBit::ObjectIsNotAMacro)), message.get_source_control_function(), true);
					}
				}
				else
				{
					LOG_WARNING("[VT Server]: Client %u execute extended macro command: invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), objectID);
					send_execute_macro_or_extended_macro_response(objectID, get_bit(static_cast<std::uint8_t>(ExecuteMacroResponseErrorBit::ObjectDoesntExist)), message.get_source_control_function(), true);
				}
			}
			break;

			case Function::DeleteObjectPoolCommand:
			{
				LOG_INFO("[VT Server]: Client %u requests deletion of object pool from volatile memory.", managedWorkingSet->get_control_function()->get_address());

				// The parse worker owns the staging tree while it runs, so a pool cannot be deleted out
				// from under one. This is checked before the pool is deactivated so that a refusal leaves
				// the working set exactly as it was rather than deactivating a pool it then declines to
				// delete. F.45 bit 0 is the answer for it, and a client only reaches this by sending
				// Delete Object Pool before the End of Object Pool response its upload is still waiting
				// on; it may simply ask again.
				if (managedWorkingSet->is_object_pool_parse_outstanding())
				{
					LOG_WARNING("[VT Server]: Client %u object pool cannot be deleted while its object pool is being parsed.", managedWorkingSet->get_control_function()->get_address());
					send_delete_object_pool_response(get_bit(static_cast<std::uint8_t>(DeleteObjectPoolErrorBit::DeletionError)), message.get_source_control_function());
				}
				else if (delete_object_pool(managedWorkingSet->get_control_function()->get_NAME()) &&
				         managedWorkingSet->reset_object_pool())
				{
					// The pool is deactivated by the server implementation above and erased from volatile
					// storage here. ISO 11783-6 F.44 is a deletion, not a deactivation: the parsed objects
					// and the raw IOP bytes both go, and with them everything the working set derived from
					// the pool. The working set itself stays connected and may upload a new pool at once.
					LOG_INFO("[VT Server]: Client %u object pool has been deleted from volatile memory.", managedWorkingSet->get_control_function()->get_address());
					send_delete_object_pool_response(0, message.get_source_control_function());

					// The screen is not left blank because the working set holding it deleted its pool.
					// A working set with no pool resolves no active mask and so cannot be selected, which
					// makes this hand the display to whichever survivor 4.6.14 ranks highest. Without it
					// the screen stays blank until some other working set happens to change its mask.
					apply_active_working_set_arbitration();
				}
				else
				{
					LOG_ERROR("[VT Server]: Client %u object pool failed to be deleted.", managedWorkingSet->get_control_function()->get_address());
					send_delete_object_pool_response(get_bit(static_cast<std::uint8_t>(DeleteObjectPoolErrorBit::DeletionError)), message.get_source_control_function());
				}
			}
			break;

			case Function::ChangePolygonPointCommand:
			{
				auto objectID = get_little_endian_uint16(data, 1);
				const std::uint8_t polygonPointIndex = data[3];
				const std::uint16_t newXValue = get_little_endian_uint16(data, 4);
				const std::uint16_t newYValue = get_little_endian_uint16(data, 6);
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);

				if (nullptr != targetObject)
				{
					if (VirtualTerminalObjectType::OutputPolygon == targetObject->get_object_type())
					{
						auto polygon = std::static_pointer_cast<OutputPolygon>(targetObject);

						if (polygon->change_point(polygonPointIndex, newXValue, newYValue))
						{
							LOG_DEBUG("[VT Server]: Client %u change polygon id %u point index %u. X = %u, Y = %u", managedWorkingSet->get_control_function()->get_address(), objectID, polygonPointIndex, newXValue, newYValue);
							send_change_polygon_point_response(objectID, 0, message.get_source_control_function());
						}
						else
						{
							LOG_WARNING("[VT Server]: Client %u change polygon point: the point index of %u is not valid for object %u", managedWorkingSet->get_control_function()->get_address(), polygonPointIndex, objectID);
							send_change_polygon_point_response(objectID, get_bit(static_cast<std::uint8_t>(ChangePolygonPointErrorBit::InvalidPointIndex)), message.get_source_control_function());
						}
					}
					else
					{
						LOG_WARNING("[VT Server]: Client %u change polygon point: object id %u is not an output polygon", managedWorkingSet->get_control_function()->get_address(), objectID);
						send_change_polygon_point_response(objectID, get_bit(static_cast<std::uint8_t>(ChangePolygonPointErrorBit::AnyOtherError)), message.get_source_control_function());
					}
				}
				else
				{
					LOG_WARNING("[VT Server]: Client %u change polygon point: invalid object ID of %u", managedWorkingSet->get_control_function()->get_address(), objectID);
					send_change_polygon_point_response(objectID, get_bit(static_cast<std::uint8_t>(ChangePolygonPointErrorBit::InvalidObjectID)), message.get_source_control_function());
				}
			}
			break;

			case Function::ChangeEndPointCommand:
			{
				auto objectID = get_little_endian_uint16(data, 1);
				const std::uint16_t newWidth = get_little_endian_uint16(data, 3);
				const std::uint16_t newHeight = get_little_endian_uint16(data, 5);
				const std::uint8_t lineDirection = data[7];
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);

				// This chain validates both the target and the direction before the final branch writes
				// any attribute, so a rejected command leaves the output line exactly as it was.
				if ((nullptr == targetObject) || (VirtualTerminalObjectType::OutputLine != targetObject->get_object_type()))
				{
					LOG_WARNING("[VT Server]: Client %u change end point: object id %u is not an output line in this pool", managedWorkingSet->get_control_function()->get_address(), objectID);
					send_change_end_point_response(objectID, get_bit(static_cast<std::uint8_t>(ChangeEndPointErrorBit::InvalidObjectID)), managedWorkingSet->get_control_function());
				}
				else if (lineDirection > 1)
				{
					LOG_WARNING("[VT Server]: Client %u change end point: line direction %u is not valid for object %u", managedWorkingSet->get_control_function()->get_address(), lineDirection, objectID);
					send_change_end_point_response(objectID, get_bit(static_cast<std::uint8_t>(ChangeEndPointErrorBit::InvalidLineDirection)), managedWorkingSet->get_control_function());
				}
				else
				{
					auto line = std::static_pointer_cast<OutputLine>(targetObject);

					line->set_width(newWidth);
					line->set_height(newHeight);
					line->set_line_direction(static_cast<OutputLine::LineDirection>(lineDirection));
					send_change_end_point_response(objectID, 0, managedWorkingSet->get_control_function());
					dispatch_repaint(managedWorkingSet);
					process_macro(targetObject, EventID::OnChangeEndpoint, targetObject->get_object_type(), managedWorkingSet);
					LOG_DEBUG("[VT Server]: Client %u change end point command: Object: %u, Width: %u, Height: %u, Direction: %u", managedWorkingSet->get_control_function()->get_address(), objectID, newWidth, newHeight, lineDirection);
				}
			}
			break;

			case Function::ChangePolygonScaleCommand:
			{
				auto objectID = get_little_endian_uint16(data, 1);
				const std::uint16_t newWidth = get_little_endian_uint16(data, 3);
				const std::uint16_t newHeight = get_little_endian_uint16(data, 5);
				auto targetObject = managedWorkingSet->get_object_by_id(objectID);

				if ((nullptr == targetObject) || (VirtualTerminalObjectType::OutputPolygon != targetObject->get_object_type()))
				{
					LOG_WARNING("[VT Server]: Client %u change polygon scale: object id %u is not an output polygon in this pool", managedWorkingSet->get_control_function()->get_address(), objectID);
					send_change_polygon_scale_response(objectID, newWidth, newHeight, get_bit(static_cast<std::uint8_t>(ChangePolygonScaleErrorBit::InvalidObjectID)), managedWorkingSet->get_control_function());
				}
				else
				{
					auto polygon = std::static_pointer_cast<OutputPolygon>(targetObject);

					// The rescale in F.54 divides by the enclosing area the points were authored against,
					// so both old dimensions must be read before either attribute takes its new value.
					const std::uint16_t oldWidth = polygon->get_width();
					const std::uint16_t oldHeight = polygon->get_height();

					for (std::uint8_t i = 0; i < polygon->get_number_of_points(); i++)
					{
						const OutputPolygon::PolygonPoint point = polygon->get_point(i);
						std::uint16_t scaledX = point.xValue;
						std::uint16_t scaledY = point.yValue;

						// PolygonPoint stores coordinates unsigned, so F.54's negative-coordinate branch cannot
						// arise here and only the positive branch, which rounds to nearest, is implemented.
						// The products reach 65535 * 65535, which overflows the signed 32 bit math F.54 names,
						// so they are formed in 64 bits and the quotient is clamped back into the point's range.
						// An enclosing area of zero has no scale factor to apply, so that axis keeps the
						// coordinates it was authored with rather than being divided by zero. F.54 defines no
						// error for a degenerate enclosing area and F.55 carries no bit for one, so the
						// command still reports success.
						if (0 != oldWidth)
						{
							const std::int64_t newX = ((static_cast<std::int64_t>(point.xValue) * newWidth) + (oldWidth / 2)) / oldWidth;
							scaledX = static_cast<std::uint16_t>((newX > 65535) ? 65535 : newX);
						}

						if (0 != oldHeight)
						{
							const std::int64_t newY = ((static_cast<std::int64_t>(point.yValue) * newHeight) + (oldHeight / 2)) / oldHeight;
							scaledY = static_cast<std::uint16_t>((newY > 65535) ? 65535 : newY);
						}

						polygon->change_point(i, scaledX, scaledY);
					}

					polygon->set_width(newWidth);
					polygon->set_height(newHeight);
					send_change_polygon_scale_response(objectID, newWidth, newHeight, 0, managedWorkingSet->get_control_function());

					// Table B.32 maps this command to the On Refresh event, which the EventID enum documents as
					// having no associated event ID because macros cannot be attached to it, so a repaint is the
					// whole required behaviour and no macro is run.
					dispatch_repaint(managedWorkingSet);
					LOG_DEBUG("[VT Server]: Client %u change polygon scale command: Object: %u, Width: %u, Height: %u", managedWorkingSet->get_control_function()->get_address(), objectID, newWidth, newHeight);
				}
			}
			break;

			case Function::ESCCommand:
			{
				const std::uint16_t openObjectID = managedWorkingSet->get_object_open_for_input();

				if (NULL_OBJECT_ID == openObjectID)
				{
					// F.9 defines bytes 2-3 only when no error is reported, so the standard's own
					// "no object" sentinel is what this branch names.
					send_esc_response(NULL_OBJECT_ID, get_bit(static_cast<std::uint8_t>(ESCErrorBit::NoInputFieldIsOpenForInput)), managedWorkingSet->get_control_function());
					LOG_DEBUG("[VT Server]: Client %u ESC command: no input field is open for input, ESC ignored", managedWorkingSet->get_control_function()->get_address());
				}
				else
				{
					// must be cleared before process_macro: Select Input Object is an allowed macro
					// command, so an OnESC macro may re-open an object for input, and that re-open has
					// to survive this handler rather than being cleared after it.
					managedWorkingSet->set_object_open_for_input(NULL_OBJECT_ID);
					send_esc_response(openObjectID, 0, managedWorkingSet->get_control_function());
					LOG_DEBUG("[VT Server]: Client %u ESC command: input aborted on object %u", managedWorkingSet->get_control_function()->get_address(), openObjectID);

					auto targetObject = managedWorkingSet->get_object_by_id(openObjectID);

					// A runtime object pool update can replace whatever occupies this object ID.
					if (nullptr != targetObject)
					{
						process_macro(targetObject, EventID::OnESC, targetObject->get_object_type(), managedWorkingSet);
					}
				}
			}
			break;

			case Function::LockUnlockMaskCommand:
			{
				// F.46 byte 2 selects between the two operations this command performs.
				constexpr std::uint8_t UNLOCK_MASK = 0;
				constexpr std::uint8_t LOCK_MASK = 1;

				const std::uint8_t command = data[1];
				const std::uint16_t objectID = get_little_endian_uint16(data, 2);
				const std::uint16_t timeout_ms = get_little_endian_uint16(data, 4);

				// F.46 bytes 3-4 must name the mask the operator can actually see, which is the active
				// working set's own active mask. A working set that is not the active one shows nothing,
				// so its lock target can never match.
				std::uint16_t visibleMaskObjectID = NULL_OBJECT_ID;

				if (activeWorkingSet == managedWorkingSet)
				{
					auto workingSetObject = managedWorkingSet->get_working_set_object();

					if (nullptr != workingSetObject)
					{
						visibleMaskObjectID = std::static_pointer_cast<WorkingSet>(workingSetObject)->get_active_mask();
					}
				}

				const bool namesTheVisibleMask = (NULL_OBJECT_ID != visibleMaskObjectID) && (objectID == visibleMaskObjectID);
				const bool isLocked = (NULL_OBJECT_ID != managedWorkingSet->get_mask_lock_object_id());

				if (LOCK_MASK == command)
				{
					if (!namesTheVisibleMask)
					{
						send_lock_unlock_mask_response(command, get_bit(static_cast<std::uint8_t>(LockUnlockMaskErrorBit::CommandIgnoredNoMaskVisibleOrObjectIDMismatch)), false, managedWorkingSet->get_control_function());
						LOG_WARNING("[VT Server]: Client %u lock mask command ignored: object %u is not the visible mask", managedWorkingSet->get_control_function()->get_address(), objectID);
					}
					else if (is_any_alarm_mask_active())
					{
						// F.46 rejects the lock when an Alarm Mask from any working set is active and is in
						// the same display area. A single display area is the only topology this server
						// supports, so every active Alarm Mask shares the area with the mask being locked.
						send_lock_unlock_mask_response(command, get_bit(static_cast<std::uint8_t>(LockUnlockMaskErrorBit::LockIgnoredAlarmMaskIsActive)), false, managedWorkingSet->get_control_function());
						LOG_WARNING("[VT Server]: Client %u lock mask command ignored: an alarm mask is active", managedWorkingSet->get_control_function()->get_address());
					}
					else if (isLocked)
					{
						send_lock_unlock_mask_response(command, get_bit(static_cast<std::uint8_t>(LockUnlockMaskErrorBit::LockIgnoredAlreadyLocked)), false, managedWorkingSet->get_control_function());
						LOG_WARNING("[VT Server]: Client %u lock mask command ignored: mask %u is already locked", managedWorkingSet->get_control_function()->get_address(), objectID);
					}
					else
					{
						managedWorkingSet->set_mask_lock(objectID, timeout_ms, SystemTiming::get_timestamp_ms(), {});
						send_lock_unlock_mask_response(command, 0, false, managedWorkingSet->get_control_function());
						LOG_DEBUG("[VT Server]: Client %u locked mask %u with a timeout of %u ms", managedWorkingSet->get_control_function()->get_address(), objectID, timeout_ms);
					}
				}
				else if (UNLOCK_MASK == command)
				{
					if (!namesTheVisibleMask)
					{
						// F.46 answers an unlock aimed at a hidden mask immediately and reports it ignored.
						send_lock_unlock_mask_response(command, get_bit(static_cast<std::uint8_t>(LockUnlockMaskErrorBit::CommandIgnoredNoMaskVisibleOrObjectIDMismatch)), false, managedWorkingSet->get_control_function());
						LOG_WARNING("[VT Server]: Client %u unlock mask command ignored: object %u is not the visible mask", managedWorkingSet->get_control_function()->get_address(), objectID);
					}
					else if (!isLocked)
					{
						send_lock_unlock_mask_response(command, get_bit(static_cast<std::uint8_t>(LockUnlockMaskErrorBit::UnlockIgnoredNotLocked)), false, managedWorkingSet->get_control_function());
						LOG_WARNING("[VT Server]: Client %u unlock mask command ignored: mask %u is not locked", managedWorkingSet->get_control_function()->get_address(), objectID);
					}
					else
					{
						// must clear the lock before the repaint, and repaint before the response: F.46
						// forbids answering the unlock until the mask has been completely refreshed, and
						// dispatch_repaint withholds that refresh for as long as the lock is held. The
						// repaint runs synchronously on this thread, so it has completed by the time the
						// response reaches the bus.
						managedWorkingSet->set_mask_lock(NULL_OBJECT_ID, 0, 0, {});
						dispatch_repaint(managedWorkingSet);
						send_lock_unlock_mask_response(command, 0, false, managedWorkingSet->get_control_function());
						LOG_DEBUG("[VT Server]: Client %u unlocked mask %u", managedWorkingSet->get_control_function()->get_address(), objectID);
					}
				}
				else
				{
					send_lock_unlock_mask_response(command, get_bit(static_cast<std::uint8_t>(LockUnlockMaskErrorBit::AnyOtherError)), false, managedWorkingSet->get_control_function());
					LOG_WARNING("[VT Server]: Client %u lock/unlock mask command has an invalid command byte of %u", managedWorkingSet->get_control_function()->get_address(), command);
				}
			}
			break;

			case Function::ChangeObjectLabelCommand:
			{
				const std::uint16_t objectID = get_little_endian_uint16(data, 1);
				const std::uint16_t stringVariableID = get_little_endian_uint16(data, 3);
				const std::uint8_t fontType = data[5];
				const std::uint16_t graphicObjectID = get_little_endian_uint16(data, 6);

				// F.51's byte 2 is a bitfield rather than a single error code, so every problem the command
				// has is accumulated and reported at once instead of returning on the first one found.
				std::uint8_t errorBitfield = 0;

				// Every lookup here goes through one snapshot of the object tree rather than repeated
				// get_object_by_id calls. This command carries three object IDs a working set may get
				// wrong, and taking them all from the same snapshot means they are all answered against
				// the same pool even if a run-time pool update publishes a new one part-way through.
				const auto objectTree = managedWorkingSet->get_object_tree();

				auto find_object = [&objectTree](std::uint16_t idToFind) -> std::shared_ptr<VTObject> {
					auto foundObject = objectTree->find(idToFind);
					return ((objectTree->end() == foundObject) ? nullptr : foundObject->second);
				};

				std::shared_ptr<ObjectLabelReferenceList> labelReferenceList;

				for (const auto &currentObject : *objectTree)
				{
					if ((nullptr != currentObject.second) &&
					    (VirtualTerminalObjectType::ObjectLabelRefrenceList == currentObject.second->get_object_type()))
					{
						labelReferenceList = std::static_pointer_cast<ObjectLabelReferenceList>(currentObject.second);
						break;
					}
				}

				if (nullptr == labelReferenceList)
				{
					errorBitfield |= get_bit(static_cast<std::uint8_t>(ChangeObjectLabelErrorBit::NoObjectLabelReferenceListInPool));
				}

				// F.50 names an object to associate a label with, so the object has to exist and the list
				// has to already carry an entry for it. A pool with no list at all is reported by bit 3
				// alone: the object ID it names is not made invalid by the list being absent.
				ObjectLabelReferenceList::ObjectLabel existingLabel{};

				if ((nullptr == find_object(objectID)) ||
				    ((nullptr != labelReferenceList) && (!labelReferenceList->get_label(objectID, existingLabel))))
				{
					errorBitfield |= get_bit(static_cast<std::uint8_t>(ChangeObjectLabelErrorBit::InvalidObjectID));
				}

				if (NULL_OBJECT_ID != stringVariableID)
				{
					auto stringVariableObject = find_object(stringVariableID);

					if ((nullptr == stringVariableObject) ||
					    (VirtualTerminalObjectType::StringVariable != stringVariableObject->get_object_type()))
					{
						errorBitfield |= get_bit(static_cast<std::uint8_t>(ChangeObjectLabelErrorBit::InvalidStringVariableObjectID));
					}

					// F.50 ignores the font type when the String Variable reference is NULL, so it is only
					// checked when a string was supplied. The accepted values are the ones the object pool
					// parser accepts for a Font Attributes object.
					if ((fontType > static_cast<std::uint8_t>(FontAttributes::FontType::ISO8859_7)) ||
					    (fontType == static_cast<std::uint8_t>(FontAttributes::FontType::Reserved_1)) ||
					    (fontType == static_cast<std::uint8_t>(FontAttributes::FontType::Reserved_2)))
					{
						errorBitfield |= get_bit(static_cast<std::uint8_t>(ChangeObjectLabelErrorBit::InvalidFontType));
					}
				}

				if ((NULL_OBJECT_ID != graphicObjectID) &&
				    (nullptr == find_object(graphicObjectID)))
				{
					errorBitfield |= get_bit(static_cast<std::uint8_t>(ChangeObjectLabelErrorBit::DesignatorReferencesInvalidObjects));
				}

				if (0 == errorBitfield)
				{
					labelReferenceList->set_label(objectID, stringVariableID, fontType, graphicObjectID);
					send_change_object_label_response(0, managedWorkingSet->get_control_function());
					dispatch_repaint(managedWorkingSet);
					LOG_DEBUG("[VT Server]: Client %u change object label command: Object: %u, String Variable: %u, Font: %u, Graphic: %u", managedWorkingSet->get_control_function()->get_address(), objectID, stringVariableID, fontType, graphicObjectID);
				}
				else
				{
					send_change_object_label_response(errorBitfield, managedWorkingSet->get_control_function());
					LOG_WARNING("[VT Server]: Client %u change object label command for object %u was rejected with an error bitfield of %u", managedWorkingSet->get_control_function()->get_address(), objectID, errorBitfield);
				}
			}
			break;

			case Function::ControlAudioSignalCommand:
			{
				send_audio_signal_successful(message.get_source_control_function());
			}
			break;

			case Function::SetAudioVolumeCommand:
			{
				send_audio_volume_response(message.get_source_control_function());
			}
			break;

			case Function::IdentifyVTMessage:
			{
				identify_vt();
			}
			break;

			case Function::ScreenCapture:
			{
				screen_capture(data[1], data[2], message.get_source_control_function());
			}
			break;

			case Function::ButtonActivationMessage:
			case Function::SoftKeyActivationMessage:
			case Function::PointingEventMessage:
			case Function::VTSelectInputObjectMessage:
			case Function::VTESCMessage:
			case Function::VTChangeNumericValueMessage:
			case Function::VTChangeActiveMaskMessage:
			case Function::VTChangeStringValueMessage:
			case Function::VTControlAudioSignalTerminationMessage:
			{
				// Todo, do something with the responses
			}
			break;

			default:
				functionSupported = false;
				break;
		}
		return functionSupported;
	}

	void VirtualTerminalServer::process_rx_message(const CANMessage &message, void *parent)
	{
		auto parentServer = static_cast<VirtualTerminalServer *>(parent);
		if ((nullptr != message.get_source_control_function()) &&
		    (nullptr != parentServer) &&
		    ((CAN_DATA_LENGTH <= message.get_data_length()) ||
		     ((message.get_data_length() > 5) && (static_cast<std::uint8_t>(Function::ChangeStringValueCommand) == message.get_uint8_at(0))))) // Technically this message can be 6 bytes
		{
			// The macro execution budget is per BUS command, and the suppression window is what makes it
			// so: the drain sets macroExecutionDepth to 1 for the whole run, so every macro-replayed
			// message re-enters here with a non-zero window and does not reset the budget. Only a message
			// that actually arrived from the bus is at window zero.
			if (0 == parentServer->macroExecutionDepth)
			{
				parentServer->macroExecutionsThisCommand = 0;
				parentServer->macroExecutionBudgetExhausted = false;
			}

			if (static_cast<std::uint32_t>(CANLibParameterGroupNumber::ECUtoVirtualTerminal) == message.get_identifier().get_parameter_group_number())
			{
				bool responseSent = parentServer->process_stateless_messages(message);
				bool isManaged = parentServer->check_if_source_is_managed(message);
				bool functionSupported = responseSent;
				if (isManaged)
				{
					for (const auto &cf : parentServer->managedWorkingSetList)
					{
						if (cf->get_control_function() == message.get_source_control_function())
						{
							functionSupported = parentServer->process_connection_dependent_messages(message, cf) || functionSupported;
						}
					}
					if (!functionSupported)
					{
						// A connected working set sent a VT function this VT does not support; reply with the
						// Unsupported VT Function message echoing the offending function code (ISO 11783-6 F.67).
						parentServer->send_unsupported_vt_function(message.get_uint8_at(0), message.get_source_control_function());
					}
				}
				else if (!responseSent)
				{
					// Whomever this is has probably timed out. Send them a NACK
					LOG_WARNING("[VT Server]: Received a non-status message from a client at address %u, but they are not connected to this VT.", message.get_identifier().get_source_address());
					parentServer->send_acknowledgement(AcknowledgementType::Negative, static_cast<std::uint32_t>(CANLibParameterGroupNumber::ECUtoVirtualTerminal), parentServer->get_internal_control_function(), message.get_source_control_function());
				}
			}
		}
	}

	bool VirtualTerminalServer::send_acknowledgement(AcknowledgementType type, std::uint32_t parameterGroupNumber, std::shared_ptr<InternalControlFunction> source, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if ((nullptr != source) && (nullptr != destination))
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer;

			buffer[0] = static_cast<std::uint8_t>(type);
			buffer[1] = 0xFF;
			buffer[2] = 0xFF;
			buffer[3] = 0xFF;
			buffer[4] = destination->get_address();
			buffer[5] = get_byte(parameterGroupNumber, 0);
			buffer[6] = get_byte(parameterGroupNumber, 1);
			buffer[7] = get_byte(parameterGroupNumber, 2);

			retVal = CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::Acknowledge),
			                                                        buffer.data(),
			                                                        CAN_DATA_LENGTH,
			                                                        source,
			                                                        nullptr,
			                                                        get_priority());
		}
		return retVal;
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

	bool VirtualTerminalServer::send_change_active_mask_response(std::uint16_t newMaskObjectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::ChangeActiveMaskCommand),
				get_low_byte(newMaskObjectID),
				get_high_byte(newMaskObjectID),
				errorBitfield,
				0xFF,
				0xFF,
				0xFF,
				0xFF
			};

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_unsupported_vt_function(std::uint8_t unsupportedFunctionCode, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;
		if (nullptr != destination)
		{
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::UnsupportedVTFunctionMessage),
				unsupportedFunctionCode,
				0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
			};
			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_select_colour_map_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::SelectColourMapCommand),
				get_low_byte(objectID),
				get_high_byte(objectID),
				errorBitfield,
				0xFF,
				0xFF,
				0xFF,
				0xFF
			};

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_auxiliary_capabilities_response(std::uint8_t requestType, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			// A distinct Set Information record for each (Function attribute, Assigned attribute) pair within a unit.
			struct SetInfo
			{
				std::uint8_t functionAttribute;
				std::uint8_t assignedAttribute;
				std::uint16_t instanceCount;
			};

			// Assigned attribute bit0: 0 = auxiliary input, 1 = auxiliary function. Bit1 (input assigned) stays 0
			// until an assignment engine exists.
			VirtualTerminalObjectType targetObjectType = VirtualTerminalObjectType::AuxiliaryFunctionType2;
			std::uint8_t assignedAttributeBase = 0x01;
			bool validRequestType = true;

			if (1 == requestType)
			{
				targetObjectType = VirtualTerminalObjectType::AuxiliaryFunctionType2;
				assignedAttributeBase = 0x01;
			}
			else if (0 == requestType)
			{
				targetObjectType = VirtualTerminalObjectType::AuxiliaryInputType2;
				assignedAttributeBase = 0x00;
			}
			else
			{
				// An unknown request type yields a well-formed response reporting zero units.
				validRequestType = false;
			}

			std::vector<std::uint8_t> payload = { static_cast<std::uint8_t>(Function::AuxiliaryCapabilitiesRequest) };
			payload.push_back(0); // Number of Auxiliary Units, filled in after the loop

			std::size_t unitCount = 0;

			if (validRequestType)
			{
				for (const auto &ws : managedWorkingSetList)
				{
					if ((nullptr == ws) || (nullptr == ws->get_control_function()))
					{
						continue;
					}

					std::vector<SetInfo> sets;
					// The snapshot is a named local because a range-for does not extend the lifetime of a
					// temporary the range expression was dereferenced from: iterating *ws->get_object_tree()
					// directly would walk a tree whose last owner died at the end of the range expression.
					const auto objectTree = ws->get_object_tree();
					for (const auto &treeEntry : *objectTree)
					{
						const auto &object = treeEntry.second;
						if ((nullptr == object) || (targetObjectType != object->get_object_type()))
						{
							continue;
						}

						// AID 2 (FunctionAttributes) returns the whole Function Attributes byte for both aux Type 2 objects.
						std::uint32_t raw = 0;
						object->get_attribute(static_cast<std::uint8_t>(AuxiliaryFunctionType2::AttributeName::FunctionAttributes), raw);
						const std::uint8_t functionAttribute = static_cast<std::uint8_t>(raw & 0xFF);
						const std::uint8_t assignedAttribute = assignedAttributeBase;

						bool merged = false;
						for (auto &set : sets)
						{
							if ((set.functionAttribute == functionAttribute) && (set.assignedAttribute == assignedAttribute))
							{
								if (set.instanceCount < 0xFFFF)
								{
									set.instanceCount++;
								}
								merged = true;
								break;
							}
						}
						if (!merged)
						{
							sets.push_back({ functionAttribute, assignedAttribute, 1 });
						}
					}

					if (sets.empty())
					{
						// Only units that actually have aux objects of the requested kind are reported.
						continue;
					}

					const std::uint64_t fullName = ws->get_control_function()->get_NAME().get_full_name();
					for (std::uint8_t nameByte = 0; nameByte < 8; nameByte++)
					{
						payload.push_back(static_cast<std::uint8_t>((fullName >> (8U * nameByte)) & 0xFFU));
					}

					const std::uint8_t numberOfSets = (sets.size() > 255) ? 255 : static_cast<std::uint8_t>(sets.size());
					payload.push_back(numberOfSets);
					for (std::uint8_t setIndex = 0; setIndex < numberOfSets; setIndex++)
					{
						const SetInfo &set = sets[setIndex];
						payload.push_back((set.instanceCount > 255) ? 255 : static_cast<std::uint8_t>(set.instanceCount));
						payload.push_back(set.functionAttribute);
						payload.push_back(set.assignedAttribute);
					}

					unitCount++;
				}
			}

			payload[1] = (unitCount > 255) ? 255 : static_cast<std::uint8_t>(unitCount);

			// Pass the true payload size so responses larger than a single frame use the transport protocol.
			retVal = send_response(payload.data(), static_cast<std::uint32_t>(payload.size()), destination);
		}
		return retVal;
	}

	void VirtualTerminalServer::on_auxiliary_preferred_assignment_received(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> functionWorkingSet, const std::vector<AuxiliaryPreferredAssignmentEntry> &entries)
	{
		(void)functionWorkingSet;
		(void)entries;
	}

	void VirtualTerminalServer::on_auxiliary_input_maintenance_received(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> inputWorkingSet, std::uint16_t modelIdentificationCode, bool ready)
	{
		(void)inputWorkingSet;
		(void)modelIdentificationCode;
		(void)ready;
	}

	void VirtualTerminalServer::on_auxiliary_assignment_response_received(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> functionWorkingSet, std::uint16_t functionObjectId, std::uint8_t errorCode)
	{
		(void)functionWorkingSet;
		(void)functionObjectId;
		(void)errorCode;
	}

	void VirtualTerminalServer::on_auxiliary_input_status_enable_response_received(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> inputWorkingSet, std::uint16_t inputObjectId, std::uint8_t status, std::uint8_t errorCode)
	{
		(void)inputWorkingSet;
		(void)inputObjectId;
		(void)status;
		(void)errorCode;
	}

	void VirtualTerminalServer::on_auxiliary_input_status_received(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> inputWorkingSet, std::uint16_t inputObjectId, std::uint16_t value1, std::uint16_t value2, std::uint8_t operatingState)
	{
		(void)inputWorkingSet;
		(void)inputObjectId;
		(void)value1;
		(void)value2;
		(void)operatingState;
	}

	void VirtualTerminalServer::set_auxiliary_learn_mode_active(bool active)
	{
		if (active != auxiliaryInputLearnModeActive)
		{
			auxiliaryInputLearnModeActive = active;
			// A learn-mode transition changes ISO status byte 7 bit 6, which G.2 lists as an on-change
			// trigger, so it is announced promptly rather than at the next 1 Hz tick -- subject to the
			// five-per-second ceiling update() enforces.
			mark_status_message_changed();
		}
	}

	bool VirtualTerminalServer::send_preferred_assignment_response(std::uint8_t errorBits, std::uint16_t faultyFunctionObjectId, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::PreferredAssignmentCommand),
				errorBits,
				get_low_byte(faultyFunctionObjectId),
				get_high_byte(faultyFunctionObjectId),
				0xFF,
				0xFF,
				0xFF,
				0xFF
			};

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_auxiliary_input_status_type_2_enable(std::uint16_t inputObjectId, bool enable, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::AuxiliaryInputStatusTypeTwoEnableCommand),
				get_low_byte(inputObjectId),
				get_high_byte(inputObjectId),
				static_cast<std::uint8_t>(enable ? 0x01 : 0x00),
				0xFF,
				0xFF,
				0xFF,
				0xFF
			};

			retVal = CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
			                                                        buffer.data(),
			                                                        CAN_DATA_LENGTH,
			                                                        serverInternalControlFunction,
			                                                        destination,
			                                                        get_priority());
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_auxiliary_assignment_type_2(std::uint64_t inputUnitName, std::uint8_t functionType, std::uint16_t inputObjectId, std::uint16_t functionObjectId, bool storeAsPreferred, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			// Flags byte: bit7 = 0 to store as preferred (1 otherwise), bits 6-5 reserved 0, bits 4-0 the function type.
			const std::uint8_t flags = static_cast<std::uint8_t>((functionType & 0x1F) | (storeAsPreferred ? 0x00 : 0x80));
			const std::array<std::uint8_t, 14> buffer = {
				static_cast<std::uint8_t>(Function::AuxiliaryAssignmentTypeTwoCommand),
				static_cast<std::uint8_t>(inputUnitName & 0xFF),
				static_cast<std::uint8_t>((inputUnitName >> 8) & 0xFF),
				static_cast<std::uint8_t>((inputUnitName >> 16) & 0xFF),
				static_cast<std::uint8_t>((inputUnitName >> 24) & 0xFF),
				static_cast<std::uint8_t>((inputUnitName >> 32) & 0xFF),
				static_cast<std::uint8_t>((inputUnitName >> 40) & 0xFF),
				static_cast<std::uint8_t>((inputUnitName >> 48) & 0xFF),
				static_cast<std::uint8_t>((inputUnitName >> 56) & 0xFF),
				flags,
				get_low_byte(inputObjectId),
				get_high_byte(inputObjectId),
				get_low_byte(functionObjectId),
				get_high_byte(functionObjectId)
			};

			retVal = CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
			                                                        buffer.data(),
			                                                        static_cast<std::uint32_t>(buffer.size()),
			                                                        serverInternalControlFunction,
			                                                        destination,
			                                                        get_priority());
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_attribute_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::uint8_t attributeID, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::ChangeAttributeCommand),
				get_low_byte(objectID),
				get_high_byte(objectID),
				attributeID,
				errorBitfield,
				0xFF,
				0xFF,
				0xFF
			};

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_get_attribute_value_response(std::uint16_t objectID, std::uint8_t attributeID, std::uint32_t value, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };

		buffer[0] = static_cast<std::uint8_t>(Function::GetAttributeValueMessage);

		if (0 != errorBitfield)
		{
			// F.59 error response: bytes 2-3 are 0xFFFF and the queried object ID moves to bytes 5-6
			buffer[1] = 0xFF;
			buffer[2] = 0xFF;
			buffer[3] = attributeID;
			buffer[4] = get_low_byte(objectID);
			buffer[5] = get_high_byte(objectID);
			buffer[6] = errorBitfield;
			buffer[7] = 0xFF; // Reserved
		}
		else
		{
			// F.59 no-error response: the attribute value occupies bytes 5-8 little endian. Writing all
			// four bytes suits every attribute data type, because a client reads only the number of bytes
			// its attribute's type defines and the LSB is always first.
			buffer[1] = get_low_byte(objectID);
			buffer[2] = get_high_byte(objectID);
			buffer[3] = attributeID;
			buffer[4] = static_cast<std::uint8_t>(value & 0xFF);
			buffer[5] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
			buffer[6] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
			buffer[7] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
		}

		return send_response(buffer.data(), CAN_DATA_LENGTH, destination);
	}

	bool VirtualTerminalServer::send_change_background_colour_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::uint8_t colour, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::ChangeBackgroundColourCommand),
				get_low_byte(objectID),
				get_high_byte(objectID),
				colour,
				errorBitfield,
				0xFF,
				0xFF,
				0xFF
			};

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_child_location_response(std::uint16_t parentObjectID, std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer;

			buffer[0] = static_cast<std::uint8_t>(Function::ChangeChildLocationCommand);
			buffer[1] = get_low_byte(parentObjectID);
			buffer[2] = get_high_byte(parentObjectID);
			buffer[3] = get_low_byte(objectID);
			buffer[4] = get_high_byte(objectID);
			buffer[5] = errorBitfield;
			buffer[6] = 0xFF;
			buffer[7] = 0xFF;

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_child_position_response(std::uint16_t parentObjectID, std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer{
				static_cast<std::uint8_t>(Function::ChangeChildPositionCommand),
				get_low_byte(parentObjectID),
				get_high_byte(parentObjectID),
				get_low_byte(objectID),
				get_high_byte(objectID),
				errorBitfield,
				0xFF,
				0xFF
			};

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_fill_attributes_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::ChangeFillAttributesCommand),
				get_low_byte(objectID),
				get_high_byte(objectID),
				errorBitfield,
				0xFF,
				0xFF,
				0xFF,
				0xFF
			};
			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_font_attributes_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::ChangeFontAttributesCommand),
				get_low_byte(objectID),
				get_high_byte(objectID),
				errorBitfield,
				0xFF,
				0xFF,
				0xFF,
				0xFF
			};
			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_line_attributes_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::ChangeLineAttributesCommand),
				get_low_byte(objectID),
				get_high_byte(objectID),
				errorBitfield,
				0xFF,
				0xFF,
				0xFF,
				0xFF
			};
			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_list_item_response(std::uint16_t objectID, std::uint16_t newObjectID, std::uint8_t errorBitfield, std::uint8_t listIndex, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::ChangeListItemCommand),
				get_low_byte(objectID),
				get_high_byte(objectID),
				listIndex,
				get_low_byte(newObjectID),
				get_high_byte(newObjectID),
				errorBitfield,
				0xFF
			};
			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_button_activation_message(KeyActivationCode activationCode, std::uint16_t objectId, std::uint16_t parentObjectId, std::uint8_t keyNumber, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer;

			buffer[0] = static_cast<std::uint8_t>(Function::ButtonActivationMessage);
			buffer[1] = static_cast<std::uint8_t>(activationCode);
			buffer[2] = get_low_byte(objectId);
			buffer[3] = get_high_byte(objectId);
			buffer[4] = get_low_byte(parentObjectId);
			buffer[5] = get_high_byte(parentObjectId);
			buffer[6] = keyNumber;
			buffer[7] = 0xFF; // Reserved TODO: TAN

			retVal = CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
			                                                        buffer.data(),
			                                                        CAN_DATA_LENGTH,
			                                                        serverInternalControlFunction,
			                                                        destination,
			                                                        get_priority());
		}
		return retVal;
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

	bool VirtualTerminalServer::send_change_numeric_value_message(std::uint16_t objectId, std::uint32_t value, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::VTChangeNumericValueMessage),
				get_low_byte(objectId),
				get_high_byte(objectId),
				0xFF, // TODO: TAN, version 6
				get_byte(value, 0),
				get_byte(value, 1),
				get_byte(value, 2),
				get_byte(value, 3)
			};

			retVal = CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
			                                                        buffer.data(),
			                                                        CAN_DATA_LENGTH,
			                                                        serverInternalControlFunction,
			                                                        destination,
			                                                        get_priority());
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_select_input_object_message(std::uint16_t objectId, bool isObjectSelected, bool isObjectOpenForInput, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::VTSelectInputObjectMessage),
				get_low_byte(objectId),
				get_high_byte(objectId),
				static_cast<std::uint8_t>(isObjectSelected),
				static_cast<std::uint8_t>(isObjectOpenForInput),
				0xFF,
				0xFF,
				0xFF // Reserved TODO: TAN
			};

			retVal = CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
			                                                        buffer.data(),
			                                                        CAN_DATA_LENGTH,
			                                                        serverInternalControlFunction,
			                                                        destination,
			                                                        get_priority());
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_soft_key_activation_message(KeyActivationCode activationCode, std::uint16_t objectId, std::uint16_t parentObjectId, std::uint8_t keyNumber, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::SoftKeyActivationMessage),
				static_cast<std::uint8_t>(activationCode),
				get_low_byte(objectId),
				get_high_byte(objectId),
				get_low_byte(parentObjectId),
				get_high_byte(parentObjectId),
				keyNumber,
				0xFF // Reserved TODO: TAN
			};

			retVal = CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
			                                                        buffer.data(),
			                                                        CAN_DATA_LENGTH,
			                                                        serverInternalControlFunction,
			                                                        destination,
			                                                        get_priority());
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_string_value_message(std::uint16_t objectId, const std::string &value, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			if (value.length() > 255)
			{
				LOG_WARNING("[VT Server] Truncated user input string value to the maximum of 255. The string was: " + value);
			}

			std::vector<std::uint8_t> buffer = {
				static_cast<std::uint8_t>(Function::VTChangeStringValueMessage),
				get_low_byte(objectId),
				get_high_byte(objectId),
				static_cast<std::uint8_t>(value.length() > 255 ? 255 : value.length())
			};

			for (std::uint16_t i = 0; i < value.length() && i < 255; i++)
			{
				buffer.push_back(static_cast<std::uint8_t>(value.at(i)));
			}

			while (buffer.size() < CAN_DATA_LENGTH)
			{
				buffer.push_back(0xFF); // The standard specifies the message must be padded to 8 bytes
			}

			retVal = CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
			                                                        buffer.data(),
			                                                        static_cast<std::uint32_t>(buffer.size()),
			                                                        serverInternalControlFunction,
			                                                        destination,
			                                                        get_priority());
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_load_version_response(std::uint8_t errorCodes, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::LoadVersionCommand),
				0xFF, // Reserved
				0xFF, // Reserved
				0xFF, // Reserved
				0xFF, // Reserved
				errorCodes,
				0xFF, // Reserved
				0xFF // Reserved
			};
			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_extended_load_version_response(std::uint8_t errorCodes, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::ExtendedLoadVersionCommand),
				0xFF, // Reserved
				0xFF, // Reserved
				0xFF, // Reserved
				0xFF, // Reserved
				errorCodes,
				0xFF, // Reserved
				0xFF // Reserved
			};
			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	void VirtualTerminalServer::process_macro(std::shared_ptr<isobus::VTObject> object, isobus::EventID macroEvent, isobus::VirtualTerminalObjectType targetObjectType, std::shared_ptr<isobus::VirtualTerminalServerManagedWorkingSet> workingset)
	{
		if (nullptr != object && targetObjectType == object->get_object_type())
		{
			// Enqueue every macro on this object that matches the event, in the object's macro-list
			// order, then drain. Enqueue-all-then-drain is what makes triggered macros run in trigger
			// order and each complete before the next starts (ISO 11783-6 4.6.11.4 b/c). If a drain is
			// already active (this object's event was itself raised by a macro-replayed command), the
			// drain call is a no-op and the active drain runs these in turn.
			for (std::uint8_t i = 0; i < object->get_number_macros(); i++)
			{
				auto macroMetadata = object->get_macro(i);
				if (macroMetadata.event == macroEvent)
				{
					macroExecutionQueue.push_back({ macroMetadata.macroID, workingset });
				}
			}
			drain_macro_execution_queue();
		}
	}

	bool VirtualTerminalServer::send_change_numeric_value_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::uint32_t value, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer;

			buffer[0] = static_cast<std::uint8_t>(Function::ChangeNumericValueCommand);
			buffer[1] = get_low_byte(objectID);
			buffer[2] = get_high_byte(objectID);
			buffer[3] = errorBitfield;
			buffer[4] = get_byte(value, 0);
			buffer[5] = get_byte(value, 1);
			buffer[6] = get_byte(value, 2);
			buffer[7] = get_byte(value, 3);

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_polygon_point_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer;

			buffer[0] = static_cast<std::uint8_t>(Function::ChangePolygonPointCommand);
			buffer[1] = get_low_byte(objectID);
			buffer[2] = get_high_byte(objectID);
			buffer[3] = errorBitfield;
			buffer[4] = 0xFF;
			buffer[5] = 0xFF;
			buffer[6] = 0xFF;
			buffer[7] = 0xFF;

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_end_point_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer;

			buffer[0] = static_cast<std::uint8_t>(Function::ChangeEndPointCommand);
			buffer[1] = get_low_byte(objectID);
			buffer[2] = get_high_byte(objectID);
			buffer[3] = errorBitfield;
			buffer[4] = 0xFF;
			buffer[5] = 0xFF;
			buffer[6] = 0xFF;
			buffer[7] = 0xFF;

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_polygon_scale_response(std::uint16_t objectID, std::uint16_t newWidth, std::uint16_t newHeight, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer;

			// F.55 echoes the commanded width and height rather than padding, so the error bitfield
			// occupies the last byte instead of byte 4 and no byte of this response is reserved.
			buffer[0] = static_cast<std::uint8_t>(Function::ChangePolygonScaleCommand);
			buffer[1] = get_low_byte(objectID);
			buffer[2] = get_high_byte(objectID);
			buffer[3] = get_low_byte(newWidth);
			buffer[4] = get_high_byte(newWidth);
			buffer[5] = get_low_byte(newHeight);
			buffer[6] = get_high_byte(newHeight);
			buffer[7] = errorBitfield;

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_esc_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer;

			buffer[0] = static_cast<std::uint8_t>(Function::ESCCommand);
			buffer[1] = get_low_byte(objectID);
			buffer[2] = get_high_byte(objectID);
			buffer[3] = errorBitfield;
			buffer[4] = 0xFF;
			buffer[5] = 0xFF;
			buffer[6] = 0xFF;
			buffer[7] = 0xFF;

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_lock_unlock_mask_response(std::uint8_t command, std::uint8_t errorBitfield, bool unsolicited, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer;

			buffer[0] = static_cast<std::uint8_t>(Function::LockUnlockMaskCommand);
			buffer[1] = command;
			buffer[2] = errorBitfield;
			buffer[3] = 0xFF;
			buffer[4] = 0xFF;
			buffer[5] = 0xFF;
			buffer[6] = 0xFF;
			buffer[7] = 0xFF;

			if (unsolicited)
			{
				// 4.6.11.4 f) withholds the VT's response to a command a macro contains. A release the VT
				// originates is not a response to any command, and F.46 requires it to reach the working
				// set, so it bypasses that suppression the same way the VT Status and the activation
				// messages do. Routing it through send_response would drop the notification whenever a
				// lock happened to be released from inside a macro, leaving the client believing it still
				// held a lock the server had already given up.
				retVal = CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
				                                                        buffer.data(),
				                                                        CAN_DATA_LENGTH,
				                                                        serverInternalControlFunction,
				                                                        destination,
				                                                        get_priority());
			}
			else
			{
				retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
			}
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_object_label_response(std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			// F.51 puts the error bitfield in byte 2 and reserves bytes 3-8. There is no object ID echo,
			// which every other change command response in the set has.
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer{
				static_cast<std::uint8_t>(Function::ChangeObjectLabelCommand),
				errorBitfield,
				0xFF,
				0xFF,
				0xFF,
				0xFF,
				0xFF,
				0xFF
			};

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_size_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer{
				static_cast<std::uint8_t>(Function::ChangeSizeCommand),
				get_low_byte(objectID),
				get_high_byte(objectID),
				errorBitfield,
				0xFF,
				0xFF,
				0xFF,
				0xFF
			};

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_soft_key_mask_response(std::uint16_t objectID, std::uint16_t newObjectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer{
				static_cast<std::uint8_t>(Function::ChangeSoftKeyMaskCommand),
				get_low_byte(objectID),
				get_high_byte(objectID),
				get_low_byte(newObjectID),
				get_high_byte(newObjectID),
				errorBitfield,
				0xFF,
				0xFF
			};

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_change_string_value_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::ChangeStringValueCommand),
				0xFF,
				0xFF,
				get_low_byte(objectID),
				get_high_byte(objectID),
				errorBitfield,
				0xFF,
				0xFF
			};
			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_delete_version_response(std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::DeleteVersionCommand),
				0xFF,
				0xFF,
				0xFF,
				0xFF,
				errorBitfield,
				0xFF,
				0xFF
			};
			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_delete_object_pool_response(std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::DeleteObjectPoolCommand),
				errorBitfield,
				0xFF,
				0xFF,
				0xFF,
				0xFF,
				0xFF,
				0xFF
			};
			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_enable_disable_object_response(std::uint16_t objectID, std::uint8_t errorBitfield, bool value, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			std::array<std::uint8_t, CAN_DATA_LENGTH> buffer;

			buffer[0] = static_cast<std::uint8_t>(Function::EnableDisableObjectCommand);
			buffer[1] = get_low_byte(objectID);
			buffer[2] = get_high_byte(objectID);
			buffer[3] = value;
			buffer[4] = errorBitfield;
			buffer[5] = 0xFF;
			buffer[6] = 0xFF;
			buffer[7] = 0xFF;

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_end_of_object_pool_response(bool success,
	                                                             std::uint16_t parentIDOfFaultingObject,
	                                                             std::uint16_t faultingObjectID,
	                                                             std::uint8_t errorCodes,
	                                                             std::shared_ptr<ControlFunction> destination) const
	{
		std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };

		buffer[0] = static_cast<std::uint8_t>(Function::EndOfObjectPoolMessage);
		buffer[1] = (success ? 0x00 : 0x01); // Error in object pool is 0x01, no error is 0x00
		buffer[2] = get_low_byte(parentIDOfFaultingObject);
		buffer[3] = get_high_byte(parentIDOfFaultingObject);
		buffer[4] = get_low_byte(faultingObjectID);
		buffer[5] = get_high_byte(faultingObjectID);
		buffer[6] = errorCodes;
		buffer[7] = 0xFF; // Reserved

		return send_response(buffer.data(), CAN_DATA_LENGTH, destination);
	}

	bool VirtualTerminalServer::send_execute_macro_or_extended_macro_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination, bool extendedMacro) const
	{
		std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };

		if (extendedMacro)
		{
			buffer[0] = static_cast<std::uint8_t>(Function::ExecuteExtendedMacroCommand);
		}
		else
		{
			buffer[0] = static_cast<std::uint8_t>(Function::ExecuteMacroCommand);
		}

		buffer[1] = get_low_byte(objectID);

		if (extendedMacro)
		{
			buffer[2] = get_high_byte(objectID);
		}
		else
		{
			buffer[2] = 0xFF;
		}

		buffer[3] = errorBitfield;
		buffer[4] = 0xFF;
		buffer[5] = 0xFF;
		buffer[6] = 0xFF;
		buffer[7] = 0xFF;

		return send_response(buffer.data(), CAN_DATA_LENGTH, destination);
	}

	bool VirtualTerminalServer::send_hide_show_object_response(std::uint16_t objectID, std::uint8_t errorBitfield, bool value, std::shared_ptr<ControlFunction> destination) const
	{
		std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };

		buffer[0] = static_cast<std::uint8_t>(Function::HideShowObjectCommand);
		buffer[1] = get_low_byte(objectID);
		buffer[2] = get_high_byte(objectID);
		buffer[3] = static_cast<std::uint8_t>(value);
		buffer[4] = errorBitfield;
		buffer[5] = 0xFF; // Reserved
		buffer[6] = 0xFF; // Reserved
		buffer[7] = 0xFF; // Reserved

		return send_response(buffer.data(), CAN_DATA_LENGTH, destination);
	}

	bool VirtualTerminalServer::send_change_priority_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::uint8_t priority, std::shared_ptr<ControlFunction> destination) const
	{
		std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };

		buffer[0] = static_cast<std::uint8_t>(Function::ChangePriorityCommand);
		buffer[1] = get_low_byte(objectID);
		buffer[2] = get_high_byte(objectID);
		buffer[3] = priority;
		buffer[4] = errorBitfield;
		buffer[5] = 0xFF; // Reserved
		buffer[6] = 0xFF; // Reserved
		buffer[7] = 0xFF; // Reserved

		return send_response(buffer.data(), CAN_DATA_LENGTH, destination);
	}

	bool VirtualTerminalServer::send_select_input_object_response(std::uint16_t objectID, std::uint8_t errorBitfield, SelectInputObjectResponse response, std::shared_ptr<ControlFunction> destination) const
	{
		std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };

		buffer[0] = static_cast<std::uint8_t>(Function::SelectInputObjectCommand);
		buffer[1] = get_low_byte(objectID);
		buffer[2] = get_high_byte(objectID);
		buffer[3] = static_cast<std::uint8_t>(response);
		buffer[4] = errorBitfield;
		buffer[5] = 0xFF; // Reserved
		buffer[6] = 0xFF; // Reserved
		buffer[7] = 0xFF; // Reserved

		return send_response(buffer.data(), CAN_DATA_LENGTH, destination);
	}

	bool VirtualTerminalServer::send_status_message() const
	{
		std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };

		buffer[0] = static_cast<std::uint8_t>(Function::VTStatusMessage);
		buffer[1] = activeWorkingSetMasterAddress;
		buffer[2] = get_low_byte(activeWorkingSetDataMaskObjectID);
		buffer[3] = get_high_byte(activeWorkingSetDataMaskObjectID);
		buffer[4] = get_low_byte(activeWorkingSetSoftkeyMaskObjectID);
		buffer[5] = get_high_byte(activeWorkingSetSoftkeyMaskObjectID);
		buffer[6] = static_cast<std::uint8_t>(busyCodesBitfield | (auxiliaryInputLearnModeActive ? 0x40 : 0x00));
		buffer[7] = currentCommandFunctionCode;
		return CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
		                                                      buffer.data(),
		                                                      CAN_DATA_LENGTH,
		                                                      serverInternalControlFunction,
		                                                      nullptr,
		                                                      get_priority());
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

	bool VirtualTerminalServer::send_supported_objects(std::shared_ptr<ControlFunction> destination) const
	{
		auto supportedObjects = get_supported_objects();
		std::vector<std::uint8_t> buffer = { static_cast<std::uint8_t>(Function::GetSupportedObjectsMessage),
			                                   static_cast<std::uint8_t>(supportedObjects.size()) };

		for (const auto &supportedObject : supportedObjects)
		{
			buffer.push_back(supportedObject);
		}

		// D.15 gives this response a variable data length: byte 2 counts the bytes that follow and
		// the list runs to the end of the message. Sending CAN_DATA_LENGTH here would cap it at six
		// object types while byte 2 still advertised the full count, so a conformant working set
		// would read past the message it was given.
		return send_response(buffer.data(), static_cast<std::uint32_t>(buffer.size()), destination);
	}

	bool VirtualTerminalServer::send_audio_signal_successful(std::shared_ptr<ControlFunction> destination) const
	{
		std::vector<std::uint8_t> buffer = { static_cast<std::uint8_t>(Function::ControlAudioSignalCommand), 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
		return send_response(buffer.data(), CAN_DATA_LENGTH, destination);
	}

	bool VirtualTerminalServer::send_get_window_mask_data_response(std::shared_ptr<ControlFunction> destination) const
	{
		std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };

		buffer[0] = static_cast<std::uint8_t>(Function::GetWindowMaskDataMessage);
		buffer[1] = get_user_layout_datamask_bg_color();
		buffer[2] = get_user_layout_softkeymask_bg_color();
		buffer[3] = 0xFF; // Reserved
		buffer[4] = 0xFF; // Reserved
		buffer[5] = 0xFF; // Reserved
		buffer[6] = 0xFF; // Reserved
		buffer[7] = 0xFF; // Reserved

		return send_response(buffer.data(), CAN_DATA_LENGTH, destination);
	}

	bool VirtualTerminalServer::send_audio_volume_response(std::shared_ptr<ControlFunction> destination) const
	{
		std::vector<std::uint8_t> buffer = { static_cast<std::uint8_t>(Function::SetAudioVolumeCommand), 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
		return send_response(buffer.data(), CAN_DATA_LENGTH, destination);
	}

	bool VirtualTerminalServer::send_capture_screen_response(std::uint8_t item, std::uint8_t path, std::uint8_t errorCode, std::uint16_t imageId, std::shared_ptr<ControlFunction> requestor) const
	{
		std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = { 0 };

		buffer[0] = static_cast<std::uint8_t>(Function::ScreenCapture);
		buffer[1] = item;
		buffer[2] = path;
		buffer[3] = errorCode;
		buffer[4] = get_low_byte(imageId);
		buffer[5] = get_high_byte(imageId);
		buffer[6] = 0xFF;
		buffer[7] = 0xFF;
		return send_response(buffer.data(), CAN_DATA_LENGTH, requestor);
	}

	void VirtualTerminalServer::update()
	{
		// G.2 byte 2 is the active working set master's address, which moves if that control function
		// re-claims, so it is tracked rather than left at the value captured when the pool activated.
		// A null address is not adopted: activeWorkingSetMasterAddress doubles as the "no working set is
		// active" sentinel the activation branch below tests, so writing the null address here would let
		// another working set claim activation while this one is still active. A master that truly goes
		// away is dropped by the 4.6.9 teardown pass, which clears the address and activeWorkingSet together.
		if (nullptr != activeWorkingSet)
		{
			auto activeControlFunction = activeWorkingSet->get_control_function();

			if ((nullptr != activeControlFunction) &&
			    (isobus::NULL_CAN_ADDRESS != activeControlFunction->get_address()) &&
			    (activeControlFunction->get_address() != activeWorkingSetMasterAddress))
			{
				activeWorkingSetMasterAddress = activeControlFunction->get_address();
				mark_status_message_changed();
			}
		}

		// G.2 byte 7 bit 4 reports that the VT is busy parsing an object pool. It is derived from every
		// managed working set so a concurrent upload cannot clear it early, and it holds from the start of a
		// parse until that pool's response is queued. G.2 lists only bytes 2-6 and byte 7 bit 6 as on-change
		// triggers, so this bit rides the once-per-second cadence rather than forcing a transmission.
		if (is_any_object_pool_parsing())
		{
			busyCodesBitfield |= 0x10;
		}
		else
		{
			busyCodesBitfield = static_cast<std::uint8_t>(busyCodesBitfield & ~0x10);
		}

		const bool heartbeatDue = isobus::SystemTiming::time_expired_ms(statusMessageTimestamp_ms, 1000);
		// G.2: the status is sent on change of any of bytes 2 to 6, or byte 7 bit 6, and once per second,
		// up to a maximum of five per second, so a pending change waits out a 200 ms floor since the last
		// transmission.
		const bool changeDue = statusMessagePending && isobus::SystemTiming::time_expired_ms(statusMessageTimestamp_ms, 200);

		if ((heartbeatDue || changeDue) && send_status_message())
		{
			statusMessageTimestamp_ms = isobus::SystemTiming::get_timestamp_ms();
			statusMessagePending = false;
		}

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

		for (const auto &ws : managedWorkingSetList)
		{
			if (VirtualTerminalServerManagedWorkingSet::ObjectPoolProcessingThreadState::Success == ws->get_object_pool_processing_state())
			{
				ws->join_parsing_thread();
				if (ws->get_was_object_pool_loaded_from_non_volatile_memory())
				{
					// A pool loaded via Load Version is completed by a Load Version response, not an
					// End of Object Pool response -- that is the message the client waits on. Consume
					// the flag so a later parse on this working set (e.g. a runtime pool update) is
					// again completed by an End of Object Pool response. A client that used the
					// Extended Load Version command (0xD5) waits on the extended response, not 0xD1.
					if (ws->get_loaded_via_extended_version_command())
					{
						send_extended_load_version_response(0, ws->get_control_function());
						ws->set_loaded_via_extended_version_command(false, {});
					}
					else
					{
						send_load_version_response(0, ws->get_control_function());
					}
					ws->set_was_object_pool_loaded_from_non_volatile_memory(false, {});
				}
				else
				{
					send_end_of_object_pool_response(true, NULL_OBJECT_ID, NULL_OBJECT_ID, 0, ws->get_control_function());
				}
				// A newly parsed pool joins the arbitration of ISO 11783-6 4.6.14 rather than being adopted
				// only when no working set is active. A pool whose initial active mask is an Alarm Mask has
				// an alarm raised the instant it activates, and 4.6.14 gives the screen to the highest
				// priority alarm regardless of which working set connected first. The arbitration also
				// covers the two cases the plain adopt used to handle: with no alarms anywhere the first
				// working set to present a usable pool takes the screen and keeps it, and a runtime object
				// pool update on the active working set refreshes the status' visible-mask fields, which
				// that update can retarget.
				apply_active_working_set_arbitration();
			}
			else if (VirtualTerminalServerManagedWorkingSet::ObjectPoolProcessingThreadState::Fail == ws->get_object_pool_processing_state())
			{
				ws->join_parsing_thread();

				// A parse failure on an already-active working set is a failed runtime object pool
				// update (ISO 11783-6 clause C.2.6): the VT deletes the entire object pool from
				// volatile memory -- including the pool as it existed prior to the update -- and
				// suspends the working set. A parse failure on a working set that never became active
				// is an initial-upload failure, which is left in place so the client can retry (only
				// the error response is sent). C.2.5 byte 2 bit 0 (set here, since success is false)
				// tells the client the error cause is in bytes 3 to 8, so the Object Pool Error Codes
				// byte (byte 7) always carries bit 2, "any other error", as the cause: the parser
				// records only a single faulting object ID with no failure category, so it cannot tell
				// a missing object reference (bit 1) from any other malformed object, and bit 2 is the
				// honest cause. get_bit(3), "object pool was deleted from volatile memory", is added
				// only for the C.2.6 case, where the pool was in fact deleted.
				// A runtime update reuses the same managed working set object that was stored in
				// activeWorkingSet at activation, so a pointer comparison identifies it exactly; an
				// address comparison would false-match a never-active working set whose control
				// function reports the null address while no working set is active.
				const bool poolWasActive = (ws == activeWorkingSet);

				///  @todo Get the parent object ID of the faulting object
				send_end_of_object_pool_response(false, NULL_OBJECT_ID, ws->get_object_pool_faulting_object_id(), static_cast<std::uint8_t>(get_bit(2) | (poolWasActive ? get_bit(3) : 0)), ws->get_control_function());

				if (poolWasActive)
				{
					// Flag the working set for teardown by the pass below, which runs in this same
					// update() call, so the invalid pool is deleted and the working set suspended
					// immediately rather than left active with a half-merged pool.
					ws->request_deletion();
				}
			}
		}

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
		// working set is gone) and auxiliary-assignment removal (AUX-N is unimplemented).
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

		// The screen is not left blank because the working set holding it went away. Once the departed
		// working sets are out of the list, 4.6.14 is re-run over the survivors, which promotes the next
		// highest priority alarm or, with no alarm anywhere, the working set that last had a Data Mask
		// visible. This runs after the erase loop so the arbitration cannot select a working set that is
		// on its way out.
		if (workingSetWasTornDown)
		{
			apply_active_working_set_arbitration();
		}
	}
}
