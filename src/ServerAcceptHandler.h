#ifndef SERVERACCEPTHANDLER_H_
#define SERVERACCEPTHANDLER_H_

#include "Common.h"
#include <string>
#include <vector>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

class EventHandler;
class ServerConnection;

class ServerAcceptHandler {
public:
	ServerAcceptHandler(EventHandler& event,SSL_CTX* ssl_ctx);
	virtual ~ServerAcceptHandler();

	void start_listen(const char *service);
	void accept(int fd, struct sockaddr *addr, int addrlen);

	EventHandler& getEv() { return event_; }
	void removeSocket(
					ServerConnection* client);
private:
	std::string  service_;
	EventHandler&  event_;

	std::vector<std::unique_ptr<ServerConnection> > connections_;
	SSL_CTX*	ssl_ctx_;

};

#endif /* SERVERACCEPTHANDLER_H_ */
