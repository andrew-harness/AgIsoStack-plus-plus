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

	/// @brief True when a `CAN_Initialize` status means the channel is usable.
	/// @details `PCAN_ERROR_CAUTION` is documented as "an operation was successfully carried out,
	/// however, irregularities were registered" -- a connected channel, not a failed one. This is
	/// the single definition of a usable open: `get_is_valid`, the bitrate read and the echo-frame
	/// setting all defer to it, so the predicate cannot drift between the value `CANHardwareInterface`
	/// gates its receive thread and every transmit on, and the value this plugin acts on internally.
	/// @param[in] status The status `CAN_Initialize` returned
	/// @returns `true` if the channel opened, `false` if it did not
	inline bool pcan_open_succeeded(TPCANStatus status)
	{
		return (PCAN_ERROR_OK == status) || (PCAN_ERROR_CAUTION == status);
	}

	/// @brief The channel condition to pass to `diagnose_pcan_open` when the `PCAN_CHANNEL_CONDITION`
	/// read itself failed.
	/// @details Zero is NOT usable for this, and that is the whole reason this constant exists:
	/// `PCAN_CHANNEL_UNAVAILABLE` IS zero, so falling back to it would report the most emphatic
	/// answer in `PcanOpenDiagnosis` -- no such channel exists -- on the strength of a read that
	/// returned nothing. The value must also carry neither `PCAN_CHANNEL_AVAILABLE` (0x01) nor
	/// `PCAN_CHANNEL_OCCUPIED` (0x02), or the bit tests in `diagnose_pcan_open` would read it as a
	/// sharing conflict instead. Those two constraints together are what makes it 0xFFFFFFFC rather
	/// than an all-ones sentinel, and `pcan_open_diagnosis_tests` pins both.
	constexpr DWORD PCAN_CONDITION_UNREAD = 0xFFFFFFFCU;

	/// @brief Enumerates the outcomes of opening a PCAN channel, distinguishing a plain success
	/// from the ones an operator needs to act on: a channel already held by another application,
	/// or one that negotiated a bitrate other than ISO 11783-2's 250 kbit/s.
	enum class PcanOpenDiagnosis
	{
		Ok, ///< opened, exclusive or shared, at the requested bitrate
		OkSharedChannel, ///< opened while another application holds the channel
		OkWrongBitrate, ///< opened, but the negotiated bitrate is not 250 kbit/s
		FailedChannelAbsent, ///< no such channel on this machine
		FailedChannelOccupied, ///< another application holds it and we could not join
		FailedOther ///< opening failed for a reason other than the channel being absent or occupied
	};

	/// @brief Classifies the result of a `CAN_Initialize` call, together with the channel condition
	/// and negotiated bitrate read immediately afterward, into an operator-actionable diagnosis.
	/// @details Branches on `condition` (`PCAN_CHANNEL_CONDITION`) rather than on `status` to decide
	/// whether a failed open was a sharing conflict, because the condition is a value this plugin
	/// can read and confirm, not one it must infer from a status code.
	/// @attention This function is `inline` and defined here, not in the .cpp, for the same reason
	/// as `pcan_decode_frame`: a unit test exercises it with synthetic status/condition/bitrate
	/// values and no link dependency on the PCAN import library.
	/// @param[in] status The status `CAN_Initialize` returned
	/// @param[in] condition The channel's `PCAN_CHANNEL_CONDITION` value, read after the initialize
	/// call regardless of whether it succeeded
	/// @param[in] busSpeed The channel's negotiated `PCAN_BUSSPEED_NOMINAL` value, in bits per
	/// second, read only when `status` reported success. Zero means the diagnostic read itself
	/// failed -- a `CAN_GetValue` error leaves its output buffer unwritten -- and must not be read
	/// as a bitrate mismatch: reporting a wrong bitrate on the strength of a failed read would be
	/// worse than reporting nothing.
	/// @returns The `PcanOpenDiagnosis` classifying this open
	inline PcanOpenDiagnosis diagnose_pcan_open(TPCANStatus status, DWORD condition, DWORD busSpeed)
	{
		if (pcan_open_succeeded(status))
		{
			// A wrong bitrate outranks a shared-channel report: it is the more serious fact, and a
			// channel can be simultaneously shared and mismatched, in which case the operator needs
			// to hear about the bitrate first.
			if ((0U != busSpeed) && (250000U != busSpeed))
			{
				return PcanOpenDiagnosis::OkWrongBitrate;
			}
			// PCAN_CHANNEL_CONDITION is a bitfield, not an enumeration -- PCAN_CHANNEL_PCANVIEW is
			// literally defined as (PCAN_CHANNEL_AVAILABLE | PCAN_CHANNEL_OCCUPIED). Testing it with
			// == is the same defect shape carry 0096 fixed for TPCANMsg::MSGTYPE: a future PEAK bit
			// added to either value would silently stop matching an equality test. Testing the
			// PCAN_CHANNEL_OCCUPIED bit covers PCAN_CHANNEL_OCCUPIED and PCAN_CHANNEL_PCANVIEW alike,
			// since both carry it.
			if (0U != (condition & PCAN_CHANNEL_OCCUPIED))
			{
				return PcanOpenDiagnosis::OkSharedChannel;
			}
			return PcanOpenDiagnosis::Ok;
		}

		// PCAN_CHANNEL_UNAVAILABLE is 0, the only condition value with no bits set, so unlike
		// PCAN_CHANNEL_OCCUPIED this one cannot be expressed as a mask test and stays an equality
		// comparison.
		if (PCAN_CHANNEL_UNAVAILABLE == condition)
		{
			return PcanOpenDiagnosis::FailedChannelAbsent;
		}
		if (0U != (condition & PCAN_CHANNEL_OCCUPIED))
		{
			return PcanOpenDiagnosis::FailedChannelOccupied;
		}
		// Includes PCAN_CHANNEL_AVAILABLE: the channel exists and is free, so a failure to open it
		// is something else entirely, not a sharing conflict and not an absent channel.
		return PcanOpenDiagnosis::FailedOther;
	}

	/// @brief Returns the operator-facing sentence for a `PcanOpenDiagnosis`.
	/// @details The occupied case names the fix, not the symptom, since that is the case this
	/// project has actually had to debug on a bench: a channel already held by PCAN-View.
	/// @param[in] diagnosis The diagnosis to describe
	/// @returns A non-null, non-empty, statically allocated string
	inline const char *pcan_open_diagnosis_text(PcanOpenDiagnosis diagnosis)
	{
		switch (diagnosis)
		{
			case PcanOpenDiagnosis::Ok:
				return "connected";
			case PcanOpenDiagnosis::OkSharedChannel:
				return "connected, sharing the channel with another application";
			case PcanOpenDiagnosis::OkWrongBitrate:
				return "connected, but the channel's negotiated bitrate is not 250 kbit/s";
			case PcanOpenDiagnosis::FailedChannelAbsent:
				return "no PCAN channel found at this handle; check the device is connected and its driver is installed";
			case PcanOpenDiagnosis::FailedChannelOccupied:
				return "channel is held by another application; close it, or if it is PCAN-View set its Connect dialog to 250 kbit/s";
			case PcanOpenDiagnosis::FailedOther:
				return "failed to open the channel for a reason other than sharing or absence";
		}
		return "unrecognized PCAN open diagnosis";
	}
}
#endif // PCAN_BASIC_WINDOWS_PLUGIN_HPP
