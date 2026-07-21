//================================================================================================
/// @file isobus_virtual_terminal_objects_isovt.cpp
///
/// @brief Implements the fork's added VT object pool object classes: the Animation object (ISO
/// 11783-6 Table B.72) and the Object Label Reference List object (Table B.64).
///
/// This translation unit is isovt-owned and has no upstream counterpart. Per ADR-0008, the fork's
/// added VTObject implementations live here rather than interleaved in
/// isobus_virtual_terminal_objects.cpp, to keep that upstream file's merge surface small.
//================================================================================================

#include "isobus/isobus/isobus_virtual_terminal_objects_isovt.hpp"

#include <algorithm>

namespace isobus
{
	VirtualTerminalObjectType Animation::get_object_type() const
	{
		return VirtualTerminalObjectType::Animation;
	}

	std::uint32_t Animation::get_minumum_object_length() const
	{
		return MIN_OBJECT_LENGTH;
	}

	bool Animation::get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool) const
	{
		bool anyWrongChildType = false;

		for (auto &child : children)
		{
			auto childObject = get_object_by_id(child.id, objectPool);
			if (nullptr != childObject)
			{
				switch (childObject->get_object_type())
				{
					case VirtualTerminalObjectType::WorkingSet:
					case VirtualTerminalObjectType::Container:
					case VirtualTerminalObjectType::Button:
					case VirtualTerminalObjectType::InputBoolean:
					case VirtualTerminalObjectType::InputString:
					case VirtualTerminalObjectType::InputNumber:
					case VirtualTerminalObjectType::InputList:
					case VirtualTerminalObjectType::OutputString:
					case VirtualTerminalObjectType::OutputNumber:
					case VirtualTerminalObjectType::OutputList:
					case VirtualTerminalObjectType::OutputLine:
					case VirtualTerminalObjectType::OutputRectangle:
					case VirtualTerminalObjectType::OutputEllipse:
					case VirtualTerminalObjectType::OutputPolygon:
					case VirtualTerminalObjectType::OutputMeter:
					case VirtualTerminalObjectType::GraphicsContext:
					case VirtualTerminalObjectType::OutputArchedBarGraph:
					case VirtualTerminalObjectType::OutputLinearBarGraph:
					case VirtualTerminalObjectType::Animation:
					case VirtualTerminalObjectType::PictureGraphic:
					case VirtualTerminalObjectType::ObjectPointer:
					case VirtualTerminalObjectType::ExternalObjectPointer:
					case VirtualTerminalObjectType::AuxiliaryFunctionType2:
					case VirtualTerminalObjectType::AuxiliaryInputType2:
					case VirtualTerminalObjectType::AuxiliaryControlDesignatorType2:
					case VirtualTerminalObjectType::Macro:
					{
						// Valid Child Object
					}
					break;

					default:
					{
						anyWrongChildType = true;
					}
					break;
				}
			}
		}
		return ((!anyWrongChildType) &&
		        (NULL_OBJECT_ID != objectID));
	}

	bool Animation::set_attribute(std::uint8_t, std::uint32_t, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &, AttributeError &returnedError)
	{
		// All attributes are read only
		returnedError = AttributeError::InvalidAttributeID;
		return false;
	}

	bool Animation::get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const
	{
		bool retVal = false;

		if (attributeID < static_cast<std::uint8_t>(AttributeName::NumberOfAttributes))
		{
			switch (attributeID)
			{
				case static_cast<std::uint8_t>(AttributeName::Type):
				{
					returnedAttributeData = static_cast<std::uint8_t>(get_object_type());
					retVal = true;
				}
				break;

				case static_cast<std::uint8_t>(AttributeName::Width):
				{
					returnedAttributeData = get_width();
					retVal = true;
				}
				break;

				case static_cast<std::uint8_t>(AttributeName::Height):
				{
					returnedAttributeData = get_height();
					retVal = true;
				}
				break;

				case static_cast<std::uint8_t>(AttributeName::RefreshInterval):
				{
					returnedAttributeData = get_refresh_interval();
					retVal = true;
				}
				break;

				case static_cast<std::uint8_t>(AttributeName::Value):
				{
					returnedAttributeData = get_value();
					retVal = true;
				}
				break;

				case static_cast<std::uint8_t>(AttributeName::Enabled):
				{
					returnedAttributeData = get_enabled();
					retVal = true;
				}
				break;

				case static_cast<std::uint8_t>(AttributeName::FirstChildIndex):
				{
					returnedAttributeData = get_first_child_index();
					retVal = true;
				}
				break;

				case static_cast<std::uint8_t>(AttributeName::LastChildIndex):
				{
					returnedAttributeData = get_last_child_index();
					retVal = true;
				}
				break;

				case static_cast<std::uint8_t>(AttributeName::DefaultChildIndex):
				{
					returnedAttributeData = get_default_child_index();
					retVal = true;
				}
				break;

				case static_cast<std::uint8_t>(AttributeName::Options):
				{
					returnedAttributeData = get_options();
					retVal = true;
				}
				break;

				default:
				{
					// Do nothing, return false
				}
				break;
			}
		}
		return retVal;
	}

	std::uint16_t Animation::get_refresh_interval() const
	{
		return refreshInterval;
	}

	void Animation::set_refresh_interval(std::uint16_t value)
	{
		refreshInterval = value;
	}

	std::uint8_t Animation::get_value() const
	{
		return value;
	}

	void Animation::set_value(std::uint8_t inputValue)
	{
		value = inputValue;
	}

	bool Animation::get_enabled() const
	{
		return enabled;
	}

	void Animation::set_enabled(bool value)
	{
		enabled = value;
	}

	std::uint8_t Animation::get_first_child_index() const
	{
		return firstChildIndex;
	}

	void Animation::set_first_child_index(std::uint8_t value)
	{
		firstChildIndex = value;
	}

	std::uint8_t Animation::get_last_child_index() const
	{
		return lastChildIndex;
	}

	void Animation::set_last_child_index(std::uint8_t value)
	{
		lastChildIndex = value;
	}

	std::uint8_t Animation::get_default_child_index() const
	{
		return defaultChildIndex;
	}

	void Animation::set_default_child_index(std::uint8_t value)
	{
		defaultChildIndex = value;
	}

	std::uint8_t Animation::get_options() const
	{
		return optionsBitfield;
	}

	void Animation::set_options(std::uint8_t value)
	{
		optionsBitfield = value;
	}

	VirtualTerminalObjectType ObjectLabelReferenceList::get_object_type() const
	{
		return VirtualTerminalObjectType::ObjectLabelRefrenceList;
	}

	std::uint32_t ObjectLabelReferenceList::get_minumum_object_length() const
	{
		return MIN_OBJECT_LENGTH;
	}

	bool ObjectLabelReferenceList::get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool) const
	{
		// Table B.64 allows an object to be labelled at most once, and requires the object pool to be
		// rejected when one is labelled more than once.
		bool anyWrongChildType = has_duplicate_labelled_objects();

		for (const auto &label : labels)
		{
			if (anyWrongChildType)
			{
				break;
			}

			// Verify the label string is a string variable or NULL_OBJECT_ID
			if (NULL_OBJECT_ID != label.stringVariableID)
			{
				auto stringVariableObject = get_object_by_id(label.stringVariableID, objectPool);

				if (nullptr != stringVariableObject)
				{
					if (VirtualTerminalObjectType::StringVariable != stringVariableObject->get_object_type())
					{
						anyWrongChildType = true;
					}
				}
				else
				{
					anyWrongChildType = true;
				}
			}

			// Verify the graphic representation exists or is NULL_OBJECT_ID. Table B.64 places no
			// restriction on which type of object may be drawn as a designator.
			if (NULL_OBJECT_ID != label.graphicObjectID)
			{
				if (nullptr == get_object_by_id(label.graphicObjectID, objectPool))
				{
					anyWrongChildType = true;
				}
			}
		}

		for (const auto &child : children)
		{
			auto childObject = get_object_by_id(child.id, objectPool);

			if ((nullptr == childObject) ||
			    (VirtualTerminalObjectType::Macro != childObject->get_object_type()))
			{
				anyWrongChildType = true;
				break;
			}
		}
		return (!anyWrongChildType);
	}

	bool ObjectLabelReferenceList::set_attribute(std::uint8_t, std::uint32_t, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &, AttributeError &returnedError)
	{
		returnedError = AttributeError::InvalidAttributeID;
		return false;
	}

	bool ObjectLabelReferenceList::get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const
	{
		bool retVal = false;

		if (attributeID < static_cast<std::uint8_t>(AttributeName::NumberOfAttributes))
		{
			switch (static_cast<AttributeName>(attributeID))
			{
				case AttributeName::Type:
				{
					returnedAttributeData = static_cast<std::uint32_t>(get_object_type());
					retVal = true;
				}
				break;

				default:
				{
					// Do nothing return false
				}
				break;
			}
		}
		return retVal;
	}

	void ObjectLabelReferenceList::add_label(std::uint16_t objectID, std::uint16_t stringVariableID, std::uint8_t fontType, std::uint16_t graphicObjectID)
	{
		labels.push_back({ objectID, stringVariableID, fontType, graphicObjectID });
	}

	std::uint16_t ObjectLabelReferenceList::get_number_of_labels() const
	{
		return static_cast<std::uint16_t>(labels.size());
	}

	bool ObjectLabelReferenceList::get_label(std::uint16_t objectID, ObjectLabel &returnedLabel) const
	{
		bool retVal = false;

		for (const auto &label : labels)
		{
			if (objectID == label.objectID)
			{
				returnedLabel = label;
				retVal = true;
				break;
			}
		}
		return retVal;
	}

	bool ObjectLabelReferenceList::set_label(std::uint16_t objectID, std::uint16_t stringVariableID, std::uint8_t fontType, std::uint16_t graphicObjectID)
	{
		bool retVal = false;

		for (auto &label : labels)
		{
			if (objectID == label.objectID)
			{
				label.stringVariableID = stringVariableID;
				label.fontType = fontType;
				label.graphicObjectID = graphicObjectID;
				retVal = true;
				break;
			}
		}
		return retVal;
	}

	bool ObjectLabelReferenceList::has_duplicate_labelled_objects() const
	{
		// The label count comes off the wire as a 16 bit field, so a pool may declare up to 65535
		// labels. This runs on the pool-parse path, where a pairwise scan of that many entries would
		// be billions of comparisons, so the IDs are sorted and adjacent equals looked for instead.
		std::vector<std::uint16_t> labelledObjectIDs;

		labelledObjectIDs.reserve(labels.size());

		for (const auto &label : labels)
		{
			labelledObjectIDs.push_back(label.objectID);
		}
		std::sort(labelledObjectIDs.begin(), labelledObjectIDs.end());
		return (labelledObjectIDs.end() != std::adjacent_find(labelledObjectIDs.begin(), labelledObjectIDs.end()));
	}
} // namespace isobus
