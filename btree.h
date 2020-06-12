#ifndef STRACE_TRIE_H
#define STRACE_TRIE_H

/* Simple trie interface */

#define TRIE_SET   ((void *) ~(intptr_t) 0)
#define TRIE_UNSET ((void *) NULL)

#define PTR_BLOCK_SIZE_LG_MAX   24
#define DATA_BLOCK_SIZE_LG_MAX  23

enum trie_iterate_flags {
	TRIE_ITERATE_KEYS_SET   = 1 << 0,
	TRIE_ITERATE_KEYS_UNSET = 1 << 1,
};

/**
 * Trie control structure.
 * Trie implemented here has the following properties:
 *  * It allows storing values of the same size, the size can vary from 1 bit to
 *    64 bit values (only power of 2 sizes are allowed).
 *  * The key can be up to 64 bits in size.
 *  * It has separate configuration for pointer block size and data block size.
 *  * It can be used for mask storage - supports storing the flag that all keys
 *    are set/unset in the middle tree layers. See also trie_mask_set() and
 *    trie_mask_unset().
 *
 * How bits of key are used for different block levels:
 *
 *     highest bits                                         lowest bits
 *     | ptr_block_size_lg | ... | < remainder > | data_block_size_lg |
 *     \______________________________________________________________/
 *                                 key_size
 *
 * So, the remainder is used on the lowest non-data node level.
 *
 * As of now, it doesn't implement any mechanisms for resizing/changing key
 * size.  De-fragmentation is also unsupported currently.
 */
struct trie {
	uint64_t set_value;         /**< Default set value */
	void *data;
	uint8_t item_size_lg;       /**< Item size log2, in bits, 0..6. */
	/** Pointer block size log2, in bits. 14-20, usually. */
	uint8_t ptr_block_size_lg;
	/** Data block size log2, in bits. 11-17, usually. */
	uint8_t data_block_size_lg;
	uint8_t key_size;           /**< Key size, in bits, 1..64. */
};

typedef void (*trie_iterate_fn)(void *data, uint64_t key, uint64_t val);

bool trie_check(uint8_t item_size_lg, uint8_t ptr_block_size_lg,
		 uint8_t data_block_size_lg, uint8_t key_size);
void trie_init(struct trie *t, uint8_t item_size_lg,
		uint8_t ptr_block_size_lg, uint8_t data_block_size_lg,
		uint8_t key_size, uint64_t set_value);
struct trie * trie_create(uint8_t item_size_lg, uint8_t ptr_block_size_lg,
			    uint8_t data_block_size_lg, uint8_t key_size,
			    uint64_t set_value);

bool trie_set(struct trie *t, uint64_t key, uint64_t val);
#if 0
/**
 * Sets to the value b->set_value all keys with 0-ed bits of mask equivalent to
 * corresponding bits in key.
 */
int trie_mask_set(struct trie *t, uint64_t key, uint8_t mask_bits);
/**
 * Sets to 0 all keys with 0-ed bits of mask equivalent to corresponding bits in
 * key.
 */
int trie_mask_unset(struct trie *t, uint64_t key, uint8_t mask_bits);
int trie_interval_set(struct trie *t, uint64_t begin, uint64_t end,
		       uint64_t val);

uint64_t trie_get_next_set_key(struct trie *t, uint64_t key);
#endif

uint64_t trie_iterate_keys(struct trie *t, uint64_t start, uint64_t end,
			    enum trie_iterate_flags flags, trie_iterate_fn fn,
			    void *fn_data);

uint64_t trie_get(struct trie *t, uint64_t key);

void trie_free_block(struct trie *t, uint64_t **block, uint8_t depth,
		      int max_depth);
void trie_free(struct trie *t);

#endif /* !STRACE_TRIE_H */
