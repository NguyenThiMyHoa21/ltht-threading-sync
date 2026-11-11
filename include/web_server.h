#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "sync_labs.h"

/**
 * Start multi-threaded POSIX TCP HTTP server on the given port.
 * Blocks until user presses ENTER (if tty) or SIGINT/SIGTERM arrives.
 */
int web_server_run(LabContext *ctx, int port);

#endif
