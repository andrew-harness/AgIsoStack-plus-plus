//================================================================================================
/// @file pcan_basic_windows_plugin.cpp
///
/// @brief An interface for using a PEAK PCAN device.
/// @attention Use of the PEAK driver is governed in part by their license, and requires you
/// to install their driver first, which in-turn requires you to agree to their terms and conditions.
/// @author Adrian Del Grosso
///
/// @copyright 2022 The Open-Agriculture Developers
//================================================================================================

#include "isobus/hardware_integration/pcan_basic_windows_plugin.hpp"
#include "isobus/isobus/can_stack_logger.hpp"

#include <thread>

namespace isobus
{
	PCANBasicWindowsPlugin::PCANBasicWindowsPlugin(WORD channel) :
	  handle(channel),
	  openResult(PCAN_ERROR_OK)
	{
	}

	PCANBasicWindowsPlugin::~PCANBasicWindowsPlugin()
	{
	}

	std::string PCANBasicWindowsPlugin::get_name() const
	{
		return "PEAK CAN";
	}

	bool PCANBasicWindowsPlugin::get_is_valid() const
	{
		// PCAN_ERROR_CAUTION means the channel connected successfully, with irregularities noted --
		// it is a working connection, not a failed one. This predicate gates the receive thread and
		// every transmit and receive call in CANHardwareInterface, so treating a caution open as
		// invalid would silently disable the whole transport despite CAN_Initialize having succeeded.
		return pcan_open_succeeded(openResult);
	}

	void PCANBasicWindowsPlugin::close()
	{
		CAN_Uninitialize(handle);
	}

	void PCANBasicWindowsPlugin::open()
	{
		// PCAN_BITRATE_ADAPTING must stay off: with it on, a mismatched open connects silently at
		// the other application's bitrate instead of refusing.
		openResult = CAN_Initialize(handle, PCAN_BAUD_250K);

#ifndef DISABLE_CAN_STACK_LOGGER
		// Every CAN_GetValue below is diagnostic, feeding only the log messages in this block: a
		// failed diagnostic read must never turn a successful open into a failure, and openResult,
		// on which get_is_valid() depends, is untouched by any of it.
		//
		// The two fallbacks below are NOT symmetric. For busSpeed, 0 is genuinely out of band -- no
		// real CAN bitrate is zero -- so diagnose_pcan_open reads it as "not measured" rather than a
		// mismatch. For channelCondition, 0 IS a valid, meaningful result: it is
		// PCAN_CHANNEL_UNAVAILABLE. Falling back to 0 on a failed read would report the single most
		// emphatic answer in PcanOpenDiagnosis -- "no such channel exists" -- purely because a
		// diagnostic read returned nothing. PCAN_CONDITION_UNREAD is the value that says "not
		// measured" without aliasing any real condition, so a failed read lands in
		// PcanOpenDiagnosis::FailedOther, the honest answer. Its two defining properties are stated
		// and pinned where it is declared.
		DWORD channelCondition = PCAN_CONDITION_UNREAD;
		if (PCAN_ERROR_OK != CAN_GetValue(handle, PCAN_CHANNEL_CONDITION, &channelCondition, sizeof(channelCondition)))
		{
			channelCondition = PCAN_CONDITION_UNREAD;
		}

		DWORD busSpeed = 0;
		if (pcan_open_succeeded(openResult) &&
		    (PCAN_ERROR_OK != CAN_GetValue(handle, PCAN_BUSSPEED_NOMINAL, &busSpeed, sizeof(busSpeed))))
		{
			busSpeed = 0;
		}

		PcanOpenDiagnosis diagnosis = diagnose_pcan_open(openResult, channelCondition, busSpeed);

		switch (diagnosis)
		{
			case PcanOpenDiagnosis::Ok:
			{
				if (0 != busSpeed)
				{
					LOG_INFO("[PCAN]: Connected, running at %lu bit/s.", static_cast<unsigned long>(busSpeed));
				}
				else
				{
					LOG_INFO("[PCAN]: Connected.");
				}
			}
			break;

			case PcanOpenDiagnosis::OkSharedChannel:
			case PcanOpenDiagnosis::OkWrongBitrate:
			{
				if (0 != busSpeed)
				{
					LOG_WARNING("[PCAN]: %s. Running at %lu bit/s.", pcan_open_diagnosis_text(diagnosis), static_cast<unsigned long>(busSpeed));
				}
				else
				{
					LOG_WARNING("[PCAN]: %s", pcan_open_diagnosis_text(diagnosis));
				}
			}
			break;

			case PcanOpenDiagnosis::FailedChannelAbsent:
			case PcanOpenDiagnosis::FailedChannelOccupied:
			case PcanOpenDiagnosis::FailedOther:
			{
				// CAN_GetErrorText can itself fail (unsupported language, unrecognised code) and
				// leaves strMsg unwritten, so it is zero-initialized and only handed to the %s
				// conversion below when the call actually reported success.
				char strMsg[256] = { 0 };
				const char *errorText = (PCAN_ERROR_OK == CAN_GetErrorText(openResult, 0, strMsg)) ? strMsg : "";
				LOG_CRITICAL("[PCAN]: %s (PEAK error code: 0x%08X %s)", pcan_open_diagnosis_text(diagnosis), static_cast<unsigned int>(openResult), errorText);
			}
			break;
		}
#endif

		if (pcan_open_succeeded(openResult))
		{
			// Defensive only: PCAN_ALLOW_ECHO_FRAMES is per-client and already defaults to off, so
			// this guards only against a future driver default change and cannot affect what
			// PCAN-View or any other client sees on the same channel. Set after CAN_Initialize
			// succeeds, not before -- PEAK parameters other than PCAN_BITRATE_ADAPTING are
			// read/written against an already-initialized channel. The returned status is ignored:
			// an older API answers PCAN_ERROR_ILLPARAMTYPE for this parameter.
			DWORD allowEchoFrames = PCAN_PARAMETER_OFF;
			CAN_SetValue(handle, PCAN_ALLOW_ECHO_FRAMES, &allowEchoFrames, sizeof(allowEchoFrames));
		}
	}

	bool PCANBasicWindowsPlugin::read_frame(isobus::CANMessageFrame &canFrame)
	{
		TPCANStatus result;
		TPCANMsg CANMsg;
		TPCANTimestamp CANTimeStamp;
		bool retVal = false;

		result = CAN_Read(handle, &CANMsg, &CANTimeStamp);

		if (PCAN_ERROR_OK == result)
		{
			// A frame the decode refuses (error/status/RTR/FD, or an over-length LEN) is simply not
			// forwarded. There may be more frames already queued behind it, and sleeping here would
			// throttle the receive thread to 1000 frames/second on a bus emitting error frames -- the
			// sleep below is reserved for the empty-queue path.
			if (pcan_decode_frame(CANMsg, canFrame))
			{
				canFrame.timestamp_us = (CANTimeStamp.millis * 1000) + CANTimeStamp.micros;
				retVal = true;
			}
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return retVal;
	}

	bool PCANBasicWindowsPlugin::write_frame(const isobus::CANMessageFrame &canFrame)
	{
		TPCANStatus result;
		TPCANMsg msgCanMessage;

		msgCanMessage.ID = canFrame.identifier;
		msgCanMessage.LEN = canFrame.dataLength;
		msgCanMessage.MSGTYPE = canFrame.isExtendedFrame ? PCAN_MESSAGE_EXTENDED : PCAN_MESSAGE_STANDARD;
		memcpy(msgCanMessage.DATA, canFrame.data, canFrame.dataLength);

		result = CAN_Write(handle, &msgCanMessage);

		return (PCAN_ERROR_OK == result);
	}
}
