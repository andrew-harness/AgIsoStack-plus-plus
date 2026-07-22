//================================================================================================
/// @file isobus_virtual_terminal_objects_isovt.hpp
///
/// @brief Declares the fork's added VT object pool object classes: the Animation object (ISO
/// 11783-6 Table B.72) and the Object Label Reference List object (Table B.64).
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
