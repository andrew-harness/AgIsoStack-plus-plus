//================================================================================================
/// @file pcan_basic_windows_plugin.hpp
///
/// @brief An interface for using a PEAK PCAN device.
/// @attention Use of the PEAK driver is governed in part by their license, and requires you
/// to install their driver first, which in-turn requires you to agree to their terms and conditions.
/// @author Adrian Del Grosso
///
/// @copyright 2022 The Open-Agriculture Developers
//================================================================================================
#ifndef PCAN_BASIC_WINDOWS_PLUGIN_HPP
#define PCAN_BASIC_WINDOWS_PLUGIN_HPP

#include <cstring>
#include <string>

// This needs to be included before PCANBasic for definitions of
// anachronistic windows C types like WORD, DWORD etc
#include <Windows.h>

#include "isobus/hardware_integration/PCANBasic.h"
#include "isobus/hardware_integration/can_hardware_plugin.hpp"
#include "isobus/isobus/can_hardware_abstraction.hpp"
#include "isobus/isobus/can_message_frame.hpp"

namespace isobus
{
	//================================================================================================
	/// @class PCANBasicWindowsPlugin
	///
	/// @brief A Windows CAN Driver for PEAK PCAN Devices
	//================================================================================================
	class PCANBasicWindowsPlugin : public CANHardwarePlugin
	{
	public:
		/// @brief Constructor for the Windows version of the PEAK PCAN Basic CAN driver
		/// @param[in] channel The channel to use. See definitions in PCANBasic.g such as `PCAN_USBBUS1`
		explicit PCANBasicWindowsPlugin(WORD channel);

		/// @brief The destructor for PCANBasicWindowsPlugin
		virtual ~PCANBasicWindowsPlugin();

		/// @brief Returns the displayable name of the plugin
		/// @returns PEAK CAN
		std::string get_name() const override;

		/// @brief Returns if the connection with the hardware is valid
		/// @returns `true` if connected, `false` if not connected
		bool get_is_valid() const override;

		/// @brief Closes the connection to the hardware
		void close() override;

		/// @brief Connects to the hardware you specified in the constructor's channel argument
		void open() override;

		/// @brief Returns a frame from the hardware (synchronous), or `false` if no frame can be read.
		/// @param[in, out] canFrame The CAN frame that was read
		/// @returns `true` if a CAN frame was read, otherwise `false`
		bool read_frame(isobus::CANMessageFrame &canFrame) override;

		/// @brief Writes a frame to the bus (synchronous)
		/// @param[in] canFrame The frame to write to the bus
		/// @returns `true` if the frame was written, otherwise `false`
		bool write_frame(const isobus::CANMessageFrame &canFrame) override;

	private:
		TPCANHandle handle; ///< The handle as defined in the PCAN driver API
		TPCANStatus openResult; ///< Stores the result of the call to begin CAN communication. Used for is_valid check later.
	};

	/// @brief Decodes a raw PCAN-Basic message into a CANMessageFrame, refusing anything that is
	/// not a plain CAN data frame.
	/// @details `TPCANMsg::MSGTYPE` is a bitmask, not an enumeration (see the `PCAN_MESSAGE_*`
	/// definitions in PCANBasic.h), so more than one bit can be set at once -- an extended frame the
	/// driver also tags as an echo, for example, needs `PCAN_MESSAGE_EXTENDED` tested with a mask
	/// rather than compared for equality. An error frame or a status message reports the state of
	/// the bus itself rather than traffic on it, so it is refused rather than forwarded as a
	/// fabricated CAN message; an RTR frame carries no payload despite a nonzero `LEN`; and an FD
	/// frame cannot be represented by `TPCANMsg`'s 8-byte `DATA` at all, since this plugin reads
	/// through the classic `CAN_Read`/`TPCANMsg` path and ISOBUS is classic CAN regardless of what
	/// the hardware can also do. `PCAN_MESSAGE_ECHO` is deliberately not refused: an echo is a real
	/// frame the driver legitimately delivers, not a bus-state report.
	/// @attention This function is `inline` and defined here, not in the .cpp, specifically so it
	/// can be exercised by a unit test with no link dependency on the PCAN import library and
	/// therefore no requirement for `PCANBasic.dll` to be installed on the machine running the test.
	/// @param[in] message The raw message read from the PCAN driver
	/// @param[in, out] canFrame The frame to populate. Left untouched when `message` is refused
	/// @returns `true` if `message` was decoded into `canFrame`, `false` if it was refused
	inline bool pcan_decode_frame(const TPCANMsg &message, isobus::CANMessageFrame &canFrame)
	{
		if (0 != (message.MSGTYPE & (PCAN_MESSAGE_ERRFRAME | PCAN_MESSAGE_STATUS | PCAN_MESSAGE_RTR | PCAN_MESSAGE_FD)))
		{
			return false;
		}

		// TPCANMsg::DATA and CANMessageFrame::data are both fixed at 8 bytes; a LEN reported longer
		// than that is malformed. Checked before the copy below so the copy is bounded by
		// construction -- silently truncating an over-length LEN would hand the stack a message of
		// the wrong length, which is worse than refusing it outright.
		if (message.LEN > 8)
		{
			return false;
		}

		canFrame.identifier = message.ID;
		canFrame.isExtendedFrame = (0 != (message.MSGTYPE & PCAN_MESSAGE_EXTENDED));
		canFrame.dataLength = message.LEN;
		memcpy(canFrame.data, message.DATA, message.LEN);
		return true;
	}
}
#endif // PCAN_BASIC_WINDOWS_PLUGIN_HPP
