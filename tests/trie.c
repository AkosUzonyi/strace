/*
 * Copyright (c) 2017-2019 The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "tests.h"
#include "trie.h"

#include <stdio.h>

static void
assert_equals(const char *msg, uint64_t expected, uint64_t actual) {
	if (actual != expected)
		error_msg_and_fail("%s: expected: %ld, actual: %ld", msg, expected, actual);
}

int
main(void)
{
	static const struct {
		uint8_t key_size;
		uint8_t item_size_lg;
		uint8_t node_key_bits;
		uint8_t data_block_key_bits;
		uint64_t empty_value;

		struct {
			uint64_t key, value, expected_value;
		} key_value_pairs[3];
	} params[] = {
		{64, 6, 10, 10, 0, {{2, 42, 42}, {0, 1, 1}, {2, 43, 43}}},
	};

	for (size_t i = 0; i < ARRAY_SIZE(params); i++) {
		struct trie *t = trie_create(params[i].key_size,
		                             params[i].item_size_lg,
					     params[i].node_key_bits,
					     params[i].data_block_key_bits,
					     params[i].empty_value);

		for (size_t j = 0; j < ARRAY_SIZE(params[i].key_value_pairs); j++) {
			uint64_t key, value, expected;
			key = params[i].key_value_pairs[j].key;
			value = params[i].key_value_pairs[j].value;
			expected = params[i].key_value_pairs[j].expected_value;

			trie_set(t, key, value);
			assert_equals("trie_get", expected, trie_get(t, key));
		}

		trie_free(t);
	}

	return 0;
}
