#include <sys/types.h>
#include <iostream>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "EventHandler.h"
#include "ServerAcceptHandler.h"
#include "TlsWrapper.h"

int main() {

	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &act, NULL);

	TlsWrapper   tlsw;
	EventHandler eventHandler;
	ServerAcceptHandler acceptHandler(eventHandler,tlsw.get_ssl_cts());
	char port[] = "23456";;
	acceptHandler.start_listen(port);

	ServerAcceptHandler acceptHandler2(eventHandler,nullptr);
	acceptHandler2.start_listen("12345");


	eventHandler.loop();

	return 0;
}
