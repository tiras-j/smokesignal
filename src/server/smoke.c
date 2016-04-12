#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "networking.h"
#include "group_manager.h"

void handle_msg(char *msg, size_t msg_sz)
{
	char *buf;
	// msg does not contain null terminator!
	buf = malloc(msg_sz + 1);
	strncpy(buf, msg, msg_sz);
	buf[msg_sz] = 0;

	printf("Msg: %s\n", buf);
	free(buf);
}

int main(int argc, char *argv[]) {
	printf("Initializing...\n");
	init_networking("51511", &handle_msg);
	printf("Starting server\n");
	start_networking_loop();
	return 0;
}

