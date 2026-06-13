/**
 * @file filesystem.cpp
 * @brief In-memory filesystem (ramfs) implementation.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * See `<filesystem.h>` for the type and API contracts; this file documents
 * implementation details, invariants, and known pitfalls. The filesystem
 * is single-threaded and lives entirely in the kernel heap. There is no
 * journaling, no caching layer, and no concurrency control.
 *
 * Implementation notes:
 *   - `strlen` and `memcpy` are provided as `extern "C"` symbols at the
 *     bottom of this file. At `-O0`, `__builtin_strlen` emits a real
 *     `strlen` call and struct copies emit `memcpy`. These are the kernel's
 *     sole definitions.
 *   - In #fs_write, `current_block` MUST be captured *after* the block-growth
 *     loop. For a new file, `first_block` is `nullptr` before #append_block
 *     runs; capturing it earlier writes through a null pointer offset 8,
 *     which on the Pi 3 lands silently in identity-mapped low RAM.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include "filesystem.h"

#include "heap_alloc.h"


/** Next inode number to hand out. Monotonic; never reused. */
static uint64_t next_ino = 1;

void fs_init() {
	const auto root = static_cast<inode *>(kmalloc(sizeof(inode)));
	root->ino = next_ino;
	root->inode_type = 2;     // directory
	root->inode_size = 0;
	root->first_block = nullptr;
	root->next = inode_list_head;
	inode_list_head = root;

	next_ino++;
}

uint64_t fs_lookup(const char* path) {
	if (path[0] == '\0') return INVALID_INO;

	// Decompose the path into segment end-indices. `start` walks the
	// beginning index of the current segment.
	uint64_t start = 1;
	uint64_t ends[16];
	const uint64_t n = parse_path(path, ends, 16);

	if (n == 0) return ROOT_INO; // parse_path returns 0 for the bare "/".

	const inode* curr = find_inode(ROOT_INO);
	if (!curr) return INVALID_INO;

	// Walk one segment at a time, hopping inodes through dirents.
	for (uint64_t i = 0; i < n; i++) {
		if (curr->inode_type != 2) return INVALID_INO; // not a directory

		bool found = false;
		for (uint64_t j = 0; j < curr->inode_size; j++) {

			const dirent* d = dirent_at(curr, j);
			if (!d) return INVALID_INO;

			if (name_matches(path, start, ends[i], d->name)) {
				curr = find_inode(d->inode_id);
				start = ends[i] + 1;
				found = true;
				break;
			}
		}
		if (!found) return INVALID_INO;
	}
	return curr->ino;
}

uint64_t fs_create(uint64_t parent_ino, const char* name, const uint8_t type) {
	inode *parent = find_inode(parent_ino);
	if (!parent) return INVALID_INO;
	if (parent->inode_type != 2) return INVALID_INO;

	// Validate name: must be non-empty and short enough to fit a dirent.
	if (__builtin_strlen(name) >= MAX_SIZE || name[0] == '\0') return INVALID_INO;

	const uint64_t dirents_per_block = sizeof(block::data) / sizeof(dirent);

	// Reject duplicates.
	for (int i = 0; i < parent->inode_size; i++) {
		const dirent* d = dirent_at(parent, i);
		if (!d) return INVALID_INO;
		if (name_matches_single(d->name, name)) return INVALID_INO;
	}

	// Allocate and initialize the new inode.
	inode* new_inode = static_cast<inode *>(kmalloc(sizeof(inode)));
	new_inode->ino = next_ino++;
	new_inode->inode_type = type;
	new_inode->inode_size = 0;
	new_inode->first_block = nullptr;
	new_inode->next = inode_list_head;
	inode_list_head = new_inode;

	// Grow the parent's block chain if the current last block is full.
	if (parent->inode_size % dirents_per_block == 0) {
		append_block(parent);
	}

	const uint64_t block_index = parent->inode_size / dirents_per_block;
	const uint64_t slot = parent->inode_size % dirents_per_block;

	block_t* curr_block = parent->first_block;
	for (uint64_t j = 0; j < block_index; j++) {
		if (curr_block) curr_block = curr_block->next;
		else break;
	}

	// Write the dirent into the parent's tail block.
	parent->inode_size++;
	dirent *new_dirent = &reinterpret_cast<dirent*>(curr_block->data)[slot];
	new_dirent->inode_id = new_inode->ino;

	int i = 0;
	while (name[i] != '\0') {
		new_dirent->name[i] = name[i];
		i++;
	}
	new_dirent->name[i] = '\0';

	return new_inode->ino;
}

int fs_unlink(const uint64_t parent_ino, const char *name) {
	inode* parent = find_inode(parent_ino);
	if (!parent) return -1;
	if (parent->inode_type != 2 || parent->inode_size == 0) return -1;

	// Find the dirent. `subject_dirent` is the last-iterated pointer when
	// the loop exits; the post-loop check below confirms it actually
	// matched.
	dirent* subject_dirent = nullptr;
	for (int i = 0; i < parent->inode_size; i++) {
		subject_dirent = dirent_at(parent, i);
		if (!subject_dirent) return -1;
		if (name_matches_single(subject_dirent->name, name)) break;
	}

	if (!subject_dirent || !name_matches_single(subject_dirent->name, name)) return -1;

	dirent* last_dirent = dirent_at(parent, parent->inode_size - 1);
	if (!last_dirent) return -1;

	// Refuse to unlink a non-empty directory.
	inode* subject_inode = find_inode(subject_dirent->inode_id);
	if (!subject_inode || (subject_inode->inode_type == 2 && subject_inode->inode_size != 0)) return -1;

	// Fast path: removing the tail dirent. Just decrement.
	if (subject_dirent == last_dirent) {
		parent->inode_size--;
		return 0;
	}

	// Swap-last: overwrite the removed slot with the last slot, then
	// shrink. Order within a directory is not preserved.
	*subject_dirent = *last_dirent;
	parent->inode_size--;
	return 0;
}

int64_t fs_read(const uint64_t ino, const uint64_t offset, uint64_t len, void *buf) {
	uint8_t* dst = static_cast<uint8_t *>(buf);

	if (!buf) return -1;

	const uint64_t block_index = offset / sizeof(block::data);
	const uint64_t byte_offset = offset % sizeof(block::data);

	const inode* subject = find_inode(ino);
	if (!subject) return -1;
	if (subject->inode_type != 1) return -1;

	// Walk to the block that contains the start offset.
	const block_t* current_block = subject->first_block;
	for (int i = 0; i < block_index; i++) {
		if (current_block) current_block = current_block->next;
		else return -1;
	}

	if (offset >= subject->inode_size) return 0; // Reading at/past EOF.

	// Clamp `len` so we never read past EOF.
	if (offset + len > subject->inode_size) {
		len = subject->inode_size - offset;
	}

	uint64_t bytes_copied = 0;
	uint64_t block_offset = byte_offset;

	while (bytes_copied < len) {
		const uint64_t bytes_left_in_block = sizeof(block::data) - block_offset;
		const uint64_t chunk = min(len - bytes_copied, bytes_left_in_block);

		for (int i = 0; i < chunk; i++) {
			dst[bytes_copied + i] = current_block->data[block_offset + i];
		}

		bytes_copied += chunk;
		block_offset = 0;

		current_block = current_block->next;
	}
	return len;
}

int64_t fs_write(uint64_t ino, uint64_t offset, uint64_t len, void *buf) {
	if (!buf) return -1;
	const uint8_t* src = static_cast<uint8_t*>(buf);

	inode* subject = find_inode(ino);
	if (!subject) return -1;
	if (subject->inode_type != 1) return -1;

	const uint64_t block_index = offset / sizeof(block::data);
	const uint64_t byte_offset = offset % sizeof(block::data);

	// Count existing blocks to compute current capacity.
	uint64_t curr_inode_blocks = 0;
	block_t* pointer = subject->first_block;
	while (pointer) {
		curr_inode_blocks++;
		pointer = pointer->next;
	}

	uint64_t remaining_space = sizeof(block::data) * curr_inode_blocks;

	// Grow the chain until capacity covers `offset + len`. NOTE: we must
	// not capture `subject->first_block` into a local until *after* this
	// loop — for a new file, the head pointer changes during growth.
	if (offset + len > remaining_space) {
		uint64_t added_blocks = 0;
		while (offset + len > remaining_space) {
			append_block(subject);
			added_blocks++;
			remaining_space = sizeof(block::data) * (curr_inode_blocks + added_blocks);
		}
	}
	block_t* current_block = subject->first_block;

	for (int i = 0; i < block_index; i++) {
		if (current_block) current_block = current_block->next;
		else return -1;
	}

	uint64_t bytes_written = 0;
	uint64_t block_offset = byte_offset;

	while (bytes_written < len) {
		const uint64_t bytes_left_in_block = sizeof(block::data) - block_offset;
		const uint64_t chunk = min(len - bytes_written, bytes_left_in_block);

		for (int i = 0; i < chunk; i++) {
			current_block->data[block_offset + i] = src[bytes_written + i];
		}

		bytes_written += chunk;
		block_offset = 0;

		current_block = current_block->next;
	}

	if (offset + len > subject->inode_size) {
		subject->inode_size = offset+len;
	}
	return len;
}

int64_t fs_readdir(const uint64_t dir_ino, const uint64_t index, dirent *out) {
	inode* subject = find_inode(dir_ino);
	if (!subject) return -1;
	if (subject->inode_type != 2) return -1;
	if (!out) return -1;
	if (index >= subject->inode_size) return 0;

	dirent* subject_dirent = dirent_at(subject, index);
	if (!subject_dirent) return -1;

	*out = *subject_dirent;

	return 1;
}

int fs_stat(const uint64_t ino, uint64_t* size_out, uint8_t* type_out) {
	const inode* n = find_inode(ino);
	if (!n) return -1;
	if (size_out) *size_out = n->inode_size;
	if (type_out) *type_out = n->inode_type;
	return 0;
}

// ====== Helper implementations ======

bool name_matches(const char* path, const uint64_t start,
	const uint64_t end, const char* name) {
	for (uint64_t i = 0; i < end - start; i++) {
		if (path[start + i] != name[i]) return false;
		if (name[i] == '\0') return false; // `name` ended before the substring did
	}
	return name[end-start] == '\0';
}

bool name_matches_single(const char *name1, const char *name2) {
	uint64_t i = 0;

	if (__builtin_strlen(name1) != __builtin_strlen(name2)) return false;

	while (name1[i] != '\0' || name2[i] != '\0') {
		if (name1[i] != name2[i]) return false;
		i++;
	}

	return true;
}

uint64_t parse_path(const char* path, uint64_t* ends,
                    const uint64_t max_segments) {
	uint64_t count = 0;
	uint64_t i = 0;

	// Special-case the bare root so fs_lookup("/") can short-circuit to
	// ROOT_INO without seeing a zero-length segment.
	if (__builtin_strlen(path) == 1 && path[0] == '/') return 0;

	while (path[i] != '\0' && count < max_segments) {
		// Skip the leading '/', then scan to the next '/' or end of string.
		i++;
		while (path[i] != '\0' && path[i] != '/') {
			i++;
		}
		ends[count++] = i;
	}

	return count;
}

inode* find_inode(const uint64_t ino) {
	inode* curr = inode_list_head;
	while (curr != nullptr) {
		if (curr->ino == ino) return curr;
		curr = curr->next;
	}
	return nullptr;
}

dirent* dirent_at(const inode *dir, const uint64_t idx) {
	if (idx >= dir->inode_size) return nullptr;

	// Currently 15 dirents per block; this is resilient to layout changes.
	const uint64_t dirents_per_block = sizeof(block::data) / sizeof(dirent);

	const uint64_t block_index = idx / dirents_per_block;
	const uint64_t slot = idx % dirents_per_block;

	block_t* curr_block = dir->first_block;
	if (!curr_block) return nullptr;

	for (uint64_t i = 0; i < block_index; i++) {
		if (curr_block) curr_block = curr_block->next;
		else return nullptr;
	}

	return &reinterpret_cast<dirent *>(curr_block->data)[slot];
}

void append_block(inode* inode) {
	if (!inode->first_block) {
		inode->first_block = static_cast<block *>(kmalloc(BLOCK_SIZE));
		return;
	}
	block_t* curr_block = inode->first_block;
	while (curr_block->next) {
		curr_block = curr_block->next;
	}
	curr_block->next = static_cast<block *>(kmalloc(BLOCK_SIZE));
}

bool name_copy(const char* path, uint64_t start, uint64_t end, const char* name) {
	return false; // stub — unused
}

uint64_t min(const uint64_t a, const uint64_t b) {
	return a < b ? a : b;
}

// ====== Freestanding libc shims ======
//
// At -O0, GCC emits real `strlen` and `memcpy` calls for `__builtin_strlen`
// and struct-copy expressions. We aren't linking libc, so the kernel
// provides its own definitions. Marked extern "C" so the symbol names
// match the compiler's call sites.
extern "C" {
	uint64_t strlen(const char* s) {
		uint64_t len = 0;
		while (s[len]) len++;
		return len;
	}

	void* memcpy(void* dst, const void* src, uint64_t n) {
		uint8_t* d = static_cast<uint8_t*>(dst);
		const uint8_t* s = static_cast<const uint8_t*>(src);
		for (uint64_t i = 0; i < n; i++) d[i] = s[i];
		return dst;
	}
}
