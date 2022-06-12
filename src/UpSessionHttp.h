#ifndef UPSESSIONHTTP_H_
#define UPSESSIONHTTP_H_

#include "Common.h"
#include "http-parser/http_parser.h"
#include "UpSession.h"
#include "SessionHttp1Handler.h"

class ServerConnection;

class UpSessionHttp : public UpSession,
					  public SessionHttp1Handler
{
public:
	UpSessionHttp(ServerConnection& handler);
	virtual ~UpSessionHttp();

	virtual int on_read(unsigned char *data, size_t datalen);
	virtual int on_write();
	virtual int on_event();
	virtual int DoFlush();

	virtual int32_t send(
			HttpMessagePtr&	message);
	size_t sendSwichingProtocol();
	size_t sendConnectResult();


	int htp_msg_begin(http_parser *htp);
	int htp_uricb(http_parser *htp, const char *data, size_t len);
	int htp_hdr_keycb(http_parser *htp, const char *data, size_t len);
	int htp_hdr_valcb(http_parser *htp, const char *data, size_t len);
	int htp_hdrs_completecb(http_parser *htp);
	int htp_bodycb(http_parser *htp, const char *data, size_t len);
	int htp_msg_completecb(http_parser *htp);

	HttpMessagePtr&	getRequest() { return request_; }

private:
	HttpHeaderField curheader_;
	HttpMessagePtr request_;

};


#endif /* UPSESSIONHTTP_H_ */
