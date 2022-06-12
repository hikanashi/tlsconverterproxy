#include "ServerAcceptHandler.h"
#include "EventHandler.h"
#include "ServerConnection.h"

static void acceptcb(
		struct evconnlistener *listener, int fd,
        struct sockaddr *addr, int addrlen, void *arg)
{
	ServerAcceptHandler* acceptHandler = static_cast<ServerAcceptHandler *>(arg);
	(void)listener;
	fprintf(stderr, "acceptcb %d\n", fd);

	acceptHandler->accept(fd, addr, addrlen);
}

ServerAcceptHandler::ServerAcceptHandler(EventHandler& event,SSL_CTX* ssl_ctx)
	: event_(event)
	, ssl_ctx_(ssl_ctx)
{
}

ServerAcceptHandler::~ServerAcceptHandler()
{
}



void ServerAcceptHandler::start_listen(const char *service)
{
	struct event_base *evbase = event_.getEventBase();
	int rv;
	struct addrinfo hints;
	struct addrinfo *res, *rp;


	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
#ifdef AI_ADDRCONFIG
	hints.ai_flags |= AI_ADDRCONFIG;
#endif /* AI_ADDRCONFIG */

	rv = getaddrinfo(NULL, service, &hints, &res);
	if (rv != 0) {
		errx(1, "Could not resolve server address");
	}
	for (rp = res; rp; rp = rp->ai_next) {
		struct evconnlistener *listener;
		listener = evconnlistener_new_bind(
						evbase, acceptcb, this,
						LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 16, rp->ai_addr,
						(int) rp->ai_addrlen);
		if (listener)
		{
			service_ = service;
			warnx("sample start port:%s\n", service_.c_str());
			freeaddrinfo(res);
			return;
		}
	}
	errx(1, "Could not start listener: %s", service);
}

void ServerAcceptHandler::accept(int fd, struct sockaddr *addr, int addrlen)
{
	warnx("accept %d %s",fd, service_.c_str());

	ServerConnection*   serverCon;
	serverCon = new ServerConnection(*this,fd, addr, addrlen, ssl_ctx_);

	connections_.push_back(std::unique_ptr<ServerConnection>(serverCon));
	return;
}

void ServerAcceptHandler::removeSocket(
				ServerConnection* client)
{
	for(auto it = connections_.begin(); it != connections_.end(); it++)
	{
		auto& checkclient = *it;

		if(checkclient.get() == client)
		{
			it = connections_.erase(it);
			break;
		}
	}

}


