//================================================================================================
/// @file isobus_virtual_terminal_objects_isovt.cpp
///
/// @brief Implements the fork's added VT object pool object classes: the Animation object (ISO
/// 11783-6 Table B.72) and the Object Label Reference List object (Table B.64), plus the VT version 6
/// Colour Palette (B.73), Graphic Data (B.74), Working Set Special Controls (B.78) and Scaled Graphic
/// (B.76) objects.
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
					// Table B.59 AID 7 is an IEEE 754 float carried as raw bits through the generic uint32,
					// with the attribute range -32.0 to +32.0. An out-of-range or non-finite value is refused
					// with the invalid-value error, symmetric with the Zoom Viewport sub-command's F.57
					// validation (the carry-0061 discipline: attribute writes are validated like the command
					// path). In-range non-positive values are accepted -- B.59's range literally permits them
					// -- and the renderer composites those at 1:1. The negated-comparison form rejects NaN,
					// which fails every ordered comparison.
					float zoom = 1.0f;
					std::memcpy(&zoom, &rawAttributeData, sizeof(float));
					if (!(zoom >= -32.0f) || !(zoom <= 32.0f))
					{
						returnedError = AttributeError::InvalidValue;
					}
					else
					{
						set_viewport_zoom(zoom);
						retVal = true;
					}
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

	VirtualTerminalObjectType ColourPalette::get_object_type() const
	{
		return VirtualTerminalObjectType::ColourPalette;
	}

	std::uint32_t ColourPalette::get_minumum_object_length() const
	{
		return MIN_OBJECT_LENGTH;
	}

	bool ColourPalette::get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &) const
	{
		// Table B.73 declares no children and no macro list, and the parse already bounds the entry count
		// to 256. The palette stands alone, so validity here is just a present object ID.
		return (NULL_OBJECT_ID != objectID);
	}

	bool ColourPalette::set_attribute(std::uint8_t attributeID, std::uint32_t rawAttributeData, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &, AttributeError &returnedError)
	{
		bool retVal = false;

		if (static_cast<std::uint8_t>(AttributeName::Options) == attributeID)
		{
			// Table B.73: Options is a writable attribute (Change Attribute allowed), but every bit is
			// reserved and sent as zero. The value is stored so it round-trips.
			set_options(static_cast<std::uint8_t>(rawAttributeData));
			retVal = true;
		}
		else
		{
			// Type [0] is read-only; any other ID does not exist on this object.
			returnedError = AttributeError::InvalidAttributeID;
		}
		return retVal;
	}

	bool ColourPalette::get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const
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

				case AttributeName::Options:
				{
					returnedAttributeData = optionsBitfield;
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

	std::uint8_t ColourPalette::get_options() const
	{
		return optionsBitfield;
	}

	void ColourPalette::set_options(std::uint8_t value)
	{
		optionsBitfield = value;
	}

	void ColourPalette::add_colour(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha)
	{
		colours.push_back({ red, green, blue, alpha });
	}

	std::uint16_t ColourPalette::get_number_of_colours() const
	{
		return static_cast<std::uint16_t>(colours.size());
	}

	ColourPalette::Colour ColourPalette::get_colour(std::uint16_t index) const
	{
		if (index < colours.size())
		{
			return colours[index];
		}
		return { 0, 0, 0, 0 };
	}

	VirtualTerminalObjectType GraphicData::get_object_type() const
	{
		return VirtualTerminalObjectType::GraphicData;
	}

	std::uint32_t GraphicData::get_minumum_object_length() const
	{
		return MIN_OBJECT_LENGTH;
	}

	bool GraphicData::get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &) const
	{
		// Table B.74 declares no children and no macro list, and the parse already rejects a non-PNG
		// format. The raw bytes are self-contained, so validity here is just a present object ID.
		return (NULL_OBJECT_ID != objectID);
	}

	bool GraphicData::set_attribute(std::uint8_t, std::uint32_t, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &, AttributeError &returnedError)
	{
		// Table B.74 allows no commands, so every attribute (Type [0], Format [1]) is read-only.
		returnedError = AttributeError::InvalidAttributeID;
		return false;
	}

	bool GraphicData::get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const
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

				case AttributeName::Format:
				{
					returnedAttributeData = static_cast<std::uint8_t>(format);
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

	GraphicData::Format GraphicData::get_format() const
	{
		return format;
	}

	void GraphicData::set_format(Format value)
	{
		format = value;
	}

	const std::vector<std::uint8_t> &GraphicData::get_raw_data() const
	{
		return rawData;
	}

	void GraphicData::set_raw_data(const std::uint8_t *data, std::uint32_t length)
	{
		if ((nullptr != data) && (0u != length))
		{
			rawData.assign(data, data + length);
		}
		else
		{
			rawData.clear();
		}
	}

	VirtualTerminalObjectType WorkingSetSpecialControls::get_object_type() const
	{
		return VirtualTerminalObjectType::WorkingSetSpecialControls;
	}

	std::uint32_t WorkingSetSpecialControls::get_minumum_object_length() const
	{
		return MIN_OBJECT_LENGTH;
	}

	bool WorkingSetSpecialControls::get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &) const
	{
		// Table B.78 declares no children and no macro list. The referenced Colour Map / Colour Palette
		// objects are consulted when the pool is first rendered, which validates them then, so validity
		// here is just a present object ID.
		return (NULL_OBJECT_ID != objectID);
	}

	bool WorkingSetSpecialControls::set_attribute(std::uint8_t, std::uint32_t, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &, AttributeError &returnedError)
	{
		// Table B.78 allows only the Get Attribute Value message, so every attribute is read-only.
		returnedError = AttributeError::InvalidAttributeID;
		return false;
	}

	bool WorkingSetSpecialControls::get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const
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

				case AttributeName::NumberOfBytesToFollow:
				{
					returnedAttributeData = numberOfBytesToFollow;
					retVal = true;
				}
				break;

				case AttributeName::ColourMapObjectID:
				{
					returnedAttributeData = colourMapObjectID;
					retVal = true;
				}
				break;

				case AttributeName::ColourPaletteObjectID:
				{
					returnedAttributeData = colourPaletteObjectID;
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

	std::uint16_t WorkingSetSpecialControls::get_number_of_bytes_to_follow() const
	{
		return numberOfBytesToFollow;
	}

	void WorkingSetSpecialControls::set_number_of_bytes_to_follow(std::uint16_t value)
	{
		numberOfBytesToFollow = value;
	}

	std::uint16_t WorkingSetSpecialControls::get_colour_map_object_id() const
	{
		return colourMapObjectID;
	}

	void WorkingSetSpecialControls::set_colour_map_object_id(std::uint16_t value)
	{
		colourMapObjectID = value;
	}

	std::uint16_t WorkingSetSpecialControls::get_colour_palette_object_id() const
	{
		return colourPaletteObjectID;
	}

	void WorkingSetSpecialControls::set_colour_palette_object_id(std::uint16_t value)
	{
		colourPaletteObjectID = value;
	}

	void WorkingSetSpecialControls::add_language_pair(std::uint8_t languageHigh, std::uint8_t languageLow, std::uint8_t countryHigh, std::uint8_t countryLow)
	{
		LanguagePair pair;
		pair.languageCode = { static_cast<char>(languageHigh), static_cast<char>(languageLow) };
		pair.countryCode = { static_cast<char>(countryHigh), static_cast<char>(countryLow) };
		languagePairs.push_back(pair);
	}

	std::uint8_t WorkingSetSpecialControls::get_number_of_language_pairs() const
	{
		return static_cast<std::uint8_t>(languagePairs.size());
	}

	WorkingSetSpecialControls::LanguagePair WorkingSetSpecialControls::get_language_pair(std::uint8_t index) const
	{
		if (index < languagePairs.size())
		{
			return languagePairs[index];
		}
		return { { ' ', ' ' }, { ' ', ' ' } };
	}

	VirtualTerminalObjectType ScaledGraphic::get_object_type() const
	{
		return VirtualTerminalObjectType::ScaledGraphic;
	}

	std::uint32_t ScaledGraphic::get_minumum_object_length() const
	{
		return MIN_OBJECT_LENGTH;
	}

	bool ScaledGraphic::get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &) const
	{
		// Table B.76 declares no children beyond the macro list. The Value attribute references a graphic
		// object (Graphic Data / Picture Graphic / Object Pointer / NULL); that reference is resolved and
		// validated when the object is rendered, so validity here is just a present object ID.
		return (NULL_OBJECT_ID != objectID);
	}

	bool ScaledGraphic::set_attribute(std::uint8_t attributeID, std::uint32_t rawAttributeData, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &, AttributeError &returnedError)
	{
		bool retVal = false;

		if (attributeID < static_cast<std::uint8_t>(AttributeName::NumberOfAttributes))
		{
			switch (static_cast<AttributeName>(attributeID))
			{
				case AttributeName::Width:
				{
					set_width(static_cast<std::uint16_t>(rawAttributeData));
					retVal = true;
				}
				break;

				case AttributeName::Height:
				{
					set_height(static_cast<std::uint16_t>(rawAttributeData));
					retVal = true;
				}
				break;

				case AttributeName::ScaleType:
				{
					set_scale_type(static_cast<std::uint8_t>(rawAttributeData));
					retVal = true;
				}
				break;

				case AttributeName::Options:
				{
					set_options(static_cast<std::uint8_t>(rawAttributeData));
					retVal = true;
				}
				break;

				case AttributeName::Value:
				{
					set_value(static_cast<std::uint16_t>(rawAttributeData));
					retVal = true;
				}
				break;

				default:
				{
					// Type [0] is read-only.
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

	bool ScaledGraphic::get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const
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

				case AttributeName::Width:
				{
					returnedAttributeData = get_width();
					retVal = true;
				}
				break;

				case AttributeName::Height:
				{
					returnedAttributeData = get_height();
					retVal = true;
				}
				break;

				case AttributeName::ScaleType:
				{
					returnedAttributeData = scaleTypeByte;
					retVal = true;
				}
				break;

				case AttributeName::Options:
				{
					returnedAttributeData = optionsBitfield;
					retVal = true;
				}
				break;

				case AttributeName::Value:
				{
					returnedAttributeData = value;
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

	std::uint8_t ScaledGraphic::get_scale_type() const
	{
		return scaleTypeByte;
	}

	void ScaledGraphic::set_scale_type(std::uint8_t value)
	{
		scaleTypeByte = value;
	}

	std::uint8_t ScaledGraphic::get_options() const
	{
		return optionsBitfield;
	}

	void ScaledGraphic::set_options(std::uint8_t value)
	{
		optionsBitfield = value;
	}

	std::uint16_t ScaledGraphic::get_value() const
	{
		return value;
	}

	void ScaledGraphic::set_value(std::uint16_t inputValue)
	{
		value = inputValue;
	}

	void picture_graphic_set_pixel(PictureGraphic &picture, std::int32_t x, std::int32_t y, std::uint8_t colourIndex)
	{
		const std::int32_t width = static_cast<std::int32_t>(picture.get_actual_width());
		const std::int32_t height = static_cast<std::int32_t>(picture.get_actual_height());
		if ((x < 0) || (y < 0) || (x >= width) || (y >= height))
		{
			return;
		}

		// The raster is expanded to one index per pixel at parse (add_raw_data caps growth at
		// actualWidth*actualHeight), so the linear index is y*width + x. A raster shorter than its declared
		// dimensions (a pool that under-supplied data) is written only where the index is in range.
		std::vector<std::uint8_t> &raster = picture.get_raw_data();
		const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
		  static_cast<std::size_t>(x);
		if (index < raster.size())
		{
			raster[index] = colourIndex;
		}
	}
} // namespace isobus
