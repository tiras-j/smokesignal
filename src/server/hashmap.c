/* Author: Josh Tiras
 * Date: 2016-04-09
 * Yet another simple hashmap impl. I do this for fun, I swear.
 * This time I get to use linked lists as buckets though! Yay!
 * No dealing with shared memory and relative offsets!
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "hashmap.h"

#define DEFAULT_BUCKETS 64
#define BUCKET_LIMIT 8

// Don't forget about strdup, so convenient!
struct hash_entry {
	char *key;
	void *data;
	struct hash_entry *next_entry;
};

struct bucket {
	int num_entries;
	struct hash_entry *head;
	struct hash_entry *tail;
};

struct hash_map {
	int num_buckets;
	struct bucket *buckets;
};

unsigned long
djb2_hash(char *str)
{
	unsigned long hash = 5381;
	int c;
	while ((c = *str++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	return hash;
}

void* initialize_map() {
	struct hash_map *new_map;

	if((new_map = malloc(sizeof(struct hash_map))) == NULL) {
		return NULL;
	}
	
	if((new_map->buckets = calloc(DEFAULT_BUCKETS, sizeof(struct bucket))) == NULL) {
		return NULL;
	}

	new_map->num_buckets = DEFAULT_BUCKETS;
	return (void*)new_map;
}

void realloc_map(void *map) {
	// TODO: implement
}

int map_put(void *map, char *key, void* data) {
	unsigned long hash_value;
	struct hash_map *hmap;
	struct bucket *insert_bucket;
	struct hash_entry *new_entry;

	assert(map != NULL);
	assert(key != NULL);

	if((new_entry = malloc(sizeof(struct hash_entry))) == NULL)
		return -1;

	new_entry->key = strdup(key);
	new_entry->data = data;
	new_entry->next_entry = NULL;
	
	hmap = (struct hash_map *)map;
	hash_value = djb2_hash(key);

	insert_bucket = &hmap->buckets[(hash_value % hmap->num_buckets)];
	if(insert_bucket->num_entries == 0) {
		insert_bucket->head = new_entry;
		insert_bucket->tail = new_entry;
	} else {
		insert_bucket->tail->next_entry = new_entry;
	}
	insert_bucket->num_entries++;

	if(insert_bucket->num_entries > BUCKET_LIMIT) {
		realloc_map(map);
	}

	return 0;
}

void *map_get(void *map, char *key) {
	unsigned long hash_value;
	struct hash_map *hmap;
	struct bucket *search_bucket;
	struct hash_entry *entry_ptr;

	assert(map != NULL);
	assert(key != NULL);

	hmap = (struct hash_map *)map;
	hash_value = djb2_hash(key);

	search_bucket = &hmap->buckets[(hash_value % hmap->num_buckets)];
	entry_ptr = search_bucket->head;

	while(entry_ptr != NULL && strcmp(key, entry_ptr->key) != 0){
		entry_ptr = entry_ptr->next_entry;
	}

	if(entry_ptr == NULL)
		return NULL;

	return entry_ptr->data;
}

void *map_remove(void *map, char *key) {
	// Just like a get, but we remove the entry
	// Could probably have better code reuse but f-it
	unsigned long hash_value;
	struct hash_map *hmap;
	struct bucket *search_bucket;
	struct hash_entry *entry_ptr, *prev_ptr;
	void *retdata;

	assert(map != NULL);
	assert(key != NULL);

	hmap = (struct hash_map *)map;
	hash_value = djb2_hash(key);

	search_bucket = &hmap->buckets[(hash_value % hmap->num_buckets)];
	prev_ptr = entry_ptr = search_bucket->head;

	while(entry_ptr != NULL && strcmp(key, entry_ptr->key) != 0){
		prev_ptr = entry_ptr;
		entry_ptr = entry_ptr->next_entry;
	}

	if(entry_ptr == NULL)
		return NULL;

	if(search_bucket->num_entries == 1) {
		// We're removing head & tail
		search_bucket->head = NULL;
		search_bucket->tail = NULL;
	} else if (prev_ptr == entry_ptr) {
		// We're removing the head
		search_bucket->head = entry_ptr->next_entry; // guaranteed to be non-NULL from prev if
	} else {
		// We're removing either middle/tail
		prev_ptr->next_entry = entry_ptr->next_entry;
		if (prev_ptr->next_entry == NULL)
			search_bucket->tail = prev_ptr;
	}
	search_bucket->num_entries--;

	// Let the user do with the data as they will.
	retdata = entry_ptr->data; 
	free(entry_ptr->key);
	free(entry_ptr);

	return retdata;
}





	

