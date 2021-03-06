#ifndef _GROUP_MANAGER_H
#define _GROUP_MANAGER_H
/* Author: Josh Tiras
 * Date: 2016-04-09
 * topic.h describes the data structures which store information
 * for groups and group subscribers. This includes the memorymapped
 * files containing IP/port information as well as structures containing
 * filedescriptors for open sockets associated with these listeners
 */

int initialize_group_manager();
int group_exists(char *name);
int create_group(char *name);
int delete_group(char *name);
int join_group(char *name, char *ip_addr);
int healthcheck_group(char *name, char *ip_addr);
int leave_group(char *name, char *ip_addr);
int sub_group(char *name, int sockfd);
int unsub_group(char *name, int sockfd);
const char *retrieve_group_members(char *name);
#endif /* _GROUP_MANAGER_H */
