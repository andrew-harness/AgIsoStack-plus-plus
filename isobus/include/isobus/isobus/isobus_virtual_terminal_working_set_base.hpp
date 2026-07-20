//================================================================================================
/// @file isobus_virtual_terminal_working_set_base.hpp
///
/// @brief A base class for a VT working set that isolates common working set functionality
/// so that things useful to VT designer application and a VT server application can be shared.
/// @author Adrian Del Grosso
///
/// @copyright 2024 The Open-Agriculture Developers
//================================================================================================
#ifndef ISOBUS_VIRTUAL_TERMINAL_WORKING_SET_BASE_HPP
#define ISOBUS_VIRTUAL_TERMINAL_WORKING_SET_BASE_HPP

#include "isobus/isobus/isobus_virtual_terminal_objects.hpp"

#include <map>
#include <memory>
#include <mutex>

namespace isobus
{
	/// @brief The deserialized object pool: every parsed VT object of one working set, keyed by object ID
	using ObjectTree = std::map<std::uint16_t, std::shared_ptr<VTObject>>;

	/// @brief A base class for a VT working set that isolates common working set functionality
	/// so that things useful to VT designer application and a VT server application can be shared.
	class VirtualTerminalWorkingSetBase
	{
	public:
		/// @brief Takes a raw block of IOP data and parses it into VT objects
		/// @param[in] iopData A pointer to the raw IOP data
		/// @param[in] iopLength The length of the raw IOP data
		/// @returns true if the IOP data was parsed successfully, otherwise false
		bool parse_iop_into_objects(std::uint8_t *iopData, std::uint32_t iopLength);

		/// @brief Returns a colour from this working set's current colour table, by index
		/// @param[in] colourIndex The index into the VT's colour table to retrieve
		/// @returns A colour from this working set's current colour table, by index
		VTColourVector get_colour(std::uint8_t colourIndex) const;

		/// @brief Returns an immutable snapshot of the working set's object tree
		/// @details The snapshot is never null and is never modified once handed out, so the caller's
		/// copy stays valid and self-consistent however the pool changes afterwards. That is what makes
		/// the tree readable from a thread other than the one parsing it: the parse worker builds into a
		/// staging tree it owns alone and swaps a finished tree in as a whole, so no reader ever
		/// traverses a map another thread is inserting into, and no reader ever sees a half-built pool.
		/// Taking a snapshot costs one mutex acquisition and one reference count increment and allocates
		/// nothing, so it is safe to call on a drawing path.
		/// @returns An immutable snapshot of the working set's object tree
		std::shared_ptr<const ObjectTree> get_object_tree() const;

		/// @brief Publishes the staging object tree as the snapshot that get_object_tree() hands out
		/// @details Call this once a pool has been parsed through to completion -- never per object, and
		/// never between the chunks of a single pool -- because everything the snapshot is for rests on
		/// it changing only as a whole. The VT server's parse worker calls this itself once all of a
		/// pool's chunks have parsed. It is public for code that drives parse_iop_into_objects()
		/// directly, such as an offline pool loader, which otherwise parses into a staging tree that
		/// nothing ever reads.
		void publish_object_tree();

		/// @brief Returns a VT object from the published object tree by object ID
		/// @details The lookup does not modify the object tree: an ID that is absent, that is
		/// NULL_OBJECT_ID, or whose entry holds a null pointer yields an empty shared pointer and
		/// leaves the tree untouched. That matters because object IDs reach here straight off the
		/// bus in the VT server's command handlers. The lookup reads the published snapshot rather
		/// than the staging tree, so a pool being parsed on the worker thread cannot be observed
		/// part-built.
		/// @param[in] objectID The object ID to retrieve from the object tree
		/// @returns A VT object from the object tree by object ID, or an empty shared pointer if not found
		std::shared_ptr<VTObject> get_object_by_id(std::uint16_t objectID);

		/// @brief Returns the working set object in the object pool, if one exists
		/// @returns The working set object in the object pool, if one exists, otherwise an empty shared pointer
		std::shared_ptr<VTObject> get_working_set_object();

		/// @brief Returns the object ID of the working set object itself
		/// @returns The object ID of the working set object, or NULL_OBJECT_ID if no pool has been parsed
		std::uint16_t get_working_set_object_id() const;

		/// @brief Appends raw IOP data to the working set's IOP file data
		/// @param[in] dataToAdd The raw IOP data to add to the working set
		void add_iop_raw_data(const std::vector<std::uint8_t> &dataToAdd);

		/// @brief Returns the number of discrete IOP file chunks that have been added to the object pool
		/// @returns The number of discrete IOP file chunks that have been added to the object pool
		std::size_t get_number_iop_files() const;

		/// @brief Returns IOP file data by index of IOP file
		/// @param[in] index The index of the IOP file to retrieve
		/// @returns The IOP file data by index of IOP file
		std::vector<std::uint8_t> &get_iop_raw_data(std::size_t index);

		/// @brief Returns the object ID of the the faulting object if parsing the object pool failed
		/// @returns The object ID of the faulting object if parsing the object pool failed
		std::uint16_t get_object_pool_faulting_object_id();

		/// @brief Checks the transferred object pool size against the memory the client declared in its Get Memory message
		/// @details Returns true unless the pool transferred more bytes than the declared size. A declared
		/// size of 0 means no Get Memory has set a budget, so the bound does not apply and this returns true.
		/// This is a size predicate only; a pool loaded from non-volatile memory is server-trusted and is
		/// exempted from the check by the caller, not here.
		/// @returns True if the transferred pool is within the declared memory, otherwise false
		bool is_object_pool_within_declared_iop_size() const;

	protected:
		/// @brief Publishes an empty object tree, so readers see no pool at all
		/// @details Holds the invariant that the published tree is non-empty only for a pool that
		/// parsed through to completion. The staging tree is left as it is, so a pool whose parse
		/// failed can still be retried by transferring the rest of it.
		void clear_published_object_tree();

		/// @brief Adds an object to the staging object tree, and replaces an object
		/// if there's already one in the tree with the same ID and the same object type.
		/// @details Writes the staging tree only. It deliberately does not publish: publishing per
		/// object would expose exactly the part-built pool the staging tree exists to hide.
		///
		/// Invariant: an object ID identifies one object of one type for the life of the pool. ISO
		/// 11783-6 clause 4.6.1.1 requires object IDs to be unique within a working set's object pool,
		/// and every parent that references an ID does so expecting a particular type -- the server
		/// itself downcasts the working set's own ID to a WorkingSet without re-checking. A replacement
		/// may change the object's contents and its record size, which is what a run-time object pool
		/// update does (clause C.2.6, whose example is lengthening a string object), but it may not
		/// change what the object is. An ID already in the tree under a different type is therefore
		/// rejected rather than overwritten.
		/// @param[in] objectToAdd The object to add to the object tree
		/// @returns true if the object was added or replaced, false if the object was null or its ID
		/// is already in the object pool under a different object type
		bool add_or_replace_object(std::shared_ptr<VTObject> objectToAdd);

		/// @brief Parses one object in the remaining object pool data
		/// @param[in,out] iopData A pointer to some object pool data
		/// @param[in,out] iopLength The number of bytes remaining in the object pool
		/// @returns true if an object was parsed
		bool parse_next_object(std::uint8_t *&iopData, std::uint32_t &iopLength);

		/// @brief Checks if the object pool contains an object with the supplied object ID
		/// @param[in] objectID The object ID to check for in the object pool
		/// @returns true if an object with the specified ID exists in the object pool
		bool get_object_id_exists(std::uint16_t objectID);

		/// @brief Returns the event ID from a byte. Does validation to ensure that the byte is valid.
		/// If the proprietary range or reserved range is used, it will be considered invalid and event 0 will be returned.
		/// @param[in] eventByte The byte to convert to an event ID
		/// @returns The event ID from a byte, or event 0 if the byte is invalid
		static EventID get_event_from_byte(std::uint8_t eventByte);

		/// @brief Sets the object ID associated with a faulting object during pool parsing
		/// @param[in] value The object ID to set as the faulting object
		void set_object_pool_faulting_object_id(std::uint16_t value);

		/// @brief Parses macro references from IOP data and updates the given object.
		/// Advances iopData and decrements iopLength accordingly.
		/// Note: On failure, iopData and iopLength may have been partially advanced.
		/// @param[in] object The IOP object which is currently parsed
		/// @param[in] numberOfMacrosToFollow The number of macro references deterined by parsing the IOP before the macros section.
		/// @param[in,out] iopData Pointer to the raw IOP data pointing to the start of the macro references of the object.
		/// After the successful parsing the pointer will be set to the next byte after the macro list.
		/// @param[in,out] iopLength The remaining IOP data length to be parsed, will be decremented with the parsed data count.
		/// @returns True if the macro reference parsing is successful otherwise returns false
		bool parse_object_macro_reference(std::shared_ptr<VTObject> object,
		                                  const std::uint8_t numberOfMacrosToFollow,
		                                  std::uint8_t *&iopData,
		                                  std::uint32_t &iopLength) const;

		/// @brief Consumes an object the VT parses for compatibility but does not functionally
		/// support, so the byte stream stays synchronized and the object pool is not rejected.
		/// @details ISO 11783-6 clause A.1.1 requires the VT to parse every object type even
		/// when it is not functionally supported; non-support is a rendering opt-out, never a
		/// parse failure. The object's serialized length is computed from its record layout
		/// (Annex B) and the pointer is advanced past it. The object is not added to the tree.
		/// Applies only to standard object types with a defined layout; a genuinely unknown
		/// type has no computable length and is still rejected.
		/// @param[in] type The object type being skipped
		/// @param[in] decodedID The object ID, for logging
		/// @param[in,out] iopData Pointer to the start of the object's record; advanced past it on success
		/// @param[in,out] iopLength Remaining IOP length; decremented by the object's length on success
		/// @returns True if the object was consumed, false if the data was too short to hold it
		bool parse_unsupported_object(VirtualTerminalObjectType type,
		                              std::uint16_t decodedID,
		                              std::uint8_t *&iopData,
		                              std::uint32_t &iopLength) const;

		std::mutex managedWorkingSetMutex; ///< A mutex to protect the interface of the managed working set
		mutable std::mutex objectTreeMutex; ///< Guards publishedObjectTree, the POINTER and nothing else. It is held only long enough to read or replace that pointer and never across a traversal of the tree it points at, so a reader can neither block the parse worker for any meaningful time nor deadlock against it.
		VTColourTable workingSetColourTable; ///< This working set's colour table
		std::uint32_t iopSize = 0; ///< Total size of the IOP in bytes
		std::uint32_t transferredIopSize = 0; ///< Total number of IOP bytes transferred
		ObjectTree vtObjectTree; ///< The staging tree: the C++ object representation (deserialized) of the object pool being managed. Written only by the pool parse worker, which owns it exclusively for the duration of a parse, and never read by another thread -- every other reader takes the published snapshot instead.
		std::shared_ptr<const ObjectTree> publishedObjectTree = std::make_shared<const ObjectTree>(); ///< The immutable snapshot readers see, swapped in whole by publish_object_tree(). Never null, so no reader has to null-check it; empty until a pool has parsed through to completion.
		std::vector<std::vector<std::uint8_t>> iopFilesRawData; ///< Raw IOP File data from the client
		std::size_t parsedIopFileCount = 0; ///< Count of iopFilesRawData chunks already parsed into the tree. A runtime object pool update (C.2.6) parses only the newer chunks so live objects -- and any runtime state on them -- are merged with, not rebuilt from, the authored bytes.
		std::uint16_t workingSetID = NULL_OBJECT_ID; ///< Stores the object ID of the working set object itself
		std::uint16_t faultingObjectID = NULL_OBJECT_ID; ///< Stores the faulting object ID to send to a client when parsing the pool fails
	};
} // namespace isobus
#endif // ISOBUS_VIRTUAL_TERMINAL_WORKING_SET_BASE_HPP
