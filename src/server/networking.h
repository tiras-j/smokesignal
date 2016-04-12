#ifndef _NETWORKING_H
#define _NETWORKING_H

typedef void (*handler_t)(char*, size_t);
int init_networking(const char *port, handler_t h_func);
void start_network_loop();

#endif /* _NETWORKING_H */
