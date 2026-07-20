//================================================================================================
/// @file isobus_virtual_terminal_working_set_base.cpp
///
/// @brief Implements a base class for a VT working set that isolates common working set functionality
/// so that things useful to VT designer application and a VT server application can be shared.
/// @author Adrian Del Grosso
///
/// @copyright 2024 The Open-Agriculture Developers
//================================================================================================
#include "isobus/isobus/isobus_virtual_terminal_working_set_base.hpp"

#include "isobus/isobus/can_stack_logger.hpp"
#include "isobus/utility/to_string.hpp"

#include <cstring>

namespace isobus
{
	/// @brief Creates an 8-bit mask for the requested bit.
	/// @param bitIndex The zero-based bit index.
	/// @returns The requested bit mask, or zero if the bit index is invalid.
	static constexpr std::uint8_t get_bit_mask(std::uint_fast8_t bitIndex) noexcept
	{
		return (bitIndex < 8U) ? static_cast<std::uint8_t>(1U << bitIndex) : static_cast<std::uint8_t>(0U);
	}

	/// @brief Applies a mask to an 8-bit value.
	/// @param value The value to mask.
	/// @param mask The mask to apply.
	/// @returns The masked 8-bit value.
	static constexpr std::uint8_t get_masked_value(std::uint8_t value, std::uint_fast16_t mask) noexcept
	{
		return static_cast<std::uint8_t>(value & mask);
	}

	/// @brief Determines whether any bit selected by a mask is set.
	/// @param value The value to inspect.
	/// @param mask The mask selecting the bits to inspect.
	/// @returns true if any selected bit is set, otherwise false.
	static constexpr bool is_bit_set(std::uint8_t value, std::uint_fast16_t mask) noexcept
	{
		return 0 != get_masked_value(value, mask);
	}

	/// @brief Decodes an unsigned 16-bit little-endian value from a byte buffer.
	/// @param data The byte buffer containing the value.
	/// @param index The index of the least-significant byte.
	/// @returns The decoded unsigned 16-bit value.
	static std::uint16_t get_little_endian_uint16(const std::uint8_t *data, std::size_t index) noexcept
	{
		const auto lowByte = static_cast<std::uint16_t>(data[index]);
		const auto highByte = static_cast<std::uint16_t>(data[index + 1]);

		return static_cast<std::uint16_t>(lowByte | static_cast<std::uint16_t>(highByte << 8));
	}

	/// @brief Decodes a signed 16-bit little-endian value from a byte buffer.
	/// @param data The byte buffer containing the value.
	/// @param index The index of the least-significant byte.
	/// @returns The decoded signed 16-bit value.
	static std::int16_t get_little_endian_int16(const std::uint8_t *data, std::size_t index) noexcept
	{
		return static_cast<std::int16_t>(get_little_endian_uint16(data, index));
	}

	/// @brief Decodes an unsigned 32-bit little-endian value from a byte buffer.
	/// @param data The byte buffer containing the value.
	/// @param index The index of the least-significant byte.
	/// @returns The decoded unsigned 32-bit value.
	static std::uint32_t get_little_endian_uint32(const std::uint8_t *data, std::size_t index) noexcept
	{
		return static_cast<std::uint32_t>(data[index]) |
		  (static_cast<std::uint32_t>(data[index + 1]) << 8) |
		  (static_cast<std::uint32_t>(data[index + 2]) << 16) |
		  (static_cast<std::uint32_t>(data[index + 3]) << 24);
	}

	std::uint16_t VirtualTerminalWorkingSetBase::get_object_pool_faulting_object_id()
	{
		std::lock_guard<std::mutex> lock(managedWorkingSetMutex);
		return faultingObjectID;
	}

	bool VirtualTerminalWorkingSetBase::is_object_pool_within_declared_iop_size() const
	{
		return (0 == iopSize) || (transferredIopSize <= iopSize);
	}

	void VirtualTerminalWorkingSetBase::add_iop_raw_data(const std::vector<std::uint8_t> &dataToAdd)
	{
		transferredIopSize += dataToAdd.size();
		iopFilesRawData.push_back(dataToAdd);
	}

	std::size_t VirtualTerminalWorkingSetBase::get_number_iop_files() const
	{
		return iopFilesRawData.size();
	}

	std::vector<std::uint8_t> &VirtualTerminalWorkingSetBase::get_iop_raw_data(std::size_t index)
	{
		return iopFilesRawData.at(index);
	}

	VTColourVector VirtualTerminalWorkingSetBase::get_colour(std::uint8_t colourIndex) const
	{
		return workingSetColourTable.get_colour(colourIndex);
	}

	std::shared_ptr<const ObjectTree> VirtualTerminalWorkingSetBase::get_object_tree() const
	{
		const std::lock_guard<std::mutex> lock(objectTreeMutex);
		return publishedObjectTree;
	}

	void VirtualTerminalWorkingSetBase::publish_object_tree()
	{
		// The copy is made outside the lock: it is the expensive part, and it touches only the staging
		// tree, which the parse that is publishing owns exclusively.
		auto snapshot = std::make_shared<const ObjectTree>(vtObjectTree);

		// The outgoing tree is carried out of the locked scope so its destructor -- which frees a node
		// per object once the last reader lets go -- runs with the mutex released. The lock covers the
		// two pointer moves and nothing else.
		std::shared_ptr<const ObjectTree> previous;

		{
			const std::lock_guard<std::mutex> lock(objectTreeMutex);
			previous = std::move(publishedObjectTree);
			publishedObjectTree = std::move(snapshot);
		}
	}

	void VirtualTerminalWorkingSetBase::clear_published_object_tree()
	{
		auto snapshot = std::make_shared<const ObjectTree>();
		std::shared_ptr<const ObjectTree> previous;

		{
			const std::lock_guard<std::mutex> lock(objectTreeMutex);
			previous = std::move(publishedObjectTree);
			publishedObjectTree = std::move(snapshot);
		}
	}

	bool VirtualTerminalWorkingSetBase::add_or_replace_object(std::shared_ptr<VTObject> objectToAdd)
	{
		bool retVal = false;

		if (nullptr != objectToAdd)
		{
			const auto existingObject = vtObjectTree.find(objectToAdd->get_id());

			if ((vtObjectTree.end() != existingObject) &&
			    (nullptr != existingObject->second) &&
			    (existingObject->second->get_object_type() != objectToAdd->get_object_type()))
			{
				LOG_ERROR("[WS]: Object %u is already in the object pool as type %u and cannot be re-declared as type %u. ISO 11783-6 clause 4.6.1.1 requires an object ID to be unique within a working set's object pool.",
				          static_cast<unsigned int>(objectToAdd->get_id()),
				          static_cast<unsigned int>(existingObject->second->get_object_type()),
				          static_cast<unsigned int>(objectToAdd->get_object_type()));
			}
			else
			{
				vtObjectTree[objectToAdd->get_id()] = objectToAdd;
				retVal = true;
			}
		}
		return retVal;
	}

	bool VirtualTerminalWorkingSetBase::parse_next_object(std::uint8_t *&iopData, std::uint32_t &iopLength)
	{
		bool retVal = false;

		if (iopLength > 3)
		{
			// We at least have object ID and type
			auto decodedID = get_little_endian_uint16(iopData, 0);
			auto decodedType = static_cast<VirtualTerminalObjectType>(iopData[2]);

			switch (decodedType)
			{
				case VirtualTerminalObjectType::WorkingSet:
				{
					// Resolved against the staging tree, not the published snapshot: the pool being
					// parsed is the one this check is about, and the snapshot still holds the previous
					// pool until this parse completes.
					const auto existingWorkingSetObject = VTObject::get_object_by_id(workingSetID, vtObjectTree);

					if ((NULL_OBJECT_ID == workingSetID) ||
					    ((nullptr != existingWorkingSetObject) &&
					     (existingWorkingSetObject->get_id() == decodedID)))
					{
						workingSetID = decodedID;
						auto tempObject = std::make_shared<WorkingSet>();

						if (iopLength >= tempObject->get_minumum_object_length())
						{
							tempObject->set_id(decodedID);
							tempObject->set_background_color(iopData[3]);
							tempObject->set_selectable(iopData[4]);
							tempObject->set_active_mask(get_little_endian_uint16(iopData, 5));

							// Now add child objects
							const std::uint8_t childrenToFollow = iopData[7];
							const auto sizeOfChildren = static_cast<std::uint16_t>(childrenToFollow * 6); // ID, X, Y 2 bytes each
							const std::uint8_t numberOfMacrosToFollow = iopData[8];
							const std::uint8_t numberOfLanguagesToFollow = iopData[9];
							iopLength -= 10; // Subtract the bytes we've processed so far.
							iopData += 10; // Move the pointer

							if (iopLength >= sizeOfChildren)
							{
								for (std::uint_fast8_t i = 0; i < childrenToFollow; i++)
								{
									auto childID = get_little_endian_uint16(iopData, 0);
									auto childX = get_little_endian_int16(iopData, 2);
									auto childY = get_little_endian_int16(iopData, 4);
									tempObject->add_child(childID, childX, childY);
									iopLength -= 6;
									iopData += 6;
								}

								// Next, parse macro list
								retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
								if (retVal)
								{
									// Next, parse language list
									if (iopLength >= static_cast<std::uint16_t>(numberOfLanguagesToFollow * 2))
									{
										for (std::uint_fast8_t i = 0; i < numberOfLanguagesToFollow; i++)
										{
											std::string langCode;
											langCode.push_back(static_cast<char>(iopData[0]));
											langCode.push_back(static_cast<char>(iopData[1]));
											iopLength -= 2;
											iopData += 2;
											tempObject->add_language_code(langCode);
											LOG_DEBUG("[WS]: IOP Language parsed: " + langCode);
										}
										retVal = true;
									}
									else
									{
										LOG_ERROR("[WS]: Not enough IOP data to parse working set language codes for object " + isobus::to_string(static_cast<int>(decodedID)));
									}
								}
							}
							else
							{
								LOG_ERROR("[WS]: Not enough IOP data to parse working set children for object " + isobus::to_string(static_cast<int>(decodedID)));
							}
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse working set object " + isobus::to_string(static_cast<int>(decodedID)));
						}

						if (retVal)
						{
							retVal = add_or_replace_object(tempObject);
						}
					}
					else
					{
						LOG_ERROR("[WS]: Multiple working set objects are not allowed in the object pool. Faulting object " + isobus::to_string(static_cast<int>(decodedID)));
					}
				}
				break;

				case VirtualTerminalObjectType::DataMask:
				{
					auto tempObject = std::make_shared<DataMask>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_background_color(iopData[3]);
						tempObject->set_soft_key_mask(get_little_endian_uint16(iopData, 4));
						// Now add child objects
						const std::uint8_t childrenToFollow = iopData[6];
						const auto sizeOfChildren = static_cast<std::uint16_t>(childrenToFollow * 6); // ID, X, Y 2 bytes each
						const std::uint8_t numberOfMacrosToFollow = iopData[7];
						iopLength -= 8; // Subtract the bytes we've processed so far.
						iopData += 8; // Move the pointer

						if (iopLength >= sizeOfChildren)
						{
							for (std::uint_fast8_t i = 0; i < childrenToFollow; i++)
							{
								auto childID = get_little_endian_uint16(iopData, 0);
								std::int16_t childX = get_little_endian_int16(iopData, 2);
								std::int16_t childY = get_little_endian_int16(iopData, 4);
								tempObject->add_child(childID, childX, childY);
								iopLength -= 6;
								iopData += 6;
							}

							// Next, parse macro list
							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse data mask children for object " + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse data mask object for object " + isobus::to_string(static_cast<int>(decodedID)));
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::AlarmMask:
				{
					auto tempObject = std::make_shared<AlarmMask>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_background_color(iopData[3]);
						tempObject->set_soft_key_mask(get_little_endian_uint16(iopData, 4));

						if (iopData[6] <= static_cast<std::uint8_t>(AlarmMask::Priority::Low))
						{
							tempObject->set_mask_priority(static_cast<AlarmMask::Priority>(iopData[6]));

							if (iopData[7] <= static_cast<std::uint8_t>(AlarmMask::AcousticSignal::None))
							{
								// Now add child objects
								const std::uint8_t childrenToFollow = iopData[8];
								const auto sizeOfChildren = static_cast<std::uint16_t>(childrenToFollow * 6); // ID, X, Y 2 bytes each
								const std::uint8_t numberOfMacrosToFollow = iopData[9];
								iopLength -= 10; // Subtract the bytes we've processed so far.
								iopData += 10; // Move the pointer

								if (iopLength >= sizeOfChildren)
								{
									for (std::uint_fast8_t i = 0; i < childrenToFollow; i++)
									{
										auto childID = get_little_endian_uint16(iopData, 0);
										auto childX = get_little_endian_int16(iopData, 2);
										auto childY = get_little_endian_int16(iopData, 4);
										tempObject->add_child(childID, childX, childY);
										iopLength -= 6;
										iopData += 6;
									}

									// Next, parse macro list
									retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
								}
								else
								{
									LOG_ERROR("[WS]: Not enough IOP data to parse alarm mask children for object " + isobus::to_string(static_cast<int>(decodedID)));
								}
							}
							else
							{
								LOG_ERROR("[WS]: Invalid acoustic signal priority " +
								          isobus::to_string(static_cast<int>(iopData[7])) +
								          " specified for alarm mask object " +
								          isobus::to_string(static_cast<int>(decodedID)));
							}
						}
						else
						{
							LOG_ERROR("[WS]: Invalid alarm mask priority " +
							          isobus::to_string(static_cast<int>(iopData[6])) +
							          " specified for alarm mask object" +
							          isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse alarm mask object for object " + isobus::to_string(static_cast<int>(decodedID)));
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::Container:
				{
					auto tempObject = std::make_shared<Container>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(get_little_endian_uint16(iopData, 5));
						tempObject->set_hidden(0 != iopData[7]);

						if (iopData[7] > 1)
						{
							LOG_WARNING("[WS]: Container " +
							            isobus::to_string(static_cast<int>(decodedID)) +
							            " hidden attribute is not a supported value. Assuming that it is hidden.");
						}

						// Now add child objects
						const std::uint8_t childrenToFollow = iopData[8];
						const auto sizeOfChildren = static_cast<std::uint16_t>(childrenToFollow * 6); // ID, X, Y 2 bytes each
						const std::uint8_t numberOfMacrosToFollow = iopData[9];
						iopLength -= 10; // Subtract the bytes we've processed so far.
						iopData += 10; // Move the pointer

						if (iopLength >= sizeOfChildren)
						{
							for (std::uint_fast8_t i = 0; i < childrenToFollow; i++)
							{
								auto childID = get_little_endian_uint16(iopData, 0);
								auto childX = get_little_endian_int16(iopData, 2);
								auto childY = get_little_endian_int16(iopData, 4);
								tempObject->add_child(childID, childX, childY);
								iopLength -= 6;
								iopData += 6;
							}

							// Next, parse macro list
							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse container children for object " + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse container object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::WindowMask:
				{
					auto tempObject = std::make_shared<WindowMask>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						retVal = true;
						tempObject->set_id(decodedID);

						if ((iopData[3] != 1) && (iopData[3] != 2))
						{
							LOG_WARNING("[WS]: Unknown window mask width for object %u. Allowed range is 1-2.", decodedID);
						}
						tempObject->set_width(iopData[3]);

						if ((iopData[4] < 1) || (iopData[4] > 6))
						{
							LOG_WARNING("[WS]: Unknown window mask height for object %u. Allowed range is 1-6.", decodedID);
						}
						tempObject->set_height(iopData[4]);

						if (iopData[5] > 18)
						{
							LOG_ERROR("[WS]: Unknown window mask type for object %u. Allowed range is 1-18.", decodedID);
							retVal = false;
						}
						else
						{
							tempObject->set_window_type(static_cast<WindowMask::WindowType>(iopData[5]));
						}

						if (retVal)
						{
							tempObject->set_background_color(iopData[6]);
							tempObject->set_options(iopData[7]);

							const auto name = get_little_endian_uint16(iopData, 8);
							const auto title = get_little_endian_uint16(iopData, 10);
							const auto icon = get_little_endian_uint16(iopData, 12);

							tempObject->set_name_object_id(name);
							tempObject->set_title_object_id(title);
							tempObject->set_icon_object_id(icon);

							const std::uint8_t numberOfObjectReferences = iopData[14];
							const std::uint8_t numberOfChildObjects = iopData[15];
							const std::uint8_t numberOfMacrosToFollow = iopData[16];
							const auto sizeOfChildren = static_cast<std::uint16_t>(numberOfChildObjects * 6); // ID, X, Y 2 bytes each

							switch (tempObject->get_window_type())
							{
								case WindowMask::WindowType::StringOutputValue1x1:
								case WindowMask::WindowType::NumericOutputValueNoUnits1x1:
								case WindowMask::WindowType::SingleButton1x1:
								case WindowMask::WindowType::StringInputValue1x1:
								case WindowMask::WindowType::SingleButton2x1:
								case WindowMask::WindowType::HorizontalLinearBarGraphNoUnits2x1:
								case WindowMask::WindowType::NumericOutputValueNoUnits2x1:
								case WindowMask::WindowType::NumericInputValueNoUnits1x1:
								case WindowMask::WindowType::HorizontalLinearBarGraphNoUnits1x1:
								case WindowMask::WindowType::StringOutputValue2x1:
								case WindowMask::WindowType::StringInputValue2x1:
								case WindowMask::WindowType::NumericInputValueNoUnits2x1:
								{
									if (1 != numberOfObjectReferences)
									{
										LOG_ERROR("[WS]: Window mask %u has an invalid number of object references. Value must be exactly 1.", decodedID);
									}
								}
								break;

								case WindowMask::WindowType::NumericOutputValueWithUnits1x1:
								case WindowMask::WindowType::DoubleButton2x1:
								case WindowMask::WindowType::NumericInputValueWithUnits1x1:
								case WindowMask::WindowType::NumericOutputValueWithUnits2x1:
								case WindowMask::WindowType::NumericInputValueWithUnits2x1:
								case WindowMask::WindowType::DoubleButton1x1:
								{
									if (2 != numberOfObjectReferences)
									{
										LOG_ERROR("[WS]: Window mask %u has an invalid number of object references. Value must be exactly 2.", decodedID);
									}
								}
								break;

								case WindowMask::WindowType::Freeform:
								{
									if (0 != numberOfObjectReferences)
									{
										LOG_ERROR("[WS]: Window mask %u has an invalid number of object references. Value must be exactly 0.", decodedID);
									}
								}
								break;
							}

							iopLength -= tempObject->get_minumum_object_length(); // Subtract the bytes we've processed so far.
							iopData += tempObject->get_minumum_object_length(); // Move the pointer

							if (iopLength >= static_cast<std::uint32_t>(2 * numberOfObjectReferences))
							{
								for (std::uint_fast8_t i = 0; i < numberOfObjectReferences; i++)
								{
									auto childID = get_little_endian_uint16(iopData, 0);
									tempObject->add_child(childID, 0, 0);
									iopLength -= 2;
									iopData += 2;
								}

								if (iopLength >= sizeOfChildren)
								{
									for (std::uint_fast8_t i = 0; i < numberOfChildObjects; i++)
									{
										auto childID = get_little_endian_uint16(iopData, 0);
										std::int16_t childX = get_little_endian_int16(iopData, 2);
										std::int16_t childY = get_little_endian_int16(iopData, 4);
										tempObject->add_child(childID, childX, childY);
										iopLength -= 6;
										iopData += 6;
									}

									// Next, parse macro list
									retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
								}
								else
								{
									LOG_ERROR("[WS]: Not enough IOP data to parse children for object " + isobus::to_string(static_cast<int>(decodedID)));
									retVal = false;
								}
							}
							else
							{
								LOG_ERROR("[WS]: Not enough IOP data to parse object references for object " + isobus::to_string(static_cast<int>(decodedID)));
								retVal = false;
							}

							if (retVal)
							{
								retVal = add_or_replace_object(tempObject);
							}
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse window mask object.");
					}
				}
				break;

				case VirtualTerminalObjectType::SoftKeyMask:
				{
					auto tempObject = std::make_shared<SoftKeyMask>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_background_color(iopData[3]);

						// Now add child objects
						const std::uint8_t childrenToFollow = iopData[4];
						const auto sizeOfChildren = static_cast<std::uint16_t>(childrenToFollow * 2); // ID 2 bytes
						const std::uint8_t numberOfMacrosToFollow = iopData[5];
						iopLength -= 6; // Subtract the bytes we've processed so far.
						iopData += 6; // Move the pointer

						if (iopLength >= sizeOfChildren)
						{
							// For soft key masks, no x,y positions are included
							for (std::uint_fast8_t i = 0; i < childrenToFollow; i++)
							{
								auto childID = get_little_endian_uint16(iopData, 0);
								tempObject->add_child(childID, 0, 0);
								iopLength -= 2;
								iopData += 2;
							}

							// Next, parse macro list
							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse soft key mask children for object " + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse soft key mask object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::Key:
				{
					auto tempObject = std::make_shared<Key>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_background_color(iopData[3]);
						tempObject->set_key_code(iopData[4]);

						// Now add child objects
						const std::uint8_t childrenToFollow = iopData[5];
						const auto sizeOfChildren = static_cast<std::uint16_t>(childrenToFollow * 6); // ID, X, Y 2 bytes each
						const std::uint8_t numberOfMacrosToFollow = iopData[6];
						iopLength -= 7; // Subtract the bytes we've processed so far.
						iopData += 7; // Move the pointer

						if (iopLength >= sizeOfChildren)
						{
							for (std::uint_fast8_t i = 0; i < childrenToFollow; i++)
							{
								auto childID = get_little_endian_uint16(iopData, 0);
								auto childX = get_little_endian_int16(iopData, 2);
								auto childY = get_little_endian_int16(iopData, 4);
								tempObject->add_child(childID, childX, childY);
								iopLength -= 6;
								iopData += 6;
							}

							// Next, parse macro list
							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse key children for object" + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to key object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::Button:
				{
					auto tempObject = std::make_shared<Button>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(get_little_endian_uint16(iopData, 5));
						tempObject->set_background_color(iopData[7]);
						tempObject->set_border_colour(iopData[8]);
						tempObject->set_key_code(iopData[9]);
						tempObject->set_options(iopData[10]);

						// Now add child objects
						const std::uint8_t childrenToFollow = iopData[11];
						const auto sizeOfChildren = static_cast<std::uint16_t>(childrenToFollow * 6); // ID, X, Y 2 bytes each
						const std::uint8_t numberOfMacrosToFollow = iopData[12];
						iopLength -= 13; // Subtract the bytes we've processed so far.
						iopData += 13; // Move the pointer

						if (iopLength >= sizeOfChildren)
						{
							for (std::uint_fast8_t i = 0; i < childrenToFollow; i++)
							{
								auto childID = get_little_endian_uint16(iopData, 0);
								std::int16_t childX = get_little_endian_int16(iopData, 2);
								std::int16_t childY = get_little_endian_int16(iopData, 4);
								tempObject->add_child(childID, childX, childY);
								iopLength -= 6;
								iopData += 6;
							}

							// Next, parse macro list
							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse button children for object " + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse button object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::KeyGroup:
				{
					auto tempObject = std::make_shared<KeyGroup>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_options(iopData[3]);
						tempObject->set_name_object_id(get_little_endian_uint16(iopData, 4)); // Output string for the object's name/label
						tempObject->set_key_group_icon(get_little_endian_uint16(iopData, 6));

						// The object count and the macro count are adjacent, and both precede the
						// child list, so the macro count must be read before the children are parsed.
						const std::uint8_t numberChildrenToFollow = iopData[8];
						const std::uint8_t numberOfMacrosToFollow = iopData[9];
						iopLength -= 10;
						iopData += 10;

						if (numberChildrenToFollow <= KeyGroup::MAX_CHILD_KEYS)
						{
							if (iopLength >= static_cast<std::uint32_t>(2 * numberChildrenToFollow))
							{
								for (std::uint_fast8_t i = 0; i < numberChildrenToFollow; i++)
								{
									tempObject->add_child(get_little_endian_uint16(iopData, 0), 0, 0);
									iopLength -= 2;
									iopData += 2;
								}

								retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
							}
							else
							{
								LOG_ERROR("[WS]: Not enough IOP data to parse key group object children");
							}
						}
						else
						{
							LOG_ERROR("[WS]: Key group " + isobus::to_string(static_cast<int>(decodedID)) + " has too many child key objects! Only 4 are permitted.");
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse key group object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::InputBoolean:
				{
					auto tempObject = std::make_shared<InputBoolean>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_background_color(iopData[3]);
						tempObject->set_width(get_little_endian_uint16(iopData, 4));
						tempObject->set_height(get_little_endian_uint16(iopData, 4));
						tempObject->set_foreground_colour_object_id(get_little_endian_uint16(iopData, 6)); // Child Font Attribute
						tempObject->set_variable_reference(get_little_endian_uint16(iopData, 8)); // Add variable reference
						tempObject->set_value(iopData[10]);
						tempObject->set_enabled(iopData[11]);

						// No children list to parse

						// Next, parse macro list
						const std::uint8_t numberOfMacrosToFollow = iopData[12];
						iopData += 13;
						iopLength -= 13;

						retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse input boolean object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::InputString:
				{
					auto tempObject = std::make_shared<InputString>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(get_little_endian_uint16(iopData, 5));
						tempObject->set_background_color(iopData[7]);
						tempObject->set_font_attributes(get_little_endian_uint16(iopData, 8));
						tempObject->set_input_attributes(get_little_endian_uint16(iopData, 10));
						tempObject->set_options(iopData[12]);
						tempObject->set_variable_reference(get_little_endian_uint16(iopData, 13)); // Number variable
						tempObject->set_justification_bitfield(iopData[15]);

						const std::size_t lengthOfStringObject = iopData[16];
						const std::int64_t iopLengthRemaining = iopLength - 17; // Use larger signed int to detect negative rollover

						if (iopLengthRemaining > static_cast<std::uint16_t>(lengthOfStringObject + 2)) // +2 is for enabled byte and number of macros to follow
						{
							std::string tempString;
							tempString.reserve(lengthOfStringObject);

							for (std::uint_fast16_t i = 0; i < lengthOfStringObject; i++)
							{
								tempString.push_back(static_cast<char>(iopData[17 + i]));
							}
							tempObject->set_value(tempString);

							tempObject->set_enabled(iopData[17 + lengthOfStringObject]);
							iopData += 18 + lengthOfStringObject;
							iopLength -= 18 + static_cast<std::uint32_t>(lengthOfStringObject);

							// Next, parse macro list
							const std::uint8_t numberOfMacrosToFollow = iopData[0];

							iopData++;
							iopLength--;

							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse input string object value");
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse input string object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::InputNumber:
				{
					auto tempObject = std::make_shared<InputNumber>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(get_little_endian_uint16(iopData, 5));
						tempObject->set_background_color(iopData[7]);
						tempObject->set_font_attributes(get_little_endian_uint16(iopData, 8));
						tempObject->set_options(iopData[10]);
						tempObject->set_variable_reference(get_little_endian_uint16(iopData, 11)); // Number variable
						tempObject->set_value(get_little_endian_uint32(iopData, 13));
						tempObject->set_minimum_value(get_little_endian_uint32(iopData, 17));
						tempObject->set_maximum_value(get_little_endian_uint32(iopData, 21));
						tempObject->set_offset(get_little_endian_uint32(iopData, 25));
						float tempFloat = 0;
						std::uint8_t floatBuffer[4] = {
							iopData[29],
							iopData[30],
							iopData[31],
							iopData[32]
						};
						std::memcpy(&tempFloat, &floatBuffer, 4); // TODO Feels kinda bad...

						tempObject->set_scale(tempFloat);
						tempObject->set_number_of_decimals(iopData[33]);
						tempObject->set_format(0 != iopData[34]);

						if (iopData[34] > 1)
						{
							LOG_WARNING("[WS]: Input number " + isobus::to_string(static_cast<int>(decodedID)) + " format byte has undefined value. Setting to exponential format.");
						}

						tempObject->set_justification_bitfield(iopData[35]);
						tempObject->set_options2(iopData[36]);

						// Parse macros
						const std::uint8_t numberOfMacrosToFollow = iopData[37];
						iopLength -= 38;
						iopData += 38;

						retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse input number object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::InputList:
				{
					auto tempObject = std::make_shared<InputList>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(get_little_endian_uint16(iopData, 5));
						tempObject->set_variable_reference(get_little_endian_uint16(iopData, 7)); // Number variable
						tempObject->set_value(iopData[9]);
						tempObject->set_options(iopData[11]);

						// Parse children
						const std::uint8_t numberOfListItems = iopData[10];
						iopData += 12;
						iopLength -= 12;

						const std::uint8_t numberOfMacrosToFollow = iopData[0];
						iopData++;
						iopLength--;

						if (iopLength >= static_cast<std::uint16_t>(2 * numberOfListItems))
						{
							for (std::uint_fast8_t i = 0; i < numberOfListItems; i++)
							{
								auto childID = get_little_endian_uint16(iopData, 0);
								tempObject->add_child(childID, 0, 0);
								iopLength -= 2;
								iopData += 2;
							}

							// Next, parse macro list
							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse children of input list object " + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse input list object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::OutputString:
				{
					auto tempObject = std::make_shared<OutputString>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(get_little_endian_uint16(iopData, 5));
						tempObject->set_background_color(iopData[7]);
						tempObject->set_font_attributes(get_little_endian_uint16(iopData, 8));
						tempObject->set_options(iopData[10]);
						tempObject->set_variable_reference(get_little_endian_uint16(iopData, 11)); // String Variable
						tempObject->set_justification_bitfield(iopData[13]);

						const auto stringLengthToFollow = get_little_endian_uint16(iopData, 14);
						std::string tempString;
						tempString.reserve(stringLengthToFollow);
						iopData += 16;
						iopLength -= 16;

						if (iopLength >= stringLengthToFollow)
						{
							for (std::uint_fast16_t i = 0; i < stringLengthToFollow; i++)
							{
								tempString.push_back(static_cast<char>(iopData[0]));
								iopData++;
								iopLength--;
							}
							tempObject->set_value(tempString);

							// Parse macros
							const std::uint8_t numberOfMacrosToFollow = iopData[0];
							iopData++;
							iopLength--;

							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse output string object value");
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse output string object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::OutputNumber:
				{
					auto tempObject = std::make_shared<OutputNumber>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(get_little_endian_uint16(iopData, 5));
						tempObject->set_background_color(iopData[7]);
						tempObject->set_font_attributes(get_little_endian_uint16(iopData, 8));
						tempObject->set_options(iopData[10]);
						tempObject->set_variable_reference(get_little_endian_uint16(iopData, 11)); // Number Variable
						tempObject->set_value(get_little_endian_uint32(iopData, 13));
						tempObject->set_offset(get_little_endian_uint32(iopData, 17));
						float tempFloat = 0;
						std::uint8_t floatBuffer[4] = {
							iopData[21],
							iopData[22],
							iopData[23],
							iopData[24]
						};
						std::memcpy(&tempFloat, &floatBuffer, 4); // TODO Feels kinda bad...

						tempObject->set_scale(tempFloat);
						tempObject->set_number_of_decimals(iopData[25]);
						tempObject->set_format(0 != iopData[26]);

						if (iopData[26] > 1)
						{
							LOG_WARNING("[WS]: Output number " +
							            isobus::to_string(static_cast<int>(decodedID)) +
							            " format byte has undefined value. Setting to exponential format.");
						}
						tempObject->set_justification_bitfield(iopData[27]);

						// Parse Macros
						const std::uint8_t numberOfMacrosToFollow = iopData[28];
						iopLength -= 29;
						iopData += 29;

						retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse output number object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::OutputList:
				{
					auto tempObject = std::make_shared<OutputList>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(get_little_endian_uint16(iopData, 5));
						tempObject->set_variable_reference(get_little_endian_uint16(iopData, 7));
						tempObject->set_value(iopData[9]);

						// Parse children
						const std::uint8_t numberOfListItems = iopData[10];
						const std::uint8_t numberOfMacrosToFollow = iopData[11];
						iopData += 12;
						iopLength -= 12;

						if (iopLength >= static_cast<std::uint16_t>(2 * numberOfListItems))
						{
							for (std::uint_fast8_t i = 0; i < numberOfListItems; i++)
							{
								auto childID = get_little_endian_uint16(iopData, 0);
								tempObject->add_child(childID, 0, 0);
								iopLength -= 2;
								iopData += 2;
							}

							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse children for output list object " + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse output list object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::OutputLine:
				{
					auto tempObject = std::make_shared<OutputLine>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_line_attributes(get_little_endian_uint16(iopData, 3));
						tempObject->set_width(get_little_endian_uint16(iopData, 5));
						tempObject->set_height(get_little_endian_uint16(iopData, 7));

						if (iopData[9] <= 1)
						{
							tempObject->set_line_direction(static_cast<OutputLine::LineDirection>(iopData[9]));
						}
						else
						{
							LOG_ERROR("[WS]: Unknown output line direction in object %u", decodedID);
						}

						iopData += 10;
						iopLength -= 10;

						// Parse macros
						const std::uint8_t numberOfMacrosToFollow = iopData[0];
						iopData++;
						iopLength--;

						retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse output line object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::OutputRectangle:
				{
					auto tempObject = std::make_shared<OutputRectangle>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_line_attributes(get_little_endian_uint16(iopData, 3));
						tempObject->set_width(get_little_endian_uint16(iopData, 5));
						tempObject->set_height(get_little_endian_uint16(iopData, 7));
						tempObject->set_line_suppression_bitfield(iopData[9]);
						tempObject->set_fill_attributes(get_little_endian_uint16(iopData, 10));
						iopData += 12;
						iopLength -= 12;

						// Parse macros
						const std::uint8_t numberOfMacrosToFollow = iopData[0];
						iopData++;
						iopLength--;

						retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse output rectangle object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::OutputEllipse:
				{
					auto tempObject = std::make_shared<OutputEllipse>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_line_attributes(get_little_endian_uint16(iopData, 3));
						tempObject->set_width(get_little_endian_uint16(iopData, 5));
						tempObject->set_height(get_little_endian_uint16(iopData, 7));

						if (iopData[9] <= static_cast<std::uint8_t>(OutputEllipse::EllipseType::ClosedEllipseSection))
						{
							tempObject->set_ellipse_type(static_cast<OutputEllipse::EllipseType>(iopData[9]));
							tempObject->set_start_angle(iopData[10]);
							tempObject->set_end_angle(iopData[11]);
							tempObject->set_fill_attributes(get_little_endian_uint16(iopData, 12));
							iopData += 14;
							iopLength -= 14;

							// Parse macros
							const std::uint8_t numberOfMacrosToFollow = iopData[0];
							iopData++;
							iopLength--;

							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Output Ellipse type is undefined for object " + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse output ellipse object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::OutputPolygon:
				{
					auto tempObject = std::make_shared<OutputPolygon>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(get_little_endian_uint16(iopData, 5));
						tempObject->set_line_attributes(get_little_endian_uint16(iopData, 7));
						tempObject->set_fill_attributes(get_little_endian_uint16(iopData, 9));

						if (iopData[11] <= 3)
						{
							tempObject->set_type(static_cast<OutputPolygon::PolygonType>(iopData[11]));

							const std::uint8_t numberOfPoints = iopData[12];
							const std::uint8_t numberOfMacrosToFollow = iopData[13];
							iopLength -= 14;
							iopData += 14;

							if (numberOfPoints < 3)
							{
								LOG_WARNING("[WS]: Output Polygon must have at least 3 points. Polygon %u will not be drawable.", decodedID);
							}

							if (iopLength >= static_cast<std::uint16_t>(numberOfPoints * 4))
							{
								for (std::uint_fast8_t i = 0; i < numberOfPoints; i++)
								{
									tempObject->add_point(get_little_endian_uint16(iopData, 0), get_little_endian_uint16(iopData, 2));
									iopLength -= 4;
									iopData += 4;
								}

								retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
							}
							else
							{
								LOG_ERROR("[WS]: Not enough IOP data to parse output polygon child points for object " + isobus::to_string(static_cast<int>(decodedID)));
							}
						}
						else
						{
							LOG_ERROR("[WS]: Polygon type is undefined for object " + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse output polygon object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::OutputMeter:
				{
					auto tempObject = std::make_shared<OutputMeter>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(tempObject->get_width());
						tempObject->set_needle_colour(iopData[5]);
						tempObject->set_border_colour(iopData[6]);
						tempObject->set_arc_and_tick_colour(iopData[7]);
						tempObject->set_options(iopData[8]);
						tempObject->set_number_of_ticks(iopData[9]);
						tempObject->set_start_angle(iopData[10]);
						tempObject->set_end_angle(iopData[11]);
						tempObject->set_min_value(get_little_endian_uint16(iopData, 12));
						tempObject->set_max_value(get_little_endian_uint16(iopData, 14));
						tempObject->set_variable_reference(get_little_endian_uint16(iopData, 16)); // Number Variable
						tempObject->set_value(get_little_endian_uint16(iopData, 18));
						const std::uint8_t numberOfMacrosToFollow = iopData[20];
						iopData += 21;
						iopLength -= 21;

						retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse output meter object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::OutputLinearBarGraph:
				{
					auto tempObject = std::make_shared<OutputLinearBarGraph>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(get_little_endian_uint16(iopData, 5));
						tempObject->set_colour(iopData[7]);
						tempObject->set_target_line_colour(iopData[8]);
						tempObject->set_options(iopData[9]);
						tempObject->set_number_of_ticks(iopData[10]);
						tempObject->set_min_value(get_little_endian_uint16(iopData, 11));
						tempObject->set_max_value(get_little_endian_uint16(iopData, 13));
						tempObject->set_variable_reference(get_little_endian_uint16(iopData, 15)); // Number Variable
						tempObject->set_value(get_little_endian_uint16(iopData, 17));
						tempObject->set_target_value_reference(get_little_endian_uint16(iopData, 19));
						tempObject->set_target_value(get_little_endian_uint16(iopData, 21));
						const std::uint8_t numberOfMacrosToFollow = iopData[23];
						iopData += 24;
						iopLength -= 24;

						retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse output linear bar graph object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::OutputArchedBarGraph:
				{
					auto tempObject = std::make_shared<OutputArchedBarGraph>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(get_little_endian_uint16(iopData, 5));
						tempObject->set_colour(iopData[7]);
						tempObject->set_target_line_colour(iopData[8]);
						tempObject->set_options(iopData[9]);
						tempObject->set_start_angle(iopData[10]);
						tempObject->set_end_angle(iopData[11]);
						tempObject->set_bar_graph_width(get_little_endian_uint16(iopData, 12));
						tempObject->set_min_value(get_little_endian_uint16(iopData, 14));
						tempObject->set_max_value(get_little_endian_uint16(iopData, 16));
						tempObject->set_variable_reference(get_little_endian_uint16(iopData, 18)); // Number Variable
						tempObject->set_value(get_little_endian_uint16(iopData, 20));
						tempObject->set_target_value_reference(get_little_endian_uint16(iopData, 22));
						tempObject->set_target_value(get_little_endian_uint16(iopData, 24));
						const std::uint8_t numberOfMacrosToFollow = iopData[26];
						iopData += 27;
						iopLength -= 27;

						retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse output arched bar graph object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::GraphicsContext:
				{
					retVal = parse_unsupported_object(decodedType, decodedID, iopData, iopLength);
				}
				break;

				case VirtualTerminalObjectType::Animation:
				{
					auto tempObject = std::make_shared<Animation>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_height(get_little_endian_uint16(iopData, 5));
						tempObject->set_refresh_interval(get_little_endian_uint16(iopData, 7));
						tempObject->set_value(iopData[9]);
						tempObject->set_enabled(0 != iopData[10]);
						tempObject->set_first_child_index(iopData[11]);
						tempObject->set_last_child_index(iopData[12]);
						tempObject->set_default_child_index(iopData[13]);
						tempObject->set_options(iopData[14]);

						// Now add child objects (Table B.72: object references before macro references)
						const std::uint8_t childrenToFollow = iopData[15];
						const auto sizeOfChildren = static_cast<std::uint16_t>(childrenToFollow * 6); // ID, X, Y 2 bytes each
						const std::uint8_t numberOfMacrosToFollow = iopData[16];
						iopLength -= 17; // Subtract the bytes we've processed so far.
						iopData += 17; // Move the pointer

						if (iopLength >= sizeOfChildren)
						{
							for (std::uint_fast8_t i = 0; i < childrenToFollow; i++)
							{
								auto childID = get_little_endian_uint16(iopData, 0);
								auto childX = get_little_endian_int16(iopData, 2);
								auto childY = get_little_endian_int16(iopData, 4);
								tempObject->add_child(childID, childX, childY);
								iopLength -= 6;
								iopData += 6;
							}

							// Next, parse macro list
							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse animation children for object " + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse animation object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::PictureGraphic:
				{
					auto tempObject = std::make_shared<PictureGraphic>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_width(get_little_endian_uint16(iopData, 3));
						tempObject->set_actual_width(get_little_endian_uint16(iopData, 5));
						tempObject->set_actual_height(get_little_endian_uint16(iopData, 7));
						tempObject->set_height(static_cast<std::uint16_t>(tempObject->get_actual_height() * (static_cast<float>(tempObject->get_width()) / static_cast<float>(tempObject->get_actual_width()))));

						if (iopData[9] <= static_cast<std::uint8_t>(PictureGraphic::Format::EightBitColour))
						{
							tempObject->set_format(static_cast<PictureGraphic::Format>(iopData[9]));
							tempObject->set_options(iopData[10]);
							tempObject->set_transparency_colour(iopData[11]);
							tempObject->set_number_of_bytes_in_raw_data(get_little_endian_uint32(iopData, 12));
							const std::uint8_t numberOfMacrosToFollow = iopData[16];
							iopData += 17;
							iopLength -= 17;

							if (tempObject->get_option(PictureGraphic::Options::RunLengthEncoded))
							{
								if (0 != tempObject->get_number_of_bytes_in_raw_data() % 2)
								{
									LOG_ERROR("[WS]: Picture graphic has RLE but an odd number of data bytes. Object: " + isobus::to_string(static_cast<int>(decodedID)));
								}
								else if (iopLength >= tempObject->get_number_of_bytes_in_raw_data())
								{
									// Decode the RLE
									std::size_t lineAmountLeft = tempObject->get_actual_width();
									for (std::uint_fast32_t i = 0; i < (tempObject->get_number_of_bytes_in_raw_data() / 2); i++)
									{
										for (std::size_t j = 0; j < iopData[0]; j++)
										{
											switch (tempObject->get_format())
											{
												case PictureGraphic::Format::EightBitColour:
												{
													tempObject->add_raw_data(iopData[1]);
												}
												break;

												case PictureGraphic::Format::FourBitColour:
												{
													tempObject->add_raw_data(iopData[1] >> 4);
													lineAmountLeft--;

													if (lineAmountLeft > 0)
													{
														//Unused bits at the end of a line are ignored.
														tempObject->add_raw_data(get_masked_value(iopData[1], 0x0FU));
														lineAmountLeft--;

														if (0 == lineAmountLeft)
														{
															lineAmountLeft = tempObject->get_actual_width();
														}
													}
													else
													{
														lineAmountLeft = tempObject->get_actual_width();
													}
												}
												break;

												case PictureGraphic::Format::Monochrome:
												{
													for (std::uint_fast8_t k = 0; k < 8U; k++)
													{
														tempObject->add_raw_data(static_cast<std::uint8_t>(is_bit_set(iopData[1], get_bit_mask(7 - k))));
														lineAmountLeft--;

														if (0 == lineAmountLeft)
														{
															break;
														}
													}

													if (0 == lineAmountLeft)
													{
														lineAmountLeft = tempObject->get_actual_width();
													}
												}
												break;

												default:
													break;
											}
										}
										iopData += 2;
										iopLength -= 2;
									}
								}
								else
								{
									LOG_ERROR("[WS]: Not enough IOP data to deserialize picture graphic's RLE pixel data. Object: " + isobus::to_string(static_cast<int>(decodedID)));
								}
							}
							else
							{
								if (iopLength >= tempObject->get_number_of_bytes_in_raw_data())
								{
									switch (tempObject->get_format())
									{
										case PictureGraphic::Format::EightBitColour:
										{
											tempObject->set_raw_data(iopData, tempObject->get_number_of_bytes_in_raw_data());
											iopData += tempObject->get_number_of_bytes_in_raw_data();
											iopLength -= tempObject->get_number_of_bytes_in_raw_data();
										}
										break;

										case PictureGraphic::Format::FourBitColour:
										{
											std::size_t lineAmountLeft = tempObject->get_actual_width();

											for (std::uint_fast32_t i = 0; i < tempObject->get_number_of_bytes_in_raw_data(); i++)
											{
												tempObject->add_raw_data(iopData[0] >> 4);
												lineAmountLeft--;

												if (lineAmountLeft > 0)
												{
													tempObject->add_raw_data(get_masked_value(iopData[0], 0x0FU));
													lineAmountLeft--;

													if (0 == lineAmountLeft)
													{
														lineAmountLeft = tempObject->get_actual_width();
													}
												}
												else
												{
													lineAmountLeft = tempObject->get_actual_width();
												}
												iopData++;
												iopLength--;
											}
										}
										break;

										case PictureGraphic::Format::Monochrome:
										{
											std::size_t lineAmountLeft = tempObject->get_actual_width();

											for (std::uint_fast32_t i = 0; i < tempObject->get_number_of_bytes_in_raw_data(); i++)
											{
												for (std::uint_fast8_t j = 0; j < 8U; j++)
												{
													tempObject->add_raw_data(static_cast<std::uint8_t>(is_bit_set(iopData[0], get_bit_mask(7 - j))));
													lineAmountLeft--;

													if (0 == lineAmountLeft)
													{
														break;
													}
												}

												if (0 == lineAmountLeft)
												{
													lineAmountLeft = tempObject->get_actual_width();
												}
												iopData++;
												iopLength--;
											}
										}
										break;
									}
								}
								else
								{
									LOG_ERROR("[WS]: Not enough IOP data to deserialize picture graphic's pixel data. Object: " + isobus::to_string(static_cast<int>(decodedID)));
								}
							}

							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);

							if (tempObject->get_raw_data().size() == tempObject->get_actual_width() * tempObject->get_actual_height())
							{
								retVal = true;
							}
							else
							{
								LOG_ERROR("[WS]: Picture graphic object has invalid dimensions compared to its data. Object: " + isobus::to_string(static_cast<int>(decodedID)));
							}
						}
						else
						{
							LOG_ERROR("[WS]: Picture graphic format is undefined for object " + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse picture graphic object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::NumberVariable:
				{
					auto tempObject = std::make_shared<NumberVariable>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_value(get_little_endian_uint32(iopData, 3));
						iopLength -= 7;
						iopData += 7;
						retVal = true;
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse number variable object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::StringVariable:
				{
					auto tempObject = std::make_shared<StringVariable>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);

						const auto length = get_little_endian_uint16(iopData, 3);
						iopLength -= 5;
						iopData += 5;

						if (iopLength >= length)
						{
							std::string tempStringValue;
							tempStringValue.reserve(length);

							for (std::uint32_t i = 0; i < length; i++)
							{
								tempStringValue.push_back(static_cast<char>(iopData[0]));
								iopData++;
								iopLength--;
							}
							tempObject->set_value(tempStringValue);
							retVal = true;
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse string variable object raw data");
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse string variable object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::FontAttributes:
				{
					auto tempObject = std::make_shared<FontAttributes>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_colour(iopData[3]);
						tempObject->set_size(static_cast<FontAttributes::FontSize>(iopData[4]));

						if ((iopData[5] <= static_cast<std::uint8_t>(FontAttributes::FontType::ISO8859_7)) &&
						    (iopData[5] != static_cast<std::uint8_t>(FontAttributes::FontType::Reserved_1)) &&
						    (iopData[5] != static_cast<std::uint8_t>(FontAttributes::FontType::Reserved_2)))
						{
							tempObject->set_type(static_cast<FontAttributes::FontType>(iopData[5]));
							tempObject->set_style(iopData[6]);

							const std::uint8_t numberOfMacrosToFollow = iopData[7];
							iopData += 8;
							iopLength -= 8;

							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Proprietary and reserved fonts are not supported, and will likely never be supported.");
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse font attributes object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::LineAttributes:
				{
					auto tempObject = std::make_shared<LineAttributes>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_background_color(iopData[3]);
						tempObject->set_width(iopData[4]);
						tempObject->set_line_art_bit_pattern(get_little_endian_uint16(iopData, 5));

						const std::uint8_t numberOfMacrosToFollow = iopData[7];
						iopData += 8;
						iopLength -= 8;

						retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse line attributes object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::FillAttributes:
				{
					auto tempObject = std::make_shared<FillAttributes>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);

						if (iopData[3] <= static_cast<std::uint8_t>(FillAttributes::FillType::FillWithPatternGivenByFillPatternAttribute))
						{
							tempObject->set_type(static_cast<FillAttributes::FillType>(iopData[3]));
							tempObject->set_background_color(iopData[4]);
							tempObject->set_fill_pattern(get_little_endian_uint16(iopData, 5)); // Object ID for a picture graphic

							const std::uint8_t numberOfMacrosToFollow = iopData[7];
							iopData += 8;
							iopLength -= 8;

							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Fill attribute type is undefined for object " + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse fill attributes object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::InputAttributes:
				{
					auto tempObject = std::make_shared<InputAttributes>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);

						if (iopData[3] <= static_cast<std::uint8_t>(InputAttributes::ValidationType::InvalidCharactersAreListed))
						{
							tempObject->set_validation_type(static_cast<InputAttributes::ValidationType>(get_masked_value(iopData[3], 0x01U)));
						}
						else
						{
							tempObject->set_validation_type(static_cast<InputAttributes::ValidationType>(get_masked_value(iopData[3], 0x01U)));
							LOG_WARNING("[WS]: Invalid input attributes validation type. Validation type must be < 2");
						}

						const std::uint8_t validationStringLength = iopData[4];
						iopData += 5;
						iopLength -= 5;

						if (iopLength >= validationStringLength)
						{
							std::string tempValidationString;
							tempValidationString.reserve(validationStringLength);

							for (std::uint_fast16_t i = 0; i < validationStringLength; i++)
							{
								tempValidationString.push_back(static_cast<char>(iopData[i]));
							}
							iopData += validationStringLength;
							iopLength -= validationStringLength;

							tempObject->set_validation_string(tempValidationString);

							const std::uint8_t numberOfMacrosToFollow = iopData[0];
							iopData++;
							iopLength--;

							retVal = parse_object_macro_reference(tempObject, numberOfMacrosToFollow, iopData, iopLength);
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse input attributes validation string");
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse input attributes object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::ExtendedInputAttributes:
				{
					retVal = parse_unsupported_object(decodedType, decodedID, iopData, iopLength);
				}
				break;

				case VirtualTerminalObjectType::ColourMap:
				{
					auto tempObject = std::make_shared<ColourMap>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						auto numberOfIndexes = get_little_endian_uint16(iopData, 3);
						if ((2 == numberOfIndexes) ||
						    (16 == numberOfIndexes) ||
						    (256 == numberOfIndexes))
						{
							tempObject->set_number_of_colour_indexes(numberOfIndexes);

							for (std::uint_fast16_t i = 0; i < numberOfIndexes; i++)
							{
								tempObject->set_colour_map_index(static_cast<std::uint8_t>(i), iopData[5 + i]);
							}

							iopData += 5 + tempObject->get_number_of_colour_indexes();
							iopLength -= 5 + tempObject->get_number_of_colour_indexes();

							retVal = true;
						}
						else
						{
							LOG_ERROR("[WS]: Colour map with invalid number of indexes: %d", numberOfIndexes);
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse colour map object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::ObjectLabelRefrenceList:
				{
					auto tempObject = std::make_shared<ObjectLabelReferenceList>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);

						const std::uint16_t numberOfLabels = get_little_endian_uint16(iopData, 3);
						iopLength -= 5;
						iopData += 5;

						// Table B.64: one labeled object consumes 7 bytes.
						if (iopLength >= (static_cast<std::uint32_t>(numberOfLabels) * 7u))
						{
							for (std::uint_fast16_t i = 0; i < numberOfLabels; i++)
							{
								tempObject->add_label(get_little_endian_uint16(iopData, 0),
								                      get_little_endian_uint16(iopData, 2),
								                      iopData[4],
								                      get_little_endian_uint16(iopData, 5));
								iopLength -= 7;
								iopData += 7;
							}
							retVal = true;
						}
						else
						{
							LOG_ERROR("[WS]: Not enough IOP data to parse object label reference list labels for object " + isobus::to_string(static_cast<int>(decodedID)));
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse object label reference list object");
					}

					if (retVal && tempObject->has_duplicate_labelled_objects())
					{
						// B.21: it is not possible to assign more than one label to an object, and a pool
						// whose list names an object more than once shall be rejected. This is enforced
						// here rather than in get_is_valid because nothing in the library calls
						// get_is_valid -- the parse path is the only place a pool is ever refused.
						LOG_ERROR("[WS]: Object label reference list " + isobus::to_string(static_cast<int>(decodedID)) + " labels an object more than once");
						retVal = false;
					}

					if (retVal)
					{
						// Table B.64: an object pool shall not contain more than one Object Label Reference
						// List object. An object of this type already carrying the ID being parsed is the
						// same object arriving again in a run-time object pool update, which replaces it
						// rather than adding a second one. The scan runs over the staging tree, which is
						// what a run-time update is merging into; the published snapshot still holds the
						// pre-update pool at this point.
						for (const auto &existingObject : vtObjectTree)
						{
							if ((nullptr != existingObject.second) &&
							    (VirtualTerminalObjectType::ObjectLabelRefrenceList == existingObject.second->get_object_type()) &&
							    (decodedID != existingObject.first))
							{
								LOG_ERROR("[WS]: An object pool may contain only one object label reference list object, but object " + isobus::to_string(static_cast<int>(decodedID)) + " is a second one");
								retVal = false;
								break;
							}
						}
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::ObjectPointer:
				{
					auto tempObject = std::make_shared<ObjectPointer>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_value(get_little_endian_uint16(iopData, 3));
						iopLength -= 5;
						iopData += 5;
						retVal = true;
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse object pointer object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::ExternalObjectDefinition:
				case VirtualTerminalObjectType::ExternalReferenceNAME:
				case VirtualTerminalObjectType::ExternalObjectPointer:
				{
					retVal = parse_unsupported_object(decodedType, decodedID, iopData, iopLength);
				}
				break;

				case VirtualTerminalObjectType::Macro:
				{
					auto tempObject = std::make_shared<Macro>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);

						auto numberBytesToFollow = get_little_endian_uint16(iopData, 3);
						std::uint16_t numberBytesProcessed = 0;
						iopLength -= 5;
						iopData += 5;

						if (iopLength >= numberBytesToFollow)
						{
							retVal = true;

							while (numberBytesProcessed < numberBytesToFollow)
							{
								auto commandLength = 8;
								switch (static_cast<Macro::Command>(iopData[0]))
								{
									case Macro::Command::ChangeChildPosition:
										// special case: 9 bytes
										retVal = tempObject->add_command_packet({
										  iopData[0],
										  iopData[1],
										  iopData[2],
										  iopData[3],
										  iopData[4],
										  iopData[5],
										  iopData[6],
										  iopData[7],
										  iopData[8],
										});
										commandLength = 9;
										break;
									case Macro::Command::GraphicsContextCommand:
										// FIXME
										break;
									case Macro::Command::ChangeStringValue:
									{
										// Change string value has variable length
										std::vector<std::uint8_t> command;
										auto stringLength = get_little_endian_uint16(iopData, 3);
										for (int i = 0; i < (stringLength + 5); i++)
										{
											command.push_back(iopData[i]);
										}
										retVal = tempObject->add_command_packet(command);
										commandLength = 5 + stringLength;
										break;
									}
									default:
										// all other macro commands are 8 byte long
										retVal = tempObject->add_command_packet({
										  iopData[0],
										  iopData[1],
										  iopData[2],
										  iopData[3],
										  iopData[4],
										  iopData[5],
										  iopData[6],
										  iopData[7],
										});
										commandLength = 8;
										break;
								}
								iopLength -= commandLength;
								iopData += commandLength;
								numberBytesProcessed += commandLength;

								if (!retVal)
								{
									LOG_ERROR("[WS]: Macro object %u cannot be parsed because a command packet could not be added.", decodedID);
									break;
								}
							}

							if (retVal)
							{
								retVal = tempObject->get_are_command_packets_valid();

								if (!retVal)
								{
									LOG_ERROR("[WS]: Macro object %u contains malformed commands", decodedID);
								}
							}
						}
						else
						{
							LOG_ERROR("[WS]: Macro object %u cannot be parsed because there is not enough IOP data left", decodedID);
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse macro object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::AuxiliaryFunctionType1:
				{
					auto tempObject = std::make_shared<AuxiliaryFunctionType1>();

					LOG_WARNING("[WS]: Deserializing an Aux function type 1 object. This object is parsed and validated but NOT utilized by version 3 or later VTs in making Auxiliary Control Assignments.");

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_background_color(iopData[3]);

						if (iopData[4] <= 2)
						{
							tempObject->set_function_type(static_cast<AuxiliaryFunctionType1::FunctionType>(iopData[4]));

							const std::uint8_t numberOfObjectsToFollow = iopData[5];
							const std::uint8_t numberOfBytesToFollow = numberOfObjectsToFollow * 6;
							iopData += 6;
							iopLength -= 6;

							if (iopLength >= numberOfBytesToFollow)
							{
								for (std::uint_fast8_t i = 0; i < numberOfObjectsToFollow; i++)
								{
									const auto objectID = get_little_endian_uint16(iopData, 0);
									const auto xPosition = get_little_endian_uint16(iopData, 2);
									const auto yPosition = get_little_endian_uint16(iopData, 4);

									tempObject->add_child(objectID, xPosition, yPosition);
									iopData += 6;
									iopLength -= 6;
								}
								retVal = true;
							}
							else
							{
								LOG_ERROR("[WS]: Not enough IOP data to parse auxiliary function type 1 object's children.");
							}
						}
						else
						{
							LOG_ERROR("[WS]: Auxiliary function type 1 object with ID %u has an invalid function type. The function type must be 2 or less.", decodedID);
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse auxiliary function type 1 object.");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::AuxiliaryInputType1:
				{
					auto tempObject = std::make_shared<AuxiliaryInputType1>();

					LOG_WARNING("[WS]: Deserializing an Aux input type 1 object. This object is parsed and validated but NOT utilized by version 3 or later VTs in making Auxiliary Control Assignments.");

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_background_color(iopData[3]);

						if (iopData[4] <= 2)
						{
							tempObject->set_function_type(static_cast<AuxiliaryInputType1::FunctionType>(iopData[4]));

							if (iopData[5] <= 250)
							{
								tempObject->set_input_id(iopData[5]);

								const std::uint8_t numberOfObjectsToFollow = iopData[6];
								const std::uint8_t numberOfBytesToFollow = numberOfObjectsToFollow * 6;
								iopData += 7;
								iopLength -= 7;

								if (iopLength >= numberOfBytesToFollow)
								{
									for (std::uint_fast8_t i = 0; i < numberOfObjectsToFollow; i++)
									{
										const auto objectID = get_little_endian_uint16(iopData, 0);
										const auto xPosition = get_little_endian_uint16(iopData, 2);
										const auto yPosition = get_little_endian_uint16(iopData, 4);

										tempObject->add_child(objectID, xPosition, yPosition);
										iopData += 6;
										iopLength -= 6;
									}
									retVal = true;
								}
								else
								{
									LOG_ERROR("[WS]: Not enough IOP data to parse auxiliary function type 2 object's children.");
								}
							}
							else
							{
								LOG_ERROR("[WS]: Auxiliary input type 1 object %u has an invalid input ID. Input ID must be 250 or less, but was decoded as %u", decodedID, iopData[5]);
							}
						}
						else
						{
							LOG_ERROR("[WS]: Auxiliary input type 1 object %u has an invalid function type. Function type must be 2 or less.");
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse auxiliary input type 1 object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::AuxiliaryFunctionType2:
				{
					auto tempObject = std::make_shared<AuxiliaryFunctionType2>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_background_color(iopData[3]);

						const auto functionType = get_masked_value(iopData[4], 0x1FU);

						if (functionType >= static_cast<std::uint8_t>(AuxiliaryFunctionType2::FunctionType::ReservedRangeStart))
						{
							LOG_ERROR("[WS]: Auxiliary function type 2 with object ID %u has a reserved function type.", decodedID);
						}
						else if (functionType == static_cast<std::uint8_t>(AuxiliaryFunctionType2::FunctionType::ReservedRangeEnd))
						{
							LOG_ERROR("[WS]: Auxiliary function type 2 with object ID %u is using the remove assignment command function type, which is not allowed.", decodedID);
						}
						else
						{
							tempObject->set_function_type(static_cast<AuxiliaryFunctionType2::FunctionType>(functionType));
							tempObject->set_function_attribute(AuxiliaryFunctionType2::CriticalControl, is_bit_set(iopData[4], 0x20U));
							tempObject->set_function_attribute(AuxiliaryFunctionType2::AssignmentRestriction, is_bit_set(iopData[4], 0x40U));
							tempObject->set_function_attribute(AuxiliaryFunctionType2::SingleAssignment, is_bit_set(iopData[4], 0x80U));

							const std::uint8_t numberOfObjectsToFollow = iopData[5];
							const std::uint8_t numberOfBytesToFollow = numberOfObjectsToFollow * 6;
							iopData += 6;
							iopLength -= 6;

							if (iopLength >= numberOfBytesToFollow)
							{
								for (std::uint_fast8_t i = 0; i < numberOfObjectsToFollow; i++)
								{
									const auto objectID = get_little_endian_uint16(iopData, 0);
									const auto xPosition = get_little_endian_uint16(iopData, 2);
									const auto yPosition = get_little_endian_uint16(iopData, 4);

									tempObject->add_child(objectID, xPosition, yPosition);
									iopData += 6;
									iopLength -= 6;
								}
								retVal = true;
							}
							else
							{
								LOG_ERROR("[WS]: Not enough IOP data to parse auxiliary function type 2 object's children.");
							}
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse auxiliary function type 2 object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::AuxiliaryInputType2:
				{
					auto tempObject = std::make_shared<AuxiliaryInputType2>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);
						tempObject->set_background_color(iopData[3]);

						const auto functionType = get_masked_value(iopData[4], 0x1FU);

						if (functionType >= static_cast<std::uint8_t>(AuxiliaryFunctionType2::FunctionType::ReservedRangeStart))
						{
							LOG_ERROR("[WS]: Auxiliary input type 2 with object ID %u has a reserved function type.", decodedID);
						}
						else if (functionType == static_cast<std::uint8_t>(AuxiliaryFunctionType2::FunctionType::ReservedRangeEnd))
						{
							LOG_ERROR("[WS]: Auxiliary input type 2 with object ID %u is using the remove assignment command function type, which is not allowed.", decodedID);
						}
						else
						{
							tempObject->set_function_type(static_cast<AuxiliaryFunctionType2::FunctionType>(functionType));
							tempObject->set_function_attribute(AuxiliaryInputType2::CriticalControl, is_bit_set(iopData[4], 0x20U));
							tempObject->set_function_attribute(AuxiliaryInputType2::SingleAssignment, is_bit_set(iopData[4], 0x80U));

							if (is_bit_set(iopData[4], 0x40U))
							{
								LOG_WARNING("[WS]: Auxiliary input type 2 with object ID %u is using the assignment restriction attribute, which is reserved and should be zero.", decodedID);
							}

							const std::uint8_t numberOfObjectsToFollow = iopData[5];
							const std::uint8_t numberOfBytesToFollow = numberOfObjectsToFollow * 6;
							iopData += 6;
							iopLength -= 6;

							if (iopLength >= numberOfBytesToFollow)
							{
								for (std::uint_fast8_t i = 0; i < numberOfObjectsToFollow; i++)
								{
									const auto objectID = get_little_endian_uint16(iopData, 0);
									const auto xPosition = get_little_endian_uint16(iopData, 2);
									const auto yPosition = get_little_endian_uint16(iopData, 4);
									tempObject->add_child(objectID, xPosition, yPosition);
									iopData += 6;
									iopLength -= 6;
								}
								retVal = true;
							}
							else
							{
								LOG_ERROR("[WS]: Not enough IOP data to parse auxiliary input type 2 object's children.");
							}
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse auxiliary input type 2 object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				case VirtualTerminalObjectType::AuxiliaryControlDesignatorType2:
				{
					auto tempObject = std::make_shared<AuxiliaryControlDesignatorType2>();

					if (iopLength >= tempObject->get_minumum_object_length())
					{
						tempObject->set_id(decodedID);

						if (iopData[3] <= 3)
						{
							tempObject->set_pointer_type(iopData[3]);
							tempObject->set_auxiliary_object_id(get_little_endian_uint16(iopData, 4));
							iopData += 6;
							iopLength -= 6;
							retVal = true;
						}
						else
						{
							LOG_ERROR("[WS]: Auxiliary control designator type 2 object %u  has an invalid pointer type. Pointer type must be 3 or less.");
						}
					}
					else
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse auxiliary control designator type 2 object");
					}

					if (retVal)
					{
						retVal = add_or_replace_object(tempObject);
					}
				}
				break;

				default:
				{
					LOG_ERROR("[WS]: Unsupported Object (Type: %d)", decodedType);
				}
				break;
			}

			if (!retVal)
			{
				set_object_pool_faulting_object_id(decodedID);
			}
		}
		return retVal;
	}

	std::shared_ptr<VTObject> VirtualTerminalWorkingSetBase::get_object_by_id(std::uint16_t objectID)
	{
		return VTObject::get_object_by_id(objectID, *get_object_tree());
	}

	std::shared_ptr<VTObject> VirtualTerminalWorkingSetBase::get_working_set_object()
	{
		return get_object_by_id(workingSetID);
	}

	std::uint16_t VirtualTerminalWorkingSetBase::get_working_set_object_id() const
	{
		return workingSetID;
	}

	bool VirtualTerminalWorkingSetBase::get_object_id_exists(std::uint16_t objectID)
	{
		bool retVal;
		const auto objectTree = get_object_tree();
		const auto foundObject = objectTree->find(objectID);

		if (objectTree->end() == foundObject)
		{
			retVal = false;
		}
		else
		{
			retVal = nullptr != foundObject->second;
		}
		return retVal;
	}

	EventID VirtualTerminalWorkingSetBase::get_event_from_byte(std::uint8_t eventByte)
	{
		EventID retVal = EventID::Reserved;

		switch (eventByte)
		{
			case static_cast<std::uint8_t>(EventID::OnActivate):
			case static_cast<std::uint8_t>(EventID::OnDeactivate):
			case static_cast<std::uint8_t>(EventID::OnShow):
			case static_cast<std::uint8_t>(EventID::OnHide):
			case static_cast<std::uint8_t>(EventID::OnEnable):
			case static_cast<std::uint8_t>(EventID::OnDisable):
			case static_cast<std::uint8_t>(EventID::OnChangeActiveMask):
			case static_cast<std::uint8_t>(EventID::OnChangeSoftKeyMask):
			case static_cast<std::uint8_t>(EventID::OnChangeAttribute):
			case static_cast<std::uint8_t>(EventID::OnChangeBackgroundColour):
			case static_cast<std::uint8_t>(EventID::ChangeFontAttributes):
			case static_cast<std::uint8_t>(EventID::ChangeLineAttributes):
			case static_cast<std::uint8_t>(EventID::ChangeFillAttributes):
			case static_cast<std::uint8_t>(EventID::ChangeChildLocation):
			case static_cast<std::uint8_t>(EventID::OnChangeSize):
			case static_cast<std::uint8_t>(EventID::OnChangeValue):
			case static_cast<std::uint8_t>(EventID::OnChangePriority):
			case static_cast<std::uint8_t>(EventID::OnChangeEndpoint):
			case static_cast<std::uint8_t>(EventID::OnInputFieldSelection):
			case static_cast<std::uint8_t>(EventID::OnInputFieldDeselection):
			case static_cast<std::uint8_t>(EventID::OnESC):
			case static_cast<std::uint8_t>(EventID::OnEntryOfAValue):
			case static_cast<std::uint8_t>(EventID::OnEntryOfANewValue):
			case static_cast<std::uint8_t>(EventID::OnKeyPress):
			case static_cast<std::uint8_t>(EventID::OnKeyRelease):
			case static_cast<std::uint8_t>(EventID::OnChangeChildPosition):
			case static_cast<std::uint8_t>(EventID::OnPointingEventPress):
			case static_cast<std::uint8_t>(EventID::OnPointingEventRelease):
			{
				retVal = static_cast<EventID>(eventByte);
				break;
			}

			default:
			{
				break;
			}
		}
		return retVal;
	}

	bool VirtualTerminalWorkingSetBase::parse_iop_into_objects(std::uint8_t *iopData, std::uint32_t iopLength)
	{
		std::uint32_t remainingLength = iopLength;
		std::uint8_t *currentIopPointer = iopData;
		bool retVal = true;

		if (iopLength > 0)
		{
			while (remainingLength > 0)
			{
				if (!parse_next_object(currentIopPointer, remainingLength))
				{
					LOG_ERROR("[WS]: Parsing object pool failed.");
					retVal = false;
					break;
				}
			}
		}
		else
		{
			retVal = false;
		}
		return retVal;
	}

	void VirtualTerminalWorkingSetBase::set_object_pool_faulting_object_id(std::uint16_t value)
	{
		const std::lock_guard<std::mutex> lock(managedWorkingSetMutex);
		faultingObjectID = value;
	}

	bool VirtualTerminalWorkingSetBase::parse_object_macro_reference(
	  std::shared_ptr<VTObject> object,
	  const std::uint8_t numberOfMacrosToFollow,
	  std::uint8_t *&iopData,
	  std::uint32_t &iopLength) const
	{
		if (iopLength < (numberOfMacrosToFollow * 2))
		{
			LOG_ERROR("[WS]: Not enough IOP data to parse working set macros for object " + isobus::to_string(object->get_id()));
			return false;
		}

		for (std::uint_fast8_t i = 0; i < numberOfMacrosToFollow; i++)
		{
			// If the first byte is 255, then more bytes are used! 4.6.22.3
			if (iopData[0] == static_cast<std::uint8_t>(EventID::UseExtendedMacroReference))
			{
				if (iopLength < 4)
				{
					LOG_ERROR("[WS]: Not enough IOP data to parse extended macro reference #%u for object %s", i, isobus::to_string(static_cast<int>(object->get_id())).c_str());
					return false;
				}

				auto macroID = static_cast<std::uint16_t>(static_cast<std::uint16_t>(iopData[1]) | (static_cast<std::uint16_t>(iopData[3]) << 8));
				auto eventID = get_event_from_byte(iopData[2]);
				if (EventID::Reserved != eventID)
				{
					object->add_macro({ eventID, macroID });
				}
				else
				{
					LOG_ERROR("[WS]: Macro with ID %u which is listed as part of object %u has an "
					          "invalid or unsupported event ID: %u",
					          macroID,
					          object->get_id(),
					          iopData[2]);
					return false;
				}
				iopLength -= 4;
				iopData += 4;

				// An extended reference occupies two 2-byte groupings, and the object's
				// "number of macros to follow" counts groupings rather than macros (4.6.22.3),
				// so this reference accounts for two of them.
				i++;
			}
			else
			{
				if (iopLength < 2)
				{
					LOG_ERROR("[WS]: Not enough IOP data to parse macro reference #%u for object %s", i, isobus::to_string(static_cast<int>(object->get_id())).c_str());
					return false;
				}

				if (EventID::Reserved != get_event_from_byte(iopData[0]))
				{
					object->add_macro({ get_event_from_byte(iopData[0]), iopData[1] });
				}
				else
				{
					LOG_ERROR("[WS]: Macro with ID %u which is listed as part of object %u has an "
					          "invalid or unsupported event ID: %u",
					          iopData[1],
					          object->get_id(),
					          iopData[0]);
					return false;
				}
				iopLength -= 2;
				iopData += 2;
			}
		}
		return true;
	}

	bool VirtualTerminalWorkingSetBase::parse_unsupported_object(VirtualTerminalObjectType type,
	                                                             std::uint16_t decodedID,
	                                                             std::uint8_t *&iopData,
	                                                             std::uint32_t &iopLength) const
	{
		// Serialized length of the object's record (Annex B). Variable-length records read their
		// count fields first, and every read is bounds-checked so a truncated or hostile pool
		// cannot drive a read past the end of the buffer.
		std::uint32_t objectLength = 0;

		switch (type)
		{
			case VirtualTerminalObjectType::GraphicsContext:
			{
				// Table B.59: fixed 34-byte record, no children and no macro list.
				objectLength = 34;
			}
			break;

			case VirtualTerminalObjectType::ExternalReferenceNAME:
			{
				// Table B.68: object id, type, options, and two 4-byte NAME halves.
				objectLength = 12;
			}
			break;

			case VirtualTerminalObjectType::ExternalObjectPointer:
			{
				// Table B.70: fixed 9-byte record (see ExternalObjectPointer::get_minumum_object_length).
				objectLength = 9;
			}
			break;

			case VirtualTerminalObjectType::ExternalObjectDefinition:
			{
				// Table B.66: 13-byte header then a count of object IDs, 2 bytes each.
				if (iopLength < 13)
				{
					LOG_ERROR("[WS]: Not enough IOP data to parse external object definition " + isobus::to_string(static_cast<int>(decodedID)));
					return false;
				}
				objectLength = 13u + (static_cast<std::uint32_t>(iopData[12]) * 2u);
			}
			break;

			case VirtualTerminalObjectType::ExtendedInputAttributes:
			{
				// Table B.53: 5-byte header, then for each code plane a 2-byte header (plane
				// number and range count) followed by that many 4-byte ranges. The plane list
				// is walked here so the total length can be computed.
				if (iopLength < 5)
				{
					LOG_ERROR("[WS]: Not enough IOP data to parse extended input attributes " + isobus::to_string(static_cast<int>(decodedID)));
					return false;
				}
				const std::uint32_t numberOfCodePlanes = iopData[4];
				std::uint32_t offset = 5u;
				for (std::uint32_t plane = 0u; plane < numberOfCodePlanes; plane++)
				{
					if (iopLength < (offset + 2u))
					{
						LOG_ERROR("[WS]: Not enough IOP data to parse extended input attributes code planes for object " + isobus::to_string(static_cast<int>(decodedID)));
						return false;
					}
					const std::uint32_t numberOfRanges = iopData[offset + 1u];
					offset += 2u + (numberOfRanges * 4u);
				}
				objectLength = offset;
			}
			break;

			default:
			{
				// A type with no defined layout has no computable length, so the stream cannot
				// be resynchronized past it. Rejecting the pool is the only safe option and is
				// permitted for unknown/proprietary objects (clause D.15).
				LOG_ERROR("[WS]: Unsupported Object (Type: %d)", static_cast<int>(type));
				return false;
			}
		}

		if (iopLength < objectLength)
		{
			LOG_ERROR("[WS]: Not enough IOP data to parse unsupported object " + isobus::to_string(static_cast<int>(decodedID)));
			return false;
		}

		iopData += objectLength;
		iopLength -= objectLength;
		return true;
	}
} // namespace isobus
