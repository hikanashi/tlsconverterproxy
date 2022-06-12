#include "SessionHttp2Handler.h"

#include "app_helper.h"
#include "HttpHeader.h"
#include <typeinfo>
#include "base64.h"

using namespace nghttp2;
using namespace std;

typedef struct {
	uint8_t length[3];
	uint8_t type;
	uint8_t	frags;
	uint8_t stream_id[4];	// 先頭1bitは予約領域
} HTTP2FrameHeaderRaw;

// TODO: 全体的に異常系のハンドリングを追加

SessionHttp2Handler::SessionHttp2Handler()
	: session_(nullptr)
	, option_(nullptr)
	, messages_()
	, sendsettingframe_(false)
	, upgrading_(false)
{

}

SessionHttp2Handler::~SessionHttp2Handler()
{
	if( session_ != nullptr)
	{
		nghttp2_session_del(session_);
	}

	if(option_ != nullptr)
	{
		nghttp2_option_del(option_);
	}
}

void SessionHttp2Handler::create_server()
{
	nghttp2_session_callbacks*	callbacks = nullptr;
	nghttp2_session_callbacks_new(&callbacks);
	setcallbacks(callbacks);

	nghttp2_option_new(&option_);
	nghttp2_option_set_no_auto_ping_ack(option_,1);
	nghttp2_option_set_no_http_messaging(option_, 1);

	nghttp2_session_server_new2(&session_, callbacks, this, option_);
	nghttp2_session_callbacks_del(callbacks);
}

void SessionHttp2Handler::create_client()
{

	nghttp2_session_callbacks*	callbacks = nullptr;
	nghttp2_session_callbacks_new(&callbacks);
	setcallbacks(callbacks);

	nghttp2_option_new(&option_);
	nghttp2_option_set_no_auto_ping_ack(option_,1);

	nghttp2_session_client_new2(&session_, callbacks, this, option_);
	nghttp2_session_callbacks_del(callbacks);
}

HttpMessagePtr& SessionHttp2Handler::getMessage(
					const nghttp2_frame_hd& hd,
					bool  exceptcomplete)
{
	static HttpMessagePtr emptyptr;

	for(auto it = messages_.begin(); it != messages_.end(); it++)
	{
		HttpMessagePtr& header = *it;
		if( hd.type != header->frame.hd.type )
		{
			if(header->storemessage == false)
			{
				continue;
			}

			if((hd.type == NGHTTP2_HEADERS && header->frame.hd.type == NGHTTP2_DATA) ||
				(hd.type == NGHTTP2_DATA && header->frame.hd.type == NGHTTP2_HEADERS))
			{
				;
			}
			else
			{
				continue;
			}
		}

		if( exceptcomplete == true &&
			header->procstat == HTTPPROCE_STAT_COMPLETE)
		{
			continue;
		}

		if( hd.stream_id == header->frame.hd.stream_id ||
			hd.stream_id < 0 )
		{
			return header;
		}
	}

	return emptyptr;
}

HttpMessagePtr& SessionHttp2Handler::appendMessage(HttpMessagePtr& message)
{
	messages_.push_back(std::move(message));
	return messages_.back();
}


void SessionHttp2Handler::submit_defaultsetting()
{


	HttpMessagePtr req = HttpMessagePtr(new HttpRequest);
	req->http_major = 2;
	req->http_minor = 0;
	req->frame.settings.hd.type = NGHTTP2_SETTINGS;
	req->frame.settings.hd.flags = NGHTTP2_FLAG_NONE;
	req->frame.settings.iv = NULL;
	req->frame.settings.niv = 0;
	req->procstat = HTTPPROCE_STAT_COMPLETE;
	appendMessage(req);
}

void SessionHttp2Handler::submit_goaway(
				int32_t last_stream_id,
				uint32_t error_code,
				std::string& opaque_data)
{


	HttpMessagePtr req = HttpMessagePtr(new HttpRequest);
	req->http_major = 2;
	req->http_minor = 0;
	req->frame.goaway.hd.type = NGHTTP2_GOAWAY;
	req->frame.goaway.hd.flags = NGHTTP2_FLAG_NONE;
	req->frame.goaway.last_stream_id = last_stream_id;
	req->frame.goaway.error_code = error_code;
	req->payload.add((uint8_t*)opaque_data.c_str(), opaque_data.size());
	req->frame.goaway.opaque_data = req->payload.pos();
	req->frame.goaway.opaque_data_len = req->payload.size();
	req->procstat = HTTPPROCE_STAT_COMPLETE;
	appendMessage(req);
}


int32_t SessionHttp2Handler::submit_message(
					HttpMessagePtr& 		message)
{
	int32_t rv = 0;
	size_t  nvlen = 0;
	const nghttp2_nv* nv = nullptr;
	size_t	trailer_nvlen = 0;
	const nghttp2_nv* trailer_nv = nullptr;

// TODO: タイプ別に処理を関数に分ける
	switch(message->frame.hd.type)
	{
	case NGHTTP2_DATA:
		set_stream_user_data(message->frame.hd.stream_id, message.get());
		rv = submit_data(
					message->frame.hd.flags,
					message->frame.hd.stream_id,
					message->payload);
		break;
	case NGHTTP2_HEADERS:
		rv = submit_headers(message);
		break;
	case NGHTTP2_PRIORITY:
		rv = submit_priority(
					message->frame.hd.flags,
					message->frame.hd.stream_id,
					&message->frame.priority.pri_spec);
		break;
	case NGHTTP2_RST_STREAM:
		rv = submit_rst_stream(
					message->frame.hd.flags,
					message->frame.hd.stream_id,
					message->frame.rst_stream.error_code);
		break;
	case NGHTTP2_SETTINGS:
		rv = submit_settings(
					message->frame.hd.flags,
					(const nghttp2_settings_entry *)message->payload.pos(),
					message->frame.settings.niv);
		break;
	case NGHTTP2_PUSH_PROMISE:
		set_stream_user_data(message->frame.hd.stream_id, message.get());
		nvlen = 0;
		nv = message->headers.getnv(nvlen);
		rv = submit_push_promise(
					message->frame.hd.flags,
					message->frame.hd.stream_id,
					nv,nvlen,message.get());
		break;
	case NGHTTP2_PING:
		rv = submit_ping(
					message->frame.hd.flags,
					message->frame.ping.opaque_data);
		break;
	case NGHTTP2_GOAWAY:
		rv = submit_goaway(
					message->frame.hd.flags,
					message->frame.goaway.last_stream_id,
					message->frame.goaway.error_code,
					message->frame.goaway.opaque_data,
					message->frame.goaway.opaque_data_len);
		break;
	case NGHTTP2_WINDOW_UPDATE:
		rv = submit_window_update(
					message->frame.hd.flags,
					message->frame.hd.stream_id,
					message->frame.window_update.window_size_increment);
		break;

	default:
		warnx("########default type:%d",message->frame.hd.type);
		break;
	}


	rv = session_send();
	return rv;
}

int32_t SessionHttp2Handler::submit_headers(
					HttpMessagePtr& 		message)
{
	int32_t rv = 0;
	size_t  nvlen = 0;
	const nghttp2_nv* nv = nullptr;
	size_t	trailer_nvlen = 0;
	const nghttp2_nv* trailer_nv = nullptr;
	// TODO : この関数はオーバライドしてリクエストとレスポンスで分ける

	nv = message->headers.getnv(nvlen);

	// HEADER/DATAフレームをそのまま転送する場合
	if(message->storemessage == false)
	{
		rv = submit_headers(
				message->frame.hd.flags,
				message->frame.hd.stream_id,
				&message->frame.headers.pri_spec,
				nv,nvlen,message.get());
		return rv;
	}

	if( message->frame.headers.cat == NGHTTP2_HCAT_REQUEST ||
			message->frame.headers.cat == NGHTTP2_HCAT_HEADERS)
	{
		rv = submit_request(
				message->frame.hd.stream_id,
				&message->frame.headers.pri_spec,
				nv,nvlen,
				message->payload,
				message.get());
	}
	else
	{
		set_stream_user_data(message->frame.hd.stream_id, message.get());
		rv = submit_response(
				message->frame.hd.stream_id,
				nv,nvlen,
				message->payload);
	}

	if(message->trailer.size() > 0)
	{
		trailer_nvlen = 0;
		trailer_nv = message->trailer.getnv(trailer_nvlen);
		rv = submit_trailer(
					message->trailerframe.hd.stream_id,
					trailer_nv, trailer_nvlen);
	}

}

int SessionHttp2Handler::session_send()
{
	int rv = nghttp2_session_send(session_);
	if(rv != 0)
	{
		warnx("########%s nghttp2_session_send is error=%d(%s)", __func__, rv, nghttp2_http2_strerror(rv));
	}

	return rv;
}

int SessionHttp2Handler::set_stream_user_data(
		int32_t stream_id,
        void *stream_user_data)
{
	int rv = nghttp2_session_set_stream_user_data(
					session_, stream_id, stream_user_data);
	if(rv != 0)
	{
		warnx("########%s nghttp2_session_set_stream_user_data is error=%d(%s)", __func__, rv, nghttp2_http2_strerror(rv));
	}

	return rv;
}


int SessionHttp2Handler::submit_settings(
        uint8_t flags,
        const nghttp2_settings_entry *iv,
        size_t niv)
{
	int rv = 0;

	if( (flags & NGHTTP2_FLAG_ACK) == 0)
	{
		rv = nghttp2_submit_settings(session_,
						flags,
						iv,niv);
		sendsettingframe_ = true;
	}
	else
	{
		HTTP2FrameHeaderRaw setting_ack;
		memset(&setting_ack, 0, sizeof(setting_ack));
		setting_ack.type = NGHTTP2_SETTINGS;
		setting_ack.frags |= NGHTTP2_FLAG_ACK;


		send_callback((const uint8_t*)(&setting_ack), sizeof(setting_ack), 0);
	}

	if(rv != 0)
	{
		warnx("########%s is error=%d(%s)", __func__, rv, nghttp2_http2_strerror(rv));
	}

	return rv;

}

int32_t SessionHttp2Handler::submit_push_promise(
		uint8_t flags, int32_t stream_id,
		const nghttp2_nv *nva, size_t nvlen, void *promised_stream_user_data)
{
	int32_t ret_stream_id = 0;
	ret_stream_id =   nghttp2_submit_push_promise(
		    			session_, flags, stream_id,
						nva, nvlen, promised_stream_user_data);
	if(ret_stream_id < 0)
	{
		warnx("########%s is error=%d(%s)", __func__, ret_stream_id, nghttp2_http2_strerror(ret_stream_id));
	}

	return ret_stream_id;
}


int32_t SessionHttp2Handler::set_next_stream_id(
					int32_t stream_id )
{
	int rv = 0;
	nghttp2_stream* header_stream = nullptr;
	int32_t ret_stream_id = stream_id;

	header_stream = nghttp2_session_find_stream(session_, stream_id);
	if( header_stream == NULL)
	{
		rv = nghttp2_session_set_next_stream_id(session_, stream_id);
		if(rv != 0)
		{
			warnx("########nghttp2_session_set_next_stream_id(%d) is error=%d(%s)",
					__func__, stream_id, rv, nghttp2_http2_strerror(rv));
		}
		ret_stream_id = -1;
	}

	return ret_stream_id;
}


int32_t SessionHttp2Handler::submit_headers(
		uint8_t flags, int32_t stream_id,
		const nghttp2_priority_spec *pri_spec, const nghttp2_nv *nva, size_t nvlen,
		void *stream_user_data)
{
	int rv = 0;
	int32_t send_stream_id = stream_id;
	send_stream_id = set_next_stream_id(stream_id);

	if( send_stream_id < 0)
	{
		rv = nghttp2_session_create_idle_stream(session_, stream_id, pri_spec);
		if(rv != 0)
		{
			warnx("########nghttp2_session_create_idle_stream(%d) is error=%d(%s)",
					__func__, stream_id, rv, nghttp2_http2_strerror(rv));
		}
	}

	int32_t ret_stream_id = 0;
	ret_stream_id =  nghttp2_submit_headers(session_,
						flags,
						send_stream_id,
						pri_spec,
						nva, nvlen, stream_user_data);

	if((send_stream_id == -1 && ret_stream_id < 0) ||
		send_stream_id != -1 && ret_stream_id != 0 )
	{
		warnx("########%s(stream_id=%d) is error=%d(%s)",
				__func__, stream_id, ret_stream_id, nghttp2_http2_strerror(ret_stream_id));
	}

	return ret_stream_id;
}

static ssize_t data_source_read_callback(
		nghttp2_session *session,
		int32_t stream_id,
		uint8_t *buf,
		size_t length,
		uint32_t *data_flags,
		nghttp2_data_source *source,
		void *user_data)
{
	MemBuff* inbuf = static_cast<MemBuff*>(source->ptr);

	HttpMessage* message = static_cast<HttpMessage*>(
							nghttp2_session_get_stream_user_data(
									session, stream_id));


	ssize_t bufsize = inbuf->size();
	if(bufsize > length)
	{
		bufsize = length;
	}

	if(bufsize > 0)
	{
		memcpy(buf,inbuf->pos(),bufsize);
		inbuf->drain(bufsize);
	}

	if( inbuf->size() == 0)
	{
		*data_flags |= NGHTTP2_DATA_FLAG_EOF;

		if( message->trailer.size() > 0 )
		{
			*data_flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
		}
	}

	return bufsize;

}

int SessionHttp2Handler::submit_data(
		uint8_t flags,
        int32_t stream_id,
		MemBuff& body)
{

	nghttp2_data_provider data_prod;
	data_prod.source.ptr = &body;
	data_prod.read_callback = data_source_read_callback;
	int rv = 0;


	rv = nghttp2_submit_data(session_, flags, stream_id, &data_prod);
	if(rv != 0)
	{
		warnx("########%s is error=%d(%s)", __func__, rv, nghttp2_http2_strerror(rv));
	}

	return rv;
}

int32_t SessionHttp2Handler::submit_request(
		int32_t stream_id,
		const nghttp2_priority_spec *pri_spec, const nghttp2_nv *nva, size_t nvlen,
		MemBuff& body, void *stream_user_data)
{
	nghttp2_stream* header_stream = nullptr;

	set_next_stream_id(stream_id);

	nghttp2_data_provider data_prod;
	data_prod.source.ptr = &body;
	data_prod.read_callback = data_source_read_callback;

	int32_t ret_stream_id = 0;
	ret_stream_id =  nghttp2_submit_request(session_,
						pri_spec,
						nva, nvlen, &data_prod, stream_user_data);

	if( ret_stream_id < 0 || stream_id != ret_stream_id )
	{
		warnx("########%s (stream_id=%d) is error=%d(%s)",
				__func__, stream_id, ret_stream_id, nghttp2_http2_strerror(ret_stream_id));
	}

	return ret_stream_id;
}

int  SessionHttp2Handler::submit_response(
		int32_t stream_id,
		const nghttp2_nv *nva, size_t nvlen,
		MemBuff& body)
{
	int rv = 0;
	nghttp2_stream* header_stream = nullptr;

	set_next_stream_id(stream_id);

	nghttp2_data_provider data_prod;
	data_prod.source.ptr = &body;
	data_prod.read_callback = data_source_read_callback;

	rv = nghttp2_submit_response(session_,
			stream_id, nva, nvlen, &data_prod);

	if( rv != 0)
	{
		warnx("########%s (stream_id=%d) is error=%d(%s)",
				__func__, stream_id, rv, nghttp2_http2_strerror(rv));
	}

	return rv;
}


int SessionHttp2Handler::submit_trailer(
			int32_t stream_id,
            const nghttp2_nv *nva, size_t nvlen)
{
	int rv = 0;
	rv = nghttp2_submit_trailer(session_,
	                              stream_id,
	                              nva,nvlen);
	if(rv != 0)
	{
		warnx("########%s is error=%d(%s)", __func__, rv, nghttp2_http2_strerror(rv));
	}

	return rv;
}



int SessionHttp2Handler::submit_ping(
		uint8_t flags,
		const uint8_t *opaque_data)
{
	int rv = 0;
	rv = nghttp2_submit_ping(session_, flags, opaque_data);

	if(rv != 0)
	{
		warnx("########%s is error=%d(%s)", __func__, rv, nghttp2_http2_strerror(rv));
	}

	return rv;
}


int SessionHttp2Handler::submit_priority(
        uint8_t flags,
        int32_t stream_id,
		const nghttp2_priority_spec *pri_spec)
{
	int rv = 0;
	rv =  nghttp2_submit_priority(session_,
				flags,stream_id, pri_spec);
	if(rv != 0)
	{
		warnx("########%s is error=%d(%s)", __func__, rv, nghttp2_http2_strerror(rv));
	}

	return rv;
}

int SessionHttp2Handler::submit_window_update(
        uint8_t flags,
        int32_t stream_id,
        int32_t window_size_increment)
{
	int rv = 0;
	rv = nghttp2_submit_window_update(session_,
						flags,
						stream_id,
						window_size_increment);
	if(rv != 0)
	{
		warnx("########%s is error=%d(%s)", __func__, rv, nghttp2_http2_strerror(rv));
	}

	return rv;
}

int SessionHttp2Handler::submit_rst_stream(
        uint8_t flags, int32_t stream_id,
        uint32_t error_code)
{
	int rv = 0;
	rv = nghttp2_submit_rst_stream(session_,flags,stream_id,error_code);

	if(rv != 0)
	{
		warnx("########%s is error=%d(%s)", __func__, rv, nghttp2_http2_strerror(rv));
	}

	return rv;
}

int SessionHttp2Handler::submit_goaway(
        uint8_t flags, int32_t last_stream_id,
        uint32_t error_code,
        const uint8_t *opaque_data,
        size_t opaque_data_len)
{
	int rv = 0;
	rv = nghttp2_submit_goaway(session_,
						flags, last_stream_id,
						error_code,
						opaque_data,
						opaque_data_len);
	if(rv != 0)
	{
		warnx("########%s is error=%d(%s)", __func__, rv, nghttp2_http2_strerror(rv));
	}

	return rv;
}

int SessionHttp2Handler::submit_shutdown_notice()
{
	int rv = 0;
	rv = nghttp2_submit_shutdown_notice(session_);
	if(rv != 0)
	{
		warnx("########%s is error=%d(%s)", __func__, rv, nghttp2_http2_strerror(rv));
	}

	return rv;
}

int SessionHttp2Handler::session_upgrade2(const std::string& settings_payload, int head_request)
{
	int rv = 0;
	auto payload = base64::decode(std::begin(settings_payload),std::end(settings_payload));

	if( nghttp2_session_check_server_session(session_) == 0)
	{
		upgrading_ = true;
	}

	rv = nghttp2_session_upgrade2(session_,
			 (const uint8_t*)payload.c_str(), payload.size(),
			 head_request, this);
	if(rv != 0)
	{
		warnx("########%s is error=%d(%s)", __func__, rv, nghttp2_http2_strerror(rv));
	}

	return rv;
}

int32_t SessionHttp2Handler::get_last_proc_stream_id()
{
	return nghttp2_session_get_last_proc_stream_id(session_);
}


ssize_t SessionHttp2Handler::recv(
				const uint8_t *in,
				size_t inlen)
{
	ssize_t readlen = 0;

	readlen = (ssize_t)nghttp2_session_mem_recv(session_, in, inlen);

    return readlen;
}



static ssize_t send_callback(nghttp2_session *session, const uint8_t *data,
                             size_t length, int flags, void *user_data)
{
	SessionHttp2Handler* conn = static_cast<SessionHttp2Handler*>(user_data);
	return conn->send_callback( data, length, flags);
}

static int on_begin_frame_callback(nghttp2_session *session,
                                  const nghttp2_frame_hd* hd, void *user_data)
{
	// WINDOW_UPDATEは接続ノード間でのフロー制御のため転送処理しない
	// 必要なタイミングでnghttp2内で実施される
	if( hd->type == NGHTTP2_WINDOW_UPDATE)
	{
		return 0;
	}

	SessionHttp2Handler* conn = static_cast<SessionHttp2Handler*>(user_data);

	if( conn->IsUpgradeing() )
	{
		if( hd->type == NGHTTP2_SETTINGS )
		{
			return 0;
		}
	}
	return conn->on_begin_frame_callback(hd);
}


static int on_frame_recv_callback(nghttp2_session *session,
                                  const nghttp2_frame *frame, void *user_data)
{
	SessionHttp2Handler* conn = static_cast<SessionHttp2Handler*>(user_data);

    const type_info& id = typeid(*conn);
	warnx("===== %s on_frame_recv_callback =====", id.name());
	verbose_on_frame_recv_callback(session,frame,user_data);

	// WINDOW_UPDATEは接続ノード間でのフロー制御のため転送処理しない
	// 必要なタイミングでnghttp2内で実施される
	if( frame->hd.type == NGHTTP2_WINDOW_UPDATE)
	{
		return 0;
	}

	if( conn->IsUpgradeing() )
	{
		if( frame->hd.type == NGHTTP2_SETTINGS )
		{
			return 0;
		}
	}
	return conn->on_frame_recv_callback(frame);
}

static int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                       int32_t stream_id, const uint8_t *data,
                                       size_t len, void *user_data)
{
	SessionHttp2Handler* conn = static_cast<SessionHttp2Handler*>(user_data);

    const type_info& id = typeid(*conn);
	warnx("===== %s on_data_chunk_recv_callback =====", id.name());
	verbose_on_data_chunk_recv_callback(session,flags,
            stream_id,data,len,user_data);


	return conn->on_data_chunk_recv_callback(flags, stream_id, data, len);
}

static int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                                    uint32_t error_code, void *user_data)
{
	SessionHttp2Handler* conn = static_cast<SessionHttp2Handler*>(user_data);
	return conn->on_stream_close_callback(stream_id, error_code);
}

static int on_header_callback2(nghttp2_session *session,
                              const nghttp2_frame *frame,
							  nghttp2_rcbuf *name, nghttp2_rcbuf *value,
                              uint8_t flags, void *user_data)
{
	SessionHttp2Handler* conn = static_cast<SessionHttp2Handler*>(user_data);

    const type_info& id = typeid(*conn);

    nghttp2_vec namebuf = nghttp2_rcbuf_get_buf(name);
    nghttp2_vec valuebuf = nghttp2_rcbuf_get_buf(value);

    warnx("===== %s on_header_callback =====", id.name());
	verbose_on_header_callback(session,frame,namebuf.base,namebuf.len,valuebuf.base,valuebuf.len,flags,user_data);


	return conn->on_header_callback2(frame, name, value, flags);
}

static int on_frame_send_callback(nghttp2_session *session, const nghttp2_frame *frame,
                           void *user_data)
{

	SessionHttp2Handler* conn = static_cast<SessionHttp2Handler*>(user_data);

    const type_info& id = typeid(*conn);
	warnx("===== %s on_frame_send_callback =====", id.name());
	verbose_on_frame_send_callback(session,frame,user_data);

	return conn->on_frame_send_callback(frame);
}


static ssize_t select_padding_callback(nghttp2_session *session,
		const nghttp2_frame *frame, size_t max_payloadlen, void *user_data)
{
	SessionHttp2Handler* conn = static_cast<SessionHttp2Handler*>(user_data);
	return conn->select_padding_callback(frame,max_payloadlen);
}


static int on_begin_headers_callback(nghttp2_session *session,
                                     const nghttp2_frame *frame,
                                     void *user_data)
{
	SessionHttp2Handler* conn = static_cast<SessionHttp2Handler*>(user_data);
	return conn->on_begin_headers_callback(frame);
}

static int before_frame_send_callback(nghttp2_session *session,
						const nghttp2_frame *frame, void *user_data)
{
	SessionHttp2Handler* conn = static_cast<SessionHttp2Handler*>(user_data);

	return conn->before_frame_send_callback(frame);
}

static int on_invalid_header_callback2(nghttp2_session *session,
				const nghttp2_frame *frame,
				nghttp2_rcbuf *name, nghttp2_rcbuf *value,
				uint8_t flags, void *user_data)

{
	SessionHttp2Handler* conn = static_cast<SessionHttp2Handler*>(user_data);

    const type_info& id = typeid(*conn);

    nghttp2_vec namebuf = nghttp2_rcbuf_get_buf(name);
    nghttp2_vec valuebuf = nghttp2_rcbuf_get_buf(value);

    warnx("===== %s on_invalid_header_callback2 =====", id.name());
	verbose_on_header_callback(session,frame,namebuf.base,namebuf.len,valuebuf.base,valuebuf.len,flags,user_data);

	return conn->on_invalid_header_callback2(frame,name,value,flags);
}

void SessionHttp2Handler::setcallbacks(nghttp2_session_callbacks *callbacks)
{
	  nghttp2_session_callbacks_set_send_callback(callbacks, ::send_callback);

	  nghttp2_session_callbacks_set_on_begin_frame_callback(callbacks,
	                                                       ::on_begin_frame_callback);

	  nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
	                                                       ::on_frame_recv_callback);

	  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
	      callbacks, ::on_data_chunk_recv_callback);

	  nghttp2_session_callbacks_set_on_stream_close_callback(
	      callbacks, ::on_stream_close_callback);

	  nghttp2_session_callbacks_set_on_header_callback2(
			  callbacks,::on_header_callback2);

	  nghttp2_session_callbacks_set_on_frame_send_callback(
			  callbacks,  ::on_frame_send_callback);

	  nghttp2_session_callbacks_set_on_begin_headers_callback(
	      callbacks, ::on_begin_headers_callback);

	  nghttp2_session_callbacks_set_before_frame_send_callback(
			  callbacks, ::before_frame_send_callback);

	  nghttp2_session_callbacks_set_select_padding_callback(
			  callbacks, ::select_padding_callback);

	  nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(
	      callbacks, nghttp2::verbose_on_invalid_frame_recv_callback);

	  nghttp2_session_callbacks_set_on_invalid_header_callback2(
			callbacks, ::on_invalid_header_callback2);

	  nghttp2_session_callbacks_set_error_callback2(
			  callbacks, nghttp2::verbose_error_callback);
}

ssize_t SessionHttp2Handler::send_callback(
			const uint8_t *data,
			size_t length,
			int flags)
{
	  return (ssize_t)length;
}

int SessionHttp2Handler::on_begin_frame_callback(
                    const nghttp2_frame_hd *frame)
{
	return 0;
}

int SessionHttp2Handler::on_frame_recv_callback(
                    const nghttp2_frame *frame)
{
	return 0;
}

int SessionHttp2Handler::on_data_chunk_recv_callback(uint8_t flags,
						int32_t stream_id, const uint8_t *data,
						size_t len)
{
	return 0;

}
int SessionHttp2Handler::on_stream_close_callback(int32_t stream_id,
                                    uint32_t error_code)
{
	return 0;
}
int SessionHttp2Handler::on_header_callback2(
                              const nghttp2_frame *frame,
							  nghttp2_rcbuf *name, nghttp2_rcbuf *value,
                              uint8_t flags)
{
	return 0;
}

int SessionHttp2Handler::on_invalid_header_callback2(
                              const nghttp2_frame *frame,
							  nghttp2_rcbuf *name, nghttp2_rcbuf *value,
                              uint8_t flags)
{

//	HttpMessage* message = static_cast<HttpMessage*>(
//							nghttp2_session_get_stream_user_data(
//										session_, frame->hd.stream_id));
//	if (message == nullptr)
//	{
//		return 0;
//	}

	int32_t stream_id = 0;
	if (frame->hd.type == NGHTTP2_PUSH_PROMISE)
	{
		stream_id = frame->push_promise.promised_stream_id;
	}
	else
	{
		stream_id = frame->hd.stream_id;
	}

	auto namebuf = nghttp2_rcbuf_get_buf(name);
	auto valuebuf = nghttp2_rcbuf_get_buf(value);

	warnx( "Invalid header field for stream_id=%d in frame type=%d:"
			" name=[%s] value=[%s]",
			stream_id, static_cast<uint32_t>(frame->hd.type),
			std::string((char*)namebuf.base, namebuf.len).c_str(),
			std::string((char*)valuebuf.base, valuebuf.len).c_str());

	submit_rst_stream(NGHTTP2_FLAG_NONE, stream_id, NGHTTP2_PROTOCOL_ERROR);

	return 0;
}


int SessionHttp2Handler::on_frame_send_callback(
		const nghttp2_frame *frame)
{
	return 0;
}

ssize_t SessionHttp2Handler::select_padding_callback(
		const nghttp2_frame *frame, size_t max_payloadlen)
{
	size_t padding = 0;

	HttpMessage* message = static_cast<HttpMessage*>(
							nghttp2_session_get_stream_user_data(
										session_, frame->hd.stream_id));

	if(message == nullptr)
	{
		return std::min(max_payloadlen, frame->hd.length);
	}

	nghttp2_frame *paddingframe = &message->frame;
	if(paddingframe->hd.type != frame->hd.type)
	{
		if(message->dataframe.hd.type == frame->hd.type)
		{
			paddingframe = &message->dataframe;
		}
	}

	switch(paddingframe->hd.type)
	{
	case NGHTTP2_DATA:
		padding = paddingframe->data.padlen;
		break;
	case NGHTTP2_HEADERS:
		padding = paddingframe->headers.padlen;
		break;
	case NGHTTP2_PUSH_PROMISE:
		padding = paddingframe->push_promise.padlen;
		break;
	default:
		break;
	}

	if( (paddingframe->hd.flags & NGHTTP2_FLAG_PADDED ) == 0 )
	{
		padding = 0;
	}

	return std::min(max_payloadlen, frame->hd.length + padding);
}

int SessionHttp2Handler::on_begin_headers_callback(
        const nghttp2_frame *frame)
{
	return 0;
}


int SessionHttp2Handler::before_frame_send_callback(
        const nghttp2_frame *frame)
{
	if( IsUpgradeing() )
	{
		if( frame->hd.type == NGHTTP2_SETTINGS )
		{
			upgrading_ = false;
			return NGHTTP2_ERR_CANCEL;
		}
	}

	if(frame->hd.type == NGHTTP2_SETTINGS &&
		( frame->hd.flags & NGHTTP2_FLAG_ACK ) != 0)
	{
		return NGHTTP2_ERR_CANCEL;
	}

	return 0;
}

// TODO: 全体的にupとDownの処理共通化
