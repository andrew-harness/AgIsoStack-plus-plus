//================================================================================================
/// @file isobus_virtual_terminal_server_auxiliary_isovt.cpp
///
/// @brief Implements the fork's AUX-N (auxiliary control; ISO 11783-6 4.5, Annex J) message
/// plumbing for the VT server: the Auxiliary Capabilities response, the assignment and input
/// status send helpers, the received-message hooks, learn-mode reporting, and the dispatch
/// handlers extracted from the message switches.
///
/// This translation unit is isovt-owned and has no upstream counterpart. Per ADR-0008, the
/// fork's additive VT-server code lives here rather than interleaved in
/// isobus_virtual_terminal_server.cpp, to keep that upstream file's merge surface small. These
/// definitions remain members of VirtualTerminalServer, declared in the upstream header.
//================================================================================================

#include "isobus/isobus/isobus_virtual_terminal_server.hpp"
#include "isobus/isobus/can_general_parameter_group_numbers.hpp"
#include "isobus/isobus/can_message.hpp"
#include "isobus/isobus/can_network_manager.hpp"
#include "isobus/isobus/can_stack_logger.hpp"

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

	bool VirtualTerminalServer::handle_auxiliary_capabilities_request(const CANMessage &message, const std::vector<std::uint8_t> &data)
	{
		bool retVal = false;
		if (data.size() >= 2)
		{
			const std::uint8_t requestType = data.at(1);
			LOG_DEBUG("[VT Server]: Client at address %u requested Auxiliary Capabilities (request type %u).", message.get_identifier().get_source_address(), requestType);
			send_auxiliary_capabilities_response(requestType, message.get_source_control_function());
			retVal = true;
		}
		return retVal;
	}

	void VirtualTerminalServer::handle_preferred_assignment_command(const CANMessage &message, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
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

	void VirtualTerminalServer::handle_auxiliary_assignment_type_2_command(const CANMessage &message, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
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

	void VirtualTerminalServer::handle_auxiliary_input_status_type_2_enable_command(const CANMessage &message, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
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

	void VirtualTerminalServer::handle_auxiliary_input_type_2_status_message(const CANMessage &message, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
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
}
