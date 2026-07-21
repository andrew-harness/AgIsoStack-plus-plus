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
} // namespace isobus

#endif // ISOBUS_VIRTUAL_TERMINAL_OBJECTS_ISOVT_HPP
