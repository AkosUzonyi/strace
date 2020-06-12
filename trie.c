/* Simple B-tree implementation for key-value mapping storage */

#include "defs.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "trie.h"

static const uint8_t ptr_sz_lg = (sizeof(uint64_t *) == 8 ? 6 : 5);

bool
trie_check(uint8_t item_size_lg, uint8_t ptr_block_size_lg,
	    uint8_t data_block_size_lg, uint8_t key_size)
{
	if (item_size_lg > 6)
		return false;
	if (key_size < 1 || key_size > 64)
		return false;
	if (ptr_block_size_lg < ptr_sz_lg || ptr_block_size_lg > PTR_BLOCK_SIZE_LG_MAX)
		return false;
	if (data_block_size_lg > DATA_BLOCK_SIZE_LG_MAX ||
	    data_block_size_lg < 6 ||
	    item_size_lg > data_block_size_lg)
		return false;

	return true;
}

void
trie_init(struct trie *b, uint8_t item_size_lg, uint8_t ptr_block_size_lg,
	   uint8_t data_block_size_lg, uint8_t key_size, uint64_t set_value)
{
	assert(trie_check(item_size_lg, ptr_block_size_lg, data_block_size_lg,
			   key_size));

	b->set_value = set_value;
	b->data = TRIE_UNSET;
	b->item_size_lg = item_size_lg;
	b->ptr_block_size_lg = ptr_block_size_lg;
	b->data_block_size_lg = data_block_size_lg;
	b->key_size = key_size;
}

static uint8_t
trie_get_depth(struct trie *b)
{
	return (b->key_size - (b->data_block_size_lg - b->item_size_lg) +
		b->ptr_block_size_lg - ptr_sz_lg - 1) / (b->ptr_block_size_lg - ptr_sz_lg);
}

/**
 * Returns lg2 of block size for the specific level of B-tree. If max_depth
 * provided is less than zero, it is calculated via trie_get_depth call.
 */
static uint8_t
trie_get_block_size(struct trie *b, uint8_t depth, int max_depth)
{
	if (max_depth < 0)
		max_depth = trie_get_depth(b);

	/* Last level contains data and we allow it having a different size */
	if (depth == max_depth)
		return b->data_block_size_lg;
	/* Last level of the tree can be smaller */
	if (depth == max_depth - 1)
		return (b->key_size -
			(b->data_block_size_lg - b->item_size_lg) - 1) %
			(b->ptr_block_size_lg - ptr_sz_lg) + 1 + ptr_sz_lg;

	return b->ptr_block_size_lg;
}

#define round_down(a, b) (((a) / (b)) * (b))

/**
 * Provides starting offset of bits in key corresponding to the block index
 * at the specific level.
 */
static uint8_t
trie_get_block_bit_offs(struct trie *b, uint8_t depth, int max_depth)
{
	uint8_t offs;

	if (max_depth < 0)
		max_depth = trie_get_depth(b);

	if (depth == max_depth)
		return 0;

	offs = b->data_block_size_lg - b->item_size_lg;

	if (depth == max_depth - 1)
		return offs;

	/* data_block_size + remainder */
	offs += trie_get_block_size(b, max_depth - 1, max_depth) - ptr_sz_lg;
	offs += (max_depth - depth - 2) * (b->ptr_block_size_lg - ptr_sz_lg);

	return offs;
}

struct trie *
trie_create(uint8_t item_size_lg, uint8_t ptr_block_size_lg,
	     uint8_t data_block_size_lg, uint8_t key_size, uint64_t set_value)
{
	struct trie *b;

	if (!trie_check(item_size_lg, ptr_block_size_lg, data_block_size_lg,
	    key_size))
		return NULL;

	b = malloc(sizeof(*b));
	if (!b)
		return NULL;

	trie_init(b, item_size_lg, ptr_block_size_lg, data_block_size_lg,
		   key_size, set_value);

	return b;
}

static uint64_t
trie_filler(uint64_t val, uint8_t item_size)
{
	val &= (1 << (1 << item_size)) - 1;

	for (; item_size < 6; item_size++)
		val |= val << (1 << item_size);

	return val;
}

static uint64_t *
trie_get_block(struct trie *b, uint64_t key, bool auto_create)
{
	void **cur_block = &(b->data);
	unsigned i;
	uint8_t cur_depth;
	uint8_t max_depth;
	uint8_t sz;

	if (b->key_size < 64 && key > (uint64_t) 1 << b->key_size)
		return NULL;

	max_depth = trie_get_depth(b);

	for (cur_depth = 0; cur_depth <= max_depth; cur_depth++) {
		sz = trie_get_block_size(b, cur_depth, max_depth);

		if (*cur_block == TRIE_SET || *cur_block == TRIE_UNSET) {
			void *old_val = *cur_block;

			if (!auto_create)
				return (uint64_t *) (*cur_block);

			*cur_block = xcalloc(1 << sz, 8);

			if (old_val == TRIE_SET) {
				uint64_t fill_value = cur_depth == max_depth ? b->set_value : (uintptr_t) TRIE_SET;
				uint8_t fill_size = cur_depth == max_depth ? b->item_size_lg : ptr_sz_lg;

				for (i = 0; i < ((unsigned int)1 << (sz - 3)); i++)
					((uint64_t *) *cur_block)[i] = trie_filler(fill_value, fill_size);
			}
		}

		if (cur_depth < max_depth) {
			size_t pos = (key >> trie_get_block_bit_offs(b,
				cur_depth, max_depth)) & ((1 << (sz - ptr_sz_lg)) - 1);

			cur_block = (((void **) (*cur_block)) + pos);
		}
	}

	return (uint64_t *) (*cur_block);
}

bool
trie_set(struct trie *b, uint64_t key, uint64_t val)
{
	uint64_t *data = trie_get_block(b, key, true);
	size_t mask = (1 << (b->data_block_size_lg - b->item_size_lg)) - 1;
	size_t pos = (key & mask) >> (6 - b->item_size_lg);

	if (!data)
		return false;

	if (b->item_size_lg == 6) {
		data[pos] = val;
	} else {
		size_t offs = (key & ((1 << (6 - b->item_size_lg)) - 1)) * (1 << b->item_size_lg);
		uint64_t mask = (((uint64_t) 1 << (1 << b->item_size_lg)) - 1) << offs;

		data[pos] &= ~mask;
		data[pos] |= (val << offs) & mask;
	}

	return true;
}

#if 0
int
trie_mask_set(struct trie *b, uint64_t key, uint8_t mask_bits)
{
}

/**
 * Sets to 0 all keys with 0-ed bits of mask equivalent to corresponding bits in
 * key.
 */
int
trie_mask_unset(struct trie *b, uint64_t key, uint8_t mask_bits)
{
}

int
trie_interval_set(struct trie *b, uint64_t begin, uint64_t end, uint64_t val)
{
}

uint64_t
trie_get_next_set_key(struct trie *b, uint64_t key)
{
}
#endif

static uint64_t
trie_data_block_get(struct trie *b, uint64_t *data, uint64_t key)
{
	size_t mask;
	size_t pos;
	size_t offs;

	if (!data)
		return 0;
	if ((void *) data == (void *) TRIE_SET)
		return b->set_value;

	mask = (1 << (b->data_block_size_lg - b->item_size_lg)) - 1;
	pos = (key & mask) >> (6 - b->item_size_lg);

	if (b->item_size_lg == 6)
		return data[pos];

	offs = (key & ((1 << (6 - b->item_size_lg)) - 1)) * (1 << b->item_size_lg);

	return (data[pos] >> offs) & (((uint64_t)1 << (1 << b->item_size_lg)) - 1);
}

uint64_t
trie_get(struct trie *b, uint64_t key)
{
	return trie_data_block_get(b, trie_get_block(b, key, false), key);
}

static uint64_t
trie_iterate_keys_block(struct trie *b, enum trie_iterate_flags flags,
				trie_iterate_fn fn, void *fn_data,
				uint64_t **block, uint64_t start, uint64_t end,
				uint8_t depth, uint8_t max_depth)
{
	if (start > end)
		return 0;

	if ((block == TRIE_SET && !(flags & TRIE_ITERATE_KEYS_SET)) ||
		(block == TRIE_UNSET && !(flags & TRIE_ITERATE_KEYS_UNSET)))
		return 0;

	if (block == TRIE_SET || block == TRIE_UNSET || depth == max_depth) {
		for (uint64_t i = start; i <= end; i++)
			fn(fn_data, i, trie_data_block_get(b, (uint64_t *) block, i));

		return end - start + 1; //TODO: overflow
	}

	uint8_t parent_block_bit_off = depth == 0 ? b->key_size : trie_get_block_bit_offs(b, depth - 1, max_depth);
	uint64_t first_key_in_block = start & (uint64_t) -1 << parent_block_bit_off;

	uint8_t block_bit_off = trie_get_block_bit_offs(b, depth, max_depth);
	uint8_t block_key_bits = parent_block_bit_off - block_bit_off;
	uint64_t mask = ((uint64_t) 1 << (block_key_bits)) - 1;
	uint64_t start_index = (start >> block_bit_off) & mask;
	uint64_t end_index = (end >> block_bit_off) & mask;
	uint64_t child_key_count = (uint64_t) 1 << block_bit_off;

	uint64_t count = 0;

	for (uint64_t i = start_index; i <= end_index; i++) {
		uint64_t child_start = first_key_in_block + i * child_key_count;
		uint64_t child_end = first_key_in_block + (i + 1) * child_key_count - 1;

		if (child_start < start)
			child_start = start;
		if (child_end > end)
			child_end = end;

		count += trie_iterate_keys_block(b, flags, fn, fn_data,
			(uint64_t **) block[i], child_start, child_end,
			depth + 1, max_depth);
	}

	return count;
}

uint64_t trie_iterate_keys(struct trie *b, uint64_t start, uint64_t end,
			    enum trie_iterate_flags flags, trie_iterate_fn fn,
			    void *fn_data)
{
	return trie_iterate_keys_block(b, flags, fn, fn_data, b->data,
		start, end, 0, trie_get_depth(b));
}

void
trie_free_block(struct trie *b, uint64_t **block, uint8_t depth,
		 int max_depth)
{
	size_t sz;
	size_t i;

	if (block == TRIE_SET || block == TRIE_UNSET)
		return;
	if (max_depth < 0)
		max_depth = trie_get_depth(b);
	if (depth >= max_depth)
		goto free_block;

	sz = 1 << (trie_get_block_size(b, depth, max_depth) - ptr_sz_lg);

	for (i = 0; i < sz; i++)
		trie_free_block(b, (uint64_t **) (block[i]), depth + 1, max_depth);

free_block:
	free(block);
}

void
trie_free(struct trie *b)
{
	trie_free_block(b, b->data, 0, -1);
	free(b);
}
