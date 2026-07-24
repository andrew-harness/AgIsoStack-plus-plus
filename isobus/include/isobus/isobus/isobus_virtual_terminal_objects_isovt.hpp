//================================================================================================
/// @file isobus_virtual_terminal_objects_isovt.hpp
///
/// @brief Declares the fork's added VT object pool object classes: the Animation object (ISO
/// 11783-6 Table B.72) and the Object Label Reference List object (Table B.64), plus the VT version 6
/// Colour Palette (B.73), Graphic Data (B.74), Working Set Special Controls (B.78) and Scaled Graphic
/// (B.76) objects.
///
/// This header is isovt-owned and has no upstream counterpart. Per ADR-0008, the fork's added
/// VTObject classes are declared here rather than interleaved in
/// isobus_virtual_terminal_objects.hpp, to keep that upstream header's merge surface small. It is
/// included once from the end of isobus_virtual_terminal_objects.hpp, so consumers that include
/// that header see these classes exactly as before.
//================================================================================================
#ifndef ISOBUS_VIRTUAL_TERMINAL_OBJECTS_ISOVT_HPP
#define ISOBUS_VIRTUAL_TERMINAL_OBJECTS_ISOVT_HPP

#include "isobus/isobus/isobus_virtual_terminal_objects.hpp"

#include <array>
#include <vector>

namespace isobus
{
	/// @brief The Animation object is used to display simple animations by cycling through a list of
	/// child objects. Each child is positioned relative to the animation's top left corner, exactly
	/// like a Container.
	/// @details The animation rate (Refresh Interval), the enabled/disabled behaviour and the frame
	/// range are parsed and stored so the object round-trips, but which child is displayed at any
	/// moment (the Value index) is decided by the VT. See ISO 11783-6 Table B.72.
	class Animation : public VTObject
	{
	public:
		/// @brief Enumerates this object's attributes which are assigned an attribute ID.
		/// The Change Attribute command allows any writable attribute with an AID to be changed.
		enum class AttributeName : std::uint8_t
		{
			Type = 0,
			Width = 1,
			Height = 2,
			RefreshInterval = 3,
			Value = 4,
			Enabled = 5,
			FirstChildIndex = 6,
			LastChildIndex = 7,
			DefaultChildIndex = 8,
			Options = 9,

			NumberOfAttributes = 10
		};

		/// @brief Constructor for an animation object
		Animation() = default;

		/// @brief Virtual destructor for an animation object
		~Animation() override = default;

		/// @brief Returns the VT object type of the underlying derived object
		/// @returns The VT object type of the underlying derived object
		VirtualTerminalObjectType get_object_type() const override;

		/// @brief Returns the minimum binary serialized length of the associated object
		/// @returns The minimum binary serialized length of the associated object
		std::uint32_t get_minumum_object_length() const override;

		/// @brief Performs basic error checking on the object and returns if the object is valid
		/// @param[in] objectPool The object pool to use when validating the object
		/// @returns `true` if the object passed basic error checks
		bool get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool) const override;

		/// @brief Sets an attribute and optionally returns an error code in the last parameter
		/// @param[in] attributeID The ID of the attribute to change
		/// @param[in] rawAttributeData The raw data to change the attribute to, as decoded in little endian format with unused
		/// bytes/bits set to zero.
		/// @param[in] objectPool The object pool to use when validating the objects affected by setting this attribute
		/// @param[out] returnedError If this function returns false, this will be the error code. If the function
		/// returns true, this value is undefined.
		/// @returns True if the attribute was changed, otherwise false (check the returnedError in this case to know why).
		bool set_attribute(std::uint8_t attributeID, std::uint32_t rawAttributeData, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool, AttributeError &returnedError) override;

		/// @brief Gets an attribute and returns the raw data in the last parameter
		/// @param[in] attributeID The ID of the attribute to get
		/// @param[out] returnedAttributeData The raw data of the attribute, as decoded in little endian format with unused
		/// bytes/bits set to zero. You may need to cast this to the correct type. If this function
		/// returns false, this value is undefined.
		/// @returns True if the attribute was retrieved, otherwise false (the attribute ID was invalid)
		bool get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const override;

		/// @brief Returns the desired time in milliseconds between refreshes of this object
		/// @returns The refresh interval in milliseconds
		std::uint16_t get_refresh_interval() const;

		/// @brief Sets the desired time in milliseconds between refreshes of this object
		/// @param[in] value The refresh interval in milliseconds
		void set_refresh_interval(std::uint16_t value);

		/// @brief Returns the list index of the child object currently displayed (the first item is index zero)
		/// @returns The list index of the child object currently displayed
		std::uint8_t get_value() const;

		/// @brief Sets the list index of the child object to display (the first item is index zero)
		/// @param[in] value The list index of the child object to display
		void set_value(std::uint8_t value);

		/// @brief Returns whether this object is enabled (animating)
		/// @returns `true` if the object is enabled (animating), otherwise `false` (stopped)
		bool get_enabled() const;

		/// @brief Sets whether this object is enabled (animating)
		/// @param[in] value The new enabled state
		void set_enabled(bool value);

		/// @brief Returns the index of the first child object in the animation sequence
		/// @returns The index of the first child object in the animation sequence
		std::uint8_t get_first_child_index() const;

		/// @brief Sets the index of the first child object in the animation sequence
		/// @param[in] value The index of the first child object in the animation sequence
		void set_first_child_index(std::uint8_t value);

		/// @brief Returns the index of the last child object in the animation sequence
		/// @returns The index of the last child object in the animation sequence
		std::uint8_t get_last_child_index() const;

		/// @brief Sets the index of the last child object in the animation sequence
		/// @param[in] value The index of the last child object in the animation sequence
		void set_last_child_index(std::uint8_t value);

		/// @brief Returns the index of the default child object in the animation sequence
		/// @returns The index of the default child object in the animation sequence
		std::uint8_t get_default_child_index() const;

		/// @brief Sets the index of the default child object in the animation sequence
		/// @param[in] value The index of the default child object in the animation sequence
		void set_default_child_index(std::uint8_t value);

		/// @brief Returns the options bitfield for this animation object (sequence mode and disabled behaviour)
		/// @returns The options bitfield for this animation object
		std::uint8_t get_options() const;

		/// @brief Sets the options bitfield for this animation object (sequence mode and disabled behaviour)
		/// @param[in] value The new options bitfield
		void set_options(std::uint8_t value);

	private:
		static constexpr std::uint32_t MIN_OBJECT_LENGTH = 17; ///< The fewest bytes of IOP data that can represent this object

		std::uint16_t refreshInterval = 0; ///< Desired time in ms between refreshes. Zero stops the timer but is not the same as enabled = 0.
		std::uint8_t value = 0; ///< List index of the child object currently displayed (the first item is index zero)
		std::uint8_t firstChildIndex = 0; ///< Index of the first child object in the animation sequence
		std::uint8_t lastChildIndex = 0; ///< Index of the last child object in the animation sequence
		std::uint8_t defaultChildIndex = 0; ///< Index of the default child object in the animation sequence
		std::uint8_t optionsBitfield = 0; ///< Bitfield of options: animation sequence mode and disabled behaviour (Table B.72)
		bool enabled = false; ///< When true the object is animating; when false it is stopped
	};

	/// @brief Defines an object label reference list object. The Object Label Reference List object, available in VT version 4
	/// and later, associates a label with objects in the object pool. A label is a string, a graphic representation, or both.
	/// An object pool contains at most one Object Label Reference List object, and an object is labelled at most once.
	/// Labels are intended for the VT's own proprietary screens, popup messages and editors rather than for the object pool's
	/// own masks.
	class ObjectLabelReferenceList : public VTObject
	{
	public:
		/// @brief Enumerates this object's attributes which are assigned an attribute ID.
		/// The Change Attribute command allows any writable attribute with an AID to be changed.
		enum class AttributeName : std::uint8_t
		{
			Type = 0,

			NumberOfAttributes = 1
		};

		/// @brief One entry of the label list: the object being labelled and the label itself.
		struct ObjectLabel
		{
			std::uint16_t objectID; ///< Object ID of the object being labelled
			std::uint16_t stringVariableID; ///< String Variable holding the label text, or NULL_OBJECT_ID
			std::uint8_t fontType; ///< Font type for the label string
			std::uint16_t graphicObjectID; ///< Object drawn as the label's designator, or NULL_OBJECT_ID
		};

		/// @brief Constructor for an object label reference list object
		ObjectLabelReferenceList() = default;

		/// @brief Virtual destructor for an object label reference list object
		~ObjectLabelReferenceList() override = default;

		/// @brief Returns the VT object type of the underlying derived object
		/// @returns The VT object type of the underlying derived object
		VirtualTerminalObjectType get_object_type() const override;

		/// @brief Returns the minimum binary serialized length of the associated object
		/// @returns The minimum binary serialized length of the associated object
		std::uint32_t get_minumum_object_length() const override;

		/// @brief Performs basic error checking on the object and returns if the object is valid
		/// @param[in] objectPool The object pool to use when validating the object
		/// @returns `true` if the object passed basic error checks
		bool get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool) const override;

		/// @brief Sets an attribute and optionally returns an error code in the last parameter
		/// @param[in] attributeID The ID of the attribute to change
		/// @param[in] rawAttributeData The raw data to change the attribute to, as decoded in little endian format with unused
		/// bytes/bits set to zero.
		/// @param[in] objectPool The object pool to use when validating the objects affected by setting this attribute
		/// @param[out] returnedError If this function returns false, this will be the error code. If the function
		/// returns true, this value is undefined.
		/// @returns True if the attribute was changed, otherwise false (check the returnedError in this case to know why).
		bool set_attribute(std::uint8_t attributeID, std::uint32_t rawAttributeData, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool, AttributeError &returnedError) override;

		/// @brief Gets an attribute and returns the raw data in the last parameter
		/// @param[in] attributeID The ID of the attribute to get
		/// @param[out] returnedAttributeData The raw data of the attribute, as decoded in little endian format with unused
		/// bytes/bits set to zero. You may need to cast this to the correct type. If this function
		/// returns false, this value is undefined.
		/// @returns True if the attribute was retrieved, otherwise false (the attribute ID was invalid)
		bool get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const override;

		/// @brief Appends a label to the list. Does not check whether the object is already labelled.
		/// @param[in] objectID The object ID of the object being labelled
		/// @param[in] stringVariableID The object ID of a String Variable holding the label string, or NULL_OBJECT_ID for no text
		/// @param[in] fontType The font type used to render the label string
		/// @param[in] graphicObjectID The object ID of an object used as the label's graphic representation, or NULL_OBJECT_ID for no designator
		void add_label(std::uint16_t objectID, std::uint16_t stringVariableID, std::uint8_t fontType, std::uint16_t graphicObjectID);

		/// @brief Returns the number of labels in this list
		/// @returns The number of labels in this list
		std::uint16_t get_number_of_labels() const;

		/// @brief Returns the label associated with an object, looked up by the object that is labelled
		/// @param[in] objectID The object ID of the labelled object to look up
		/// @param[out] returnedLabel The label associated with that object. Undefined if this function returns false.
		/// @returns True if the object has a label in this list, otherwise false
		bool get_label(std::uint16_t objectID, ObjectLabel &returnedLabel) const;

		/// @brief Updates the label associated with an object. Does not add a label for an object that has none.
		/// @param[in] objectID The object ID of the labelled object to update
		/// @param[in] stringVariableID The object ID of a String Variable holding the label string, or NULL_OBJECT_ID for no text
		/// @param[in] fontType The font type used to render the label string
		/// @param[in] graphicObjectID The object ID of an object used as the label's graphic representation, or NULL_OBJECT_ID for no designator
		/// @returns True if the label was updated, otherwise false (the object has no label in this list)
		bool set_label(std::uint16_t objectID, std::uint16_t stringVariableID, std::uint8_t fontType, std::uint16_t graphicObjectID);

		/// @brief Returns whether any object ID appears more than once in this list, which an object pool is not allowed to do
		/// @returns True if some object is labelled more than once, otherwise false
		bool has_duplicate_labelled_objects() const;

	private:
		static constexpr std::uint32_t MIN_OBJECT_LENGTH = 5; ///< The fewest bytes of IOP data that can represent this object
		std::vector<ObjectLabel> labels; ///< The labels this list associates with objects, one entry per labelled object
	};

	/// @brief The Graphics Context object (ISO 11783-6 Table B.59, VT version 4 and later) is a bitmap with a
	/// fixed canvas and a movable/zoomable viewport that a Working Set draws into at run time.
	/// @details B.18: the canvas pixels persist even when the object is off screen or another mask is shown,
	/// and are never saved by the Store Version command. The canvas is stored here as one colour-table index
	/// per pixel, row-major, canvasWidth*canvasHeight bytes, filled with the Background Colour attribute at
	/// parse (Table B.59, byte 25 note). The 21 Graphics Context sub-commands (Table F.1) are executed by the
	/// display layer through a host painter (ADR-0007/ADR-0008); this class holds the attributes and the
	/// canvas and offers the pixel primitives the painter drives.
	class GraphicsContext : public VTObject
	{
	public:
		/// @brief Enumerates this object's attributes which are assigned an attribute ID (ISO 11783-6 Table
		/// B.59). The bracketed AIDs -- Type [0], Canvas Width [5] and Canvas Height [6] -- are read-only:
		/// B.18 states the canvas size cannot change unless an entirely new object is uploaded.
		enum class AttributeName : std::uint8_t
		{
			Type = 0,
			ViewportWidth = 1,
			ViewportHeight = 2,
			ViewportX = 3,
			ViewportY = 4,
			CanvasWidth = 5, ///< read-only (bracketed [5])
			CanvasHeight = 6, ///< read-only (bracketed [6])
			ViewportZoom = 7, ///< IEEE 754 32-bit float, delivered as raw bits through the generic accessor
			GraphicsCursorX = 8,
			GraphicsCursorY = 9,
			ForegroundColour = 10,
			BackgroundColour = 11,
			FontAttributesObject = 12,
			LineAttributesObject = 13,
			FillAttributesObject = 14,
			Format = 15,
			Options = 16,
			TransparencyColour = 17,

			NumberOfAttributes = 18
		};

		/// @brief The canvas storage format (ISO 11783-6 Table B.59 Format, byte 32). The canvas is held one
		/// colour-table index per pixel regardless of format; the format only constrains the valid index
		/// range (0-1, 0-15, 0-255).
		enum class Format : std::uint8_t
		{
			Monochrome = 0, ///< 1 bit per pixel; indices 0 or 1
			FourBitColour = 1, ///< 4 bits per pixel; indices 0-15
			EightBitColour = 2 ///< 8 bits per pixel; indices 0-255
		};

		/// @brief Bit indices of the Options attribute (ISO 11783-6 Table B.59 Options, byte 33).
		enum class Options : std::uint8_t
		{
			Transparency = 0, ///< 0 = opaque, 1 = transparent (the Transparency Colour index shows through)
			Colour = 1 ///< 0 = draw with this object's Foreground/Background, 1 = draw with the Line/Font/Fill attribute objects
		};

		/// @brief Constructor for a graphics context object
		GraphicsContext() = default;

		/// @brief Virtual destructor for a graphics context object
		~GraphicsContext() override = default;

		/// @brief Returns the VT object type of the underlying derived object
		/// @returns The VT object type of the underlying derived object
		VirtualTerminalObjectType get_object_type() const override;

		/// @brief Returns the minimum binary serialized length of the associated object
		/// @returns The minimum binary serialized length of the associated object
		std::uint32_t get_minumum_object_length() const override;

		/// @brief Performs basic error checking on the object and returns if the object is valid
		/// @param[in] objectPool The object pool to use when validating the object
		/// @returns `true` if the object passed basic error checks
		bool get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool) const override;

		/// @brief Sets an attribute and optionally returns an error code in the last parameter
		/// @param[in] attributeID The ID of the attribute to change
		/// @param[in] rawAttributeData The raw data to change the attribute to, as decoded in little endian format with unused
		/// bytes/bits set to zero.
		/// @param[in] objectPool The object pool to use when validating the objects affected by setting this attribute
		/// @param[out] returnedError If this function returns false, this will be the error code. If the function
		/// returns true, this value is undefined.
		/// @returns True if the attribute was changed, otherwise false (check the returnedError in this case to know why).
		bool set_attribute(std::uint8_t attributeID, std::uint32_t rawAttributeData, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool, AttributeError &returnedError) override;

		/// @brief Gets an attribute and returns the raw data in the last parameter
		/// @param[in] attributeID The ID of the attribute to get
		/// @param[out] returnedAttributeData The raw data of the attribute, as decoded in little endian format with unused
		/// bytes/bits set to zero. You may need to cast this to the correct type. If this function
		/// returns false, this value is undefined.
		/// @returns True if the attribute was retrieved, otherwise false (the attribute ID was invalid)
		bool get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const override;

		/// @brief Returns the X position of the upper left corner of the viewport relative to the canvas (Table B.59 AID 3, signed)
		/// @returns The viewport X position
		std::int16_t get_viewport_x() const;

		/// @brief Sets the X position of the upper left corner of the viewport relative to the canvas (Table B.59 AID 3, signed)
		/// @param[in] value The new viewport X position
		void set_viewport_x(std::int16_t value);

		/// @brief Returns the Y position of the upper left corner of the viewport relative to the canvas (Table B.59 AID 4, signed)
		/// @returns The viewport Y position
		std::int16_t get_viewport_y() const;

		/// @brief Sets the Y position of the upper left corner of the viewport relative to the canvas (Table B.59 AID 4, signed)
		/// @param[in] value The new viewport Y position
		void set_viewport_y(std::int16_t value);

		/// @brief Returns the width of the canvas in pixels (Table B.59 AID [5], read-only)
		/// @returns The canvas width in pixels
		std::uint16_t get_canvas_width() const;

		/// @brief Returns the height of the canvas in pixels (Table B.59 AID [6], read-only)
		/// @returns The canvas height in pixels
		std::uint16_t get_canvas_height() const;

		/// @brief Returns the viewport magnification (Table B.59 AID 7). Zoom rendering is not applied yet; 1.0 is 1:1.
		/// @returns The viewport zoom factor
		float get_viewport_zoom() const;

		/// @brief Sets the viewport magnification (Table B.59 AID 7)
		/// @param[in] value The new viewport zoom factor
		void set_viewport_zoom(float value);

		/// @brief Returns the graphics cursor X position relative to the canvas (Table B.59 AID 8, signed)
		/// @returns The graphics cursor X position
		std::int16_t get_cursor_x() const;

		/// @brief Sets the graphics cursor X position relative to the canvas (Table B.59 AID 8, signed)
		/// @param[in] value The new graphics cursor X position
		void set_cursor_x(std::int16_t value);

		/// @brief Returns the graphics cursor Y position relative to the canvas (Table B.59 AID 9, signed)
		/// @returns The graphics cursor Y position
		std::int16_t get_cursor_y() const;

		/// @brief Sets the graphics cursor Y position relative to the canvas (Table B.59 AID 9, signed)
		/// @param[in] value The new graphics cursor Y position
		void set_cursor_y(std::int16_t value);

		/// @brief Moves the graphics cursor by a relative offset (Table F.1 sub-command 1). F.56 leaves the
		/// cursor unclamped -- it may move outside the canvas -- so the result is clamped only to the signed
		/// 16-bit range the attribute can hold.
		/// @param[in] deltaX The relative X offset to add to the cursor
		/// @param[in] deltaY The relative Y offset to add to the cursor
		void move_cursor(std::int32_t deltaX, std::int32_t deltaY);

		/// @brief Returns the foreground colour attribute (Table B.59 AID 10)
		/// @returns The foreground colour palette index
		std::uint8_t get_foreground_colour() const;

		/// @brief Sets the foreground colour attribute (Table B.59 AID 10)
		/// @param[in] value The new foreground colour palette index
		void set_foreground_colour(std::uint8_t value);

		/// @brief Returns the object ID of the Font Attributes object used for drawing text, or NULL_OBJECT_ID (Table B.59 AID 12)
		/// @returns The Font Attributes object ID
		std::uint16_t get_font_attributes_object_id() const;

		/// @brief Sets the object ID of the Font Attributes object used for drawing text (Table B.59 AID 12)
		/// @param[in] value The new Font Attributes object ID, or NULL_OBJECT_ID
		void set_font_attributes_object_id(std::uint16_t value);

		/// @brief Returns the object ID of the Line Attributes object used for drawing lines, or NULL_OBJECT_ID (Table B.59 AID 13)
		/// @returns The Line Attributes object ID
		std::uint16_t get_line_attributes_object_id() const;

		/// @brief Sets the object ID of the Line Attributes object used for drawing lines (Table B.59 AID 13)
		/// @param[in] value The new Line Attributes object ID, or NULL_OBJECT_ID
		void set_line_attributes_object_id(std::uint16_t value);

		/// @brief Returns the object ID of the Fill Attributes object used for filling, or NULL_OBJECT_ID (Table B.59 AID 14)
		/// @returns The Fill Attributes object ID
		std::uint16_t get_fill_attributes_object_id() const;

		/// @brief Sets the object ID of the Fill Attributes object used for filling (Table B.59 AID 14)
		/// @param[in] value The new Fill Attributes object ID, or NULL_OBJECT_ID
		void set_fill_attributes_object_id(std::uint16_t value);

		/// @brief Returns the canvas storage format (Table B.59 AID 15)
		/// @returns The canvas format
		Format get_format() const;

		/// @brief Sets the canvas storage format (Table B.59 AID 15)
		/// @param[in] value The new canvas format
		void set_format(Format value);

		/// @brief Returns the options bitfield (Table B.59 AID 16, bits 0-1; reserved bits 2-7 are never stored set)
		/// @returns The options bitfield
		std::uint8_t get_options() const;

		/// @brief Sets the options bitfield, masking the reserved bits 2-7 to zero (Table B.59 AID 16)
		/// @param[in] value The new options bitfield
		void set_options(std::uint8_t value);

		/// @brief Returns whether an option bit is set (Table B.59 AID 16)
		/// @param[in] option The option bit to test
		/// @returns True if the option bit is set, otherwise false
		bool get_option(Options option) const;

		/// @brief Returns the transparency colour index; pixels of this index show the background through when the transparency option is set (Table B.59 AID 17)
		/// @returns The transparency colour palette index
		std::uint8_t get_transparency_colour() const;

		/// @brief Sets the transparency colour index (Table B.59 AID 17)
		/// @param[in] value The new transparency colour palette index
		void set_transparency_colour(std::uint8_t value);

		/// @brief Allocates the canvas and fills it with the background colour, once, at parse time (B.18 /
		/// Table B.59 byte 25 note). The canvas size cannot change afterward.
		/// @param[in] width The canvas width in pixels
		/// @param[in] height The canvas height in pixels
		/// @param[in] backgroundColourIndex The palette index every canvas pixel starts at
		void allocate_canvas(std::uint16_t width, std::uint16_t height, std::uint8_t backgroundColourIndex);

		/// @brief Returns the canvas pixels: one colour-table index per pixel, row-major, canvasWidth*canvasHeight bytes.
		/// @returns The canvas pixel buffer (read-only)
		const std::vector<std::uint8_t> &get_canvas() const;

		/// @brief Returns the colour index of one canvas pixel, or 0 if the coordinate is outside the canvas.
		/// @param[in] x The canvas X coordinate
		/// @param[in] y The canvas Y coordinate
		/// @returns The colour index at (x, y), or 0 if out of bounds
		std::uint8_t get_pixel(std::int32_t x, std::int32_t y) const;

		/// @brief Sets one canvas pixel to a colour index. F.56: drawing is clipped to the canvas, so a
		/// coordinate outside the canvas is ignored.
		/// @param[in] x The canvas X coordinate
		/// @param[in] y The canvas Y coordinate
		/// @param[in] colourIndex The colour index to store
		void set_pixel(std::int32_t x, std::int32_t y, std::uint8_t colourIndex);

		/// @brief Fills a rectangle of the canvas with a colour index, clipped to the canvas edges (F.56:
		/// "The graphics drawn by this command shall be clipped to the size of the canvas.").
		/// @param[in] x The rectangle's left canvas X coordinate
		/// @param[in] y The rectangle's top canvas Y coordinate
		/// @param[in] width The rectangle width in pixels
		/// @param[in] height The rectangle height in pixels
		/// @param[in] colourIndex The colour index to fill with
		void fill_rectangle(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height, std::uint8_t colourIndex);

		/// @brief Fills the whole canvas with a colour index (Table B.59 byte 25 note: writing the Background
		/// Colour attribute at run time fills this object, erasing any content).
		/// @param[in] colourIndex The colour index to fill the whole canvas with
		void fill_canvas(std::uint8_t colourIndex);

		/// @brief The largest canvas area (width*height) that will be allocated. A 34-byte Graphics Context
		/// record declares its canvas dimensions with no pixel data behind them, so an unbounded declaration
		/// would let a tiny record demand a huge allocation. B.18 already warns pools to keep the canvas small
		/// (it costs canvasWidth*canvasHeight bytes at 8 bpp); a record above this ceiling is rejected at parse.
		static constexpr std::uint32_t MAX_CANVAS_AREA = 8u * 1024u * 1024u;

	private:
		static constexpr std::uint32_t MIN_OBJECT_LENGTH = 34; ///< Table B.59: fixed 34-byte record

		std::int16_t viewportX = 0; ///< Viewport X, signed, relative to the canvas (AID 3)
		std::int16_t viewportY = 0; ///< Viewport Y, signed, relative to the canvas (AID 4)
		std::uint16_t canvasWidth = 0; ///< Canvas width in pixels (AID [5], read-only)
		std::uint16_t canvasHeight = 0; ///< Canvas height in pixels (AID [6], read-only)
		float viewportZoom = 1.0f; ///< Viewport magnification (AID 7); 1.0 is 1:1
		std::int16_t cursorX = 0; ///< Graphics cursor X, signed, relative to the canvas (AID 8)
		std::int16_t cursorY = 0; ///< Graphics cursor Y, signed, relative to the canvas (AID 9)
		std::uint8_t foregroundColour = 0; ///< Foreground colour palette index (AID 10). Background colour uses the base VTObject backgroundColor (AID 11).
		std::uint16_t fontAttributesObjectID = NULL_OBJECT_ID; ///< Font Attributes object ID or NULL (AID 12)
		std::uint16_t lineAttributesObjectID = NULL_OBJECT_ID; ///< Line Attributes object ID or NULL (AID 13)
		std::uint16_t fillAttributesObjectID = NULL_OBJECT_ID; ///< Fill Attributes object ID or NULL (AID 14)
		Format format = Format::Monochrome; ///< Canvas format (AID 15)
		std::uint8_t optionsBitfield = 0; ///< Options bits 0-1 (AID 16); reserved bits 2-7 never stored set
		std::uint8_t transparencyColour = 0; ///< Transparency colour palette index (AID 17)
		std::vector<std::uint8_t> canvas; ///< canvasWidth*canvasHeight colour indices, row-major
	};

	/// @brief The Colour Palette object (ISO 11783-6 Table B.73, VT version 6 and later) replaces the VT
	/// standard colour palette in use for a Working Set with up to 256 ARGB values, arranged in the order
	/// of the VT colour number.
	/// @details B.26: a subset of the palette can be redefined, starting with colour zero. Each entry is
	/// four bytes on the wire in little-endian ARGB order (B, G, R, A). The entries are parsed and stored
	/// here so the object round-trips; the renderer applies them during colour resolution (deferred).
	class ColourPalette : public VTObject
	{
	public:
		/// @brief Enumerates this object's attributes which are assigned an attribute ID (ISO 11783-6 Table B.73).
		enum class AttributeName : std::uint8_t
		{
			Type = 0,
			Options = 1,

			NumberOfAttributes = 2
		};

		/// @brief One palette entry: an ARGB colour. The wire stores the four channels little-endian (B, G, R, A).
		struct Colour
		{
			std::uint8_t red; ///< Red channel 0-255
			std::uint8_t green; ///< Green channel 0-255
			std::uint8_t blue; ///< Blue channel 0-255
			std::uint8_t alpha; ///< Alpha channel: 0 (transparent) to 255 (opaque)
		};

		/// @brief Constructor for a colour palette object
		ColourPalette() = default;

		/// @brief Virtual destructor for a colour palette object
		~ColourPalette() override = default;

		/// @brief Returns the VT object type of the underlying derived object
		/// @returns The VT object type of the underlying derived object
		VirtualTerminalObjectType get_object_type() const override;

		/// @brief Returns the minimum binary serialized length of the associated object
		/// @returns The minimum binary serialized length of the associated object
		std::uint32_t get_minumum_object_length() const override;

		/// @brief Performs basic error checking on the object and returns if the object is valid
		/// @param[in] objectPool The object pool to use when validating the object
		/// @returns `true` if the object passed basic error checks
		bool get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool) const override;

		/// @brief Sets an attribute and optionally returns an error code in the last parameter
		/// @param[in] attributeID The ID of the attribute to change
		/// @param[in] rawAttributeData The raw data to change the attribute to, as decoded in little endian format with unused
		/// bytes/bits set to zero.
		/// @param[in] objectPool The object pool to use when validating the objects affected by setting this attribute
		/// @param[out] returnedError If this function returns false, this will be the error code. If the function
		/// returns true, this value is undefined.
		/// @returns True if the attribute was changed, otherwise false (check the returnedError in this case to know why).
		bool set_attribute(std::uint8_t attributeID, std::uint32_t rawAttributeData, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool, AttributeError &returnedError) override;

		/// @brief Gets an attribute and returns the raw data in the last parameter
		/// @param[in] attributeID The ID of the attribute to get
		/// @param[out] returnedAttributeData The raw data of the attribute, as decoded in little endian format with unused
		/// bytes/bits set to zero. You may need to cast this to the correct type. If this function
		/// returns false, this value is undefined.
		/// @returns True if the attribute was retrieved, otherwise false (the attribute ID was invalid)
		bool get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const override;

		/// @brief Returns the options bitfield (Table B.73 AID 1; reserved, sent as zero)
		/// @returns The options bitfield
		std::uint8_t get_options() const;

		/// @brief Sets the options bitfield (Table B.73 AID 1)
		/// @param[in] value The new options bitfield
		void set_options(std::uint8_t value);

		/// @brief Appends one ARGB entry to the palette, in VT colour-number order.
		/// @param[in] red The red channel
		/// @param[in] green The green channel
		/// @param[in] blue The blue channel
		/// @param[in] alpha The alpha channel (0 transparent, 255 opaque)
		void add_colour(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha);

		/// @brief Returns the number of ARGB entries this palette redefines (starting at colour zero)
		/// @returns The number of palette entries
		std::uint16_t get_number_of_colours() const;

		/// @brief Returns one palette entry by index, or a fully transparent black if the index is out of range
		/// @param[in] index The palette entry index (VT colour number)
		/// @returns The ARGB entry at that index
		Colour get_colour(std::uint16_t index) const;

	private:
		static constexpr std::uint32_t MIN_OBJECT_LENGTH = 6; ///< Table B.73: id + type + options + count, with zero entries
		std::uint8_t optionsBitfield = 0; ///< Options (AID 1); reserved, sent as zero
		std::vector<Colour> colours; ///< The ARGB entries, one per redefined VT colour number starting at zero
	};

	/// @brief The Graphic Data object (ISO 11783-6 Table B.74, VT version 6 and later) carries the raw
	/// bytes of a graphic image, self-contained with its own colour palette.
	/// @details B.27: the Format byte is read-only and must be 0 (PNG, restricted to 32-bit RGBA); a
	/// non-zero format is a parse error. The raw bytes are stored here undecoded -- decoding into a raster
	/// is a later phase -- and are referenced by a Scaled Graphic object's Value attribute. B.74 allows no
	/// commands on this object.
	class GraphicData : public VTObject
	{
	public:
		/// @brief Enumerates this object's attributes which are assigned an attribute ID (ISO 11783-6 Table B.74).
		enum class AttributeName : std::uint8_t
		{
			Type = 0,
			Format = 1, ///< read-only (Table B.74 [1])

			NumberOfAttributes = 2
		};

		/// @brief The only Graphic Data format defined (Table B.74): PNG, restricted to 32-bit RGBA.
		enum class Format : std::uint8_t
		{
			PNG = 0
		};

		/// @brief Constructor for a graphic data object
		GraphicData() = default;

		/// @brief Virtual destructor for a graphic data object
		~GraphicData() override = default;

		/// @brief Returns the VT object type of the underlying derived object
		/// @returns The VT object type of the underlying derived object
		VirtualTerminalObjectType get_object_type() const override;

		/// @brief Returns the minimum binary serialized length of the associated object
		/// @returns The minimum binary serialized length of the associated object
		std::uint32_t get_minumum_object_length() const override;

		/// @brief Performs basic error checking on the object and returns if the object is valid
		/// @param[in] objectPool The object pool to use when validating the object
		/// @returns `true` if the object passed basic error checks
		bool get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool) const override;

		/// @brief Sets an attribute and optionally returns an error code in the last parameter. B.74 allows
		/// no commands on this object, so every attribute is read-only.
		/// @param[in] attributeID The ID of the attribute to change
		/// @param[in] rawAttributeData The raw data to change the attribute to, as decoded in little endian format
		/// @param[in] objectPool The object pool to use when validating the objects affected by setting this attribute
		/// @param[out] returnedError The error code when this function returns false
		/// @returns True if the attribute was changed, otherwise false
		bool set_attribute(std::uint8_t attributeID, std::uint32_t rawAttributeData, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool, AttributeError &returnedError) override;

		/// @brief Gets an attribute and returns the raw data in the last parameter
		/// @param[in] attributeID The ID of the attribute to get
		/// @param[out] returnedAttributeData The raw data of the attribute
		/// @returns True if the attribute was retrieved, otherwise false (the attribute ID was invalid)
		bool get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const override;

		/// @brief Returns the graphic format (Table B.74 AID 1). Always PNG for a pool this VT accepts.
		/// @returns The graphic format
		Format get_format() const;

		/// @brief Sets the graphic format (Table B.74 AID 1). The parser accepts only PNG.
		/// @param[in] value The new graphic format
		void set_format(Format value);

		/// @brief Returns the raw, undecoded graphic bytes (interpreted according to the format).
		/// @returns The raw graphic bytes
		const std::vector<std::uint8_t> &get_raw_data() const;

		/// @brief Replaces the raw graphic bytes with a copy of `length` bytes from `data`.
		/// @param[in] data Pointer to the raw graphic bytes
		/// @param[in] length The number of bytes to copy
		void set_raw_data(const std::uint8_t *data, std::uint32_t length);

	private:
		static constexpr std::uint32_t MIN_OBJECT_LENGTH = 8; ///< Table B.74: id + type + format + 4-byte length, with zero raw bytes
		Format format = Format::PNG; ///< Graphic format (AID 1); only PNG is accepted
		std::vector<std::uint8_t> rawData; ///< The raw, undecoded graphic bytes
	};

	/// @brief The Working Set Special Controls object (ISO 11783-6 Table B.78, VT version 6 and later)
	/// defines the initial Colour Map and Colour Palette for a Working Set and a list of language/country
	/// pairs that supersede the Working Set object's language list.
	/// @details B.29: an object pool contains zero or one of these (a second one faults the pool). The
	/// "Number of bytes to follow" attribute makes the record extensible; attributes that do not exist per
	/// that count take their NULL-equivalent values. All attributes are read-only (Get Attribute Value only).
	class WorkingSetSpecialControls : public VTObject
	{
	public:
		/// @brief Enumerates this object's attributes which are assigned an attribute ID (ISO 11783-6 Table
		/// B.78). All are read-only (bracketed AIDs): the object allows only the Get Attribute Value message.
		enum class AttributeName : std::uint8_t
		{
			Type = 0,
			NumberOfBytesToFollow = 1, ///< read-only (Table B.78 [1])
			ColourMapObjectID = 2, ///< read-only (Table B.78 [2])
			ColourPaletteObjectID = 3, ///< read-only (Table B.78 [3])

			NumberOfAttributes = 4
		};

		/// @brief One language/country pair (Table B.78): a 2-character ISO 639-1 language code paired with a
		/// 2-character ISO 3166-1 country code (or two blanks if not applicable).
		struct LanguagePair
		{
			std::array<char, 2> languageCode; ///< ISO 639-1 language code, 2 characters
			std::array<char, 2> countryCode; ///< ISO 3166-1 country code, 2 characters (or two blanks)
		};

		/// @brief Constructor for a working set special controls object
		WorkingSetSpecialControls() = default;

		/// @brief Virtual destructor for a working set special controls object
		~WorkingSetSpecialControls() override = default;

		/// @brief Returns the VT object type of the underlying derived object
		/// @returns The VT object type of the underlying derived object
		VirtualTerminalObjectType get_object_type() const override;

		/// @brief Returns the minimum binary serialized length of the associated object
		/// @returns The minimum binary serialized length of the associated object
		std::uint32_t get_minumum_object_length() const override;

		/// @brief Performs basic error checking on the object and returns if the object is valid
		/// @param[in] objectPool The object pool to use when validating the object
		/// @returns `true` if the object passed basic error checks
		bool get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool) const override;

		/// @brief Sets an attribute and optionally returns an error code in the last parameter. B.29 allows
		/// only Get Attribute Value, so every attribute is read-only.
		/// @param[in] attributeID The ID of the attribute to change
		/// @param[in] rawAttributeData The raw data to change the attribute to, as decoded in little endian format
		/// @param[in] objectPool The object pool to use when validating the objects affected by setting this attribute
		/// @param[out] returnedError The error code when this function returns false
		/// @returns True if the attribute was changed, otherwise false
		bool set_attribute(std::uint8_t attributeID, std::uint32_t rawAttributeData, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool, AttributeError &returnedError) override;

		/// @brief Gets an attribute and returns the raw data in the last parameter
		/// @param[in] attributeID The ID of the attribute to get
		/// @param[out] returnedAttributeData The raw data of the attribute
		/// @returns True if the attribute was retrieved, otherwise false (the attribute ID was invalid)
		bool get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const override;

		/// @brief Returns the "number of bytes to follow" count that bounds this object's body (Table B.78 AID 1)
		/// @returns The number of body bytes that follow the count attribute
		std::uint16_t get_number_of_bytes_to_follow() const;

		/// @brief Sets the "number of bytes to follow" count (Table B.78 AID 1)
		/// @param[in] value The new count
		void set_number_of_bytes_to_follow(std::uint16_t value);

		/// @brief Returns the object ID of the initial Colour Map object, or NULL_OBJECT_ID for none (Table B.78 AID 2)
		/// @returns The Colour Map object ID, or NULL_OBJECT_ID
		std::uint16_t get_colour_map_object_id() const;

		/// @brief Sets the object ID of the initial Colour Map object (Table B.78 AID 2)
		/// @param[in] value The new Colour Map object ID, or NULL_OBJECT_ID
		void set_colour_map_object_id(std::uint16_t value);

		/// @brief Returns the object ID of the initial Colour Palette object, or NULL_OBJECT_ID for the VT standard palette (Table B.78 AID 3)
		/// @returns The Colour Palette object ID, or NULL_OBJECT_ID
		std::uint16_t get_colour_palette_object_id() const;

		/// @brief Sets the object ID of the initial Colour Palette object (Table B.78 AID 3)
		/// @param[in] value The new Colour Palette object ID, or NULL_OBJECT_ID
		void set_colour_palette_object_id(std::uint16_t value);

		/// @brief Appends one language/country pair to the list that supersedes the Working Set object's languages.
		/// @param[in] languageHigh The first language-code character
		/// @param[in] languageLow The second language-code character
		/// @param[in] countryHigh The first country-code character
		/// @param[in] countryLow The second country-code character
		void add_language_pair(std::uint8_t languageHigh, std::uint8_t languageLow, std::uint8_t countryHigh, std::uint8_t countryLow);

		/// @brief Returns the number of language/country pairs in this object
		/// @returns The number of language pairs
		std::uint8_t get_number_of_language_pairs() const;

		/// @brief Returns one language/country pair by index, or a pair of blanks if the index is out of range
		/// @param[in] index The pair index
		/// @returns The language/country pair at that index
		LanguagePair get_language_pair(std::uint8_t index) const;

	private:
		static constexpr std::uint32_t MIN_OBJECT_LENGTH = 10; ///< Table B.78: id + type + count + colour map + colour palette + language count
		std::uint16_t numberOfBytesToFollow = 5; ///< Table B.78 AID 1: the body length (min 5)
		std::uint16_t colourMapObjectID = NULL_OBJECT_ID; ///< Initial Colour Map object ID or NULL (AID 2)
		std::uint16_t colourPaletteObjectID = NULL_OBJECT_ID; ///< Initial Colour Palette object ID or NULL (AID 3)
		std::vector<LanguagePair> languagePairs; ///< Language/country pairs that supersede the Working Set object's list
	};

	/// @brief The Scaled Graphic object (ISO 11783-6 Table B.76, VT version 6 and later) displays a
	/// scaled representation of a referenced graphic object (a Graphic Data or Picture Graphic object, or
	/// an Object Pointer to one, or NULL).
	/// @details B.28: the VT scales the referenced graphic from its actual size to the target width and
	/// height, honouring a scale mode and horizontal/vertical justification. The attributes are parsed and
	/// stored here; the scaling render is a later phase. Change Numeric Value retargets the Value attribute.
	class ScaledGraphic : public VTObject
	{
	public:
		/// @brief Enumerates this object's attributes which are assigned an attribute ID (ISO 11783-6 Table B.76).
		enum class AttributeName : std::uint8_t
		{
			Type = 0,
			Width = 1,
			Height = 2,
			ScaleType = 3,
			Options = 4,
			Value = 5,

			NumberOfAttributes = 6
		};

		/// @brief Constructor for a scaled graphic object
		ScaledGraphic() = default;

		/// @brief Virtual destructor for a scaled graphic object
		~ScaledGraphic() override = default;

		/// @brief Returns the VT object type of the underlying derived object
		/// @returns The VT object type of the underlying derived object
		VirtualTerminalObjectType get_object_type() const override;

		/// @brief Returns the minimum binary serialized length of the associated object
		/// @returns The minimum binary serialized length of the associated object
		std::uint32_t get_minumum_object_length() const override;

		/// @brief Performs basic error checking on the object and returns if the object is valid
		/// @param[in] objectPool The object pool to use when validating the object
		/// @returns `true` if the object passed basic error checks
		bool get_is_valid(const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool) const override;

		/// @brief Sets an attribute and optionally returns an error code in the last parameter
		/// @param[in] attributeID The ID of the attribute to change
		/// @param[in] rawAttributeData The raw data to change the attribute to, as decoded in little endian format
		/// @param[in] objectPool The object pool to use when validating the objects affected by setting this attribute
		/// @param[out] returnedError The error code when this function returns false
		/// @returns True if the attribute was changed, otherwise false
		bool set_attribute(std::uint8_t attributeID, std::uint32_t rawAttributeData, const std::map<std::uint16_t, std::shared_ptr<VTObject>> &objectPool, AttributeError &returnedError) override;

		/// @brief Gets an attribute and returns the raw data in the last parameter
		/// @param[in] attributeID The ID of the attribute to get
		/// @param[out] returnedAttributeData The raw data of the attribute
		/// @returns True if the attribute was retrieved, otherwise false (the attribute ID was invalid)
		bool get_attribute(std::uint8_t attributeID, std::uint32_t &returnedAttributeData) const override;

		/// @brief Returns the ScaleType byte (Table B.76 AID 3): bits 0-2 scale mode, bits 3-4 horizontal justification, bits 5-6 vertical justification
		/// @returns The ScaleType byte
		std::uint8_t get_scale_type() const;

		/// @brief Sets the ScaleType byte (Table B.76 AID 3)
		/// @param[in] value The new ScaleType byte
		void set_scale_type(std::uint8_t value);

		/// @brief Returns the options bitfield (Table B.76 AID 4; bit 0 flashing)
		/// @returns The options bitfield
		std::uint8_t get_options() const;

		/// @brief Sets the options bitfield (Table B.76 AID 4)
		/// @param[in] value The new options bitfield
		void set_options(std::uint8_t value);

		/// @brief Returns the object ID of the referenced graphic object, or NULL_OBJECT_ID (Table B.76 AID 5)
		/// @returns The referenced graphic object ID, or NULL_OBJECT_ID
		std::uint16_t get_value() const;

		/// @brief Sets the object ID of the referenced graphic object (Table B.76 AID 5)
		/// @param[in] value The new referenced graphic object ID, or NULL_OBJECT_ID
		void set_value(std::uint16_t value);

	private:
		static constexpr std::uint32_t MIN_OBJECT_LENGTH = 12; ///< Table B.76: id + type + width + height + scale type + options + value + macro count
		std::uint8_t scaleTypeByte = 0; ///< ScaleType (AID 3): scale mode + H/V justification
		std::uint8_t optionsBitfield = 0; ///< Options (AID 4); bit 0 flashing
		std::uint16_t value = NULL_OBJECT_ID; ///< Referenced graphic object ID or NULL (AID 5)
	};

	/// @brief Writes one pixel into a Picture Graphic's decoded raster.
	/// @details The raster is one colour-table index per pixel, row-major, actualWidth*actualHeight bytes
	/// (every stored format is expanded to one byte per pixel at parse). This is the runtime raster-mutation
	/// seam the Graphics Context Copy-to-Picture-Graphic sub-commands need (ISO 11783-6 F.56 sub-commands 19
	/// and 20 write the canvas / viewport into a Picture Graphic). A coordinate outside the picture's actual
	/// dimensions, or one past the end of a raster shorter than its declared dimensions, is ignored. Kept a
	/// free function in this isovt-owned TU rather than a PictureGraphic member (ADR-0008): it adds no line to
	/// the upstream object files.
	/// @param[in,out] picture The Picture Graphic whose raster is written
	/// @param[in] x The pixel X coordinate
	/// @param[in] y The pixel Y coordinate
	/// @param[in] colourIndex The colour-table index to store
	void picture_graphic_set_pixel(PictureGraphic &picture, std::int32_t x, std::int32_t y, std::uint8_t colourIndex);
} // namespace isobus

#endif // ISOBUS_VIRTUAL_TERMINAL_OBJECTS_ISOVT_HPP
