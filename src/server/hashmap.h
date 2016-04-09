#ifndef _HASHMAP_H
#define _HASHMAP_H

void *initialize_map();
int map_put(void *map, char *key, void *data);
void *map_get(void *map, char *key);
void *map_remove(void *map, char *key);

#endif
