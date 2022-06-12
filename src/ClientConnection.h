#ifndef CLIENTCONNECTION_H_
#define CLIENTCONNECTION_H_

#include "Common.h"
#include <string>
#include "MemBuff.h"
#include "HttpHeader.h"

class EventHandler;
class DownSession;
class ServerConnection;

class ClientConnection
{
public:
	ClientConnection(
			EventHandler& handle,
			ServerConnection* server,
			SSL_CTX* ssl_ctx);
	virtual ~ClientConnection();

	virtual int initiate_connection(
			const std::string&	host,
			uint16_t			port,
			ConnectionProtocol	protocol);

	bool IsConnected() const { return connected_; }

	int32_t submit_message(
			HttpMessagePtr& 		message);

	virtual size_t write(
					const uint8_t *data,
			        size_t length);

	virtual size_t tranfer(
					const uint8_t *data,
			        size_t length);

	virtual int on_read(struct bufferevent *bev);
	virtual int on_write(struct bufferevent *bev);
	virtual int on_event(struct bufferevent *bev,short events);

	ServerConnection* getServer() { return server_;}
	const std::string& 	getHost() const { return host_;}
	bool  IsSentSetting();

	int send_goaway(
	        uint32_t error_code,
			const char* opaque_data);

	void setSwitchingProtocol( const std::string& protocol,
								const std::string& option);

protected:
	void switchingProtocol();

protected:
	EventHandler& handle_;
	ServerConnection* server_;
	struct bufferevent *bev_;
	struct evdns_base *dnsbase_;
	std::string			host_;
	uint16_t			port_;
	SSL_CTX*	ssl_ctx_;
	SSL* 		ssl_;
	int			socket_;
	bool		connected_;
	std::unique_ptr<DownSession>	downstream_;
	ConnectionProtocol	protocol_;
	bool	recvwait_;
	std::string switchingprotocol_;
	std::string switchingoption_;

};

#endif /* CLIENTCONNECTION_H_ */
