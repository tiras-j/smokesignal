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
#define DEFAUL_MAX_LISTENERS 128

struct group_file {
	int fd;
	void *mmap_addr;
	size_t mapped_size;
	int num_entries;
	char group_name[256]; /* linux max file name size */
	int num_listeners;
	int max_listeners;
	int *listener_fd_array;
};

static void *group_map = NULL;
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

static struct group_file *create_or_open_group_file(char *file_path)
{
	struct stat statb;
	struct group_file *gfile;
	char *file_name, *memptr;

	if(stat(file_path, &statb) == -1) {
		if(errno == ENOENT) {
			int fd = open(file_path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
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

	// Finally, iterates for ',' in file and count entries in there
	memptr = (char *)gfile->mmap_addr;
	while(memptr < ((char*)gfile->mmap_addr + gfile->mapped_size) && (memptr = strchr(memptr, ',')) != NULL) {
		gfile->num_entries++;
		memptr++;
	}

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
/* Exposed Functions */
int initialize_group_manager()
{
	DIR *groups_dir;
	int fd;
	if((group_map = initialize_map()) == NULL) {
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

int retrieve_group_fd(char *name)
{
	struct group_file *gfile;

	if((gfile = (struct group_file *)map_get(group_map, name)) == NULL)
		return -1;

	return gfile->fd
}

int create_group(char *name)
{
	char *file_path = build_path(DEFAULT_DIR, name);
	struct group_file *gfile = create_or_open_group_file(file_path);
	if(gfile)
		return map_put(group_map, gfile->group_name, (void *)gfile);
	return -1;
}
int delete_group(char *name)
{
	struct group_file *gfile;
	gfile = map_remove(group_map, name);
	if(gfile) {
		munmap(gfile->mmap_addr, gfile->mapped_size);
		close(gfile->fd);
		free(gfile);
	}
	return 0;
}

int join_group(char *name, char *ip_addr, int *port_array, int num_ports)
{


	return 0;
}
int leave_group(char *name, char *ip_addr)
{

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
