#ifndef DOWNSESSIONHTTP_H_
#define DOWNSESSIONHTTP_H_

#include "DownSession.h"
#include "SessionHttp1Handler.h"

class DownSessionHttp: public DownSession
					, public SessionHttp1Handler
{
public:
	DownSessionHttp(
				ClientConnection& handle);
	virtual ~DownSessionHttp();

	virtual int on_read(unsigned char *data, size_t datalen);
	virtual int on_write();
	virtual int on_event(short events);
	virtual int DoFlush();

	virtual int32_t send(
						HttpMessagePtr&	message);

	virtual int htp_msg_begin(http_parser *htp);
	virtual int htp_statuscb(http_parser *htp, const char *data, size_t len);
	virtual int htp_hdr_keycb(http_parser *htp, const char *data, size_t len);
	virtual int htp_hdr_valcb(http_parser *htp, const char *data, size_t len);
	virtual int htp_hdrs_completecb(http_parser *htp);
	virtual int htp_bodycb(http_parser *htp, const char *data, size_t len);
	virtual int htp_msg_completecb(http_parser *htp);

	HttpMessagePtr&		GetMessage() { return message_; }

protected:
	HttpMessagePtr message_;
	HttpHeaderField curheader_;
	std::string  protocoloption_;
};

#endif /* DOWNSESSIONHTTP_H_ */
