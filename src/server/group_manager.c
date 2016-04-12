#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include <time.h>
#include <string.h>
#include <utime.h>
#include <fcntl.h>
#include "group_manager.h"
#include "hashmap.h"

#define RESET_TIME 300 //5 minutes
#define DEFAULT_MAX_LISTENERS 128
#define MAX_IP4_STRING_SIZE 21 // xxx.xxx.xxx.xxx:ppppp -> 21 characters, we only support IPv4 atm

struct group_file {
	int fd;
	void *mmap_addr;
	size_t mapped_size;
	char group_name[256]; /* linux max file name size */
	int num_listeners;
	int max_listeners;
	int *listener_fd_array;
};

static void *group_map = NULL;
static void *health_map = NULL;
static char *DEFAULT_DIR = "/tmp/.groups";
static char *TIMESTAMP_FILE = ".lasttime";

/* Local functions */
static char *build_path(char *p1, char *p2) {
	char *new_path;

	// +2 for '/' + null term
	new_path = malloc(strlen(p1) + strlen(p2) + 2);
	strcpy(new_path, p1);
	strcat(new_path, "/");
	strcat(new_path, p2);

	return new_path;
}

static void realloc_listener_array(struct group_file *gfile) {
	int *new_array;
	int new_max_listeners = 2 * gfile->max_listeners;

	new_array = malloc(new_max_listeners * sizeof(int));
	memcpy(new_array, gfile->listener_fd_array, gfile->num_listeners * sizeof(int));

	free(gfile->listener_fd_array);
	gfile->listener_fd_array = new_array;
	gfile->max_listeners = new_max_listeners; 
}

static void extend_mapped_file(struct group_file *gfile)
{
	size_t new_size = gfile->mapped_size * 2;
	// We know gfile->mapped_size is a page multiple.
	// So we're just going to double it.
	ftruncate(gfile->fd, new_size);
	munmap(gfile->mmap_addr, gfile->mapped_size);
	gfile->mmap_addr = mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, gfile->fd, 0);
	gfile->mapped_size = new_size;
}
	
static struct group_file *create_or_open_group_file(char *file_path)
{
	struct stat statb;
	struct group_file *gfile;
	char *file_name, *memptr;

	if(stat(file_path, &statb) == -1) {
		if(errno == ENOENT) {
			// TODO: Fix this to not open fd, close fd, and reopen fd
			// that's just silly.
			int fd = open(file_path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
			ftruncate(fd, getpagesize());
			close(fd);
			stat(file_path, &statb);
		}else {
			return NULL;
		}
	}

	// Locate the last '/' and copy the file name
	file_name = strrchr(file_path, '/') + 1;	
	
	// Open the file, mmap it and set up struct
	gfile = malloc(sizeof(struct group_file));
	gfile->fd = open(file_path, O_RDWR);
	gfile->mmap_addr = mmap(NULL, statb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, gfile->fd, 0);
	gfile->mapped_size = statb.st_size;
	strcpy(gfile->group_name, file_name);
	// Unfortunately we can't reconstruct listeners
	// so we'll have to just deal with that
	gfile->num_listeners = 0;
	gfile->max_listeners = DEFAULT_MAX_LISTENERS;
	gfile->listener_fd_array = malloc(sizeof(int) * DEFAULT_MAX_LISTENERS);

	return gfile;
}

static int open_existing_groups(DIR *dir)
{
	struct dirent *entry;
	struct stat statb;
	int delete_all = 0;
	char *time_file_path;

	time_file_path = build_path(DEFAULT_DIR, TIMESTAMP_FILE);
	// Check timestamp if we want to delete everything
	if(stat(time_file_path, &statb) == -1) {
		perror("time check, stat");
		return -1;
	}
	utime(time_file_path, NULL);
	free(time_file_path);

	if(time(NULL) - statb.st_atime > RESET_TIME)
		delete_all = 1;

	while((entry = readdir(dir)) != NULL) {
		char *file_path;
		int fd;

		if(strcmp(entry->d_name, TIMESTAMP_FILE) == 0)
			continue;

		file_path = build_path(DEFAULT_DIR, entry->d_name);
		if(delete_all) {
			remove(file_path);
		} else {
			struct group_file *gfile = create_or_open_group_file(file_path);
			if(gfile)
				map_put(group_map, gfile->group_name, (void*)gfile);
		}
		free(file_path);
	}

	return 0;
}

static int already_member(struct group_file *gfile, char *member)
{
	const char *memptr = (const char *)gfile->mmap_addr;
	if(strstr(memptr, member) != NULL)
		return 1;
	return 0;
}

/* Exposed Functions */
int initialize_group_manager()
{
	DIR *groups_dir;
	int fd;
	if((group_map = initialize_map()) == NULL) {
		return -1;
	}

    if((health_map = initialize_map()) == NULL) {
        return -1;
    }

	if((groups_dir = opendir(DEFAULT_DIR)) == NULL) {
		if(errno == ENOENT) {
			char *time_path;
			if(mkdir(DEFAULT_DIR, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1)
				return -1;
			// Create timestamp file
			time_path = build_path(DEFAULT_DIR, TIMESTAMP_FILE);
			if((fd = open(time_path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)) == -1){
				free(time_path);
				return -1;
			}
			free(time_path);
			close(fd);
		}else{
			return -1;
		}
	}else{
		if(open_existing_groups(groups_dir) == -1)
			return -1;
	}

	return 0;
}
int group_exists(char *name)
{
	if(map_get(group_map, name) != NULL)
		return 1;
	return 0;
}

const char *retrieve_group_members(char *name)
{
	struct group_file *gfile;
	if((gfile = (struct group_file *)map_get(group_map, name)) == NULL)
		return NULL;

	return (const char *)gfile->mmap_addr;
}

int create_group(char *name)
{
	char *file_path = build_path(DEFAULT_DIR, name);
	struct group_file *gfile = create_or_open_group_file(file_path);
	if(gfile)
		return map_put(group_map, gfile->group_name, (void *)gfile);
	return -1;
}
// Should this delete the underlying file?
// for now we are
int delete_group(char *name)
{
	struct group_file *gfile;
	gfile = map_remove(group_map, name);
	if(gfile) {
		char *file_path = build_path(DEFAULT_DIR, gfile->group_name);
		munmap(gfile->mmap_addr, gfile->mapped_size);
		close(gfile->fd);
		if(file_path) {
			unlink(file_path);
			free(file_path);
		}
		free(gfile);
	}
	return 0;
}

/* For now we're putting it on the user to compose the ip:port string
 * and to have to call in a single ip/port at a time, if they have 
 * multiple they need to deal with it by making multiple calls.
 * This simplifies things on our end and satisfies the typical use case.
 */
int join_group(char *name, char *ip_addr)
{
	// ip_addr should be form "x.x.x.x:port"
	char buf[256];
	struct group_file *gfile;
	int current_offset;
    time_t *time_ptr;

	if((gfile = map_get(group_map, name)) == NULL)
		return -1;

	// Set the buf, add comma, perhaps do sanitization later.
	if(strlen(ip_addr) > 254)
		return -1;

    // Add to health map
    time_ptr = malloc(sizeof(*time_ptr));
    time(time_ptr);
    map_put(health_map, ip_addr, (void*)time_ptr);

	snprintf(buf, 256, "%s,", ip_addr);

	if(already_member(gfile, buf))
		return 0;
	current_offset = strlen((char*)gfile->mmap_addr);

	// The -1 is to maintain a single null terminator at the end
	// of the mmap so we can pass it as a string
	if(current_offset + strlen(buf) > gfile->mapped_size - 1)
		extend_mapped_file(gfile);

	strcpy((char*)gfile->mmap_addr + current_offset, buf);
	current_offset += strlen(buf);
	return 0;
}

int healthcheck_group(char *name, char *ip_addr)
{
    time_t *time_ptr;
    struct group_file *gfile;

    if((gfile = map_get(group_map, name)) == NULL)
        return -1;

    if(!already_member(gfile, ip_addr))
        return -1;

    if((time_ptr = map_get(health_map, ip_addr)) == NULL)
        return -1;

    time(time_ptr);

    return 0;
}

int leave_group(char *name, char *ip_addr)
{
	struct group_file *gfile;
	char *memptr, *start_del, *end_del, *end_file;
	size_t bytes_to_move, bytes_to_clear;
	
	if((gfile = map_get(group_map, name)) == NULL)
		return -1;

	if(!already_member(gfile, ip_addr))
		return 0;

	memptr = (char *)gfile->mmap_addr;
	end_file = memptr + strlen(memptr);
	start_del = strstr(memptr, ip_addr);
	end_del = start_del + strlen(ip_addr);  
	bytes_to_move = end_file - end_del;
	bytes_to_clear = strlen(ip_addr);
	
	memcpy(start_del, end_del + 1, bytes_to_move); 
	memset(end_file - bytes_to_clear, 0, bytes_to_clear);

	return 0;
}

int sub_group(char *name, int sockfd)
{
	struct group_file *gfile;
	int idx;
	if((gfile = map_get(group_map, name)) == NULL)
		return -1;

	if(gfile->num_listeners == gfile->max_listeners)
		realloc_listener_array(gfile);

	// Unfortunately we should make certain the socket fd
	// isn't already on the sub list
	// this is linear, but then again I've been dropping
	// syscalls like I'm making it rain up in this file.
	for(idx = 0; idx < gfile->num_listeners; ++idx) {
		if(gfile->listener_fd_array[idx] == sockfd)
			return 0;
	}

	gfile->listener_fd_array[gfile->num_listeners++] = sockfd;
	return 0;
}

int unsub_group(char *name, int sockfd)
{
	struct group_file *gfile;
	int idx;
	if((gfile = map_get(group_map, name)) == NULL)
		return -1;

	for(idx = 0; idx < gfile->num_listeners; ++idx) {
		if(gfile->listener_fd_array[idx] == sockfd)
			break;
	}
	if(idx == gfile->num_listeners)
		return -1;

	// Check if we're the last in the list to avoid undefined behavor with
	// memcpy(x, y, 0) where y could be invalid if num_listeners ==
	// max_listeners
	if(idx < gfile->num_listeners - 1)
		memcpy(gfile->listener_fd_array + idx, gfile->listener_fd_array + idx + 1, gfile->num_listeners - idx - 1);
	gfile->num_listeners--;
	return 0;
}
