//================================================================================================
/// @file isobus_virtual_terminal_server_handlers_isovt.cpp
///
/// @brief Implements the fork's added VT command handlers and their responses for the VT server:
/// the Get Supported Objects, Extended Get/Store/Load/Delete Version, Select Colour Map, Get
/// Attribute Value, Change End Point, Change Polygon Scale, ESC, Lock/Unlock Mask and Change
/// Object Label dispatch handlers extracted from the message switches, together with the send
/// helpers that answer them.
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
#include "isobus/isobus/isobus_virtual_terminal_objects.hpp"
#include "isobus/utility/system_timing.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

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

	bool VirtualTerminalServer::handle_get_supported_objects_message(const CANMessage &message)
	{
		bool retVal = false;
		// D.14/D.15 is a Get Technical Data query about the VT itself, like Get Hardware and
		// Get Memory beside it, and its answer does not depend on any object pool. Answering
		// it here rather than from the connection-dependent path lets a control function ask
		// before it has uploaded anything -- which is when a working set actually wants to
		// know, since the answer decides what it puts in the pool.
		LOG_DEBUG("[VT Server]: Client at address %u requested the supported object list.", message.get_identifier().get_source_address());
		send_supported_objects(message.get_source_control_function());
		retVal = true;
		return retVal;
	}

	void VirtualTerminalServer::handle_extended_get_versions_message(const CANMessage &message)
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

	void VirtualTerminalServer::handle_extended_store_version_command(const CANMessage &message, const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
	{
		if (data.size() < static_cast<std::size_t>(EXTENDED_VERSION_LABEL_LENGTH) + 1u)
		{
			LOG_WARNING("[VT Server]: Received a malformed Extended Store Version command (too short to contain a 32 byte label)");
			return;
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

	void VirtualTerminalServer::handle_extended_delete_version_command(const CANMessage &message, const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
	{
		if (data.size() < static_cast<std::size_t>(EXTENDED_VERSION_LABEL_LENGTH) + 1u)
		{
			LOG_WARNING("[VT Server]: Received a malformed Extended Delete Version command (too short to contain a 32 byte label)");
			return;
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

	void VirtualTerminalServer::handle_extended_load_version_command(const CANMessage &message, const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
	{
		if (data.size() < static_cast<std::size_t>(EXTENDED_VERSION_LABEL_LENGTH) + 1u)
		{
			LOG_WARNING("[VT Server]: Received a malformed Extended Load Version command (too short to contain a 32 byte label)");
			return;
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
			return;
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

	void VirtualTerminalServer::handle_select_colour_map_command(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
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

	void VirtualTerminalServer::handle_get_attribute_value_message(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
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

	void VirtualTerminalServer::handle_change_end_point_command(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
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

	void VirtualTerminalServer::handle_change_polygon_scale_command(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
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

	void VirtualTerminalServer::handle_esc_command(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
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

	void VirtualTerminalServer::handle_lock_unlock_mask_command(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
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

	void VirtualTerminalServer::handle_change_object_label_command(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
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

	void VirtualTerminalServer::set_control_audio_signal_callback(ControlAudioSignalCallback callback)
	{
		controlAudioSignalCallback = std::move(callback);
	}

	void VirtualTerminalServer::set_set_audio_volume_callback(SetAudioVolumeCallback callback)
	{
		setAudioVolumeCallback = std::move(callback);
	}

	void VirtualTerminalServer::handle_control_audio_signal_command(const std::vector<std::uint8_t> &data, std::shared_ptr<ControlFunction> source)
	{
		// F.10: byte 2 activations (0 terminates any audio in process from the source, and frequency and
		// the durations are then ignored), bytes 3-4 frequency Hz, bytes 5-6 on-time ms, bytes 7-8
		// off-time ms, all little-endian like the surrounding change commands.
		const std::uint8_t activations = data[1];
		const std::uint16_t frequencyHz = get_little_endian_uint16(data, 2);
		const std::uint16_t onTimeMs = get_little_endian_uint16(data, 4);
		const std::uint16_t offTimeMs = get_little_endian_uint16(data, 6);

		AudioSignalCommandResult verdict = AudioSignalCommandResult::Acknowledged;
		if (controlAudioSignalCallback)
		{
			verdict = controlAudioSignalCallback(source, activations, frequencyHz, onTimeMs, offTimeMs);
		}

		// F.11 byte 2: bit 0 audio device busy, bit 4 any other error, 0 no error. F.10 has no
		// "not supported" bit, so that verdict maps to any-other-error for this command.
		std::uint8_t errorCode = 0;
		switch (verdict)
		{
			case AudioSignalCommandResult::AudioDeviceIsBusy:
				errorCode = get_bit(0);
				break;
			case AudioSignalCommandResult::NotSupported:
			case AudioSignalCommandResult::AnyOtherError:
				errorCode = get_bit(4);
				break;
			case AudioSignalCommandResult::Acknowledged:
			default:
				errorCode = 0;
				break;
		}

		send_control_audio_signal_response(errorCode, source);
		LOG_DEBUG("[VT Server]: Client %u control audio signal command: activations %u, %u Hz, on %u ms, off %u ms -> error %u", source->get_address(), activations, frequencyHz, onTimeMs, offTimeMs, errorCode);
	}

	void VirtualTerminalServer::handle_set_audio_volume_command(const std::vector<std::uint8_t> &data, std::shared_ptr<ControlFunction> source)
	{
		// F.12: byte 2 is the volume as a percent (0-100) of the operator-set maximum.
		const std::uint8_t volumePercent = data[1];

		AudioSignalCommandResult verdict = AudioSignalCommandResult::Acknowledged;
		if (setAudioVolumeCallback)
		{
			verdict = setAudioVolumeCallback(source, volumePercent);
		}

		// F.13 byte 2: bit 0 audio device busy, bit 1 command not supported (VT version 4 and later),
		// bit 4 any other error, 0 no error.
		std::uint8_t errorCode = 0;
		switch (verdict)
		{
			case AudioSignalCommandResult::AudioDeviceIsBusy:
				errorCode = get_bit(0);
				break;
			case AudioSignalCommandResult::NotSupported:
				errorCode = get_bit(1);
				break;
			case AudioSignalCommandResult::AnyOtherError:
				errorCode = get_bit(4);
				break;
			case AudioSignalCommandResult::Acknowledged:
			default:
				errorCode = 0;
				break;
		}

		send_set_audio_volume_response(errorCode, source);
		LOG_DEBUG("[VT Server]: Client %u set audio volume command: %u percent -> error %u", source->get_address(), volumePercent, errorCode);
	}

	bool VirtualTerminalServer::send_control_audio_signal_response(std::uint8_t errorCode, std::shared_ptr<ControlFunction> destination) const
	{
		// F.11: byte 1 command echo, byte 2 error bitfield, bytes 3-8 reserved 0xFF. Routed through
		// send_response, the choke point that withholds a command's response inside a macro (4.6.11.4 f).
		const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
			static_cast<std::uint8_t>(Function::ControlAudioSignalCommand),
			errorCode,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
		};
		return send_response(buffer.data(), CAN_DATA_LENGTH, destination);
	}

	bool VirtualTerminalServer::send_set_audio_volume_response(std::uint8_t errorCode, std::shared_ptr<ControlFunction> destination) const
	{
		// F.13: byte 1 command echo, byte 2 error bitfield, bytes 3-8 reserved 0xFF. Routed through
		// send_response like the Control Audio Signal response.
		const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
			static_cast<std::uint8_t>(Function::SetAudioVolumeCommand),
			errorCode,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
		};
		return send_response(buffer.data(), CAN_DATA_LENGTH, destination);
	}

	void VirtualTerminalServer::set_graphics_context_command_callback(GraphicsContextCommandCallback callback)
	{
		graphicsContextCommandCallback = std::move(callback);
	}

	void VirtualTerminalServer::handle_graphics_context_command(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet)
	{
		// F.56: byte 1 the function (0xB8), bytes 2-3 the Graphics Context object ID, byte 4 the sub-command
		// ID, bytes 5-n the parameters. Decoded little-endian like the surrounding commands.
		const std::uint16_t objectID = get_little_endian_uint16(data, 1);
		const std::uint8_t subCommand = data[3];
		auto object = managedWorkingSet->get_object_by_id(objectID);

		// F.57 byte 5 bit 0: invalid object ID, or the object is not a Graphics Context object.
		if ((nullptr == object) || (VirtualTerminalObjectType::GraphicsContext != object->get_object_type()))
		{
			send_graphics_context_response(objectID, subCommand, get_bit(0), managedWorkingSet->get_control_function());
			LOG_WARNING("[VT Server]: Client %u graphics context command: object id %u is not a graphics context in this pool", managedWorkingSet->get_control_function()->get_address(), objectID);
			return;
		}

		// F.57 byte 5 bit 1: invalid sub-command ID. Table F.1 defines sub-commands 0-20; the function
		// itself is supported, so an out-of-range sub-command is a bit-1 error, not an Unsupported VT
		// Function (0xFD).
		if (subCommand > static_cast<std::uint8_t>(GraphicsContextSubCommandID::CopyViewportToPictureGraphic))
		{
			send_graphics_context_response(objectID, subCommand, get_bit(1), managedWorkingSet->get_control_function());
			LOG_WARNING("[VT Server]: Client %u graphics context command on object %u has an invalid sub-command id of %u", managedWorkingSet->get_control_function()->get_address(), objectID, subCommand);
			return;
		}

		// F.56: a sub-command smaller than 8 bytes is padded to 8, so every fixed-length sub-command's
		// parameters arrive in one frame; a larger one uses Transport Protocol and arrives reassembled here.
		// Bound each sub-command's parameters against the received length so the painter cannot read past the
		// data. Sub-commands 8-13 are the drawing primitives (fixed 4-byte for 8-11; variable for 12 Draw
		// Polygon and 13 Draw Text, whose declared point-count / string-length byte sets the length). Sub-
		// commands 14-17 are the viewport ops: 14 Pan Viewport, 15 Zoom Viewport and 17 Change Viewport Size
		// are fixed 4-byte; 16 Pan and Zoom Viewport is a fixed 8-byte block (bytes 5-12) that exceeds one
		// frame, so it arrives TP-reassembled and the same single length check below bounds it -- there is no
		// "variable" handling for it, only a larger fixed length. Sub-commands 18-20 are not executed in this
		// slice; the painter returns NotExecuted and reads no parameters, so they need no bound here.
		std::size_t requiredParameterBytes = 0;
		switch (static_cast<GraphicsContextSubCommandID>(subCommand))
		{
			case GraphicsContextSubCommandID::SetGraphicsCursor: // F.56 bytes 5-8: X, Y (signed)
			case GraphicsContextSubCommandID::MoveGraphicsCursor: // F.56 bytes 5-8: X offset, Y offset (signed)
			case GraphicsContextSubCommandID::EraseRectangle: // F.56 bytes 5-8: width, height
			case GraphicsContextSubCommandID::DrawPoint: // F.56 bytes 5-8: X, Y offset (signed)
			case GraphicsContextSubCommandID::DrawLine: // F.56 bytes 5-8: end X, Y offset (signed)
			case GraphicsContextSubCommandID::DrawRectangle: // F.56 bytes 5-8: width, height
			case GraphicsContextSubCommandID::DrawClosedEllipse: // F.56 bytes 5-8: width, height
			case GraphicsContextSubCommandID::PanViewport: // F.56 bytes 5-8: Viewport X, Y attributes (signed)
			case GraphicsContextSubCommandID::ZoomViewport: // F.56 bytes 5-8: zoom value (float)
			case GraphicsContextSubCommandID::ChangeViewportSize: // F.56 bytes 5-8: new width, height
				requiredParameterBytes = 4;
				break;

			case GraphicsContextSubCommandID::PanAndZoomViewport: // F.56 bytes 5-12: Viewport X, Y (signed) + zoom (float)
				requiredParameterBytes = 8;
				break;

			case GraphicsContextSubCommandID::SetForegroundColour: // F.56 byte 5: colour
			case GraphicsContextSubCommandID::SetBackgroundColour: // F.56 byte 5: colour
				requiredParameterBytes = 1;
				break;

			case GraphicsContextSubCommandID::SetLineAttributesObjectID: // F.56 bytes 5-6: object ID
			case GraphicsContextSubCommandID::SetFillAttributesObjectID: // F.56 bytes 5-6: object ID
			case GraphicsContextSubCommandID::SetFontAttributesObjectID: // F.56 bytes 5-6: object ID
				requiredParameterBytes = 2;
				break;

			case GraphicsContextSubCommandID::DrawPolygon:
				// F.56 sub-command 12: byte 5 is the point count N, then N points of 4 bytes each. Validate
				// the count byte is present, then the whole point list against the declared count. A count
				// that overruns the received data is a bit-2 parameter error (the frame did not carry the
				// points it declared).
				if (data.size() < 5u)
				{
					requiredParameterBytes = 1; // no count byte: fail the length check below with bit 2
				}
				else
				{
					requiredParameterBytes = 1u + (4u * static_cast<std::size_t>(data[4]));
				}
				break;

			case GraphicsContextSubCommandID::DrawText:
				// F.56 sub-command 13: byte 5 opacity, byte 6 text length L, then L text bytes. Validate the
				// opacity and length bytes are present, then the whole string against the declared length.
				if (data.size() < 6u)
				{
					requiredParameterBytes = 2; // no length byte: fail the length check below with bit 2
				}
				else
				{
					requiredParameterBytes = 2u + static_cast<std::size_t>(data[5]);
				}
				break;

			default:
				requiredParameterBytes = 0;
				break;
		}

		if (data.size() < (4u + requiredParameterBytes))
		{
			// F.57 byte 5 bit 2: the parameter is invalid because the message is too short to carry it -- the
			// declared point count (sub-command 12) or string length (sub-command 13) exceeds the data
			// received, or a fixed-length sub-command's frame is short.
			send_graphics_context_response(objectID, subCommand, get_bit(2), managedWorkingSet->get_control_function());
			LOG_WARNING("[VT Server]: Client %u graphics context command on object %u sub-command %u is too short for its parameters", managedWorkingSet->get_control_function()->get_address(), objectID, subCommand);
			return;
		}

		GraphicsContextCommandResult verdict = GraphicsContextCommandResult::NotExecuted;
		if (graphicsContextCommandCallback)
		{
			verdict = graphicsContextCommandCallback(managedWorkingSet, objectID, subCommand, data.data() + 4, data.size() - 4);
		}

		// F.57 byte 5: map the painter verdict onto the error bits. Object-ID (bit 0) and sub-command-ID
		// (bit 1) errors are handled above, so the painter reports only bits 2, 3 and 4. With no painter
		// installed the verdict stays NotExecuted, which is the any-other-error bit (the VT executes no
		// drawing).
		std::uint8_t errorBitfield = 0;
		switch (verdict)
		{
			case GraphicsContextCommandResult::Executed:
				errorBitfield = 0;
				break;

			case GraphicsContextCommandResult::InvalidParameter:
				errorBitfield = get_bit(2);
				break;

			case GraphicsContextCommandResult::InvalidResult:
				errorBitfield = get_bit(3);
				break;

			case GraphicsContextCommandResult::NotExecuted:
			default:
				errorBitfield = get_bit(4);
				break;
		}

		send_graphics_context_response(objectID, subCommand, errorBitfield, managedWorkingSet->get_control_function());

		if (GraphicsContextCommandResult::Executed == verdict)
		{
			// A sub-command that executed may have changed the canvas or an attribute the screen shows;
			// refresh the display the same way every other change command does.
			dispatch_repaint(managedWorkingSet);
		}
		LOG_DEBUG("[VT Server]: Client %u graphics context command on object %u sub-command %u -> error %u", managedWorkingSet->get_control_function()->get_address(), objectID, subCommand, errorBitfield);
	}

	bool VirtualTerminalServer::send_graphics_context_response(std::uint16_t objectID, std::uint8_t subCommand, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			// F.57: byte 1 command echo 0xB8, bytes 2-3 object ID, byte 4 sub-command ID, byte 5 error
			// bitfield, bytes 6-8 reserved 0xFF.
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::GraphicsContextCommand),
				get_low_byte(objectID),
				get_high_byte(objectID),
				subCommand,
				errorBitfield,
				0xFF,
				0xFF,
				0xFF
			};

			retVal = send_response(buffer.data(), CAN_DATA_LENGTH, destination);
		}
		return retVal;
	}

	bool VirtualTerminalServer::send_control_audio_signal_termination(std::shared_ptr<ControlFunction> destination) const
	{
		bool retVal = false;

		if (nullptr != destination)
		{
			// H.22: byte 1 VT function 0x0A, byte 2 termination cause with bit 0 set (the only defined
			// cause), bytes 3-8 reserved 0xFF.
			const std::array<std::uint8_t, CAN_DATA_LENGTH> buffer = {
				static_cast<std::uint8_t>(Function::VTControlAudioSignalTerminationMessage),
				get_bit(0),
				0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
			};

			// Like the VT ESC and Pointing Event messages, this goes directly onto the bus rather than
			// through send_response: H.22 is a VT-originated event (the VT terminated a Control Audio
			// Signal before completion), not a response to a command, so it must reach the working set
			// even during macro execution, which 4.6.11.4 f suppresses only for command responses.
			retVal = CANNetworkManager::CANNetwork.send_can_message(static_cast<std::uint32_t>(CANLibParameterGroupNumber::VirtualTerminalToECU),
			                                                        buffer.data(),
			                                                        CAN_DATA_LENGTH,
			                                                        serverInternalControlFunction,
			                                                        destination,
			                                                        get_priority());
		}
		return retVal;
	}
}
