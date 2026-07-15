//================================================================================================
/// @file vector_xl_plugin.hpp
///
/// @brief An interface for using a Vector CAN channel via the Vector XL Driver Library (vxlapi).
/// @attention Use of the Vector driver is governed in part by their license, and requires you
/// to install their driver first, which in-turn requires you to agree to their terms and conditions.
/// @author The Open-Agriculture Developers
///
/// @copyright 2026 The Open-Agriculture Developers
//================================================================================================
#ifndef VECTOR_XL_PLUGIN_HPP
#define VECTOR_XL_PLUGIN_HPP

#include <cstdint>
#include <string>

// This needs to be included before vxlapi.h for the definition of the
// windows HANDLE type, which vxlapi.h uses (typedef HANDLE XLhandle)
#include <Windows.h>

#include "isobus/hardware_integration/can_hardware_plugin.hpp"
#include "isobus/hardware_integration/vxlapi.h"
#include "isobus/isobus/can_hardware_abstraction.hpp"
#include "isobus/isobus/can_message_frame.hpp"

namespace isobus
{
	//================================================================================================
	/// @class VectorXLWindowsPlugin
	///
	/// @brief A Windows CAN Driver for Vector devices using the Vector XL Driver Library
	//================================================================================================
	class VectorXLWindowsPlugin : public CANHardwarePlugin
	{
	public:
		/// @brief Constructor for the Windows version of the Vector XL Driver Library CAN driver
		/// @param[in] channelIndex The ordinal index of the VIRTUAL CAN channel to use (0 or 1)
		/// @param[in] bitrate The bitrate to use for the channel, in bit/s. Defaults to the ISOBUS 250 kbit/s.
		explicit VectorXLWindowsPlugin(std::uint8_t channelIndex = 0, std::uint32_t bitrate = 250000);

		/// @brief The destructor for VectorXLWindowsPlugin
		virtual ~VectorXLWindowsPlugin() = default;

		/// @brief Returns the displayable name of the plugin
		/// @returns Vector XL
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
		std::uint8_t channelIndex; ///< The ordinal index of the VIRTUAL CAN channel to use
		std::uint32_t bitrate; ///< The bitrate to use for the channel, in bit/s
		XLportHandle portHandle = XL_INVALID_PORTHANDLE; ///< The port handle as defined in the Vector XL driver API
		XLaccess channelMask = 0; ///< The access mask for the selected channel
		XLaccess permissionMask = 0; ///< The init-access (permission) mask returned when opening the port
		bool valid = false; ///< Stores if the connection is currently valid. Used for is_valid check later.
	};
}
#endif // VECTOR_XL_PLUGIN_HPP
