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
#include <cstring>

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

	VirtualTerminalObjectType GraphicsContext::get_object_type() const
	{
		return VirtualTerminalObjectType::GraphicsContext;
	}

	std::uint32_t GraphicsContext::get_minumum_object_length() const
	{
		return MIN_OBJECT_LENGTH;
	}

	bool GraphicsContext::get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &) const
	{
		// Table B.59 declares no children and no macro list, and the parse already rejects an invalid
		// canvas format and an oversize canvas. A dangling Font/Line/Fill Attributes reference is not a
		// reason to reject the pool -- those objects are only consulted by the drawing sub-commands, which
		// validate the reference themselves at execution time -- so validity here is just a present ID.
		return (NULL_OBJECT_ID != objectID);
	}

	bool GraphicsContext::set_attribute(std::uint8_t attributeID, std::uint32_t rawAttributeData, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &, AttributeError &returnedError)
	{
		bool retVal = false;

		if (attributeID < static_cast<std::uint8_t>(AttributeName::NumberOfAttributes))
		{
			switch (static_cast<AttributeName>(attributeID))
			{
				case AttributeName::ViewportWidth:
				{
					// Viewport width/height are the object's display size, so they are the base width/height.
					set_width(static_cast<std::uint16_t>(rawAttributeData));
					retVal = true;
				}
				break;

				case AttributeName::ViewportHeight:
				{
					set_height(static_cast<std::uint16_t>(rawAttributeData));
					retVal = true;
				}
				break;

				case AttributeName::ViewportX:
				{
					set_viewport_x(static_cast<std::int16_t>(static_cast<std::uint16_t>(rawAttributeData)));
					retVal = true;
				}
				break;

				case AttributeName::ViewportY:
				{
					set_viewport_y(static_cast<std::int16_t>(static_cast<std::uint16_t>(rawAttributeData)));
					retVal = true;
				}
				break;

				case AttributeName::ViewportZoom:
				{
					// Table B.59 AID 7 is an IEEE 754 float carried as raw bits through the generic uint32.
					float zoom = 1.0f;
					std::memcpy(&zoom, &rawAttributeData, sizeof(float));
					set_viewport_zoom(zoom);
					retVal = true;
				}
				break;

				case AttributeName::GraphicsCursorX:
				{
					set_cursor_x(static_cast<std::int16_t>(static_cast<std::uint16_t>(rawAttributeData)));
					retVal = true;
				}
				break;

				case AttributeName::GraphicsCursorY:
				{
					set_cursor_y(static_cast<std::int16_t>(static_cast<std::uint16_t>(rawAttributeData)));
					retVal = true;
				}
				break;

				case AttributeName::ForegroundColour:
				{
					set_foreground_colour(static_cast<std::uint8_t>(rawAttributeData));
					retVal = true;
				}
				break;

				case AttributeName::BackgroundColour:
				{
					// Table B.59 byte 25 note / Table B.58 On Change Background: "Writing this attribute at
					// runtime shall fill this object, effectively erasing any content." The whole-canvas fill
					// is the erase.
					const std::uint8_t colour = static_cast<std::uint8_t>(rawAttributeData);
					set_background_color(colour);
					fill_canvas(colour);
					retVal = true;
				}
				break;

				case AttributeName::FontAttributesObject:
				{
					set_font_attributes_object_id(static_cast<std::uint16_t>(rawAttributeData));
					retVal = true;
				}
				break;

				case AttributeName::LineAttributesObject:
				{
					set_line_attributes_object_id(static_cast<std::uint16_t>(rawAttributeData));
					retVal = true;
				}
				break;

				case AttributeName::FillAttributesObject:
				{
					set_fill_attributes_object_id(static_cast<std::uint16_t>(rawAttributeData));
					retVal = true;
				}
				break;

				case AttributeName::Format:
				{
					if (static_cast<std::uint8_t>(rawAttributeData) <= static_cast<std::uint8_t>(Format::EightBitColour))
					{
						set_format(static_cast<Format>(static_cast<std::uint8_t>(rawAttributeData)));
						retVal = true;
					}
					else
					{
						returnedError = AttributeError::InvalidValue;
					}
				}
				break;

				case AttributeName::Options:
				{
					// set_options masks the reserved bits 2-7 to zero (Table B.59 byte 33).
					set_options(static_cast<std::uint8_t>(rawAttributeData));
					retVal = true;
				}
				break;

				case AttributeName::TransparencyColour:
				{
					set_transparency_colour(static_cast<std::uint8_t>(rawAttributeData));
					retVal = true;
				}
				break;

				default:
				{
					// Type [0], Canvas Width [5] and Canvas Height [6] are read-only (bracketed AIDs).
					returnedError = AttributeError::InvalidAttributeID;
				}
				break;
			}
		}
		else
		{
			returnedError = AttributeError::InvalidAttributeID;
		}
		return retVal;
	}

	bool GraphicsContext::get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const
	{
		bool retVal = false;

		if (attributeID < static_cast<std::uint8_t>(AttributeName::NumberOfAttributes))
		{
			switch (static_cast<AttributeName>(attributeID))
			{
				case AttributeName::Type:
				{
					returnedAttributeData = static_cast<std::uint8_t>(get_object_type());
					retVal = true;
				}
				break;

				case AttributeName::ViewportWidth:
				{
					returnedAttributeData = get_width();
					retVal = true;
				}
				break;

				case AttributeName::ViewportHeight:
				{
					returnedAttributeData = get_height();
					retVal = true;
				}
				break;

				case AttributeName::ViewportX:
				{
					returnedAttributeData = static_cast<std::uint16_t>(viewportX);
					retVal = true;
				}
				break;

				case AttributeName::ViewportY:
				{
					returnedAttributeData = static_cast<std::uint16_t>(viewportY);
					retVal = true;
				}
				break;

				case AttributeName::CanvasWidth:
				{
					returnedAttributeData = canvasWidth;
					retVal = true;
				}
				break;

				case AttributeName::CanvasHeight:
				{
					returnedAttributeData = canvasHeight;
					retVal = true;
				}
				break;

				case AttributeName::ViewportZoom:
				{
					// AID 7 is a float returned as its raw bits, like the Output/Input Number Scale attribute.
					std::uint32_t bits = 0;
					std::memcpy(&bits, &viewportZoom, sizeof(float));
					returnedAttributeData = bits;
					retVal = true;
				}
				break;

				case AttributeName::GraphicsCursorX:
				{
					returnedAttributeData = static_cast<std::uint16_t>(cursorX);
					retVal = true;
				}
				break;

				case AttributeName::GraphicsCursorY:
				{
					returnedAttributeData = static_cast<std::uint16_t>(cursorY);
					retVal = true;
				}
				break;

				case AttributeName::ForegroundColour:
				{
					returnedAttributeData = foregroundColour;
					retVal = true;
				}
				break;

				case AttributeName::BackgroundColour:
				{
					returnedAttributeData = get_background_color();
					retVal = true;
				}
				break;

				case AttributeName::FontAttributesObject:
				{
					returnedAttributeData = fontAttributesObjectID;
					retVal = true;
				}
				break;

				case AttributeName::LineAttributesObject:
				{
					returnedAttributeData = lineAttributesObjectID;
					retVal = true;
				}
				break;

				case AttributeName::FillAttributesObject:
				{
					returnedAttributeData = fillAttributesObjectID;
					retVal = true;
				}
				break;

				case AttributeName::Format:
				{
					returnedAttributeData = static_cast<std::uint8_t>(format);
					retVal = true;
				}
				break;

				case AttributeName::Options:
				{
					returnedAttributeData = optionsBitfield;
					retVal = true;
				}
				break;

				case AttributeName::TransparencyColour:
				{
					returnedAttributeData = transparencyColour;
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

	std::int16_t GraphicsContext::get_viewport_x() const
	{
		return viewportX;
	}

	void GraphicsContext::set_viewport_x(std::int16_t value)
	{
		viewportX = value;
	}

	std::int16_t GraphicsContext::get_viewport_y() const
	{
		return viewportY;
	}

	void GraphicsContext::set_viewport_y(std::int16_t value)
	{
		viewportY = value;
	}

	std::uint16_t GraphicsContext::get_canvas_width() const
	{
		return canvasWidth;
	}

	std::uint16_t GraphicsContext::get_canvas_height() const
	{
		return canvasHeight;
	}

	float GraphicsContext::get_viewport_zoom() const
	{
		return viewportZoom;
	}

	void GraphicsContext::set_viewport_zoom(float value)
	{
		viewportZoom = value;
	}

	std::int16_t GraphicsContext::get_cursor_x() const
	{
		return cursorX;
	}

	void GraphicsContext::set_cursor_x(std::int16_t value)
	{
		cursorX = value;
	}

	std::int16_t GraphicsContext::get_cursor_y() const
	{
		return cursorY;
	}

	void GraphicsContext::set_cursor_y(std::int16_t value)
	{
		cursorY = value;
	}

	void GraphicsContext::move_cursor(std::int32_t deltaX, std::int32_t deltaY)
	{
		// F.56: the cursor may be moved outside the canvas. It is clamped only to the signed 16-bit range
		// the Graphics Cursor X/Y attributes (Table B.59) can represent, so a large relative move saturates
		// rather than wrapping.
		auto clampToInt16 = [](std::int32_t value) -> std::int16_t {
			if (value < -32768)
			{
				return -32768;
			}
			if (value > 32767)
			{
				return 32767;
			}
			return static_cast<std::int16_t>(value);
		};
		cursorX = clampToInt16(static_cast<std::int32_t>(cursorX) + deltaX);
		cursorY = clampToInt16(static_cast<std::int32_t>(cursorY) + deltaY);
	}

	std::uint8_t GraphicsContext::get_foreground_colour() const
	{
		return foregroundColour;
	}

	void GraphicsContext::set_foreground_colour(std::uint8_t value)
	{
		foregroundColour = value;
	}

	std::uint16_t GraphicsContext::get_font_attributes_object_id() const
	{
		return fontAttributesObjectID;
	}

	void GraphicsContext::set_font_attributes_object_id(std::uint16_t value)
	{
		fontAttributesObjectID = value;
	}

	std::uint16_t GraphicsContext::get_line_attributes_object_id() const
	{
		return lineAttributesObjectID;
	}

	void GraphicsContext::set_line_attributes_object_id(std::uint16_t value)
	{
		lineAttributesObjectID = value;
	}

	std::uint16_t GraphicsContext::get_fill_attributes_object_id() const
	{
		return fillAttributesObjectID;
	}

	void GraphicsContext::set_fill_attributes_object_id(std::uint16_t value)
	{
		fillAttributesObjectID = value;
	}

	GraphicsContext::Format GraphicsContext::get_format() const
	{
		return format;
	}

	void GraphicsContext::set_format(Format value)
	{
		format = value;
	}

	std::uint8_t GraphicsContext::get_options() const
	{
		return optionsBitfield;
	}

	void GraphicsContext::set_options(std::uint8_t value)
	{
		// Table B.59 byte 33: only bits 0 (transparency) and 1 (colour source) are defined; bits 2-7 are
		// reserved and shall be zero, so they are never stored set.
		optionsBitfield = static_cast<std::uint8_t>(value & 0x03u);
	}

	bool GraphicsContext::get_option(Options option) const
	{
		return 0u != (optionsBitfield & static_cast<std::uint8_t>(1u << static_cast<std::uint8_t>(option)));
	}

	std::uint8_t GraphicsContext::get_transparency_colour() const
	{
		return transparencyColour;
	}

	void GraphicsContext::set_transparency_colour(std::uint8_t value)
	{
		transparencyColour = value;
	}

	void GraphicsContext::allocate_canvas(std::uint16_t width, std::uint16_t height, std::uint8_t backgroundColourIndex)
	{
		canvasWidth = width;
		canvasHeight = height;
		// B.18 / Table B.59 byte 25 note: at parsing time the object is filled with the background colour.
		canvas.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), backgroundColourIndex);
	}

	const std::vector<std::uint8_t> &GraphicsContext::get_canvas() const
	{
		return canvas;
	}

	std::uint8_t GraphicsContext::get_pixel(std::int32_t x, std::int32_t y) const
	{
		if ((x < 0) || (y < 0) ||
		    (x >= static_cast<std::int32_t>(canvasWidth)) || (y >= static_cast<std::int32_t>(canvasHeight)))
		{
			return 0u;
		}
		return canvas[(static_cast<std::size_t>(y) * static_cast<std::size_t>(canvasWidth)) + static_cast<std::size_t>(x)];
	}

	void GraphicsContext::set_pixel(std::int32_t x, std::int32_t y, std::uint8_t colourIndex)
	{
		if ((x < 0) || (y < 0) ||
		    (x >= static_cast<std::int32_t>(canvasWidth)) || (y >= static_cast<std::int32_t>(canvasHeight)))
		{
			return;
		}
		canvas[(static_cast<std::size_t>(y) * static_cast<std::size_t>(canvasWidth)) + static_cast<std::size_t>(x)] = colourIndex;
	}

	void GraphicsContext::fill_rectangle(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, std::uint8_t colourIndex)
	{
		if ((width <= 0) || (height <= 0) || canvas.empty())
		{
			return;
		}

		// F.56: "The graphics drawn by this command shall be clipped to the size of the canvas." Clip the
		// rectangle to [0, canvasWidth) x [0, canvasHeight) before writing.
		std::int32_t left = (x < 0) ? 0 : x;
		std::int32_t top = (y < 0) ? 0 : y;
		std::int32_t right = x + width;
		std::int32_t bottom = y + height;
		if (right > static_cast<std::int32_t>(canvasWidth))
		{
			right = static_cast<std::int32_t>(canvasWidth);
		}
		if (bottom > static_cast<std::int32_t>(canvasHeight))
		{
			bottom = static_cast<std::int32_t>(canvasHeight);
		}

		for (std::int32_t row = top; row < bottom; ++row)
		{
			const std::size_t rowStart = static_cast<std::size_t>(row) * static_cast<std::size_t>(canvasWidth);
			for (std::int32_t col = left; col < right; ++col)
			{
				canvas[rowStart + static_cast<std::size_t>(col)] = colourIndex;
			}
		}
	}

	void GraphicsContext::fill_canvas(std::uint8_t colourIndex)
	{
		std::fill(canvas.begin(), canvas.end(), colourIndex);
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
