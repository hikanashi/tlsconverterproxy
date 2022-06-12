#ifndef SERVERCONNECTION_H_
#define SERVERCONNECTION_H_

#include "Common.h"
#include <string>
#include <vector>
#include "MemBuff.h"
#include "HttpHeader.h"

class ServerAcceptHandler;
class UpSession;
class ClientConnection;

class ServerConnection
{
public:
	ServerConnection(ServerAcceptHandler& accept,int fd,struct sockaddr *addr, int addrlen,SSL_CTX* ssl_ctx );
	virtual ~ServerConnection();

	int readcb(struct bufferevent *bev);
	int writecb(struct bufferevent *bev);
	int eventcb(struct bufferevent *bev, short events);

	int  init_client(
			const std::string&	host,
			uint16_t			port,
			ConnectionProtocol	protocol);

	int32_t submit_message(
			HttpMessagePtr& 		message);

	virtual size_t write(
					const uint8_t *data,
			        size_t length);

	void switchinghttp2();
	void switchTunnel();
	void switchingProtocol( const std::string& protocol,
								const std::string& option);

//	int send_goaway(
//			int32_t last_stream_id,
//	        uint32_t error_code,
//			std::string& opaque_data);


	bool				IsSSL() { return (ssl_ctx_ != nullptr); }
	bool 			 	IsSentSetting();

	ServerAcceptHandler&				getAccept() { return accept_; }
	std::unique_ptr<ClientConnection>&	getClient();
	std::unique_ptr<UpSession>& getUpstream() { return upstream_; }

private:
	std::string   	host_;
	uint16_t		port_;
	ServerAcceptHandler&   accept_;
	std::unique_ptr<UpSession>		upstream_;
	SSL_CTX*	ssl_ctx_;

	//struct sever_http2_stream_data root;
	struct bufferevent *bev_;
	std::string client_addr_;
	bool upgrade_;
	bool tunnel_;

	std::unique_ptr<ClientConnection>		client_;
	bool									connected_;

};

#endif /* SERVERCONNECTION_H_ */
