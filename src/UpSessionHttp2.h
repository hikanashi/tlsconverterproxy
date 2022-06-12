#ifndef UPSESSIONHTTP2_H_
#define UPSESSIONHTTP2_H_

#include "Common.h"
#include "MemBuff.h"
#include "SessionHttp2Handler.h"
#include "UpSession.h"
#include "HttpHeader.h"

class ServerConnection;

class UpSessionHttp2 : public UpSession,
					  public SessionHttp2Handler
{
public:
	UpSessionHttp2(ServerConnection& handler);
	virtual ~UpSessionHttp2();

	virtual int on_read(unsigned char *data, size_t datalen);
	virtual int on_write();
	virtual int on_event();
	virtual int DoFlush();
	virtual int32_t send(
			HttpMessagePtr&	message);

	virtual ssize_t send_callback(
						const uint8_t *data,
	                    size_t length,
						int flags);
	virtual int on_begin_frame_callback(
            			const nghttp2_frame_hd *frame);
	virtual int on_header_callback2(
	                              const nghttp2_frame *frame,
								  nghttp2_rcbuf *name, nghttp2_rcbuf *value,
	                              uint8_t flags);
	virtual int on_data_chunk_recv_callback(uint8_t flags,
							int32_t stream_id, const uint8_t *data,
							size_t len);
	virtual int on_frame_recv_callback(
	                    const nghttp2_frame *frame);

	int appendHttp1Request(
			HttpMessagePtr&	mes);

private:
	HttpMessagePtr 	createRequest();

private:
	bool	recvclientmagic_;
};


#endif /* UPSTREAMHTTP_H_ */
