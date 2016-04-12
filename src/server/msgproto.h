#ifndef _MSGPROTO_H
#define _MSGPROTO_H
/* Author: Josh Tiras
 * Date: 2016-04-11
 * Defines the protocol message types used by smokesignal
 * Most messages are quite simple, comprised of a magic value followed by a full msg size int
 * This describes the length of the entire packet (for easier network handling). 
 * The message itself is headed with a msg_type byte. This indicates the type of to be parsed
 * This parsing is done manually by the smoke handler, which then invokes the necessary behavior.
 */

#define SMOKEMAGIC 19910121

/* GLEN < 256, MSGLEN < 65kb etc */
/* Message Types */
/*  1  |  1 |    N    |  2   |           N         BYTES*/
/* TYPE|GLEN|GROUPNAME|STRLEN|(IP/CDN):PORT string */
#define JOINGROUP 1
#define LEAVEGROUP 2

/*  1  |  1 |   N     |  2   |      N          BYTES*/
/* TYPE|GLEN|GROUPNAME|MSGLEN|MSG(untouched) */
#define BROADCAST 3 // Broadcast sends to LISTENERS of a group, not group members themselves

/*  1  | 1  |   N      */
/* TYPE|GLEN|GROUPNAME */
#define LEAVEGROUP 4
#define HEALTHCHECK 5
#define LISTMEMBERS 6

#endif /* _MSGPROTO_H */
