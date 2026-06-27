/**
 * @file filesystem.h
 * @brief In-memory filesystem (ramfs) — types, constants, and public API.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Layout: a single global linked list of `inode` records (`inode_list_head`)
 * keyed by monotonically-increasing `ino`. Tree shape is implicit — a
 * directory inode owns a chain of 4 KiB `block`s whose payload is an array
 * of `dirent` (inode_id + name) entries. Files use the same block chain to
 * store raw byte data.
 *
 * Internal API is stateless and inode-keyed: every function except
 * #fs_lookup takes inode numbers, not paths. Path resolution lives in
 * #fs_lookup only.
 *
 * Storage backing comes from #kmalloc, which is a bump allocator. There is
 * no reclamation: #fs_unlink removes a dirent but does not free the
 * unreferenced inode or its blocks. Will be addressed when a slab/free-list
 * allocator replaces the bump.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef RASPBERRY_PI_OPERATING_SYSTEM_FILESYSTEM_H
#define RASPBERRY_PI_OPERATING_SYSTEM_FILESYSTEM_H
#include <stdint.h>


typedef struct block block_t;
struct inode;

/** Size of one storage block, in bytes. Matches `PAGE_SIZE` so each block
 *  consumes exactly one heap-allocated chunk. */
constexpr uint64_t BLOCK_SIZE = 4096;

/** Maximum filename length in characters (not counting the null terminator). */
constexpr uint64_t MAX_SIZE = 255;

/** Sentinel for "no such inode" / failed lookup. */
constexpr uint64_t INVALID_INO = 0;

/** Inode number of the root directory. Allocated by #fs_init. */
constexpr uint64_t ROOT_INO = 1;

/** Head of the global inode list. `inline` so the header can be included
 *  from multiple translation units without multiple-definition errors. */
inline inode* inode_list_head = nullptr;

/**
 * @brief Storage block — link pointer plus a raw byte payload.
 *
 * Blocks are chained singly via `next`. Used both as the directory-entry
 * array for directories (`data` reinterpreted as `dirent[]`) and as raw
 * file content for regular files.
 */
struct block {
	block_t* next;
	uint8_t data[BLOCK_SIZE - sizeof(block_t*)];
};

/**
 * @brief Filesystem object — represents one file or directory.
 *
 * `inode_size` is bytes for regular files, dirent count for directories.
 * `inode_type` is 1 for file, 2 for directory.
 */
struct inode {
	uint64_t ino;
	uint8_t inode_type;
	uint64_t inode_size;
	block_t* first_block;
	inode* next;
};

/**
 * @brief Directory entry — name and the inode it resolves to.
 *
 * `inode_id` is placed first so the struct's alignment is governed by the
 * 8-byte field rather than the trailing `char[]`. 264 bytes total; 15 fit
 * per 4096-byte block payload.
 */
struct dirent {
	uint64_t inode_id;
	char name[MAX_SIZE + 1];
};

// 15 dirents must fit in one block (BLOCK_SIZE - sizeof(block_t*) = 4088)
// Thus each dirent must be 264 bytes.
static_assert(sizeof(dirent) == 264);

// ====== Public API ======

/**
 * @brief Initialize the filesystem and allocate the root directory.
 *
 * Allocates inode #1 as an empty directory and installs it at the head of
 * `inode_list_head`. Must be called after #kheap_init.
 */
void fs_init();

/**
 * @brief Resolve an absolute path to an inode number.
 *
 * Walks `/`-separated path components from the root. The single-character
 * path `"/"` resolves to #ROOT_INO.
 *
 * @param path Null-terminated absolute path.
 * @return Inode number on success; #INVALID_INO if any component is missing
 *         or a non-final component is not a directory.
 */
uint64_t fs_lookup(const char* path);

/**
 * @brief Create a new file or directory under @p parent_ino.
 *
 * Allocates a fresh inode, links it into the global list, and appends a
 * dirent into @p parent_ino's block chain (growing the chain by one block
 * if the current last block is full).
 *
 * @param parent_ino Inode number of an existing directory.
 * @param name       Null-terminated name; must be non-empty and shorter
 *                   than #MAX_SIZE.
 * @param type       1 for regular file, 2 for directory.
 * @return New inode number on success; #INVALID_INO if the parent isn't a
 *         directory, the name is invalid or duplicate, or allocation fails.
 */
uint64_t fs_create(uint64_t parent_ino, const char* name, uint8_t type);

/**
 * @brief Remove the dirent named @p name from directory @p parent_ino.
 *
 * Uses the swap-last trick (overwrite the removed dirent with the last
 * dirent, then decrement count) for O(1) removal — order is not preserved.
 * Refuses to unlink a non-empty directory. The unreferenced inode is left
 * in the global list (no reclamation under the bump allocator).
 *
 * @param parent_ino Directory inode containing the entry.
 * @param name       Null-terminated entry name to remove.
 * @return `0` on success; `-1` if the parent is not a directory, the name
 *         is not found, or the target is a non-empty directory.
 */
int fs_unlink(uint64_t parent_ino, const char* name);

/**
 * @brief Read up to @p len bytes from file @p ino starting at @p offset.
 *
 * Reads past EOF return `0` (not an error). Partial reads at EOF return
 * the number of bytes actually copied.
 *
 * @param ino    Inode number of a regular file.
 * @param offset Byte offset within the file.
 * @param len    Maximum number of bytes to copy.
 * @param buf    Destination buffer; must not be `nullptr`.
 * @return Bytes copied, or `-1` on error (no such inode, not a file, or
 *         `buf` is `nullptr`).
 */
int64_t fs_read(uint64_t ino, uint64_t offset, uint64_t len, void* buf);

/**
 * @brief Write @p len bytes from @p buf into file @p ino starting at @p offset.
 *
 * Grows the file (appends blocks) as needed to cover `offset + len`.
 * Updates `inode_size` if the write extends the end of the file.
 *
 * @param ino    Inode number of a regular file.
 * @param offset Byte offset to start writing at.
 * @param len    Number of bytes to write.
 * @param buf    Source buffer; must not be `nullptr`.
 * @return Bytes written, or `-1` on error.
 */
int64_t fs_write(uint64_t ino, uint64_t offset, uint64_t len, void* buf);

/**
 * @brief Copy the @p index-th dirent of directory @p dir_ino into @p *out.
 *
 * Intended for iteration:
 * `for (i = 0; fs_readdir(d, i, &ent) == 1; i++) { ... }`.
 *
 * @param dir_ino Directory inode to enumerate.
 * @param index   Zero-based dirent index.
 * @param out     Destination dirent; must not be `nullptr`.
 * @return `1` if a dirent was copied, `0` if @p index is past the end,
 *         `-1` on error.
 */
int64_t fs_readdir(uint64_t dir_ino, uint64_t index, dirent* out);

/**
 * @brief Return the size and type of an inode.
 *
 * Either out-pointer may be `nullptr` to skip that field.
 *
 * @param ino       Inode number to query.
 * @param size_out  Optional: receives `inode_size`.
 * @param type_out  Optional: receives `inode_type`.
 * @return `0` on success, `-1` if @p ino does not exist.
 */
int fs_stat(uint64_t ino, uint64_t* size_out, uint8_t* type_out);

// ====== Internal helpers (exposed for testing / shell use) ======

/**
 * @brief Compare `path[start..end)` against a null-terminated @p name.
 *
 * @return `true` iff the substring and @p name are byte-equal *and* the
 *         substring length matches `strlen(name)`.
 */
bool name_matches(const char* path, uint64_t start, uint64_t end, const char* name);

/** @brief Byte-equality test for two null-terminated strings. */
bool name_matches_single(const char* name1, const char* name2);

/**
 * @brief Parse a `/`-separated path into segment end-indices.
 *
 * For input `"/foo/bar"`, writes `[4, 8]` into @p ends and returns `2`.
 * The single-character `"/"` returns `0` (no segments).
 *
 * @param path         Null-terminated absolute path.
 * @param ends         Output array; receives the index *past* each segment.
 * @param max_segments Capacity of @p ends.
 * @return Number of segments written (0 to @p max_segments).
 */
uint64_t parse_path(const char* path, uint64_t* ends, uint64_t max_segments);

/** @brief Linear search of `inode_list_head` for the inode with the given number. */
inode* find_inode(uint64_t ino);

/**
 * @brief Return a pointer to the @p idx-th dirent in directory @p dir.
 *
 * Walks the block chain to the right block and indexes the dirent array.
 *
 * @return Pointer into the directory's blocks, or `nullptr` if @p idx is
 *         out of range or the chain ends early.
 */
dirent* dirent_at(const inode *dir, uint64_t idx);

/**
 * @brief Append one fresh block to the end of @p inode's chain.
 *
 * If `first_block` is null, installs the new block as the head; otherwise
 * walks to the tail and links it on.
 */
void append_block(inode* inode);

/** @brief Stub. Currently unused; returns `false`. */
bool name_copy(inode* dst, inode* src);

/** @brief `(a < b) ? a : b`. */
uint64_t min(uint64_t a, uint64_t b);

#endif //RASPBERRY_PI_OPERATING_SYSTEM_FILESYSTEM_H
