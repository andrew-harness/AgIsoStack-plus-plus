//================================================================================================
/// @file vector_xl_plugin.cpp
///
/// @brief An interface for using a Vector CAN channel via the Vector XL Driver Library (vxlapi).
/// @attention Use of the Vector driver is governed in part by their license, and requires you
/// to install their driver first, which in-turn requires you to agree to their terms and conditions.
/// @author The Open-Agriculture Developers
///
/// @copyright 2026 The Open-Agriculture Developers
//================================================================================================

#include "isobus/hardware_integration/vector_xl_plugin.hpp"
#include "isobus/isobus/can_stack_logger.hpp"

#include <chrono>
#include <cstring>
#include <thread>

namespace isobus
{
	VectorXLWindowsPlugin::VectorXLWindowsPlugin(std::uint8_t channelIndex, std::uint32_t bitrate) :
	  channelIndex(channelIndex),
	  bitrate(bitrate)
	{
	}

	std::string VectorXLWindowsPlugin::get_name() const
	{
		return "Vector XL";
	}

	bool VectorXLWindowsPlugin::get_is_valid() const
	{
		return valid;
	}

	void VectorXLWindowsPlugin::close()
	{
		if (XL_INVALID_PORTHANDLE != portHandle)
		{
			xlDeactivateChannel(portHandle, channelMask);
			xlClosePort(portHandle);
			xlCloseDriver();
			portHandle = XL_INVALID_PORTHANDLE;
			valid = false;
		}
	}

	void VectorXLWindowsPlugin::open()
	{
		XLstatus status = xlOpenDriver();

		if (XL_SUCCESS != status)
		{
#ifndef DISABLE_CAN_STACK_LOGGER
			LOG_CRITICAL("[Vector XL]: Error opening the Vector XL driver: %s", xlGetErrorString(status));
#endif
			return;
		}

		XLdriverConfig driverConfig;
		status = xlGetDriverConfig(&driverConfig);

		if (XL_SUCCESS != status)
		{
#ifndef DISABLE_CAN_STACK_LOGGER
			LOG_CRITICAL("[Vector XL]: Error reading the Vector XL driver configuration: %s", xlGetErrorString(status));
#endif
			xlCloseDriver();
			return;
		}

		bool channelFound = false;
		std::uint8_t virtualChannelsSeen = 0;

		for (unsigned int i = 0; i < driverConfig.channelCount; i++)
		{
			if (XL_HWTYPE_VIRTUAL == driverConfig.channel[i].hwType)
			{
				if (virtualChannelsSeen == channelIndex)
				{
					channelMask = driverConfig.channel[i].channelMask;
					channelFound = true;
					break;
				}
				virtualChannelsSeen++;
			}
		}

		if (!channelFound)
		{
#ifndef DISABLE_CAN_STACK_LOGGER
			LOG_CRITICAL("[Vector XL]: Could not find a virtual CAN channel at index %u", static_cast<std::uint32_t>(channelIndex));
#endif
			xlCloseDriver();
			return;
		}

		permissionMask = channelMask;
		status = xlOpenPort(&portHandle, const_cast<char *>("AgIsoStack"), channelMask, &permissionMask, 256, XL_INTERFACE_VERSION, XL_BUS_TYPE_CAN);

		if ((XL_SUCCESS != status) || (XL_INVALID_PORTHANDLE == portHandle))
		{
#ifndef DISABLE_CAN_STACK_LOGGER
			LOG_CRITICAL("[Vector XL]: Error opening a port on the virtual CAN channel: %s", xlGetErrorString(status));
#endif
			portHandle = XL_INVALID_PORTHANDLE;
			xlCloseDriver();
			return;
		}

		// Only the port granted init access may set the bitrate, and a channel grants init access to
		// exactly one port. When another application already initialised the shared channel (the
		// virtual-bus case this driver targets), the permission mask comes back empty; the channel is
		// already configured, so skip the re-init rather than fail to connect as a second node.
		if (0 != permissionMask)
		{
			status = xlCanSetChannelBitrate(portHandle, permissionMask, bitrate);

			if (XL_SUCCESS != status)
			{
#ifndef DISABLE_CAN_STACK_LOGGER
				LOG_CRITICAL("[Vector XL]: Error setting the channel bitrate: %s", xlGetErrorString(status));
#endif
				xlClosePort(portHandle);
				portHandle = XL_INVALID_PORTHANDLE;
				xlCloseDriver();
				return;
			}
		}

		status = xlActivateChannel(portHandle, channelMask, XL_BUS_TYPE_CAN, XL_ACTIVATE_RESET_CLOCK);

		if (XL_SUCCESS != status)
		{
#ifndef DISABLE_CAN_STACK_LOGGER
			LOG_CRITICAL("[Vector XL]: Error activating the CAN channel: %s", xlGetErrorString(status));
#endif
			xlClosePort(portHandle);
			portHandle = XL_INVALID_PORTHANDLE;
			xlCloseDriver();
			return;
		}

		valid = true;
	}

	bool VectorXLWindowsPlugin::read_frame(isobus::CANMessageFrame &canFrame)
	{
		XLevent receiveEvent;
		unsigned int eventCount = 1;
		XLstatus status = xlReceive(portHandle, &eventCount, &receiveEvent);
		bool retVal = false;

		if (XL_SUCCESS == status)
		{
			// Received data frames, the driver's echo of this port's own transmissions, error frames
			// and remote frames all arrive as XL_RECEIVE_MSG events, distinguished only by the message
			// flags. Report only real inbound data frames; a flagged event is consumed but not
			// delivered (and does not sleep, so the queue keeps draining), so the stack never treats
			// its own transmissions as incoming traffic.
			if ((XL_RECEIVE_MSG == receiveEvent.tag) &&
			    (0 == (receiveEvent.tagData.msg.flags & (XL_CAN_MSG_FLAG_TX_COMPLETED | XL_CAN_MSG_FLAG_TX_REQUEST | XL_CAN_MSG_FLAG_ERROR_FRAME | XL_CAN_MSG_FLAG_REMOTE_FRAME))))
			{
				const s_xl_can_msg &receivedMessage = receiveEvent.tagData.msg;

				canFrame.identifier = receivedMessage.id & ~XL_CAN_EXT_MSG_ID;
				canFrame.isExtendedFrame = (0 != (receivedMessage.id & XL_CAN_EXT_MSG_ID));
				canFrame.dataLength = static_cast<std::uint8_t>(receivedMessage.dlc > MAX_MSG_LEN ? MAX_MSG_LEN : receivedMessage.dlc);
				memcpy(canFrame.data, receivedMessage.data, canFrame.dataLength);
				canFrame.timestamp_us = static_cast<std::uint64_t>(receiveEvent.timeStamp / 1000);
				retVal = true;
			}
		}
		else
		{
			// No event available (the receive queue is empty); avoid busy-spinning the receive thread.
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return retVal;
	}

	bool VectorXLWindowsPlugin::write_frame(const isobus::CANMessageFrame &canFrame)
	{
		XLevent transmitEvent;
		memset(&transmitEvent, 0, sizeof(transmitEvent));
		transmitEvent.tag = XL_TRANSMIT_MSG;

		s_xl_can_msg &transmitMessage = transmitEvent.tagData.msg;
		transmitMessage.id = canFrame.identifier | (canFrame.isExtendedFrame ? XL_CAN_EXT_MSG_ID : 0);
		transmitMessage.dlc = canFrame.dataLength;
		transmitMessage.flags = 0;
		memcpy(transmitMessage.data, canFrame.data, canFrame.dataLength);

		unsigned int messageCount = 1;
		XLstatus status = xlCanTransmit(portHandle, channelMask, &messageCount, &transmitEvent);

		return (XL_SUCCESS == status);
	}
}
