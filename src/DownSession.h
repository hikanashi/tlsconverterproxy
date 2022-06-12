#ifndef DOWNSESSION_H_
#define DOWNSESSION_H_

#include "Common.h"
#include "HttpHeader.h"

class ClientConnection;

class DownSession
{
public:
	DownSession(ClientConnection& handle);
	virtual ~DownSession();

	virtual int on_read(unsigned char *data, size_t datalen) = 0;
	virtual int on_write() = 0;
	virtual int on_event(short events) = 0;
	virtual int DoFlush() = 0;


	virtual int32_t send(
						HttpMessagePtr&	message) = 0;
protected:
	bool IsStoreMessage(
				HttpMessage*	message);
	int inflate(
			HttpMessage*	message);
	int inflateEnd(
			HttpMessage*	message);

protected:
	ClientConnection&	handler_;
};

#endif /* DOWNSTREAM_H_ */
