//================================================================================================
/// @file isobus_virtual_terminal_server.hpp
///
/// @brief An abstract VT server.
/// @author Adrian Del Grosso
///
/// @copyright 2023 Adrian Del Grosso
//================================================================================================
#ifndef ISOBUS_VIRTUAL_TERMINAL_SERVER_HPP
#define ISOBUS_VIRTUAL_TERMINAL_SERVER_HPP

#include "isobus/isobus/can_callbacks.hpp"
#include "isobus/isobus/can_constants.hpp"
#include "isobus/isobus/can_internal_control_function.hpp"
#include "isobus/isobus/isobus_language_command_interface.hpp"
#include "isobus/isobus/isobus_virtual_terminal_base.hpp"
#include "isobus/isobus/isobus_virtual_terminal_server_managed_working_set.hpp"
#include "isobus/utility/event_dispatcher.hpp"

#include <array>
#include <deque>

namespace isobus
{
	/// @brief This class is an abstract VT server interface.
	/// @details The VT is a control function that provides a way for operators to interact with other
	/// control functions via a GUI. A VT has a pixel-addressable (graphical) display.
	/// The information that is shown in display areas are defined by Data Masks, Alarm Masks and Soft Key Masks.
	/// The data for these masks is contained in object definitions that are loaded into a VT via the ISO 11783 CAN bus, or from non-volatile memory.
	/// See ISO 11783-6 for the complete definition of this interface, and the objects involved.
	class VirtualTerminalServer : public VirtualTerminalBase
	{
	public:
		/// @brief Constructor for a VirtualTerminalServer
		/// @param[in] controlFunctionToUse The internal control function to use when sending messages to VT clients
		VirtualTerminalServer(std::shared_ptr<InternalControlFunction> controlFunctionToUse);

		/// @brief Destructor for the VirtualTerminalServer
		~VirtualTerminalServer();

		/// @brief Initializes the interface, which registers it with the network manager.
		void initialize();

		/// @brief Returns if the interface has been initialized yet.
		/// @returns true if initialize has been called on this object, otherwise false
		bool get_initialized() const;

		/// @brief Returns the internal control function used by the VT server
		/// @returns The internal control function used by the VT server
		std::shared_ptr<InternalControlFunction> get_internal_control_function() const;

		/// @brief Returns a pointer to the currently active working set
		/// @returns Pointer to the currently active working set, or nullptr if none is active
		std::shared_ptr<VirtualTerminalServerManagedWorkingSet> get_active_working_set() const;

		/// @brief Records the operator's choice of active working set, per ISO 11783-6 clause 4.6.8's
		/// requirement that "the VT shall provide some means to allow the operator to select the Working
		/// Set that is to be active".
		/// @details The choice ranks BELOW a raised Alarm Mask: clause 4.6.14 keeps the highest priority
		/// alarm displayed until its owner changes the active mask, so this decides who is active among
		/// the working sets that are not showing an alarm. It is held as a weak reference and outlives an
		/// alarm that temporarily takes the screen, so the display returns to the operator's choice when
		/// that alarm clears rather than to the working set that last happened to show a Data Mask. A
		/// choice whose working set is torn down expires on its own and the remaining fallbacks take over.
		/// Call on the CAN thread, like the rest of the server's state.
		/// @param[in] workingSet The working set the operator chose, or nullptr to clear the choice
		void set_operator_selected_working_set(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> workingSet);

		/// @brief One managed working set's standing in the contest for the display, as the pure ranking
		/// rule below reads it. Every field is a property of that working set at the moment the
		/// arbitration runs; nothing here needs an object tree or a control function.
		struct ActiveWorkingSetCandidate
		{
			std::uint32_t alarmActivationSequence = 0; ///< When this working set's Alarm Mask was raised, lower being earlier; meaningless unless hasAlarmRaised
			std::uint8_t alarmPriority = 0; ///< The raised Alarm Mask's priority attribute, which encodes High as 0, so a LOWER value is a HIGHER priority; meaningless unless hasAlarmRaised
			bool hasAlarmRaised = false; ///< Whether this working set's active mask is an Alarm Mask
			bool isOperatorSelection = false; ///< Whether the operator selected this working set (clause 4.6.8)
			bool isLastDataMaskHolder = false; ///< Whether this working set most recently had a Data Mask displayed
			bool isCurrentlyActive = false; ///< Whether this working set currently owns the display
		};

		/// @brief The pure ranking rule behind select_active_working_set: does `candidate` deserve the
		/// display more than `incumbent` does?
		/// @details Candidates are ordered in tiers, and only within the alarm tier does anything else
		/// matter. A raised alarm outranks everything, because ISO 11783-6 clause 4.6.14 keeps the
		/// highest priority alarm displayed "until the owner Working Set changes the active mask" -- so
		/// neither an operator selection (4.6.8) nor any fallback may suppress one. Among alarms, 4.6.14
		/// ranks first by the priority attribute (numerically lower is higher priority) and second by
		/// chronological order of activation. Below the alarm tier comes the operator's selection, which
		/// 4.6.8 requires the VT to honour; then the working set that last had a Data Mask displayed,
		/// which is where Table 4's "alarm to data" transition returns the screen; then the incumbent;
		/// then anything eligible at all, which is what activates the first client to connect. Ties
		/// outside the alarm tier are refused, so the earliest working set in the caller's iteration
		/// order keeps the display.
		/// @param[in] candidate The working set being considered
		/// @param[in] incumbent The best working set found so far
		/// @returns true if `candidate` should displace `incumbent`, otherwise false
		static bool active_working_set_candidate_outranks(const ActiveWorkingSetCandidate &candidate,
		                                                  const ActiveWorkingSetCandidate &incumbent);

		/// @brief The Button Activation message allows the VT to transmit operator selection of a Button object to the Working
		/// Set Master
		/// @param[in] activationCode 0 for released, 1 for "pressed", 2 for "still held", or 3 for "aborted"
		/// @param[in] objectId Object ID of Button object
		/// @param[in] parentObjectId Object ID of parent Data Mask or in the case where the Button is in a visible Window Mask object, the Object ID of the Window Mask object
		/// @param[in] keyNumber Button key code (see ISO11783-6)
		/// @param[in] destination The VT client to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_button_activation_message(KeyActivationCode activationCode, std::uint16_t objectId, std::uint16_t parentObjectId, std::uint8_t keyNumber, std::shared_ptr<ControlFunction> destination) const;

		/// @brief The Pointing Event message allows the VT to transmit an operator touch, click or drag of a position in the
		/// Data Mask area to the Working Set Master, when the VT has a touch screen or a pointing device.
		/// @details This message is not used when a Button or an input object is touched or clicked on; the Button Activation
		/// message or the VT Select Input Object message is sent in that case. Annex H.6 asks for the message on press and on
		/// release, and every 200 ms while the position is held.
		/// @param[in] xPosition X position in pixels relative to the top left corner of the Data Mask area
		/// @param[in] yPosition Y position in pixels relative to the top left corner of the Data Mask area
		/// @param[in] touchState 0 for released, 1 for pressed, 2 for held in VT version 4 and later; 0xFF in VT version 3 and
		/// prior, where the byte is reserved and a pressed event is implied
		/// @param[in] destination The VT client to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_pointing_event_message(std::uint16_t xPosition, std::uint16_t yPosition, std::uint8_t touchState, std::shared_ptr<ControlFunction> destination) const;

		/// @brief The VT ESC message tells the Working Set Master that operator input on an object was aborted.
		/// @details ISO 11783-6 Annex H.10: the VT sends this message any time the operator presses the ESC
		/// means, and when the VT closes an open input field because of a Change Active Mask command. Byte 1 is
		/// the VT ESC function, bytes 2-3 the Object ID where input was aborted (used only when no error code is
		/// set), byte 4 the error bitfield (bit 0 "No input field is selected", used only when the VT has a
		/// permanent ESC means; bits 1-3 undefined set to 0; bit 4 any other error), and bytes 5-8 reserved
		/// 0xFF. It is an operator input event the VT originates, not a response to a command, so like the
		/// Pointing Event it bypasses the macro-response suppression choke point.
		/// @param[in] abortedObjectId The Object ID where input was aborted, or NULL_OBJECT_ID when an error code is set
		/// @param[in] errorBitfield The Annex H.10 byte 4 error bitfield (0 when input was aborted with no error)
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_vt_esc_message(std::uint16_t abortedObjectId, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends the VT Change Numeric Value message
		/// @details The VT sends this message any time the operator enters a numeric value for an input object or variable,
		/// regardless of whether or not the value changed.This message is not sent if the input was aborted(in this case a VT ESC message would be sent instead).For input objects that have a numeric variable reference,
		/// the Object ID of the numeric variable object is used in this message.
		/// @param[in] objectId The object ID of the affected object
		/// @param[in] value The value that the referenced object was changed to
		/// @param[in] destination The control function to send the message to
		/// @returns True if the message was sent, otherwise false
		bool send_change_numeric_value_message(std::uint16_t objectId, std::uint32_t value, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends the VT Select Input Object message
		/// @details This message is sent by the VT any time an input field, Button, or Key object is selected (gets focus),
		/// deselected (loses focus), opened for edit or closed after edit by the operator or an ESC command.
		/// @param[in] objectId The object ID of the affected object
		/// @param[in] isObjectSelected True of the object has focus, false if the object has been deselected
		/// @param[in] isObjectOpenForInput True if the object is open for data input (only is object is selected!), otherwise false
		/// @param[in] destination The control function to send the message to
		/// @returns True if the message was sent, otherwise false
		bool send_select_input_object_message(std::uint16_t objectId, bool isObjectSelected, bool isObjectOpenForInput, std::shared_ptr<ControlFunction> destination) const;

		/// @brief The Button Activation message allows the VT to transmit operator selection of a key object to the Working
		/// Set Master
		/// @param[in] activationCode 0 for released, 1 for "pressed", 2 for "still held", or 3 for "aborted"
		/// @param[in] objectId Object ID of Button object
		/// @param[in] parentObjectId Object ID of parent Data Mask or in the case where the Button is in a visible Window Mask object, the Object ID of the Window Mask object
		/// @param[in] keyNumber Button key code (see ISO11783-6)
		/// @param[in] destination The VT client to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_soft_key_activation_message(KeyActivationCode activationCode, std::uint16_t objectId, std::uint16_t parentObjectId, std::uint8_t keyNumber, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends the VT Change String Value Message.
		/// The VT uses this message to transfer a string entered into an Input String object or referenced String
		/// Variable object.
		/// @param[in] objectId The object ID that was altered, either for a string variable or an input string
		/// @param[in] value The entered string
		/// @param[in] destination The VT client to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_string_value_message(std::uint16_t objectId, const std::string &value, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a load version command
		/// The reason this is exposed is because you will need to send this message after
		/// the object pool processing thread completes at some point to tell the client to proceed if their
		/// object pool was loaded via a load version command.
		/// @param[in] errorCodes A set of error bits to report to the client. These will be reported from the managed working set's parsing results.
		/// @param[in] destination The VT client to send the message to
		/// @returns True if the message was sent, otherwise false
		bool send_load_version_response(std::uint8_t errorCodes, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to an extended load version command (command byte 0xD5)
		/// This is the extended-label counterpart to send_load_version_response, sent after the object
		/// pool processing thread completes for a pool loaded via an Extended Load Version command. Such
		/// a client waits on the extended (0xD5) response rather than the standard (0xD1) one.
		/// @param[in] errorCodes A set of error bits to report to the client. These will be reported from the managed working set's parsing results.
		/// @param[in] destination The VT client to send the message to
		/// @returns True if the message was sent, otherwise false
		bool send_extended_load_version_response(std::uint8_t errorCodes, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Conditionally executes a macro. If the object passed in is of the specified type, and
		/// a macro is defined for that object, the macro will be executed if the macro event matches the
		/// event ID of the macro.
		/// @param[in] object The object to check for a macro (or macros) to execute
		/// @param[in] macroEvent The event ID of the macro(s) to execute
		/// @param[in] targetObjectType The type of object that the macro is defined for. Used to validate the object
		/// @param[in] workingset The working set to execute the macro on
		void process_macro(std::shared_ptr<isobus::VTObject> object, isobus::EventID macroEvent, isobus::VirtualTerminalObjectType targetObjectType, std::shared_ptr<isobus::VirtualTerminalServerManagedWorkingSet> workingset);

		// ----------- Mandatory Functions you must implement -----------------------

		/// @brief This function is called when the client wants to know if the server has enough memory to store the object pool.
		/// You should return true if the server has enough memory to store the object pool, otherwise false.
		/// @param[in] requestedMemory The amount of memory requested by the client
		/// @returns True if the server has enough memory to store the object pool, otherwise false
		virtual bool get_is_enough_memory(std::uint32_t requestedMemory) const = 0;

		/// @brief This function is called when the client wants to know the version of the VT.
		/// @returns The version of the VT
		virtual VTVersion get_version() const = 0;

		/// @brief This function is called when the interface wants to know the number of navigation soft keys.
		/// @returns The number of navigation soft keys
		virtual std::uint8_t get_number_of_navigation_soft_keys() const = 0;

		/// @brief This function is called when the interface needs to know the number of x pixels (width) of your soft keys
		/// @returns The number of x pixels (width) of your soft keys
		virtual std::uint8_t get_soft_key_descriptor_x_pixel_width() const = 0;

		/// @brief This function is called when the interface needs to know the number of y pixels (height) of your soft keys
		/// @returns The number of y pixels (height) of your soft keys
		virtual std::uint8_t get_soft_key_descriptor_y_pixel_height() const = 0;

		/// @brief This function is called when the interface needs to know the number of possible virtual soft keys in your soft key mask render area
		/// @returns The number of possible virtual soft keys in your soft key mask render area
		virtual std::uint8_t get_number_of_possible_virtual_soft_keys_in_soft_key_mask() const = 0;

		/// @brief This function is called when the interface needs to know the number of physical soft keys
		/// @returns The number of physical soft keys
		virtual std::uint8_t get_number_of_physical_soft_keys() const = 0;

		/// @brief This function is called when the interface needs to know the number of x pixels (width) of your data key mask render area
		/// @returns The number of x pixels (width) of your soft key mask render area
		virtual std::uint16_t get_data_mask_area_size_x_pixels() const = 0;

		/// @brief This function is called when the interface needs to know the number of y pixels (height) of your data key mask render area
		/// @returns The number of y pixels (height) of your data key mask render area
		virtual std::uint16_t get_data_mask_area_size_y_pixels() const = 0;

		/// @brief The interface calls this function when it wants you to discontinue/suspend a working set
		/// @param[in] workingSetWithError The working set to suspend
		virtual void suspend_working_set(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> workingSetWithError) = 0;

		/// @brief This function is called when the interface needs to know the wide chars you support
		/// @param[in] codePlane The code plane to inquire about
		/// @param[in] firstWideCharInInquiryRange The first wide char in the inquiry range
		/// @param[in] lastWideCharInInquiryRange The last wide char in the inquiry range
		/// @param[out] numberOfRanges The number of wide char ranges supported
		/// @param[out] wideCharRangeArray The wide char range array
		/// @returns The error code for the supported wide chars inquiry
		virtual SupportedWideCharsErrorCode get_supported_wide_chars(std::uint8_t codePlane,
		                                                             std::uint16_t firstWideCharInInquiryRange,
		                                                             std::uint16_t lastWideCharInInquiryRange,
		                                                             std::uint8_t &numberOfRanges,
		                                                             std::vector<std::uint8_t> &wideCharRangeArray) = 0;

		/// @brief This function is called when the interface needs to know what versions of object pools are available for a client.
		/// @param[in] clientNAME The client requesting the object pool versions
		/// @returns A vector of object pool versions available for the client
		virtual std::vector<std::array<std::uint8_t, 7>> get_versions(NAME clientNAME) = 0;

		/// @brief This function is called when the interface needs to know what extended (32-byte) versions of object
		/// pools are available for a client, in response to the Extended Get Versions message.
		/// @note Unlike get_versions this is not pure virtual; the default returns empty so servers that do not persist
		/// extended versions continue to compile and simply report no stored extended versions.
		/// @param[in] clientNAME The client requesting the extended object pool versions
		/// @returns A vector of 32-byte extended object pool version labels stored for the client, empty if none
		virtual std::vector<std::array<std::uint8_t, 32>> get_extended_versions(NAME clientNAME);

		/// @brief This function is called when the interface needs to know what objects are supported by the server.
		/// @returns A vector of supported objects
		virtual std::vector<std::uint8_t> get_supported_objects() const = 0;

		/// @brief This function is called when the client wants the server to load a previously stored object pool.
		/// If there exists in the VT's non-volatile memory an object pool matching the provided version label,
		/// return it. If one does not exist, return an empty vector.
		/// @param[in] versionLabel The object pool version to load for the given client NAME
		/// @param[in] clientNAME The client requesting the object pool
		/// @returns The requested object pool associated with the version label.
		virtual std::vector<std::uint8_t> load_version(const std::vector<std::uint8_t> &versionLabel, NAME clientNAME) = 0;

		/// @brief This function is called when the client wants the server to save an object pool
		/// to the VT's non-volatile memory.
		/// If the object pool is saved successfully, return true, otherwise return false.
		/// @note This may be called multiple times with the same version, but different data. When this
		/// happens, the expectation is that you will append each objectPool together into one large file.
		/// @param[in] objectPool The object pool data to save
		/// @param[in] versionLabel The object pool version to save for the given client NAME
		/// @param[in] clientNAME The client requesting the object pool
		/// @returns The requested object pool associated with the version label.
		virtual bool save_version(const std::vector<std::uint8_t> &objectPool, const std::vector<std::uint8_t> &versionLabel, NAME clientNAME) = 0;

		/// @brief This function is called when the client wants the server to delete a stored object pool.
		/// All object pool files matching the specified version label should then be deleted from the VT's
		/// non-volatile storage.
		/// @param[in] versionLabel The version label for the object pool(s) to delete
		/// @param[in] clientNAME The NAME of the client that is requesting deletion
		/// @returns True if the version was deleted from VT non-volatile storage, otherwise false.
		virtual bool delete_version(const std::vector<std::uint8_t> &versionLabel, NAME clientNAME) = 0;

		/// @brief This function is called when the client wants the server to delete ALL stored object pools associated to it's NAME.
		/// All object pool files matching the specified client NAME should then be deleted from the VT's
		/// non-volatile storage.
		/// @param[in] clientNAME The NAME of the client that is requesting deletion
		/// @returns True if all relevant object pools were deleted from VT non-volatile storage, otherwise false.
		virtual bool delete_all_versions(NAME clientNAME) = 0;

		/// @brief This function is called when the client wants the server to deactivate its object pool.
		/// You should treat this as a disconnection by the client, as it may be moving to another VT.
		/// @attention This does not mean to delete the pool from non-volatile memory!!! This only deactivates the active pool.
		/// @details This command is used to delete the entire object pool of this Working Set from volatile storage.
		/// This command can be used by an implement when it wants to move its object pool to another VT,
		/// or when it is shutting down or during the development of object pools.
		/// @param[in] clientNAME The NAME of the client that is requesting deletion
		/// @returns True if the client's active object pool was deactivated and removed from volatile storage, otherwise false.
		virtual bool delete_object_pool(NAME clientNAME) = 0;

		//------------ Optional functions you can override --------------------

		/// @brief If you want to override the graphics mode from its default 256 color mode, you can override this function.
		/// Though, that would be unusual.
		/// @returns The graphic mode of the VT to report to clients
		virtual VirtualTerminalBase::GraphicMode get_graphic_mode() const;

		/// @brief If you want to override the amount of time the VT reports it takes to power up, you can override this function.
		/// @returns The amount of time the VT reports it takes to power up, or 255 if it is not known
		virtual std::uint8_t get_powerup_time() const;

		/// @brief By default, the VT server will report that it supports all small and large fonts.
		/// If you want to override this, you can override this function.
		/// @returns The bitfield of supported small fonts
		virtual std::uint8_t get_supported_small_fonts_bitfield() const;

		/// @brief By default, the VT server will report that it supports all small and large fonts.
		/// If you want to override this, you can override this function.
		/// @returns The bitfield of supported large fonts
		virtual std::uint8_t get_supported_large_fonts_bitfield() const;

		/// @brief This function is called when the Identify VT version message is received
		virtual void identify_vt();

		/// @brief This function is called when the Screen capture command is received
		/// @param[in] item Item requested from the Screen Capture command
		/// @param[in] path Path requested from the Screen Capture command
		/// @param[in] requestor The control function requesting screen capture
		virtual void screen_capture(std::uint8_t item, std::uint8_t path, std::shared_ptr<ControlFunction> requestor);

		/// @brief This function returns the Background colour of VT’s User-Layout Data Masks
		/// Used in the Get Window Mask Data response
		/// @returns The background color on the datamasks
		virtual std::uint8_t get_user_layout_datamask_bg_color() const;

		/// @brief This function returns the Background colour of VT’s Key-Cells when on a User-Layout softkey mask
		/// Used in the Get Window Mask Data response
		/// @returns The background color on the softkey mask
		virtual std::uint8_t get_user_layout_softkeymask_bg_color() const;

		/// @brief Callback function which is called before the transferred IOP data parsing is started
		/// Useful to save IOP data for debugging purposes in the case if the parsing would lead to a crash
		/// @param[in] ws the working set which object pool processing is about to be started
		virtual void transferred_object_pool_parse_start(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> &ws) const;

		//-------------- Callbacks/Event driven interface ---------------------

		/// @brief Returns the event dispatcher for repaint events
		/// @returns The event dispatcher for repaint events
		EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>> &get_on_repaint_event_dispatcher();

		/// @brief Returns the event dispatcher for change active data/alarm mask events
		/// @returns The event dispatcher for change active data/alarm mask events
		EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, std::uint16_t, std::uint16_t> &get_on_change_active_mask_event_dispatcher();

		/// @brief Returns the event dispatcher for change active softkey mask events
		/// @returns The event dispatcher for change active softkey mask events
		EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, std::uint16_t, std::uint16_t> &get_on_change_active_softkey_mask_event_dispatcher();

		/// @brief Returns the event dispatcher for when an object is focused
		/// @returns The event dispatcher for when an object is focused
		EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, std::uint16_t, bool> &get_on_focus_object_event_dispatcher();

		/// @brief Returns the event dispatcher raised each time an Alarm Mask becomes the mask the VT
		/// displays, carrying the working set that owns it and that Alarm Mask's object ID.
		/// ISO 11783-6 clause 4.6.14 c) ties the acoustic signal to a mask change that causes an Alarm
		/// Mask to appear or reappear, which covers a working set activating with an Alarm Mask as its
		/// initial mask, a runtime Change Active Mask, and a priority arbitration that hands the screen
		/// to another working set's alarm. An Alarm Mask that simply stays displayed does not raise it.
		/// @returns The event dispatcher for an Alarm Mask becoming the displayed mask
		EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, std::uint16_t> &get_on_alarm_mask_displayed_event_dispatcher();

		/// @brief Why a working set was torn down, carried by the working-set-lost event so the display
		/// layer can tell the operator which condition occurred.
		enum class WorkingSetLossReason : std::uint8_t
		{
			MaintenanceTimeout = 0, ///< ISO 11783-6 clause 4.6.9: no Working Set Maintenance message for over 3 s while its object pool was present
			InvalidObjectPoolUpdate = 1 ///< ISO 11783-6 clause C.2.6: a runtime object pool update failed to parse, so the entire pool is deleted and the working set suspended
		};

		/// @brief Returns the event dispatcher raised when a working set is torn down, carrying the lost
		/// working set and the reason it was lost. ISO 11783-6 clause 4.6.9 requires the VT to "alert the
		/// operator to this condition" on an unexpected working-set shutdown, by a means "proprietary to
		/// the VT"; this event is the seam the display layer registers to surface that alert (and the
		/// equivalent C.2.6 suspension). It is raised from the loss-teardown pass in update(), on the CAN
		/// thread, while the working set is still valid and before it is erased.
		/// @returns The event dispatcher for a working set being torn down
		EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, WorkingSetLossReason> &get_on_working_set_lost_event_dispatcher();

		//----------------- Other Server Settings -----------------------------

		/// @brief Returns the language command interface for the server, which
		/// can be used to inform clients of the current unit systems, language, and country code
		/// @returns The language command interface for the server
		LanguageCommandInterface &get_language_command_interface();

	protected:
		/// @brief Enumerates the bit indices of the error fields that can be set in a change active mask response
		enum class ChangeActiveMaskErrorBit : std::uint8_t
		{
			InvalidWorkingSetObjectID = 0,
			InvalidMaskObjectID = 1,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a select colour map response
		enum class SelectColourMapErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			InvalidColourMap = 1,
			AnyOtherError = 2
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change background colour response
		enum class ChangeBackgroundColourErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			InvalidColourCode = 1,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change child location/position value response
		enum class ChangeChildLocationorPositionErrorBit : std::uint8_t
		{
			ParentObjectDoesntExistOrIsNotAParentOfSpecifiedObject = 0,
			TargetObjectDoesNotExistOrIsNotApplicable = 1,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change fill attributes response
		enum class ChangeFillAttributesErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			InvalidType = 1,
			InvalidColour = 2,
			InvalidPatternObjectID = 3,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change font attributes response
		enum class ChangeFontAttributesErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			InvalidColour = 1,
			InvalidSize = 2,
			InvalidType = 3,
			InvalidStyle = 4,
			AnyOtherError = 5
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change list item response
		enum class ChangeListItemErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			InvalidListIndex = 1,
			InvalidNewListItemObjectID = 2,
			ValueInUse = 3, ///< Value in use (e.g. open for input); ISO 11783-6:2014 F.43, VT version 4 and later
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change numeric value response
		enum class ChangeNumericValueErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			InvalidValue = 1,
			ValueInUse = 2, // such as: open for input
			Undefined = 3,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change priority response
		enum class ChangePriorityErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			InvalidPriority = 1,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change size response
		enum class ChangeSizeErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change soft key mask response
		enum class ChangeSoftKeyMaskErrorBit : std::uint8_t
		{
			InvalidDataOrAlarmMaskObjectID = 0,
			InvalidSoftKeyMaskObjectID = 1,
			MissingObjects = 2,
			MaskOrChildObjectHasErrors = 3,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change string value response
		enum class ChangeStringValueErrorBit : std::uint8_t
		{
			Undefined = 0, ///< This bit should always be set to zero
			InvalidObjectID = 1,
			StringTooLong = 2,
			AnyOtherError = 3,
			ValueInUse = 4 ///< Value in use (e.g. open for input); ISO 11783-6:2014 F.25, VT version 4 and 5 (deprecated in later editions)
		};

		/// @brief Enumerates the different error bit indices that can be set in a delete version response
		enum class DeleteVersionErrorBit : std::uint8_t
		{
			Reserved = 0,
			VersionLabelNotCorrectOrUnknown = 1,
			AnyOtherError = 3
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a enable/disable object response
		enum class EnableDisableObjectErrorBit : std::uint8_t
		{
			Undefined = 0,
			InvalidObjectID = 1,
			InvalidEnableDisableCommandValue = 2,
			CouldNotCompleteTheInputObjectIsCurrentlyBeingModified = 3,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in an execute macro response
		enum class ExecuteMacroResponseErrorBit : std::uint8_t
		{
			ObjectDoesntExist = 0,
			ObjectIsNotAMacro = 1,
			AnyOtherError = 2
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a get attribute value response
		enum class GetAttributeValueErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			InvalidAttributeID = 1,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a hide/show object response
		enum class HideShowObjectErrorBit : std::uint8_t
		{
			ReferencesToMissingChildObjects = 0,
			InvalidObjectID = 1,
			CommandError = 2,
			Undefined = 3,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a select input object response
		enum class SelectInputObjectErrorBit : std::uint8_t
		{
			ObjectIsDisabled = 0,
			InvalidObjectID = 1,
			ObjectIsNotOnTheActiveMaskOrIsInAHiddenContainer = 2,
			CouldNotCompleteAnotherFieldIsBeingModified = 3,
			AnyOtherError = 4,
			InvalidOptionValue = 5
		};

		/// @brief Enumerates the different responses to a select input object message
		enum class SelectInputObjectResponse : std::uint8_t
		{
			ObjectIsNotSelectedOrIsNullOrError = 0,
			ObjectIsSelected = 1,
			ObjectIsOpenedForEdit = 2 // VT version 4 and later
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change polygon point response
		enum class ChangePolygonPointErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			InvalidPointIndex = 1,
			AnyOtherError = 2
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change end point response
		enum class ChangeEndPointErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			InvalidLineDirection = 1,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change polygon scale response
		enum class ChangePolygonScaleErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in an ESC response
		enum class ESCErrorBit : std::uint8_t
		{
			NoInputFieldIsOpenForInput = 0,
			AnyOtherError = 4
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a lock/unlock mask response
		enum class LockUnlockMaskErrorBit : std::uint8_t
		{
			CommandIgnoredNoMaskVisibleOrObjectIDMismatch = 0,
			LockIgnoredAlreadyLocked = 1,
			UnlockIgnoredNotLocked = 2,
			LockIgnoredAlarmMaskIsActive = 3,
			UnsolicitedUnlockTimeoutOccurred = 4,
			UnsolicitedUnlockMaskIsHidden = 5,
			UnsolicitedUnlockOperatorInduced = 6,
			AnyOtherError = 7
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a change object label response
		enum class ChangeObjectLabelErrorBit : std::uint8_t
		{
			InvalidObjectID = 0,
			InvalidStringVariableObjectID = 1,
			InvalidFontType = 2,
			NoObjectLabelReferenceListInPool = 3,
			DesignatorReferencesInvalidObjects = 4,
			AnyOtherError = 5
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a delete object pool response
		enum class DeleteObjectPoolErrorBit : std::uint8_t
		{
			DeletionError = 0,
			AnyOtherError = 8
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a load version
		/// response and in an extended load version response
		/// @details ISO 11783-6 E.7 and E.15 define byte 6 of both responses identically. Bit 0 exists
		/// in VT version 4 and later.
		enum class LoadVersionErrorBit : std::uint8_t
		{
			FileSystemErrorOrPoolDataCorruption = 0,
			VersionLabelNotCorrectOrUnknown = 1,
			InsufficientMemoryAvailable = 2,
			AnyOtherError = 3
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a store version
		/// response and in an extended store version response
		/// @details ISO 11783-6 E.5 and E.13 define byte 6 of both responses identically. Bit 0 is
		/// reserved, unlike the load version responses where bit 0 is a file system error.
		enum class StoreVersionErrorBit : std::uint8_t
		{
			Reserved = 0,
			VersionLabelNotCorrect = 1,
			InsufficientMemoryAvailable = 2,
			AnyOtherError = 3
		};

		/// @brief Enumerates the possible values of the Screen Capture command Item Requested field
		enum class ScreenCaptureItem
		{
			ScreenImage = 0,
			ManufacturerProprietary_240,
			ManufacturerProprietary_241,
			ManufacturerProprietary_242,
			ManufacturerProprietary_243,
			ManufacturerProprietary_244,
			ManufacturerProprietary_245,
			ManufacturerProprietary_246,
			ManufacturerProprietary_247,
			ManufacturerProprietary_248,
			ManufacturerProprietary_249,
			ManufacturerProprietary_250,
			ManufacturerProprietary_251,
			ManufacturerProprietary_252,
			ManufacturerProprietary_253,
			ManufacturerProprietary_254,
			ManufacturerProprietary_255,
		};

		/// @brief Enumerates the possible values of the Screen Capture command Path field
		enum class ScreenCapturePath
		{
			VT_StorageOrRemovableMedia = 1,
			ManufacturerProprietary_240,
			ManufacturerProprietary_241,
			ManufacturerProprietary_242,
			ManufacturerProprietary_243,
			ManufacturerProprietary_244,
			ManufacturerProprietary_245,
			ManufacturerProprietary_246,
			ManufacturerProprietary_247,
			ManufacturerProprietary_248,
			ManufacturerProprietary_249,
			ManufacturerProprietary_250,
			ManufacturerProprietary_251,
			ManufacturerProprietary_252,
			ManufacturerProprietary_253,
			ManufacturerProprietary_254,
			ManufacturerProprietary_255,
		};

		/// @brief Enumerates the bit indices of the error fields that can be set in a screen capture response
		enum class ScreenCaptureResponseErrorBit : std::uint8_t
		{
			NoError = 0,
			ScreenCaptureNotEnabled = 1,
			TransferBufferBusy = 2,
			UnsupportedItemRequest = 4,
			UnsupportedPathRequest = 8,
			RemovableMediaUnavailable = 16,
			AnyOtherError = 32
		};

		/// @brief One (function -> input) pair decoded from a Preferred Assignment command (J.7.7), tagged with its
		/// auxiliary input unit's NAME and Model Identification Code.
		struct AuxiliaryPreferredAssignmentEntry
		{
			std::uint64_t inputUnitName; ///< The NAME of the auxiliary input unit the pair belongs to
			std::uint16_t modelIdentificationCode; ///< The Model Identification Code of the auxiliary input unit
			std::uint16_t functionObjectId; ///< The object ID of the auxiliary function in our object pool
			std::uint16_t inputObjectId; ///< The object ID of the auxiliary input on the input unit
		};

		/// @brief Checks to see if the message should be listened to based on
		/// what the message is, and if the client has sent the proper working set master message
		/// @param[in] message The CAN message to check
		/// @returns true if the source of the message is in a valid, managed state by our server, otherwise false
		bool check_if_source_is_managed(const CANMessage &message);

		/// @brief Processes a macro's execution synchronously as if it were a CAN message.
		/// Basically, if you want the server to execute a macro as if it were a CAN message, you can call this function
		/// though it will require you to create a CAN message to pass in. If you don't want to use this and
		/// instead want to manually affect the required changes in the object pool, that's fine too.
		/// @param[in] message The macro to execute
		void execute_macro_as_rx_message(const CANMessage &message);

		/// @brief Executes a single macro's command packets synchronously by object ID, replaying each
		/// through the server's own receive path. Called only by drain_macro_execution_queue; a command
		/// that triggers another macro enqueues it, it is not run inline.
		/// @param[in] objectIDOfMacro The object ID of the macro to execute
		/// @param[in] workingSet The working set to execute the macro on
		/// @returns true if the macro was executed, otherwise false
		bool execute_macro(std::uint16_t objectIDOfMacro, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> workingSet);

		/// @brief Executes every macro on the FIFO queue, in trigger order, each to completion before the
		/// next starts, then returns (ISO 11783-6 4.6.11.4 b/c/d). A macro command that triggers another
		/// enqueues it at the back and re-enters this function, which returns immediately because a drain
		/// is already active -- so there is exactly one drain in flight and never more than one level of
		/// nesting. The per-command execution budget is the only bound: a self-referencing macro
		/// re-enqueues itself forever, and the budget stops it.
		void drain_macro_execution_queue();

		/// @brief Returns the priority to use, depending on the VT version
		/// @returns The priority to use, depending on the VT version
		CANIdentifier::CANPriority get_priority() const;

		/// @brief Maps a VTVersion to its corresponding byte representation
		/// @param[in] version The version to get the corresponding byte for
		/// @returns The VT version byte associated to the specified version
		static std::uint8_t get_vt_version_byte(VTVersion version);

		/// @brief Processes a stateless CAN message from any VT client
		/// @param[in] message The CAN message being received
		/// @returns True if answer is being sent to the client, false if the message have not been processed/answered
		bool process_stateless_messages(const CANMessage &message);

		/// @brief Processes a connection-dependent CAN message from only VT clients
		/// with whom we've established a working set master relationship
		/// @param[in] message The CAN message being received
		/// @param[in] managedWorkingSet The working set that is associated to the client sending the message
		/// @returns True if the function code was recognised and handled, false if it is unsupported
		bool process_connection_dependent_messages(const CANMessage &message, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Processes a CAN message from any VT client
		/// @param[in] message The CAN message being received
		/// @param[in] parent A context variable to find the relevant VT server class
		static void process_rx_message(const CANMessage &message, void *parent);

		/// @brief Sends a message using the acknowledgement PGN
		/// @param[in] type The type of acknowledgement to send (Ack, vs Nack, etc)
		/// @param[in] parameterGroupNumber The PGN to acknowledge
		/// @param[in] source The source control function to send from
		/// @param[in] destination The destination control function to send the acknowledgement to
		/// @returns true if the message was sent, false otherwise
		bool send_acknowledgement(AcknowledgementType type, std::uint32_t parameterGroupNumber, std::shared_ptr<InternalControlFunction> source, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a client on the VT to ECU parameter group, unless a macro is executing
		/// @param[in] buffer The message payload
		/// @param[in] length The payload length in bytes
		/// @param[in] destination The control function to send the response to
		/// @returns true if the response was sent or was deliberately withheld, false if the send failed
		bool send_response(const std::uint8_t *buffer, std::uint32_t length, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends the Unsupported VT Function message in response to a VT function this VT does not support
		/// @param[in] unsupportedFunctionCode The function code (received Byte 1) that is not supported
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_unsupported_vt_function(std::uint8_t unsupportedFunctionCode, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change active mask command
		/// @param[in] newMaskObjectID The object ID for the new active mask
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_active_mask_response(std::uint16_t newMaskObjectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change attribute command
		/// @param[in] objectID The object ID for the target object
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] attributeID The attribute ID that was changed
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_attribute_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::uint8_t attributeID, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a get attribute value message (ISO 11783-6 F.59)
		/// @details A no-error response carries the object ID in bytes 2-3 and the attribute value little endian
		/// in bytes 5-8. An error response instead sets bytes 2-3 to 0xFFFF and reports the queried object ID in
		/// bytes 5-6 alongside the error bitfield in byte 7.
		/// @param[in] objectID The object ID that was queried
		/// @param[in] attributeID The attribute ID that was queried
		/// @param[in] value The current value of the attribute, ignored when errorBitfield is non-zero
		/// @param[in] errorBitfield An error bitfield, or zero for a no-error response
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_get_attribute_value_response(std::uint16_t objectID, std::uint8_t attributeID, std::uint32_t value, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a select colour map command
		/// @param[in] objectID The object ID of the Colour Map that was selected, or 0xFFFF for the default palette
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_select_colour_map_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to an Auxiliary Capabilities request (VT function 0x27, ISO 11783-6 J.7.14)
		/// @details Inventories every managed auxiliary unit whose object pool contains objects of the requested
		/// aux kind. For each such unit the response carries the unit's NAME followed by one Set Information record
		/// per distinct (Function attribute, Assigned attribute) pair, with the number of instances of that pair.
		/// @param[in] requestType The request type from the request message: 0 = Auxiliary Input Units, 1 = Auxiliary Function Units
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_auxiliary_capabilities_response(std::uint8_t requestType, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Called when a Preferred Assignment command (0x22, J.7.7) is received from a working set.
		/// @details The base implementation does nothing. A subclass implementing the AUX-N assignment engine
		/// overrides this to act on the requested preferred assignments.
		/// @param[in] functionWorkingSet The working set that sent the command
		/// @param[in] entries The flattened (function -> input) pairs decoded from the command, or an empty list if the command was malformed
		virtual void on_auxiliary_preferred_assignment_received(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> functionWorkingSet, const std::vector<AuxiliaryPreferredAssignmentEntry> &entries);

		/// @brief Called when an Auxiliary Input Type 2 Maintenance message (0x23, J.7.10) is received from a working set.
		/// @details The base implementation does nothing. A subclass overrides this to track auxiliary input unit readiness.
		/// @param[in] inputWorkingSet The working set that sent the message
		/// @param[in] modelIdentificationCode The Model Identification Code reported by the auxiliary input unit
		/// @param[in] ready True if the unit reports it is ready (status 1), false if it is initializing (status 0)
		virtual void on_auxiliary_input_maintenance_received(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> inputWorkingSet, std::uint16_t modelIdentificationCode, bool ready);

		/// @brief Called when an Auxiliary Assignment Type 2 response (0x24, J.7.6) is received from a working set.
		/// @details The base implementation does nothing. A subclass overrides this to process assignment results.
		/// @param[in] functionWorkingSet The working set that sent the response
		/// @param[in] functionObjectId The object ID of the auxiliary function the response refers to
		/// @param[in] errorCode The error codes reported by the responder
		virtual void on_auxiliary_assignment_response_received(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> functionWorkingSet, std::uint16_t functionObjectId, std::uint8_t errorCode);

		/// @brief Called when an Auxiliary Input Status Type 2 Enable response (0x25, J.7.12) is received from a working set.
		/// @details The base implementation does nothing. A subclass overrides this to process the enable/disable result.
		/// @param[in] inputWorkingSet The working set that sent the response
		/// @param[in] inputObjectId The object ID of the auxiliary input the response refers to
		/// @param[in] status The enable state reported by the responder (0 = disabled, 1 = enabled)
		/// @param[in] errorCode The error codes reported by the responder
		virtual void on_auxiliary_input_status_enable_response_received(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> inputWorkingSet, std::uint16_t inputObjectId, std::uint8_t status, std::uint8_t errorCode);

		/// @brief Called when an Auxiliary Input Type 2 Status message (0x26, J.7.9) is received from a working set.
		/// @details The base implementation does nothing. In normal operation this status is broadcast and consumed
		/// by the assigned function working set directly; in learn mode the input unit sends it destination-specific
		/// to the VT. Whichever form reaches this server is surfaced here; a subclass overrides this to observe input
		/// status (e.g. to capture the operator-activated input while learn mode is active).
		/// @param[in] inputWorkingSet The working set that sent the status
		/// @param[in] inputObjectId The object ID of the auxiliary input the status refers to
		/// @param[in] value1 The first value reported by the input (meaning per function type, Table J.5)
		/// @param[in] value2 The second value reported by the input
		/// @param[in] operatingState The operating state byte (bit 0: learn mode active, bit 1: activated in learn mode)
		virtual void on_auxiliary_input_status_received(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> inputWorkingSet, std::uint16_t inputObjectId, std::uint16_t value1, std::uint16_t value2, std::uint8_t operatingState);

		/// @brief Sets whether the auxiliary input learn mode flag is reported in the VT status message.
		/// @details While active, the status message's busy-codes byte carries the auxiliary-input learn mode
		/// bit (0x40), which tells input units to send their Auxiliary Input Type 2 Status messages
		/// destination-specific to this VT with the learn bits set in the operating state byte (J.7.9).
		/// @param[in] active True to report learn mode active, false to report it inactive
		void set_auxiliary_learn_mode_active(bool active);

		/// @brief Sends a Preferred Assignment response (0x22, VT->ECU, J.7.8)
		/// @param[in] errorBits The error bitfield to report
		/// @param[in] faultyFunctionObjectId The object ID of the auxiliary function that caused the fault, or NULL_OBJECT_ID
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_preferred_assignment_response(std::uint8_t errorBits, std::uint16_t faultyFunctionObjectId, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends an Auxiliary Input Status Type 2 Enable command (0x25, VT->ECU, J.7.11)
		/// @param[in] inputObjectId The object ID of the auxiliary input to enable or disable
		/// @param[in] enable True to enable status messages for the input, false to disable them
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_auxiliary_input_status_type_2_enable(std::uint16_t inputObjectId, bool enable, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends an Auxiliary Assignment Type 2 command (0x24, VT->ECU, J.7.5)
		/// @param[in] inputUnitName The NAME of the auxiliary input unit the assignment targets
		/// @param[in] functionType The auxiliary function type (0-14), or 0x1F to remove the assignment
		/// @param[in] inputObjectId The object ID of the auxiliary input to assign
		/// @param[in] functionObjectId The object ID of the auxiliary function to assign
		/// @param[in] storeAsPreferred True to request the assignment be stored as a preferred assignment
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_auxiliary_assignment_type_2(std::uint64_t inputUnitName, std::uint8_t functionType, std::uint16_t inputObjectId, std::uint16_t functionObjectId, bool storeAsPreferred, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Handles an Auxiliary Capabilities request (0x27, J.7.14) from the stateless message path
		/// @param[in] message The CAN message that was received
		/// @param[in] data The message data buffer
		/// @returns true if a response was sent, otherwise false
		bool handle_auxiliary_capabilities_request(const CANMessage &message, const std::vector<std::uint8_t> &data);

		/// @brief Handles a Preferred Assignment command (0x22, J.7.7) from a working set, decoding its
		/// (function -> input) pairs and routing them to on_auxiliary_preferred_assignment_received
		/// @param[in] message The CAN message that was received
		/// @param[in] managedWorkingSet The working set that sent the command
		void handle_preferred_assignment_command(const CANMessage &message, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles an Auxiliary Assignment Type 2 response (0x24, J.7.6) from a working set, routing
		/// it to on_auxiliary_assignment_response_received
		/// @param[in] message The CAN message that was received
		/// @param[in] managedWorkingSet The working set that sent the response
		void handle_auxiliary_assignment_type_2_command(const CANMessage &message, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles an Auxiliary Input Status Type 2 Enable response (0x25, J.7.12) from a working set,
		/// routing it to on_auxiliary_input_status_enable_response_received
		/// @param[in] message The CAN message that was received
		/// @param[in] managedWorkingSet The working set that sent the response
		void handle_auxiliary_input_status_type_2_enable_command(const CANMessage &message, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles an Auxiliary Input Type 2 Status message (0x26, J.7.9) from a working set, routing
		/// it to on_auxiliary_input_status_received
		/// @param[in] message The CAN message that was received
		/// @param[in] managedWorkingSet The working set that sent the status
		void handle_auxiliary_input_type_2_status_message(const CANMessage &message, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles a Get Supported Objects message (0xC5, D.14/D.15) from the stateless message path,
		/// replying with the object types the server supports
		/// @param[in] message The CAN message that was received
		/// @returns true if a response was sent, otherwise false
		bool handle_get_supported_objects_message(const CANMessage &message);

		/// @brief Handles an Extended Get Versions message (0xD3, Annex E), replying with the extended version
		/// labels stored for the client
		/// @param[in] message The CAN message that was received
		void handle_extended_get_versions_message(const CANMessage &message);

		/// @brief Handles an Extended Store Version command (0xD4, E.13), saving the client's object pool under
		/// a 32 byte version label
		/// @param[in] message The CAN message that was received
		/// @param[in] data The message data buffer
		/// @param[in] managedWorkingSet The working set that sent the command
		void handle_extended_store_version_command(const CANMessage &message, const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles an Extended Delete Version command (0xD6, Annex E), deleting the stored object pool
		/// named by a 32 byte version label
		/// @param[in] message The CAN message that was received
		/// @param[in] data The message data buffer
		/// @param[in] managedWorkingSet The working set that sent the command
		void handle_extended_delete_version_command(const CANMessage &message, const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles an Extended Load Version command (0xD5, E.14/E.15), loading a stored object pool named
		/// by a 32 byte version label
		/// @param[in] message The CAN message that was received
		/// @param[in] data The message data buffer
		/// @param[in] managedWorkingSet The working set that sent the command
		void handle_extended_load_version_command(const CANMessage &message, const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles a Select Colour Map command (0xBA), selecting the working set's active Colour Map object
		/// @param[in] data The message data buffer
		/// @param[in] managedWorkingSet The working set that sent the command
		void handle_select_colour_map_command(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles a Get Attribute Value message (0xB9, F.59), replying with the requested object
		/// attribute's value
		/// @param[in] data The message data buffer
		/// @param[in] managedWorkingSet The working set that sent the command
		void handle_get_attribute_value_message(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles a Change End Point command (0xA9), resizing an Output Line and setting its direction
		/// @param[in] data The message data buffer
		/// @param[in] managedWorkingSet The working set that sent the command
		void handle_change_end_point_command(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles a Change Polygon Scale command (0xB7, F.54), rescaling an Output Polygon's points and
		/// dimensions
		/// @param[in] data The message data buffer
		/// @param[in] managedWorkingSet The working set that sent the command
		void handle_change_polygon_scale_command(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles an ESC command (0x92, F.9), aborting input on the object currently open for input
		/// @param[in] managedWorkingSet The working set that sent the command
		void handle_esc_command(std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles a Lock/Unlock Mask command (0xBD, F.46), locking or unlocking the active working set's
		/// visible mask
		/// @param[in] data The message data buffer
		/// @param[in] managedWorkingSet The working set that sent the command
		void handle_lock_unlock_mask_command(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Handles a Change Object Label command (0xB5, F.50/F.51), updating an entry in the pool's
		/// Object Label Reference List
		/// @param[in] data The message data buffer
		/// @param[in] managedWorkingSet The working set that sent the command
		void handle_change_object_label_command(const std::vector<std::uint8_t> &data, std::shared_ptr<VirtualTerminalServerManagedWorkingSet> managedWorkingSet);

		/// @brief Sends a response to a change background colour command
		/// @param[in] objectID The object ID for the object to change
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] colour The colour the background was set to
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_background_colour_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::uint8_t colour, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change child location command
		/// @param[in] parentObjectID The object ID for the parent of the object to move
		/// @param[in] objectID The object ID for the object to move
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_child_location_response(std::uint16_t parentObjectID, std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change child position command
		/// @param[in] parentObjectID The object ID for the parent of the object to move
		/// @param[in] objectID The object ID for the object to move
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_child_position_response(std::uint16_t parentObjectID, std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change fill attributes command
		/// @param[in] objectID The object ID for the object to change
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_fill_attributes_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change font attributes command
		/// @param[in] objectID The object ID for the object to change
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_font_attributes_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change line attributes command
		/// @param[in] objectID The object ID for the object to change
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_line_attributes_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change list item command
		/// @param[in] objectID The object ID for the object to change
		/// @param[in] newObjectID The object ID for the object to place at the specified list index, or NULL_OBJECT_ID (0xFFFF)
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] listIndex The list index to change, numbered 0 to n
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_list_item_response(std::uint16_t objectID, std::uint16_t newObjectID, std::uint8_t errorBitfield, std::uint8_t listIndex, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change numeric value command
		/// @param[in] objectID The object ID for the object whose numeric value was meant to be changed
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] value The value that was set by the client
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_numeric_value_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::uint32_t value, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change polygon point command
		/// @param[in] objectID The object ID of the modified polygon
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_polygon_point_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change end point command
		/// @param[in] objectID The object ID of the output line whose end point was meant to be changed
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_end_point_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change polygon scale command
		/// @param[in] objectID The object ID of the output polygon that was meant to be scaled
		/// @param[in] newWidth The new width attribute the command carried, echoed back to the client
		/// @param[in] newHeight The new height attribute the command carried, echoed back to the client
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_polygon_scale_response(std::uint16_t objectID, std::uint16_t newWidth, std::uint16_t newHeight, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to an ESC command
		/// @param[in] objectID The object ID of the object whose input was aborted, or NULL_OBJECT_ID when none was
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_esc_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a lock/unlock mask command, or an unsolicited response when the VT
		/// releases a lock on its own initiative
		/// @param[in] command The command being answered, 0 for unlock and 1 for lock
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] unsolicited True for a release the VT originates, which bypasses the macro response
		/// suppression because it is not a response to any command, false for an answer to a received command
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_lock_unlock_mask_response(std::uint8_t command, std::uint8_t errorBitfield, bool unsolicited, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change object label command. Unlike the other change command
		/// responses, F.51 carries no object ID echo: byte 2 is the error bitfield and bytes 3-8 are reserved.
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_object_label_response(std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change size command
		/// @param[in] objectID The object ID for the object whose size was meant to be changed
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_size_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change soft key mask command
		/// @param[in] objectID The object ID of a data mask or alarm mask
		/// @param[in] newObjectID The object ID of the soft key mask to apply to the mask indicated by objectID
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_soft_key_mask_response(std::uint16_t objectID, std::uint16_t newObjectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a change string value command
		/// @param[in] objectID The object ID for the object whose value was meant to be changed
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_change_string_value_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a delete version command
		/// @param[in] errorBitfield An error bitfield to report back to the client
		/// @param[in] destination The control function to send the message to
		/// @returns True if the message was sent, otherwise false
		bool send_delete_version_response(std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to a delete object pool command
		/// @param[in] errorBitfield An error bitfield to report back to the client
		/// @param[in] destination The control function to send the message to
		/// @returns True if the message was sent, otherwise false
		bool send_delete_object_pool_response(std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to the enable/disable object command
		/// @param[in] objectID The object ID for the object
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] value The enable/disable state that was set by the client
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_enable_disable_object_response(std::uint16_t objectID, std::uint8_t errorBitfield, bool value, std::shared_ptr<ControlFunction> destination) const;

		/// @brief This message is sent by the VT to a Working Set Master to acknowledge the End of Object Pool message.
		/// @details When the VT replies with an error of any type
		/// the VT should delete the object pool from volatile memory storage and inform the operator
		/// by an alarm type method of the suspension of the Working Set and indicate the reason for
		/// the deletion. On reception of this message, the responsible ECU(s) should enter a failsafe
		/// operation mode providing a safe shutdown procedure of the whole device.
		/// @param[in] success Indicates if the pool was error free
		/// @param[in] parentIDOfFaultingObject The parent object ID for the faulty object, or NULL_OBJECT_ID
		/// @param[in] faultingObjectID The faulty object's ID or the NULL_OBJECT_ID
		/// @param[in] errorCodes A bitfield of error codes that describe the issues with the pool
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_end_of_object_pool_response(bool success,
		                                      std::uint16_t parentIDOfFaultingObject,
		                                      std::uint16_t faultingObjectID,
		                                      std::uint8_t errorCodes,
		                                      std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to the execute macro or extended macro command
		/// @param[in] objectID The object ID for the macro
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] destination The control function to send the message to
		/// @param[in] extendedMacro True if the macro is an extended macro, otherwise false
		/// @returns true if the message was sent, otherwise false
		bool send_execute_macro_or_extended_macro_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::shared_ptr<ControlFunction> destination, bool extendedMacro) const;

		/// @brief Sends a response to the hide/show object command
		/// @param[in] objectID The object ID for the object
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] value The hide/show state that was set by the client
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false
		bool send_hide_show_object_response(std::uint16_t objectID, std::uint8_t errorBitfield, bool value, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to the change priority command
		/// @param[in] objectID The object ID for the object
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] priority The priority that was set by the client
		/// @param[in] destination The control function to send the message to
		/// @returns True if the message was sent, otherwise false
		bool send_change_priority_response(std::uint16_t objectID, std::uint8_t errorBitfield, std::uint8_t priority, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response to the select input object command
		/// @param[in] objectID The object ID for the object
		/// @param[in] errorBitfield An error bitfield
		/// @param[in] response The response to the select input object command
		/// @param[in] destination The control function to send the message to
		/// @returns True if the message was sent, otherwise false
		bool send_select_input_object_response(std::uint16_t objectID, std::uint8_t errorBitfield, SelectInputObjectResponse response, std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends the VT status message broadcast. The status message
		/// contains information such as which working set is the active one, and information about
		/// what the VT server is doing, such as busy flags. This message should be sent at 1 Hz.
		/// @returns true if the message was sent, otherwise false
		bool send_status_message() const;

		/// @brief Flags that a VT Status field the standard tracks on change has been modified, so
		/// the next update() transmits the status promptly instead of waiting for the 1 Hz tick.
		void mark_status_message_changed();

		/// @brief Returns the soft key mask that the specified Data Mask or Alarm Mask makes visible
		/// @param[in] workingSet The working set that owns the mask object
		/// @param[in] maskObjectId The object ID of the Data Mask or Alarm Mask to inspect
		/// @returns The object ID of the mask's soft key mask, or NULL_OBJECT_ID if it has none
		std::uint16_t get_visible_soft_key_mask(const std::shared_ptr<VirtualTerminalServerManagedWorkingSet> &workingSet, std::uint16_t maskObjectId) const;

		/// @brief Recomputes the VT Status' visible-mask fields from the active working set's current state,
		/// flagging the status only if either field actually moved (ISO 11783-6 G.2 bytes 3-6)
		void refresh_active_mask_status_fields();

		/// @brief Returns whether any managed working set has an object pool parse in flight, meaning it has
		/// started and its response has not yet been queued (ISO 11783-6 G.2 byte 7 bit 4)
		/// @returns true if a parse is in flight for any managed working set
		bool is_any_object_pool_parsing() const;

		/// @brief Returns whether the mask any managed working set currently shows is an Alarm Mask
		/// @returns true if an Alarm Mask is the active mask of any managed working set
		bool is_any_alarm_mask_active() const;

		/// @brief Returns whether the given object is the one a working set currently has open for operator
		/// input (ISO 11783-6 Table 5 Data-input state).
		/// @details This is the choke point for Table 5's forced-abort and rejection rows: while an object is
		/// open for input, commands that target that same object (Change Numeric Value, Change String Value,
		/// Change Attribute, Change List Item, Enable/Disable, or a Select Input Object naming it) are refused
		/// so the operator's open edit is not disturbed. It keys on the open-for-input object (set by a Select
		/// Input Object command with option byte 0), not on a merely-focused object: Table 5's Navigating state
		/// does not reject these commands. It compares the object's own ID and does not chase variable
		/// indirection, per Table 5's "on the object that has focus".
		/// @param[in] workingSet The working set whose open-for-input object is checked
		/// @param[in] objectID The object ID the command targets
		/// @returns true if the working set has objectID open for input, otherwise false
		bool is_object_open_for_input(const std::shared_ptr<VirtualTerminalServerManagedWorkingSet> &workingSet, std::uint16_t objectID) const;

		/// @brief Returns the mask object a working set currently has active.
		/// @details This is safe to call for a working set other than the one being served. It takes one
		/// snapshot of that working set's object tree and resolves both the Working Set object and the
		/// mask it names from that same snapshot, so the answer describes a single coherent pool. A
		/// working set whose pool is still being parsed, or whose parse failed, reports no mask. That is
		/// a policy choice rather than a safety requirement: the snapshot would make either read safe,
		/// but neither is a pool that may be presented to the operator.
		/// @param[in] workingSet The working set to inspect
		/// @returns The active mask object, or an empty shared pointer if it cannot be resolved
		std::shared_ptr<VTObject> get_active_mask_object(const std::shared_ptr<VirtualTerminalServerManagedWorkingSet> &workingSet) const;

		/// @brief Returns the working set whose mask should be displayed: the working set with an active
		/// Alarm Mask of the highest priority per ISO 11783-6 4.6.14, ties broken by the earliest
		/// activation, and with no alarm raised anywhere the working set the operator selected per
		/// clause 4.6.8, then the one that last had a Data Mask visible.
		/// @details Every managed working set whose active mask resolves is scored into an
		/// ActiveWorkingSetCandidate and ranked by active_working_set_candidate_outranks, which is where
		/// the whole ordering lives; this function only reads the state that fills the score in.
		/// @returns The working set that should be active, or an empty shared pointer if there is none
		std::shared_ptr<VirtualTerminalServerManagedWorkingSet> select_active_working_set() const;

		/// @brief Stamps an activation sequence number on each working set whose active mask has become
		/// an Alarm Mask, and clears it on each whose active mask is no longer one, so that
		/// select_active_working_set can order equal-priority alarms by when they were raised.
		void stamp_alarm_activation_sequences();

		/// @brief Recomputes which working set the VT displays (ISO 11783-6 clause 4.6.14) and switches
		/// to it when it differs from the current one, then raises the Alarm Mask displayed event if the
		/// resulting mask is an Alarm Mask that was not already on screen.
		void apply_active_working_set_arbitration();

		/// @brief Raises the repaint event for a working set unless that working set has its visible mask
		/// locked, in which case the on screen presentation is held until the lock is released.
		/// @param[in] workingSet The working set whose presentation would be refreshed
		void dispatch_repaint(const std::shared_ptr<VirtualTerminalServerManagedWorkingSet> &workingSet);

		/// @brief Releases the mask lock of each managed working set whose lock timeout has expired,
		/// announcing the release with an unsolicited Lock/Unlock Mask Response (ISO 11783-6 F.46), so a
		/// client that stops talking cannot freeze the operator's screen indefinitely. Run from update().
		void release_expired_mask_locks();

		/// @brief Tears down each managed working set that has been lost, either because no Working Set
		/// Maintenance message has arrived for over 3 s (ISO 11783-6 clause 4.6.9) or because a runtime
		/// object pool update failed to parse (clause C.2.6), deleting its object pool and dropping it as
		/// the active working set. A teardown that lands while a parse is still outstanding is deferred to
		/// a later update(). Run from update().
		/// @returns true if at least one working set was torn down, so the caller re-runs the arbitration
		bool tear_down_lost_working_sets();

		/// @brief Sends the list of objects that the server supports to a client, usually in
		/// response to a "get supported objects" message, which is used by a client.
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false.
		bool send_supported_objects(std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends the Control Audio Signal response to the client with "No errors" error code
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false.
		bool send_audio_signal_successful(std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends the Set Audio Volume response to the client with "No error" error code
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent, otherwise false.
		bool send_audio_volume_response(std::shared_ptr<ControlFunction> destination) const;

		/// @brief Sends a response message to the Screen capture command
		/// @param[in] item Item requested from the Screen Capture command
		/// @param[in] path Path requested from the Screen Capture command
		/// @param[in] errorCode Error codes
		/// @param[in] imageId Error codes
		/// @param[in] requestor The control function which requested the screen capture
		/// @returns true if the message was sent, otherwise false
		bool send_capture_screen_response(std::uint8_t item, std::uint8_t path, std::uint8_t errorCode, std::uint16_t imageId, std::shared_ptr<ControlFunction> requestor) const;

		/// @brief Sends the response to the get window mask data message
		/// @param[in] destination The control function to send the message to
		/// @returns true if the message was sent
		bool send_get_window_mask_data_response(std::shared_ptr<ControlFunction> destination) const;

		/// @brief Cyclic update function
		void update();

		static constexpr std::uint8_t VERSION_LABEL_LENGTH = 7; ///< The length of a standard object pool version label
		static constexpr std::uint8_t EXTENDED_VERSION_LABEL_LENGTH = 32; ///< The length of an extended object pool version label (VT v4+)

		EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>> onRepaintEventDispatcher; ///< Event dispatcher for repaint events
		EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, std::uint16_t, std::uint16_t> onChangeActiveMaskEventDispatcher; ///< Event dispatcher for active data/alarm mask change events
		EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, std::uint16_t, std::uint16_t> onChangeActiveSoftKeyMaskEventDispatcher; ///< Event dispatcher for active softkey mask change events
		EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, std::uint16_t, bool> onFocusObjectEventDispatcher; ///< Event dispatcher for focus object events
		EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, std::uint16_t> onAlarmMaskDisplayedEventDispatcher; ///< Event dispatcher for an Alarm Mask appearing or reappearing as the displayed mask (ISO 11783-6 4.6.14 c)
		EventDispatcher<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, WorkingSetLossReason> onWorkingSetLostEventDispatcher; ///< Event dispatcher for a working set being torn down (ISO 11783-6 4.6.9 / C.2.6), carrying the reason so the display layer can alert the operator
		LanguageCommandInterface languageCommandInterface; ///< The language command interface for the server
		std::shared_ptr<InternalControlFunction> serverInternalControlFunction; ///< The internal control function for the server
		std::vector<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>> managedWorkingSetList; ///< The list of managed working sets
		std::map<std::shared_ptr<VirtualTerminalServerManagedWorkingSet>, bool> managedWorkingSetIopLoadStateMap; ///< A map to hold the IOP load state per session
		std::shared_ptr<VirtualTerminalServerManagedWorkingSet> activeWorkingSet; ///< The active working set
		std::weak_ptr<VirtualTerminalServerManagedWorkingSet> lastDataMaskWorkingSet; ///< The working set that most recently had a Data Mask displayed, which 4.6.14 falls back to when the last alarm clears
		std::weak_ptr<VirtualTerminalServerManagedWorkingSet> operatorSelectedWorkingSet; ///< The working set the operator selected (clause 4.6.8), which outranks the fallbacks but never a raised alarm; weak so a torn down selection expires instead of being kept alive
		std::weak_ptr<VirtualTerminalServerManagedWorkingSet> displayedAlarmWorkingSet; ///< The working set whose Alarm Mask is on screen, so that the Alarm Mask displayed event fires on appearance rather than on every arbitration
		std::uint32_t alarmActivationSequenceCounter = 0; ///< Monotonic source of the activation sequence numbers that order equal-priority alarms (4.6.14)
		std::uint32_t statusMessageTimestamp_ms = 0; ///< The timestamp of the last status message sent
		std::uint16_t displayedAlarmMaskObjectID = NULL_OBJECT_ID; ///< The object ID of the Alarm Mask currently on screen, or NULL_OBJECT_ID when the displayed mask is not an Alarm Mask
		std::uint16_t activeWorkingSetDataMaskObjectID = NULL_OBJECT_ID; ///< The object ID of the active working set's data mask
		std::uint16_t activeWorkingSetSoftkeyMaskObjectID = NULL_OBJECT_ID; ///< The object ID of the active working set's soft key mask
		std::uint8_t activeWorkingSetMasterAddress = NULL_CAN_ADDRESS; ///< The address of the active working set's master
		std::uint8_t busyCodesBitfield = 0; ///< The busy codes bitfield
		std::uint8_t currentCommandFunctionCode = 0; ///< The current command function code being processed
		/// @brief One entry on the macro execution queue: a macro object ID and the working set it runs on.
		struct QueuedMacro
		{
			std::uint16_t macroID; ///< The object ID of the macro to execute
			std::shared_ptr<VirtualTerminalServerManagedWorkingSet> workingSet; ///< The working set the macro executes on
		};
		std::deque<QueuedMacro> macroExecutionQueue; ///< FIFO of macros awaiting execution. process_macro and the Execute Macro handler append matching macros here in trigger order; the drain runs them front to back. FIFO trigger order is the invariant ISO 11783-6 4.6.11.4 b/c require: a macro completes before another starts (b), and macros run in the order they were triggered (c)
		std::uint8_t macroExecutionDepth = 0; ///< A 0/1 response-suppression window, not an unbounded depth: set to 1 for the whole drain and back to 0 when it empties. Non-zero withholds the responses of the macro-replayed commands (ISO 11783-6 4.6.11.4 f) and marks re-entrant messages so the per-command budget is not reset for them. The queue flattens all macro nesting to one drain level, so this never exceeds 1
		bool macroQueueDraining = false; ///< True while drain_macro_execution_queue is running. A macro command that enqueues another calls the drain again; that call returns immediately on this flag, leaving the one active drain to reach the new entry -- which is what keeps exactly one drain in flight
		static constexpr std::uint32_t MAX_MACRO_EXECUTIONS_PER_COMMAND = 1000; ///< How many macro executions one bus command may trigger before the VT abandons the rest. Under the queue this is the sole bound: a self-referencing macro re-enqueues itself on every run, an infinite loop the budget stops. The standard sets no such number; this bound is the VT's own, sized so a worst-case trigger blocks the CAN thread for well under the 3 s working set maintenance timeout
		std::uint32_t macroExecutionsThisCommand = 0; ///< Macro executions attributed to the bus command currently being processed
		bool macroExecutionBudgetExhausted = false; ///< Latched when the budget is spent, so abandoning the rest of the macros is logged once rather than once per remaining macro
		bool statusMessagePending = true; ///< Set when a VT Status field the standard tracks (ISO 11783-6 G.2 bytes 2-6, or byte 7 bit 6) changes, so update() transmits promptly instead of waiting for the next 1 Hz tick
		bool auxiliaryInputLearnModeActive = false; ///< Whether the status message reports auxiliary input learn mode (busy-codes bit 0x40)
		bool initialized = false; ///< True if the server has been initialized, otherwise false
	};
} // namespace isobus
#endif //ISOBUS_VIRTUAL_TERMINAL_SERVER_HPP
