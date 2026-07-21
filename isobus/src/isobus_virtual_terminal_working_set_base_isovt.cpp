//================================================================================================
/// @file isobus_virtual_terminal_working_set_base_isovt.cpp
///
/// @brief Implements the fork's faulting-object parent resolution for the VT working set base: the
/// storage accessors for the faulting object's parent ID and the staging-tree scan that resolves it
/// (ISO 11783-6 C.2.5 bytes 3-4, the "Parent Object ID of faulty object" the End of Object Pool Fail
/// response carries).
///
/// This translation unit is isovt-owned and has no upstream counterpart. Per ADR-0008, the fork's
/// added VT code lives here rather than interleaved in isobus_virtual_terminal_working_set_base.cpp,
/// so that upstream file's merge surface does not grow. These definitions remain members of
/// VirtualTerminalWorkingSetBase, declared in the upstream header. The one-line resolver call at the
/// parse funnel and the mirrored reset in reset_object_pool_storage() are in-place edits and stay in
/// the upstream file, where a merge shows them against upstream's lines directly.
//================================================================================================

#include "isobus/isobus/isobus_virtual_terminal_working_set_base.hpp"

namespace isobus
{
	std::uint16_t VirtualTerminalWorkingSetBase::get_object_pool_faulting_parent_object_id()
	{
		const std::lock_guard<std::mutex> lock(managedWorkingSetMutex);
		return faultingParentObjectID;
	}

	void VirtualTerminalWorkingSetBase::set_object_pool_faulting_parent_object_id(std::uint16_t value)
	{
		const std::lock_guard<std::mutex> lock(managedWorkingSetMutex);
		faultingParentObjectID = value;
	}

	std::uint16_t VirtualTerminalWorkingSetBase::resolve_faulting_parent_object_id(std::uint16_t childObjectID) const
	{
		// vtObjectTree is read directly and without a lock: this runs on the parse worker thread, from
		// parse_next_object, and that worker owns the staging tree exclusively for the duration of a
		// parse. A run-time object pool update (ISO 11783-6 C.2.6) merges its new chunks into this same
		// tree on top of the pre-update pool, so a fault in an updated pool can resolve to a parent that
		// was declared in the pool before the update.
		//
		// std::map iterates in ascending key (object ID) order, so the first match is the lowest parent
		// ID -- the deterministic choice when an object is a child of more than one parent.
		for (const auto &entry : vtObjectTree)
		{
			const auto &object = entry.second;

			if (nullptr == object)
			{
				continue;
			}

			const std::uint16_t childCount = object->get_number_children();

			for (std::uint16_t i = 0; i < childCount; i++)
			{
				if (childObjectID == object->get_child_id(i))
				{
					return entry.first;
				}
			}
		}

		return NULL_OBJECT_ID;
	}
} // namespace isobus
