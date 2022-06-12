#include "ServerConnection.h"
#include "ServerAcceptHandler.h"
#include "EventHandler.h"
#include "ClientConnection.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>


#include "util.h"
#include "UpSessionHttp.h"
#include "UpSessionHttp2.h"
#include "UpSessionPlane.h"

static void server_readcb(struct bufferevent *bev, void *ptr)
{
	ServerConnection* serverCon = static_cast<ServerConnection *>(ptr);
	int rv = serverCon->readcb(bev);
	if( rv < 0 )
	{
		ServerAcceptHandler& accept = serverCon->getAccept();
		accept.removeSocket(serverCon);
	}
}

static void server_writecb(struct bufferevent *bev, void *ptr)
{
	ServerConnection* serverCon = static_cast<ServerConnection *>(ptr);
	int rv = serverCon->writecb(bev);
	if( rv < 0 )
	{
		ServerAcceptHandler& accept = serverCon->getAccept();
		accept.removeSocket(serverCon);
	}
}

/* eventcb for bufferevent */
static void server_eventcb(struct bufferevent *bev, short events, void *ptr)
{
	ServerConnection* serverCon = static_cast<ServerConnection *>(ptr);
	int rv = serverCon->eventcb(bev,events);
	if( rv < 0 )
	{
		ServerAcceptHandler& accept = serverCon->getAccept();
		accept.removeSocket(serverCon);
	}
}

ServerConnection::ServerConnection(ServerAcceptHandler& accept,int fd,struct sockaddr *addr, int addrlen,SSL_CTX* ssl_ctx)
	: accept_(accept)
	, ssl_ctx_(ssl_ctx)
	, upstream_(nullptr)
	, upgrade_(false)
	, tunnel_(false)
	, client_ (nullptr)
	, connected_(true)
{
	int val = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val));

	bev_ = bufferevent_socket_new(
	  accept_.getEv().getEventBase(), fd,
	  BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS  );

	bufferevent_enable(bev_, EV_READ | EV_WRITE);

	char host[NI_MAXHOST];
	int rv = getnameinfo(addr, (socklen_t)addrlen, host, sizeof(host), NULL, 0,
				   NI_NUMERICHOST);
	if (rv != 0)
	{
		client_addr_ = "(unknown)";
	}
	else
	{
		client_addr_ = host;
	}

	upstream_ = std::unique_ptr<UpSession>(new UpSessionHttp(*this));
	warnx("ServerConnection %d %s create\n", fd, client_addr_.c_str());

	bufferevent_setcb(bev_, server_readcb, server_writecb, server_eventcb, this);

}

ServerConnection::~ServerConnection()
{

	warnx("ServerConnection %s delete start\n", host_.c_str());
	connected_ = false;
	bufferevent_flush(bev_,EV_WRITE, BEV_FLUSH  );
	bufferevent_flush(bev_,EV_READ, BEV_FINISHED );

	bufferevent_free(bev_);
	warnx("ServerConnection %s delete end\n", host_.c_str());
}


int  ServerConnection::init_client(
		const std::string&	host,
		uint16_t			port,
		ConnectionProtocol	protocol)
{
	client_ = std::unique_ptr<ClientConnection>(
						new ClientConnection(accept_.getEv() ,this, ssl_ctx_));

	if(port == 0)
	{
		if( ssl_ctx_ )
		{
			port = 443;
		}
		else
		{
			port = 80;
		}

	}

	int rv = client_->initiate_connection(host,port,protocol);

	return rv;
}

std::unique_ptr<ClientConnection>&	ServerConnection::getClient()
{
	return client_;
}

int32_t ServerConnection::submit_message(
		HttpMessagePtr& 		message)
{
	return upstream_->send(message);
}


size_t ServerConnection::write(
				const uint8_t *data,
		        size_t length)
{
	if( connected_ == false)
	{
		return 0;
	}

	bufferevent_write(bev_, data, length);
	warnx("<<<<<< SERVER send len=%d", length, data);
	util::dumpbinary(data,length);

	return length;
}


int ServerConnection::readcb(struct bufferevent *bev)
{
	if(connected_ == false)
	{
		return 0;
	}

	ssize_t readlen = 0;
	struct evbuffer* input = bufferevent_get_input(bev_);
	size_t datalen = evbuffer_get_length(input);
	unsigned char *data = evbuffer_pullup(input, -1);

	warnx(">>>>>>>> ServerConnection::readcb(start size: %lu) >>>>>>>>",datalen);
	warnx("%30.30s",data);
//	warnx("%s",data);
	util::dumpbinary(data,datalen);

	if( upgrade_ == false && tunnel_ != true &&
		memcmp(NGHTTP2_CLIENT_MAGIC, data, NGHTTP2_CLIENT_MAGIC_LEN) == 0)
	{
		upgrade_ = true;
		// HTTP/2に切り替わっていない場合(Direct)はクライアントプリフェイス受信でHTTP/2に切り替える
		UpSessionHttp2* sessionh2 = dynamic_cast<UpSessionHttp2*>(upstream_.get());
		if(sessionh2 == nullptr)
		{
			upstream_ = std::unique_ptr<UpSession>(new UpSessionHttp2(*this));
		}
	}

	if( upstream_ != nullptr)
	{
		readlen = upstream_->on_read(data,datalen);
	}


	if (evbuffer_drain(input, (size_t)readlen) != 0)
	{
		warnx("Fatal error: evbuffer_drain failed");
		return -1;
	}

	return 0;
//	  if (server_session_recv(session_data) != 0) {
//	    delete_server_http2_session_data(session_data);
//	    return;
//	  }
}

void ServerConnection::switchinghttp2()
{
	UpSessionHttp* sessionh1 = dynamic_cast<UpSessionHttp*>(upstream_.get());
	if( sessionh1 == nullptr)
	{
		return;
	}

	HttpMessagePtr& mes = sessionh1->getRequest();
	HttpRequest* req = dynamic_cast<HttpRequest*>(mes.get());
	if( req == nullptr )
	{
		return;
	}
	if(req->upgrade_http2 != true)
	{
		return;
	}

	UpSessionHttp2* sessionh2 = new UpSessionHttp2(*this);
	sessionh2->appendHttp1Request(mes);
	upstream_.release();
	upstream_ = std::unique_ptr<UpSession>(sessionh2);

	sessionh1->sendSwichingProtocol();
	delete sessionh1;

}


void ServerConnection::switchTunnel()
{
	UpSessionHttp* sessionh1 = dynamic_cast<UpSessionHttp*>(upstream_.get());
	if( sessionh1 == nullptr)
	{
		return;
	}

	HttpMessagePtr& mes = sessionh1->getRequest();
	HttpRequest* req = dynamic_cast<HttpRequest*>(mes.get());
	if( req == nullptr )
	{
		return;
	}
	if(req->methodnum != HTTP_CONNECT )
	{
		return;
	}

#ifndef CONNECTHTTP2
	tunnel_ = true;
	UpSessionPlane* sessionPlane = new UpSessionPlane(*this);
#else
	UpSessionHttp2* sessionPlane = new UpSessionHttp2(*this);
#endif
	upstream_.release();
	upstream_ = std::unique_ptr<UpSession>(sessionPlane);
	sessionh1->sendConnectResult();
	delete sessionh1;

}

void ServerConnection::switchingProtocol(
				const std::string& protocol,
				const std::string& option)
{
	if( protocol.size() <= 0)
	{
		return;
	}

	UpSession* sessionnew = nullptr;
	UpSessionHttp2* sessionh2 = dynamic_cast<UpSessionHttp2*>(upstream_.get());
	if( protocol == "h2c")
	{
		if(sessionh2 != nullptr)
		{
			return;
		}

		sessionh2 = new UpSessionHttp2(*this);
		sessionh2->session_upgrade2(option,0);
		sessionnew = sessionh2;
	}
	else
	{
		sessionnew = new UpSessionPlane(*this);
	}

	upstream_ = std::unique_ptr<UpSession>(sessionnew);

}

bool ServerConnection::IsSentSetting()
{
	UpSessionHttp2* session = dynamic_cast<UpSessionHttp2*>(upstream_.get());
	if( session == nullptr )
	{
		return true;
	}

	return session->IsSentSetting();

}

//int ServerConnection::send_goaway(
//		int32_t last_stream_id,
//        uint32_t error_code,
//		std::string& opaque_data)
//{
//	UpSessionHttp2* sessionh2 = dynamic_cast<UpSessionHttp2*>(upstream_.get());
//	if( sessionh2 == nullptr)
//	{
//		return 0;
//	}
//
//	sessionh2->send_goaway(
//						last_stream_id,
//						error_code, opaque_data);
//
//	sessionh2->DoFlush();
//	return 0;
//}

int ServerConnection::writecb(struct bufferevent *bev)
{
//	warnx("===============writecb==================");

	if (evbuffer_get_length(bufferevent_get_output(bev)) > 0)
	{
//		return -1;
	}

	return 0;
	// TODO: writeに失敗した時の削除
//
//	if (nghttp2_session_want_read(session_data->session) == 0
//			&& nghttp2_session_want_write(session_data->session) == 0)
//	{
//		return -1;
//	}
//	if (server_session_send(session_data) != 0)
//	{
//		delete_server_http2_session_data (session_data);
//		return;
//	}
}

int ServerConnection::eventcb(struct bufferevent *bev, short events)
{
	if (events & BEV_EVENT_CONNECTED)
	{
		(void) bev;

		warnx("%s connected\n", client_addr_.c_str());

		return 0;
	}

	if (events & BEV_EVENT_EOF)
	{
		warnx("%s EOF\n", client_addr_.c_str());
	}
	else if (events & BEV_EVENT_ERROR)
	{
		warnx("%s network error\n", client_addr_.c_str());
	}
	else if (events & BEV_EVENT_TIMEOUT)
	{
		warnx("%s timeout\n", client_addr_.c_str());
	}

	return -1;
}
