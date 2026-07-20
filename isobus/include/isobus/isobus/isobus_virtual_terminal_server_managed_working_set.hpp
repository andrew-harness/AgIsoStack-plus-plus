//================================================================================================
/// @file isobus_virtual_terminal_server_managed_working_set.hpp
///
/// @brief Defines a managed working set for a VT server.
/// @author Adrian Del Grosso
///
/// @copyright 2023 Adrian Del Grosso
//================================================================================================
#ifndef ISOBUS_VIRTUAL_TERMINAL_MANAGED_WORKING_SET_HPP
#define ISOBUS_VIRTUAL_TERMINAL_MANAGED_WORKING_SET_HPP

#include <array>
#include <map>
#include <mutex>
#include <thread>

#include "isobus/isobus/can_badge.hpp"
#include "isobus/isobus/can_control_function.hpp"
#include "isobus/isobus/isobus_virtual_terminal_objects.hpp"
#include "isobus/isobus/isobus_virtual_terminal_working_set_base.hpp"
#include "isobus/utility/event_dispatcher.hpp"

namespace isobus
{
	class VirtualTerminalServer;

	/// @brief Defines a managed working set.
	/// @details This class is meant to be used as the basis for a VT server.
	/// It keeps track of one active object pool.
	class VirtualTerminalServerManagedWorkingSet : public VirtualTerminalWorkingSetBase
	{
	public:
		/// @brief Enumerates the states of the processing thread for the object pool
		enum class ObjectPoolProcessingThreadState
		{
			None, ///< Thread has never been started for this working set
			Running, ///< We are currently parsing the object pool
			Success, ///< We have finished parsing the pool successfully and need to respond to the working set
			Fail, ///< The object pool is bad and we need to respond to the working set
			Joined ///< We have sent our response to the working set master and are done parsing
		};

		/// @brief Default constructor
		VirtualTerminalServerManagedWorkingSet();

		/// @brief Constructor that takes a control function to associate with this working set
		/// @param[in] associatedControlFunction The control function to associate with this working set
		VirtualTerminalServerManagedWorkingSet(std::shared_ptr<ControlFunction> associatedControlFunction);

		/// @brief Destructor
		~VirtualTerminalServerManagedWorkingSet() = default;

		/// @brief Starts a thread to parse the received object pool files
		void start_parsing_thread();

		/// @brief Joins the parsing thread
		void join_parsing_thread();

		/// @brief Returns if any object pools are being managed for this working set master
		/// @returns true if at least 1 object pool has been received for this working set master, otherwise false
		bool get_any_object_pools() const;

		/// @brief Returns whether a pool parse worker exists for this working set
		/// @details True from start_parsing_thread() until join_parsing_thread(). That spans more than
		/// the Running state: it also covers the moment after the thread is spawned but before the
		/// worker has recorded Running, and the settled Success and Fail states, whose thread has
		/// finished but has not been joined and whose response to the client is still owed. The thread
		/// handle is the authoritative test because it is written only on the CAN thread, whereas the
		/// processing state is written by the worker.
		/// @returns True while a pool parse worker is outstanding, otherwise false
		bool is_object_pool_parse_outstanding() const;

		/// @brief Deletes this working set's object pool from volatile storage, returning the working
		/// set to the state it was in before any pool was uploaded (ISO 11783-6 F.44)
		/// @details Discards the parsed objects and the raw IOP bytes, and clears the server-side state
		/// that is derived from the pool: the object open for operator input, the mask lock, the Colour
		/// Map selection, the focused object, the alarm activation sequence, the record of how the pool
		/// was obtained, and the pool processing state.
		///
		/// What describes the CONNECTION survives, because F.44 deletes a pool and not a working set --
		/// the client stays connected and may upload a new pool immediately. So the associated control
		/// function, the working set and auxiliary input maintenance timestamps that clause 4.6.9 times
		/// out against, the VT version the master reported in Working Set Maintenance, the retained
		/// callback handles, and any pending teardown request are all left alone.
		///
		/// Refused while a pool parse worker is outstanding: the worker owns the staging tree while it
		/// parses, and clearing it underneath would be a data race. Joining the worker here is not an
		/// option because this runs on the CAN thread and a parse can take seconds.
		/// @returns True if the object pool was deleted, false if a pool parse is outstanding and the
		/// pool was therefore left alone
		bool reset_object_pool();

		/// @brief Returns the state of object pool processing, useful when parsing the object pool
		/// on its own thread.
		/// @returns The state of object pool processing
		ObjectPoolProcessingThreadState get_object_pool_processing_state();

		/// @brief Returns the control function that is the working set master
		/// @returns The control function that is the working set master
		std::shared_ptr<ControlFunction> get_control_function() const;

		/// @brief Returns the working set maintenance message timestamp
		/// @returns The working set maintenance message timestamp in milliseconds
		std::uint32_t get_working_set_maintenance_message_timestamp_ms() const;

		/// @brief Sets the timestamp for when we sent the maintenance message timestamp
		/// @param[in] value New timestamp value in milliseconds
		void set_working_set_maintenance_message_timestamp_ms(std::uint32_t value);

		/// @brief Saves an event callback handle for the lifetime of this object
		/// which is useful for keeping track of callback lifetimes in a VT server
		/// @param[in] callbackHandle The event callback handle to save
		void save_callback_handle(isobus::EventCallbackHandle callbackHandle);

		/// @brief Clears all event callback handles for the this working set
		/// which is useful if you want to stop drawing this working set
		void clear_callback_handles();

		/// @brief Tells the server where this pool originated from.
		/// @returns True if this pool was loaded via a Load Version Command, otherwise false (transferred normally)
		bool get_was_object_pool_loaded_from_non_volatile_memory() const;

		/// @brief Tells the server where this pool originated from.
		/// @param[in] value True if this pool was loaded via a Load Version Command, otherwise false (transferred normally)
		void set_was_object_pool_loaded_from_non_volatile_memory(bool value, CANLibBadge<VirtualTerminalServer>);

		/// @brief Tells the server whether the Load Version that fetched this pool was the extended
		/// (0xD5) variant, so the deferred load-completed response uses the matching command byte.
		/// @returns True if this pool was loaded via an Extended Load Version Command, otherwise false
		bool get_loaded_via_extended_version_command() const;

		/// @brief Records whether the Load Version that fetched this pool was the extended (0xD5) variant.
		/// @param[in] value True if this pool was loaded via an Extended Load Version Command, otherwise false
		void set_loaded_via_extended_version_command(bool value, CANLibBadge<VirtualTerminalServer>);

		/// @brief Returns the VT version the working set master reported in its Working Set Maintenance
		/// message (ISO 11783-6 G.3). Values: 3, 4, 5, or 0xFF for version 2 and prior. Defaults to
		/// 0xFF until a maintenance message is received.
		/// @returns The working set master's reported VT version byte
		std::uint8_t get_working_set_maintenance_version() const;

		/// @brief Stores the VT version the working set master reported in its Working Set Maintenance message.
		/// @param[in] value The reported version byte (3, 4, 5, or 0xFF for version 2 and prior)
		void set_working_set_maintenance_version(std::uint8_t value, CANLibBadge<VirtualTerminalServer>);

		/// @brief Returns the object ID of the Colour Map selected by the Select Colour Map command (F.60)
		/// for this working set, or NULL_OBJECT_ID when the default palette is in use.
		/// @returns The active Colour Map object ID, or NULL_OBJECT_ID for the default palette
		std::uint16_t get_active_colour_map_object_id() const;

		/// @brief Stores the object ID of the Colour Map selected by the Select Colour Map command (F.60).
		/// @param[in] value The Colour Map object ID to activate, or NULL_OBJECT_ID for the default palette
		void set_active_colour_map_object_id(std::uint16_t value, CANLibBadge<VirtualTerminalServer>);

		/// @brief Returns the object ID of the mask this working set has locked with the Lock/Unlock Mask
		/// command (F.46), or NULL_OBJECT_ID when it holds no lock.
		/// @returns The locked mask's object ID, or NULL_OBJECT_ID when no mask is locked
		std::uint16_t get_mask_lock_object_id() const;

		/// @brief Returns the lock timeout that the Lock Mask command carried (F.46 bytes 5-6)
		/// @returns The lock timeout in milliseconds, or zero when the lock does not time out
		std::uint16_t get_mask_lock_timeout_ms() const;

		/// @brief Returns the timestamp at which the mask lock was taken
		/// @returns The timestamp in milliseconds at which the mask lock was taken
		std::uint32_t get_mask_lock_timestamp_ms() const;

		/// @brief Stores the mask lock taken by the Lock/Unlock Mask command (F.46).
		/// @param[in] objectID The object ID of the mask being locked, or NULL_OBJECT_ID to release the lock
		/// @param[in] timeout_ms The lock timeout in milliseconds, or zero for no timeout
		/// @param[in] timestamp_ms The timestamp in milliseconds at which the lock was taken
		void set_mask_lock(std::uint16_t objectID, std::uint16_t timeout_ms, std::uint32_t timestamp_ms, CANLibBadge<VirtualTerminalServer>);

		/// @brief Returns the sequence number stamped on this working set when its active mask became
		/// an Alarm Mask, which orders alarms chronologically for the priority arbitration of
		/// ISO 11783-6 clause 4.6.14. A lower number was activated earlier. Zero means this working
		/// set has no Alarm Mask asserted.
		/// @returns The alarm activation sequence number, or zero when no Alarm Mask is asserted
		std::uint32_t get_alarm_activation_sequence() const;

		/// @brief Stores the sequence number that orders this working set's Alarm Mask activation
		/// against those of other working sets (ISO 11783-6 clause 4.6.14).
		/// @param[in] value The activation sequence number, or zero to record that no Alarm Mask is asserted
		void set_alarm_activation_sequence(std::uint32_t value, CANLibBadge<VirtualTerminalServer>);

		/// @brief Sets the object ID of the currently focused object
		/// @param[in] objectID The object ID to set as the focused object
		void set_object_focus(std::uint16_t objectID);

		/// @brief Returns the object ID of the currently focused object
		/// @returns The object ID of the currently focused object
		std::uint16_t get_object_focus() const;

		/// @brief Sets the object ID of the object that is currently open for operator input
		/// @param[in] objectID The object ID that is open for input, or NULL_OBJECT_ID if none is
		void set_object_open_for_input(std::uint16_t objectID);

		/// @brief Returns the object ID of the object that is currently open for operator input
		/// @returns The object ID that is open for input, or NULL_OBJECT_ID if none is
		std::uint16_t get_object_open_for_input() const;

		/// @brief Sets the timestamp for when we received the last auxiliary input maintenance message
		/// from the client.
		/// @param[in] value New timestamp value in milliseconds
		void set_auxiliary_input_maintenance_timestamp_ms(std::uint32_t value);

		/// @brief Returns the timestamp for when we received the last auxiliary input maintenance message
		/// from the client.
		/// @returns The timestamp for when we received the last auxiliary input maintenance message
		std::uint32_t get_auxiliary_input_maintenance_timestamp_ms() const;

		/// @brief Marks the working set for deletion/deactivation by the server.
		/// The server will call this when object pool deletion is requested for this working set
		/// by the appropriate working set master.
		void request_deletion();

		/// @brief Returns if the server has marked this working set for deletion
		/// @returns true if the working set should be deleted, otherwise false
		bool is_deletion_requested() const;

		/// @brief Set the IOP size used for download percentage calculations
		/// @param[in] newIopSize IOP size in bytes
		void set_iop_size(std::uint32_t newIopSize);

		/// @brief Function to retrieve the IOP load progress
		/// @returns state of the IOP loading in percentage (0-100.0). Returns 0 if the IOP size is not set.
		float iop_load_percentage() const;

		/// @brief Function to check the IOP loading state
		/// @returns returns true if the IOP size is known but the transfer is not finished
		bool is_object_pool_transfer_in_progress() const;

	private:
		/// @brief Sets the object pool processing state to a new value
		/// @param[in] value The new state of processing the object pool
		void set_object_pool_processing_state(ObjectPoolProcessingThreadState value);

		/// @brief The object pool processing thread will execute this function when it runs
		void worker_thread_function();

		std::unique_ptr<std::thread> objectPoolProcessingThread = nullptr; ///< A thread to process the object pool with, since that can be fairly time consuming.
		std::shared_ptr<ControlFunction> workingSetControlFunction = nullptr; ///< Stores the control function associated with this working set
		std::vector<isobus::EventCallbackHandle> callbackHandles; ///< A convenient way to associate callback handles to a working set
		ObjectPoolProcessingThreadState processingState = ObjectPoolProcessingThreadState::None; ///< Stores the state of processing the object pool
		std::uint32_t workingSetMaintenanceMessageTimestamp_ms = 0; ///< A timestamp (in ms) to track sending of the maintenance message
		std::uint32_t auxiliaryInputMaintenanceMessageTimestamp_ms = 0; ///< A timestamp (in ms) to track if/when the working set sent an auxiliary input maintenance message
		std::uint32_t maskLockTimestamp_ms = 0; ///< A timestamp (in ms) marking when the Lock/Unlock Mask command (F.46) took the current lock
		std::uint32_t alarmActivationSequence = 0; ///< Orders this working set's Alarm Mask activation against other working sets' for the clause 4.6.14 tie-break; zero means no Alarm Mask is asserted
		std::uint16_t focusedObject = NULL_OBJECT_ID; ///< Stores the object ID of the currently focused object
		std::uint16_t objectOpenForInput = NULL_OBJECT_ID; ///< Stores the object ID of the object that is open for operator input, or NULL_OBJECT_ID when no input field is open
		std::uint16_t activeColourMapObjectId = NULL_OBJECT_ID; ///< The object ID of the Colour Map selected by the Select Colour Map command (F.60), or NULL_OBJECT_ID for the default palette
		std::uint16_t maskLockObjectID = NULL_OBJECT_ID; ///< The object ID of the mask locked by the Lock/Unlock Mask command (F.46), or NULL_OBJECT_ID when no mask is locked
		std::uint16_t maskLockTimeout_ms = 0; ///< The lock timeout (in ms) the Lock Mask command carried, or zero when the lock does not time out
		bool wasLoadedFromNonVolatileMemory = false; ///< Used to tell the server how this object pool was obtained
		bool loadedViaExtendedVersionCommand = false; ///< True when the pool was loaded via an Extended Load Version command (0xD5), so the deferred load response uses the extended command byte
		bool workingSetDeletionRequested = false; ///< Used to tell the server to delete this working set
		std::uint8_t workingSetMaintenanceVersion = 0xFF; ///< The VT version the master reported in Working Set Maintenance (0xFF = version 2 and prior, the conservative default)
	};
} // namespace isobus

#endif // ISOBUS_VIRTUAL_TERMINAL_MANAGED_WORKING_SET_HPP
