#ifndef UPSESSION_H_
#define UPSESSION_H_

#include "Common.h"
#include "HttpHeader.h"

class ServerConnection;

class UpSession
{
public:
	UpSession(ServerConnection& handler);
	virtual ~UpSession();
	virtual int on_read(unsigned char *data, size_t datalen) = 0;
	virtual int on_write() = 0;
	virtual int on_event() = 0;
	virtual int DoFlush() = 0;
	virtual int32_t send(
					HttpMessagePtr&	message) = 0;

protected:
	int parseAuthority(const char* authority,
					size_t		authoritylen,
					std::string& host,
					uint16_t&	port);

	int parseURI(const char* uri,
				size_t		urilen,
				std::string& schema,
				std::string& host,
				uint16_t&	port,
				std::string& path);
	bool IsStoreMessage(
				HttpMessage*	message);

	int deflate(
			HttpMessage*	message);
	int deflateEnd(
			HttpMessage*	message);

protected:
	ServerConnection &handler_;
};

#endif /* UPSTREAM_H_ */
