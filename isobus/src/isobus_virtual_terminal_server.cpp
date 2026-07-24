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
		// A macro replays each stored command packet as a received message. Most are a single 8-byte frame,
		// but a macro command can be longer -- Change Child Position (9 bytes) and the variable Graphics
		// Context (Draw Polygon / Draw Text / Pan and Zoom) and Change String Value commands all exceed one
		// frame (Table B.56 stores them at their natural length). Accept any packet at least one CAN frame
		// long; process_rx_message applies each function's own length validation.
		if ((message.get_destination_control_function() == serverInternalControlFunction) &&
		    (message.get_source_control_function() != nullptr) &&
		    (CAN_DATA_LENGTH <= message.get_data_length()))
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

	CANIdentifier::CANPriority VirtualTerminalServer::get_priority() const
	{
		return (VTVersion::Version6 == get_version()) ? CANIdentifier::CANPriority::Priority5 : CANIdentifier::CANPriority::PriorityLowest7;
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
				retVal = handle_auxiliary_capabilities_request(message, data);
				break;

			case Function::GetSupportedObjectsMessage:
				retVal = handle_get_supported_objects_message(message);
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
				handle_extended_get_versions_message(message);
				break;

			case Function::ExtendedStoreVersionCommand:
				handle_extended_store_version_command(message, data, managedWorkingSet);
				break;

			case Function::ExtendedDeleteVersionCommand:
				handle_extended_delete_version_command(message, data, managedWorkingSet);
				break;

			case Function::ExtendedLoadVersionCommand:
				handle_extended_load_version_command(message, data, managedWorkingSet);
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

				if ((get_version() >= VTVersion::Version6) && is_wildcard_version_label(versionLabel))
				{
					// E.8: a version label of a single '*' padded right with 6 blanks is a wild card that
					// deletes ALL seven-character version object pools in NVM owned by the requesting working
					// set, and only its own (get_versions and delete_version are both keyed by NAME).
					// get_versions returns only the seven-character labels, so extended (32-character) versions
					// are left alone. The feature is available at VT version 6 and later; a version-5 server
					// falls through and treats '*'-padded as an ordinary, not-found label, byte-identical to
					// before. E.9 byte 6 is 0 ("successfully deleted or wild card delete was commanded") even
					// when no versions existed.
					const NAME clientNAME = managedWorkingSet->get_control_function()->get_NAME();
					const auto sevenCharacterVersions = get_versions(clientNAME);
					for (const auto &label : sevenCharacterVersions)
					{
						delete_version(std::vector<std::uint8_t>(label.begin(), label.end()), clientNAME);
					}
					LOG_INFO("[VT Server]: Wild card delete removed %u seven-character object pool version(s) for client NAME %s", static_cast<unsigned>(sevenCharacterVersions.size()), nameString.str().c_str());
					send_delete_version_response(0, managedWorkingSet->get_control_function());
					break;
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

				// ISO 11783-6 Table 5, Data-input state: a Change Numeric Value command on the object that
				// is open for input is refused with "the object is in use" (F.23 byte 4 bit 2), leaving the
				// open edit untouched. Only the object's own ID is guarded, not a variable it references.
				// 4.6.10.2 deprecates this refusal for a VT-version-6 pair, where the change is accepted and
				// applied even while the object is in use; a lower pair keeps the refusal byte-identically.
				if (is_object_open_for_input(managedWorkingSet, objectId) && !is_version6_pair(managedWorkingSet))
				{
					send_change_numeric_value_response(objectId, get_bit(static_cast<std::uint8_t>(ChangeNumericValueErrorBit::ValueInUse)), value, managedWorkingSet->get_control_function());
					LOG_WARNING("[VT Server]: Client %u change numeric value on object %u refused: it is open for operator input", managedWorkingSet->get_control_function()->get_address(), objectId);
					break;
				}

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

						case VirtualTerminalObjectType::ScaledGraphic:
						{
							// ISO 11783-6:2018 F.22 / Table B.76: a Change Numeric Value on a Scaled Graphic
							// (VT version 6 and later) retargets its Value attribute, a 2-byte object ID in
							// bytes 5-6. B.28 restricts that target to a Graphic Data object, a Picture Graphic
							// object, an Object Pointer object, or the NULL object; anything else is refused.
							// F.23 footnote c sets byte 4 bit 0 (Invalid Object ID) specifically when the
							// command changes a pointer value to an invalid object, which is exactly this case.
							const std::uint16_t newTarget = get_low_16_bits(value);
							bool validTarget = (NULL_OBJECT_ID == newTarget);
							if (!validTarget)
							{
								const auto referenced = managedWorkingSet->get_object_by_id(newTarget);
								if (nullptr != referenced)
								{
									const VirtualTerminalObjectType referencedType = referenced->get_object_type();
									validTarget = (VirtualTerminalObjectType::GraphicData == referencedType) ||
									  (VirtualTerminalObjectType::PictureGraphic == referencedType) ||
									  (VirtualTerminalObjectType::ObjectPointer == referencedType);
								}
							}

							if (validTarget)
							{
								std::static_pointer_cast<ScaledGraphic>(lTargetObject)->set_value(newTarget);
								dispatch_repaint(managedWorkingSet);
								send_change_numeric_value_response(objectId, 0, value, managedWorkingSet->get_control_function());
							}
							else
							{
								send_change_numeric_value_response(objectId, get_bit(static_cast<std::uint8_t>(ChangeNumericValueErrorBit::InvalidObjectID)), value, managedWorkingSet->get_control_function());
								LOG_WARNING("[VT Server]: Client %u change numeric value on scaled graphic %u refused: target %u is not a graphic data, picture graphic, object pointer, or NULL", managedWorkingSet->get_control_function()->get_address(), objectId, newTarget);
								logSuccess = false;
							}
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

				// ISO 11783-6 Table 5, Data-input state: disabling the object that is open for input is
				// refused with "operator input is active on this object" (F.5 byte 5 bit 3); the object
				// stays enabled and keeps focus. Only a disable (byte 4 = 0) is refused -- enabling an
				// already-enabled open object is the no-op F.4 requires to answer with no error. This
				// refusal is NOT version-gated: 4.6.10.2's deprecation covers the "object in use"
				// attribute/value error family, and F.5's "input object is currently being modified" bit is
				// a different error for a different command, still live at VT version 6.
				if ((0 == data[3]) && is_object_open_for_input(managedWorkingSet, objectId))
				{
					send_enable_disable_object_response(objectId, get_bit(static_cast<std::uint8_t>(EnableDisableObjectErrorBit::CouldNotCompleteTheInputObjectIsCurrentlyBeingModified)), (0 != data[3]), managedWorkingSet->get_control_function());
					LOG_WARNING("[VT Server]: Client %u disable object %u refused: operator input is active on it", managedWorkingSet->get_control_function()->get_address(), objectId);
					break;
				}

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
						const std::uint16_t oldActiveMaskObjectId = std::static_pointer_cast<WorkingSet>(workingSetObject)->get_active_mask();
						std::static_pointer_cast<WorkingSet>(workingSetObject)->set_active_mask(newActiveMaskObjectId);

						// ISO 11783-6 Table 5, Data-input rows: a Change Active Mask command while an object
						// is open for operator input forces that input closed, and the row's responses "shall
						// be sent in the indicated sequence" -- VT ESC message, then VT Select Input Object
						// (loses focus), then the Change Active Mask response (sent below), then the VT Status
						// message (raised by the arbitration that follows this block). H.10's second sentence
						// makes the VT ESC obligatory here: it is sent "when the VT closes an open input field
						// due to a Change Active Mask command". H.8's note that the selection message is
						// withheld when the Working Set requested the focus change with the Select Input Object
						// COMMAND does not apply -- a mask change is not that command -- so the deselect IS
						// sent. This VT sets no initial focus (proprietary), so no "gains focus" selection
						// follows. The Navigating-state row (no open input, a merely-focused object) sends only
						// the deselect. These precede the Change Active Mask response so the wire order matches
						// the indicated sequence.
						//
						// The row binds "only if new mask is a new Data Mask, or if the new mask is an Alarm
						// Mask and the VT does not store the state of the input object" (this VT stores none).
						// A re-select of the mask already active -- the refresh idiom the mask-lock release
						// below also special-cases -- changes no mask and must not disturb an open edit.
						const std::uint16_t openForInputBeforeMaskChange = managedWorkingSet->get_object_open_for_input();
						if (newActiveMaskObjectId != oldActiveMaskObjectId)
						{
							if (NULL_OBJECT_ID != openForInputBeforeMaskChange)
							{
								send_vt_esc_message(openForInputBeforeMaskChange, 0, managedWorkingSet->get_control_function());
								send_select_input_object_message(openForInputBeforeMaskChange, false, false, managedWorkingSet->get_control_function());
								managedWorkingSet->set_object_open_for_input(NULL_OBJECT_ID);
								managedWorkingSet->set_object_focus(NULL_OBJECT_ID);
							}
							else if (NULL_OBJECT_ID != managedWorkingSet->get_object_focus())
							{
								send_select_input_object_message(managedWorkingSet->get_object_focus(), false, false, managedWorkingSet->get_control_function());
								managedWorkingSet->set_object_focus(NULL_OBJECT_ID);
							}
						}

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
						// The new mask is presented by this command's own repaint: the arbitration above only
						// repaints when the screen moves to a DIFFERENT working set, and a macro-driven mask
						// change (a Table A.3 key event) has no follow-on client traffic whose repaints would
						// refresh a stale frame. Routed through dispatch_repaint so an F.46 mask lock still
						// withholds the refresh.
						dispatch_repaint(managedWorkingSet);
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
				handle_select_colour_map_command(data, managedWorkingSet);
				break;

			case Function::GetAttributeValueMessage:
				handle_get_attribute_value_message(data, managedWorkingSet);
				break;

			case Function::ChangeStringValueCommand:
			{
				auto objectIdToChange = get_little_endian_uint16(data, 1);

				// ISO 11783-6 Table 5, Data-input state: a Change String Value command on the object that is
				// open for input is refused with "the object is in use". At VT version 5 that is F.25 byte 6
				// bit 4 (the bit was withdrawn in later editions). 4.6.10.2 deprecates the refusal for a
				// VT-version-6 pair, where the value change is accepted while in use; a lower pair keeps it.
				if (is_object_open_for_input(managedWorkingSet, objectIdToChange) && !is_version6_pair(managedWorkingSet))
				{
					send_change_string_value_response(objectIdToChange, get_bit(static_cast<std::uint8_t>(ChangeStringValueErrorBit::ValueInUse)), message.get_source_control_function());
					LOG_WARNING("[VT Server]: Client %u change string value on object %u refused: it is open for operator input", managedWorkingSet->get_control_function()->get_address(), objectIdToChange);
					break;
				}

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

				// ISO 11783-6 Table 5, Data-input state: a Change Attribute command on the object that is open
				// for input is refused with "the object is in use" (F.39 byte 5 bit 3), leaving the open edit
				// untouched. 4.2 also protects the value being composed, so nothing is written. 4.6.10.2
				// deprecates this refusal for a VT-version-6 pair -- its worked example is precisely a Change
				// Attribute (background colour) accepted while an Input Number is being edited -- so a version-6
				// pair falls through to the normal change path; a lower pair keeps the refusal byte-identically.
				if (is_object_open_for_input(managedWorkingSet, objectID) && !is_version6_pair(managedWorkingSet))
				{
					send_change_attribute_response(objectID, get_bit(static_cast<std::uint8_t>(VTObject::AttributeError::ValueInUse)), data.at(3), message.get_source_control_function());
					LOG_WARNING("[VT Server]: Client %u change attribute %u on object %u refused: it is open for operator input", managedWorkingSet->get_control_function()->get_address(), attributeID, objectID);
					break;
				}

				if ((NULL_OBJECT_ID != objectID) && (nullptr != targetObject))
				{
					// The snapshot is held in a named local for the duration of the call that reads it,
					// so the tree reference passed below outlives the call rather than dangling on a
					// temporary that expired at the end of the argument list.
					const auto objectTree = managedWorkingSet->get_object_tree();

					// Attribute 3 on a Working Set is its active mask (Table B.2), so this write can make the very
					// mask change a Change Active Mask command makes, reached by a different command. The old value
					// is read before the generic set_attribute below so the same-mask gate can compare it.
					const bool retargetsWorkingSetActiveMask =
					  (VirtualTerminalObjectType::WorkingSet == targetObject->get_object_type()) &&
					  (static_cast<std::uint8_t>(WorkingSet::AttributeName::ActiveMask) == attributeID);
					const std::uint16_t oldActiveMaskObjectId = retargetsWorkingSetActiveMask ?
					  std::static_pointer_cast<WorkingSet>(targetObject)->get_active_mask() :
					  NULL_OBJECT_ID;

					if (targetObject->set_attribute(attributeID, attributeData, *objectTree, errorCode)) // 0 Is always the read-only "type" attribute
					{
						// A Change Attribute that retargets a Working Set's active mask makes the same mask change a
						// Change Active Mask command does, so ISO 11783-6 Table 5's Data-input rows bind here too: an
						// object open for operator input is forced closed, and the responses "shall be sent in the
						// indicated sequence" -- VT ESC, then VT Select Input Object (loses focus), then the command
						// response (sent below), then the VT Status raised by the arbitration that follows. Annex
						// H.10's sentence names the Change Active Mask COMMAND; this path is the same mask change
						// caused by a different command and is closed identically (the analogy carries 0058 and 0059
						// document). Sending it ahead of the response makes the wire order match the indicated
						// sequence, and clearing the open/focus state here means the arbitration below (carry 0059)
						// sees it already cleared and does not double-send for this working set. A re-select of the
						// mask already active changes no mask and must not disturb an open edit, so the same-mask gate
						// carry 0058 uses is applied here too.
						if (retargetsWorkingSetActiveMask)
						{
							const std::uint16_t newActiveMaskObjectId = std::static_pointer_cast<WorkingSet>(targetObject)->get_active_mask();
							const std::uint16_t openForInputBeforeMaskChange = managedWorkingSet->get_object_open_for_input();
							if (newActiveMaskObjectId != oldActiveMaskObjectId)
							{
								if (NULL_OBJECT_ID != openForInputBeforeMaskChange)
								{
									send_vt_esc_message(openForInputBeforeMaskChange, 0, managedWorkingSet->get_control_function());
									send_select_input_object_message(openForInputBeforeMaskChange, false, false, managedWorkingSet->get_control_function());
									managedWorkingSet->set_object_open_for_input(NULL_OBJECT_ID);
									managedWorkingSet->set_object_focus(NULL_OBJECT_ID);
								}
								else if (NULL_OBJECT_ID != managedWorkingSet->get_object_focus())
								{
									send_select_input_object_message(managedWorkingSet->get_object_focus(), false, false, managedWorkingSet->get_control_function());
									managedWorkingSet->set_object_focus(NULL_OBJECT_ID);
								}
							}
						}

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

				// ISO 11783-6 Table 5, Data-input state: a Change List Item command on the list that is open
				// for input is refused with "the object is in use" (F.43 byte 7 bit 3), leaving the open edit
				// untouched. 4.6.10.2 deprecates this refusal for a VT-version-6 pair (the list item is a
				// value/reference the clause lets a version-6 pair change while in use); a lower pair keeps it.
				if (is_object_open_for_input(managedWorkingSet, objectID) && !is_version6_pair(managedWorkingSet))
				{
					send_change_list_item_response(objectID, newObjectID, get_bit(static_cast<std::uint8_t>(ChangeListItemErrorBit::ValueInUse)), listIndex, message.get_source_control_function());
					LOG_WARNING("[VT Server]: Client %u change list item on object %u refused: it is open for operator input", managedWorkingSet->get_control_function()->get_address(), objectID);
					break;
				}

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
									// The retargeted soft-key column is presented by this command's own repaint:
									// nothing else repaints on its behalf, and a macro-driven change has no follow-on
									// client traffic (the Change Active Mask case makes the same guarantee).
									dispatch_repaint(managedWorkingSet);
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
									// The retargeted soft-key column is presented by this command's own repaint:
									// nothing else repaints on its behalf, and a macro-driven change has no follow-on
									// client traffic (the Change Active Mask case makes the same guarantee).
									dispatch_repaint(managedWorkingSet);
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

							if (VirtualTerminalObjectType::GraphicsContext == targetObject->get_object_type())
							{
								// Table B.58 On Change Background Colour / Table B.59 byte-25 note: writing a
								// Graphics Context's background colour at runtime fills the object with it,
								// erasing any canvas content -- by this command exactly as by Change Attribute.
								std::static_pointer_cast<GraphicsContext>(targetObject)->fill_canvas(backgroundColour);
							}

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

				// ISO 11783-6 Table 5, Data-input state: a Select Input Object command that names an object
				// other than the one open for input is refused with "another input field is currently being
				// entered" (F.7 byte 5 bit 3), leaving focus and the open edit unchanged. A command that names
				// the object already open (a re-select of the same object) is not rejected here and falls
				// through to the normal handling below.
				const std::uint16_t openForInputObjectID = managedWorkingSet->get_object_open_for_input();
				if ((nullptr != targetObject) && (NULL_OBJECT_ID != openForInputObjectID) && (openForInputObjectID != objectID))
				{
					send_select_input_object_response(objectID, get_bit(static_cast<std::uint8_t>(SelectInputObjectErrorBit::CouldNotCompleteAnotherFieldIsBeingModified)), SelectInputObjectResponse::ObjectIsNotSelectedOrIsNullOrError, message.get_source_control_function());
					LOG_WARNING("[VT Server]: Client %u select input object %u refused: object %u is currently being entered", managedWorkingSet->get_control_function()->get_address(), objectID, openForInputObjectID);
					break;
				}

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
				handle_preferred_assignment_command(message, managedWorkingSet);
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
				handle_auxiliary_assignment_type_2_command(message, managedWorkingSet);
				break;

			case Function::AuxiliaryInputStatusTypeTwoEnableCommand:
				handle_auxiliary_input_status_type_2_enable_command(message, managedWorkingSet);
				break;

			case Function::AuxiliaryInputTypeTwoStatusMessage:
				handle_auxiliary_input_type_2_status_message(message, managedWorkingSet);
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
				handle_change_end_point_command(data, managedWorkingSet);
				break;

			case Function::ChangePolygonScaleCommand:
				handle_change_polygon_scale_command(data, managedWorkingSet);
				break;

			case Function::ESCCommand:
				handle_esc_command(managedWorkingSet);
				break;

			case Function::LockUnlockMaskCommand:
				handle_lock_unlock_mask_command(data, managedWorkingSet);
				break;

			case Function::ChangeObjectLabelCommand:
				handle_change_object_label_command(data, managedWorkingSet);
				break;

			case Function::ControlAudioSignalCommand:
				handle_control_audio_signal_command(data, message.get_source_control_function());
				break;

			case Function::SetAudioVolumeCommand:
				handle_set_audio_volume_command(data, message.get_source_control_function());
				break;

			case Function::GraphicsContextCommand:
				handle_graphics_context_command(data, managedWorkingSet);
				break;

			case Function::IdentifyVTMessage:
			{
				identify_vt();
			}
			break;

			case Function::ScreenCapture:
			{
				// D.16 introduces Screen Capture at VT version 6; to a lower-version VT the code is
				// unknown, and Annex C routes an unknown command byte to the Unsupported VT Function
				// reply rather than a D.17 response.
				if (get_version() >= VTVersion::Version6)
				{
					screen_capture(data[1], data[2], message.get_source_control_function());
				}
				else
				{
					send_unsupported_vt_function(data[0], message.get_source_control_function());
				}
			}
			break;

			case Function::SelectActiveWorkingSet:
				handle_select_active_working_set_command(message, managedWorkingSet);
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
				// ISO 11783-6 Annex H: a working set compatible with VT version 6 answers each activation
				// message with a response echoing its TAN. Pair it with the outstanding activation so the
				// retry/teardown machinery stops. The TAN sits in the same byte as the message it answers
				// (H.2/H.4/H.8/H.10 byte 8, H.12 byte 4); a function with no tracked TAN maps to the
				// no-offset sentinel and is ignored, and a version-5 pair recorded nothing so the response
				// finds no outstanding entry -- either way a harmless no-op.
				const std::uint8_t responseFunction = data[0];
				const std::size_t tanByte = activation_tan_byte_offset(responseFunction);
				if ((tanByte < CAN_DATA_LENGTH) && (tanByte < data.size()))
				{
					const std::uint8_t responseTan = static_cast<std::uint8_t>((data[tanByte] >> 4) & 0x0Fu);
					handle_activation_response(managedWorkingSet, responseFunction, responseTan);
				}
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
			buffer[7] = 0xFF; // ISO 11783-6 H.4 byte 8: reserved 0xFF at version 5 and prior; stamped with the TAN for a version-6 pair below

			// Annex H.1: for a version-6 pair this stamps the TAN into byte 8 and records the activation for
			// response tracking; for any lower pair it leaves the frame byte-identical.
			stamp_and_record_activation(destination, static_cast<std::uint8_t>(Function::ButtonActivationMessage), buffer);

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
				0xFF, // ISO 11783-6 H.12 byte 4: reserved 0xFF at version 5 and prior; stamped with the TAN for a version-6 pair below
				get_byte(value, 0),
				get_byte(value, 1),
				get_byte(value, 2),
				get_byte(value, 3)
			};

			// Annex H.1: for a version-6 pair this stamps the TAN into byte 4 and records the activation for
			// response tracking; for any lower pair it leaves the frame byte-identical.
			stamp_and_record_activation(destination, static_cast<std::uint8_t>(Function::VTChangeNumericValueMessage), buffer);

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
				0xFF // ISO 11783-6 H.8 byte 8: reserved 0xFF at version 5 and prior; stamped with the TAN for a version-6 pair below
			};

			// Annex H.1: for a version-6 pair this stamps the TAN into byte 8 and records the activation for
			// response tracking; for any lower pair it leaves the frame byte-identical.
			stamp_and_record_activation(destination, static_cast<std::uint8_t>(Function::VTSelectInputObjectMessage), buffer);

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
				0xFF // ISO 11783-6 H.2 byte 8: reserved 0xFF at version 5 and prior; stamped with the TAN for a version-6 pair below
			};

			// Annex H.1: for a version-6 pair this stamps the TAN into byte 8 and records the activation for
			// response tracking; for any lower pair it leaves the frame byte-identical.
			stamp_and_record_activation(destination, static_cast<std::uint8_t>(Function::SoftKeyActivationMessage), buffer);

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

		// ISO 11783-6 G.2 byte 2 (buffer[1]) is the source address of the Working Set Master that owns the
		// VT. G.2 places an extra requirement on it for a version-6-or-later VT: when the Data Mask is
		// completely covered or not visible at all -- "for example if a proprietary screen is visible",
		// which our operator TAB screens are -- and the cover is not the mask's own input means, the VT
		// "shall set Byte 2 to FF16, FE16 or the VT's source address". We report the VT's own source
		// address: G.2's own example lists it first ("the address of the VT itself, the null address FE,
		// or the global address FF"), and it is the most self-describing of the three -- it names the VT
		// itself as what is on the screen, and a Working Set comparing byte 2 to its own address sees a
		// mismatch and correctly concludes it no longer owns the VT. This is keyed on the VT version alone
		// (a proprietary VT screen is a VT-level condition, not a per-client one), so it does not use the
		// version-6 PAIR gate. A version-5-or-prior VT never takes this branch, so its byte 2 is unchanged.
		std::uint8_t owningAddress = activeWorkingSetMasterAddress;
		if (proprietaryScreenActive && (get_version() >= VTVersion::Version6))
		{
			owningAddress = (nullptr != serverInternalControlFunction) ? serverInternalControlFunction->get_address() : isobus::NULL_CAN_ADDRESS;
		}

		buffer[0] = static_cast<std::uint8_t>(Function::VTStatusMessage);
		buffer[1] = owningAddress;
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

		release_expired_mask_locks();

		for (const auto &ws : managedWorkingSetList)
		{
			if (VirtualTerminalServerManagedWorkingSet::ObjectPoolProcessingThreadState::Success == ws->get_object_pool_processing_state())
			{
				ws->join_parsing_thread();

				// ISO 11783-6 B.29 (VT version 6 and later): a Working Set Special Controls object's initial
				// Colour Map / Colour Palette references are activated before the pool is first rendered, so
				// apply them here -- after the parse published the tree, ahead of the arbitration that puts a
				// mask on screen.
				if (get_version() >= VTVersion::Version6)
				{
					apply_working_set_special_controls(ws);
				}

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

				send_end_of_object_pool_response(false, ws->get_object_pool_faulting_parent_object_id(), ws->get_object_pool_faulting_object_id(), static_cast<std::uint8_t>(get_bit(2) | (poolWasActive ? get_bit(3) : 0)), ws->get_control_function());

				if (poolWasActive)
				{
					// Flag the working set for teardown by the pass below, which runs in this same
					// update() call, so the invalid pool is deleted and the working set suspended
					// immediately rather than left active with a half-merged pool.
					ws->request_deletion();
				}
			}
		}

		// ISO 11783-6 Annex H.1: retry outstanding activations and flag a working set whose required
		// activation responses never arrived. This runs before the loss-teardown pass below so a working
		// set flagged here is torn down (as a clause 4.6.9 unexpected shutdown) in this same update().
		update_activation_trackers();

		const bool workingSetWasTornDown = tear_down_lost_working_sets();

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
