#include "ClientConnection.h"
#include "EventHandler.h"
#include "ServerConnection.h"
#include "ServerAcceptHandler.h"
#include "util.h"

#include <event.h>
#include <event2/event.h>
#include <event2/bufferevent_ssl.h>
#include <event2/dns.h>
#include <netinet/tcp.h>

#include "DownSessionHttp.h"
#include "DownSessionHttp2.h"
#include "DownSessionPlane.h"
#include "UpSession.h"

static void readcb(struct bufferevent *bev, void *ptr)
{
	ClientConnection *client = static_cast<ClientConnection *>(ptr);
	int rv = client->on_read(bev);

	if( rv < 0 )
	{
		ServerConnection* server = client->getServer();
		ServerAcceptHandler& accept = server->getAccept();
		accept.removeSocket(server);
	}
}

static void writecb(struct bufferevent *bev, void *ptr)
{
	ClientConnection *client = static_cast<ClientConnection *>(ptr);

	int rv = client->on_write(bev);
	if( rv < 0 )
	{
		ServerConnection* server = client->getServer();
		ServerAcceptHandler& accept = server->getAccept();
		accept.removeSocket(server);
	}
}

static void eventcb(struct bufferevent *bev, short events, void *ptr)
{
	ClientConnection *client = static_cast<ClientConnection *>(ptr);

	int rv = client->on_event(bev,events);
	if( rv < 0 )
	{
		ServerConnection* server = client->getServer();
		ServerAcceptHandler& accept = server->getAccept();
		accept.removeSocket(server);
	}
}

ClientConnection::ClientConnection(
			EventHandler& handle,
			ServerConnection* server,
			SSL_CTX* ssl_ctx)
	: handle_(handle)
	, bev_(nullptr)
	, dnsbase_(nullptr)
	, host_()
	, port_()
	, ssl_ctx_(ssl_ctx)
	, ssl_(nullptr)
	, socket_(0)
	, connected_(false)
	, downstream_(nullptr)
	, server_(server)
	, protocol_(ConnectionProtocol_HTTP1)
	, recvwait_(true)
	, switchingprotocol_()
	, switchingoption_()
{
	if(ssl_ctx_ != nullptr)
	{
		ssl_ = SSL_new(ssl_ctx_);
		if (!ssl_)
		{
			errx(1, "Could not create SSL/TLS session object: %s",
				 ERR_error_string(ERR_get_error(), NULL));
		}
	}

	dnsbase_ = evdns_base_new(handle.getEventBase(), 1);
}

ClientConnection::~ClientConnection()
{
	warnx("ClientConnection %s:%d delete start\n", host_.c_str(),port_);
	connected_ = false;
	warnx("%s client disconnected", host_.c_str());
	bufferevent_flush(bev_,EV_WRITE, BEV_FLUSH  );
	bufferevent_flush(bev_,EV_READ, BEV_FINISHED );

	if( ssl_ != nullptr)
	{
	    warnx("%s server shutdown", host_.c_str());
		SSL_shutdown(ssl_);
//		SSL_free(ssl_);  // bufferevent_free内で開放
		ssl_ = nullptr;
	}

	if(socket_ != 0)
	{
	    warnx("%s server shutdown", host_.c_str());
	    shutdown(socket_,SHUT_RDWR);
	}

	bufferevent_free(bev_);
	evdns_base_free(dnsbase_, 1);
	warnx("ClientConnection %s:%d delete end\n", host_.c_str(),port_);

}

int ClientConnection::initiate_connection(
			const std::string&	host,
			uint16_t			port,
			ConnectionProtocol	protocol)
{
	host_ = host;
	port_ = port;
	protocol_ = protocol;
	if( protocol == ConnectionProtocol_PLANE)
	{
		downstream_ = std::unique_ptr<DownSession>(new DownSessionPlane(*this));
	}
	else if( protocol == ConnectionProtocol_HTTP2)
	{
		downstream_ = std::unique_ptr<DownSession>(new DownSessionHttp2(*this));
	}
	else
	{
		downstream_ = std::unique_ptr<DownSession>(new DownSessionHttp(*this));
	}

	if(ssl_)
	{
		std::string protos;
		if( ConnectionProtocol_HTTP2Both  == protocol )
		{
			protos += "\x08http/1.1";
		}
		if(ConnectionProtocol_HTTP2 == protocol ||
			ConnectionProtocol_HTTP2Both  == protocol )
		{
			protos += "\x02h2";
		}


		if(protos.size() > 0)
		{
			SSL_set_alpn_protos(ssl_, (const unsigned char*)protos.c_str(), protos.size());
		}
		bev_ = bufferevent_openssl_socket_new(
				  handle_.getEventBase(), -1, ssl_, BUFFEREVENT_SSL_CONNECTING,
				  BEV_OPT_DEFER_CALLBACKS | BEV_OPT_CLOSE_ON_FREE  );
	}
	else
	{
		socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	    if (socket_ < 0)
	    {
	    	errx(1, "Could not create socket %s:%d", host_.c_str(), port);
	        return -1;
	    }

		bev_ = bufferevent_socket_new(
				handle_.getEventBase(), socket_,
				BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS  );
	}

	bufferevent_enable(bev_, EV_READ | EV_WRITE);
	bufferevent_setcb(bev_, readcb, writecb, eventcb, this);
	int rv = bufferevent_socket_connect_hostname(
				bev_, dnsbase_, AF_UNSPEC, host_.c_str(), port_);

	if (rv != 0)
	{
		errx(1, "Could not connect to the remote host %s:%d", host_.c_str(), port);
	}

	return rv;
}



int32_t ClientConnection::submit_message(
		HttpMessagePtr& 		message)
{
	if( connected_ != true)
	{
		return 0;
	}

	return downstream_->send(message);
}

bool ClientConnection::IsSentSetting()
{
	DownSessionHttp2* session = dynamic_cast<DownSessionHttp2*>(downstream_.get());
	if( session == nullptr )
	{
		return true;
	}

	return session->IsSentSetting();

}

size_t ClientConnection::write(
				const uint8_t *data,
		        size_t length)
{
	if( connected_ != true)
	{
		return length;
	}

	bufferevent_write(bev_, data, length);
	warnx(">>>>>>> CLIENT send len=%d data=%10.10s", length, data);
	util::dumpbinary(data,length);

	if( recvwait_ == true)
	{
		recvwait_ = false;
//		on_read(bev_);
	}
	return length;
}

size_t ClientConnection::tranfer(
				const uint8_t *data,
		        size_t length)
{
	size_t sendlen = 0;

	sendlen = server_->write(data,length);
	return length;
}

int ClientConnection::on_read(struct bufferevent *bev)
{
	if(connected_ == false)
	{
		return 0;
	}

	if(recvwait_ == true)
	{
		return 0;
	}

	struct evbuffer* input = bufferevent_get_input(bev_);
	size_t datalen = evbuffer_get_length(input);
	unsigned char *data = evbuffer_pullup(input, -1);
	ssize_t readlen = 0;

	warnx("<<<<<<<< ClientConnection::readcb(start size: %lu)==================",datalen);
	util::dumpbinary(data,datalen);

	if( downstream_ != nullptr)
	{
		readlen += downstream_->on_read(data,datalen);
	}

	switchingProtocol();

	if (evbuffer_drain(input, (size_t)readlen) != 0)
	{
		warnx("Fatal error: evbuffer_drain failed");
		return -1;
	}
}

int ClientConnection::on_write(struct bufferevent *bev)
{
	if( downstream_ != nullptr)
	{
		downstream_->on_write();
	}

}

int ClientConnection::on_event(struct bufferevent *bev, short events)
{
  if (events & BEV_EVENT_CONNECTED)
  {
    int fd = bufferevent_getfd(bev);
    int val = 1;

    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val));

    warnx(">>>>>> %s:%d Connected", host_.c_str(), port_);

    if(ssl_)
    {
		const unsigned char *alpn = NULL;
		unsigned int alpnlen = 0;
		SSL *ssl = NULL;
		ssl = bufferevent_openssl_get_ssl(bev_);

		// NPN の結果を取得
		SSL_get0_next_proto_negotiated(ssl_, &alpn, &alpnlen);
		if (alpn == NULL)
		{
			// NPNでなければALPNの結果を取得
			SSL_get0_alpn_selected(ssl, &alpn, &alpnlen);
		}

		if(alpn != NULL && alpnlen == 2 && memcmp("h2", alpn, 2) == 0)
		{
			std::string proto((const char*)alpn, alpnlen);
			warnx("<<<<<<<< %s:%d alpn select %s", host_.c_str(), port_, proto.c_str());

			DownSessionHttp2* sessionh2 = dynamic_cast<DownSessionHttp2*>(downstream_.get());
			if(sessionh2 == nullptr)
			{
				downstream_ = std::unique_ptr<DownSession>(new DownSessionHttp2(*this));
			}

			server_->switchinghttp2();
		}
		else
		{
			if( protocol_ == ConnectionProtocol_HTTP2 )
			{
				send_goaway(NGHTTP2_HTTP_1_1_REQUIRED,"Use HTTP/1.1 for Request");
			    return 0;
			}
		}
    }

    server_->switchTunnel();



    downstream_->on_event(events);

	connected_ = true;
	server_->getUpstream()->DoFlush();

    return 0;
  }

  if (events & BEV_EVENT_EOF)
  {
	  warnx("%s:%d Client Disconnected from the remote host", host_.c_str(), port_);
  }
  else if (events & BEV_EVENT_ERROR)
  {
	  warnx("%s:%d Client Network error", host_.c_str(), port_);
  }
  else if (events & BEV_EVENT_TIMEOUT)
  {
	  warnx("%s:%d Client Timeout", host_.c_str(), port_);
  }

  downstream_->on_event(events);

  return -1;
}

void ClientConnection::setSwitchingProtocol(
				const std::string& protocol,
				const std::string& option)
{
	switchingprotocol_ = protocol;
	switchingoption_ = option;

}

void ClientConnection::switchingProtocol()
{
	if( switchingprotocol_.size() <= 0)
	{
		return;
	}

	DownSession* sessionnew = nullptr;
	DownSessionHttp2* sessionh2 = nullptr;
	if( switchingprotocol_ == "h2c")
	{
		sessionh2 = new DownSessionHttp2(*this);
		sessionh2->session_upgrade2(switchingoption_,0);
		sessionnew = sessionh2;
	}
	else
	{
		sessionnew = new DownSessionPlane(*this);
	}

	recvwait_ = true;
	downstream_ = std::unique_ptr<DownSession>(sessionnew);

	getServer()->switchingProtocol(switchingprotocol_,switchingoption_);
	switchingprotocol_.clear();
	switchingoption_.clear();


}

int ClientConnection::send_goaway(
        uint32_t error_code,
		const char* opaque_data)
{
	ServerConnection* server = getServer();
	if(server == nullptr)
	{
		return -1;
	}

	DownSessionHttp2* sessionh2 = dynamic_cast<DownSessionHttp2*>(downstream_.get());
	if( sessionh2 == nullptr )
	{
		return 0;
	}

	// SETTING未送信ならデフォルトで送信する
	if( server->IsSentSetting() == false)
	{
		sessionh2->submit_defaultsetting();
	}

	int32_t last_stream_id = 0;
	std::string opaque;
	if( opaque_data != nullptr)
	{
		opaque = opaque_data;
	}

	last_stream_id = sessionh2->get_last_proc_stream_id();

	sessionh2->submit_goaway(
						last_stream_id,
						error_code, opaque);
	sessionh2->DoFlush();

	return 0;
}
