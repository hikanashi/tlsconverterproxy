#include "ServerConnection.h"
#include "ClientConnection.h"
#include <string>
#include <cstdlib>
#include "UpSessionHttp2.h"
#include "util.h"


UpSessionHttp2::UpSessionHttp2(ServerConnection& handler)
	: UpSession(handler), SessionHttp2Handler()
	, recvclientmagic_(false)
{
	create_server();
}

UpSessionHttp2::~UpSessionHttp2()
{
	DoFlush();
}


int UpSessionHttp2::on_read(unsigned char *data, size_t datalen)
{
	size_t readlen = 0;
	unsigned char * readpos = data;

	if (datalen == 0  /* || handler_.get_should_close_after_write()*/)
	{
		return readlen;
	}

	if(recvclientmagic_ == false )
	{
		if( datalen < NGHTTP2_CLIENT_MAGIC_LEN )
		{
			return readlen;
		}

		if(	memcmp(NGHTTP2_CLIENT_MAGIC, readpos, NGHTTP2_CLIENT_MAGIC_LEN) == 0)
		{
			recvclientmagic_ = true;
		}
		else
		{
			return readlen;
		}
	}

    readlen += recv(data, datalen);


	auto& client = handler_.getClient();

	if( client== nullptr )
	{
		nghttp2_frame_hd searchhd;
		searchhd.stream_id = -1;
		searchhd.type = NGHTTP2_HEADERS;

		HttpMessagePtr& mes = getMessage(searchhd, false);
		HttpRequest* req = dynamic_cast<HttpRequest*>(mes.get());
		if(req != nullptr &&
			req->host.size() > 0)
		{
			handler_.init_client(
					req->host,
					req->port,
					ConnectionProtocol_HTTP2);
		}
	}

	DoFlush();

	return readlen;
}

int32_t UpSessionHttp2::send(
		HttpMessagePtr&	message)
{
	return submit_message(message);
}



int UpSessionHttp2::on_write()
{
	return 0;
}

int UpSessionHttp2::on_event()
{
	return 0;
}

int UpSessionHttp2::DoFlush()
{
	auto& client = handler_.getClient();
	if(client == nullptr)
	{
		return -1;
	}

	if( client->IsConnected() != true )
	{
		return -1;
	}

	auto it = messages_.begin();
	while (it != messages_.end())
	{
		HttpMessagePtr& mes = *it;
		bool sentsetting = client->IsSentSetting();

		// 最初のSETTINGフレーム送信までは他のフレームは送信しない
		if(sentsetting == false)
		{
			if( mes->frame.hd.type != NGHTTP2_SETTINGS ||
				( mes->frame.hd.flags & NGHTTP2_FLAG_ACK ) != 0)
			{
				it++;
				continue;
			}
		}

		// メッセージが完成していないフレームは送信しない(受信中のDATAフレームなど)
		if(mes->procstat != HTTPPROCE_STAT_COMPLETE)
		{
			it++;
			continue;
		}

		client->submit_message(mes);
// TODO: メッセージ送信失敗や内部異常でRST_FRAMEを送信する
		it = messages_.erase(it);
	}

	session_send();
	return 0;
}


HttpMessagePtr UpSessionHttp2::createRequest()
{
	HttpMessagePtr req = HttpMessagePtr(new HttpRequest);
	req->procstat = HTTPPROCE_STAT_INIT;
	req->http_major = 2;
	req->http_minor = 0;
	return req;
}


ssize_t UpSessionHttp2::send_callback(
			const uint8_t *data,
			size_t length,
			int flags)
{
	(int)flags; // The flags is currently not used and always 0.
	size_t writelen = 0;
	writelen = handler_.write(data,length);
	return (ssize_t)writelen;
}

int UpSessionHttp2::on_begin_frame_callback(
                    const nghttp2_frame_hd *hd)
{

	HttpMessagePtr& mes = getMessage(*hd,true);
	if( mes == nullptr )
	{
		warnx("<<<<<<<<<<<<<<<<<<<<< Create type=%d flags=%x, length=%d, streamid=%d",
				hd->type, hd->flags, hd->length, hd->stream_id);
		mes= createRequest();
		mes->frame.hd = *hd;
		mes->procstat = HTTPPROCE_STAT_HEADER;
		appendMessage(mes);

	}

	return 0;
}

int UpSessionHttp2::on_header_callback2(
                              const nghttp2_frame *frame,
							  nghttp2_rcbuf *name, nghttp2_rcbuf *value,
                              uint8_t flags)
{
	nghttp2_frame_hd searchhd;
	searchhd = frame->hd;
	HttpMessagePtr& mes = getMessage(searchhd,true);
    nghttp2_vec namebuf = nghttp2_rcbuf_get_buf(name);
    nghttp2_vec valuebuf = nghttp2_rcbuf_get_buf(value);


    // TODO:最大ヘッダフィールド数、最大ヘッダデータサイズでのガード処置(データ破棄)
	if(frame->hd.type == NGHTTP2_HEADERS &&
			mes->procstat == HTTPPROCE_STAT_BODY )
	{
		mes->trailer.append(namebuf.base,namebuf.len, valuebuf.base, valuebuf.len);
	}
	else
	{
		mes->headers.append(namebuf.base,namebuf.len, valuebuf.base, valuebuf.len);
	}

	return 0;
}

int UpSessionHttp2::on_data_chunk_recv_callback(uint8_t flags,
						int32_t stream_id, const uint8_t *data,
						size_t len)
{

	nghttp2_frame_hd searchhd;
	searchhd.stream_id = stream_id;
	searchhd.type = NGHTTP2_DATA;

	HttpMessagePtr& mes = getMessage(searchhd,true);

	mes->payload.add(data,len);

	return 0;

}

int UpSessionHttp2::on_frame_recv_callback(
                    const nghttp2_frame *frame)
{
	nghttp2_frame_hd searchhd;
	searchhd = frame->hd;
	HttpMessagePtr& mes = getMessage(searchhd,true);

	// upgrade2 実行時は on_begin_frame_callbackが実行されない
	if( mes == nullptr)
	{
		return 0;
	}


	HttpRequest* req = nullptr;
	HttpHeaderField* pathfld = nullptr;
	HttpHeaderField* schemefld = nullptr;

	HttpHeaderField* authorityfld = nullptr;
	size_t portpos = 0;
	int rv = 0;

	switch(frame->hd.type)
	{
	case NGHTTP2_HEADERS:
		req = dynamic_cast<HttpRequest*>(mes.get());
		if(req == nullptr)
		{
			warnx("HEADER frame is'nt HttpRequest stream=%d", frame->hd.stream_id);
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		}

		pathfld = req->headers.get(":path");

		if(pathfld != nullptr)
		{
			parseURI(pathfld->value.c_str(),
					pathfld->value.size(),
					req->scheme,
					req->host,
					req->port,
					req->path);
			pathfld->value = req->path;
		}

		if(req->host.size() == 0)
		{
			authorityfld = req->headers.get(":authority");
			if( authorityfld != nullptr)
			{
				parseAuthority(
						authorityfld->value.c_str(),
						authorityfld->value.size(),
						req->host, req->port);
			}
		}


		schemefld = req->headers.get(":scheme");

		if(schemefld != nullptr &&
			handler_.IsSSL() )
		{
			schemefld->value = "https";
		}

		mes->storemessage = IsStoreMessage(mes.get());

		// END_HEADER already received
		if(mes->storemessage == true &&
				mes->procstat == HTTPPROCE_STAT_BODY  &&
				(mes->frame.hd.flags & NGHTTP2_FLAG_END_HEADERS)  != 0)
		{
			mes->trailerframe = *frame;
		}
		else
		{
			mes->frame = *frame;
		}

		if( mes->storemessage == false ||
			(frame->hd.flags & NGHTTP2_FLAG_END_STREAM ) != 0 )
		{
			mes->procstat = HTTPPROCE_STAT_COMPLETE;
			rv = deflateEnd(mes.get());
		}
		else
		{
			mes->procstat = HTTPPROCE_STAT_BODY;
		}
		break;
	case NGHTTP2_DATA:
		if( mes->storemessage == true )
		{
			mes->dataframe = *frame;
		}
		else
		{
			mes->frame = *frame;
		}

		rv = deflate(mes.get());
		if(rv != 0)
		{
			// TODO: RST_STREAM送信
			return -1;
		}

		if( mes->storemessage == false ||
			(frame->hd.flags & NGHTTP2_FLAG_END_STREAM ) != 0 )
		{
			mes->procstat = HTTPPROCE_STAT_COMPLETE;
			rv = deflateEnd(mes.get());
		}

		break;
	case NGHTTP2_SETTINGS:
		mes->frame = *frame;
		mes->payload.add((uint8_t*)frame->settings.iv,frame->settings.niv * sizeof(nghttp2_settings_entry));
		mes->procstat = HTTPPROCE_STAT_COMPLETE;
		break;
	case NGHTTP2_GOAWAY:
		mes->frame = *frame;
		mes->payload.add(frame->goaway.opaque_data,frame->goaway.opaque_data_len);
		mes->procstat = HTTPPROCE_STAT_COMPLETE;
		break;
	default:
		mes->frame = *frame;
		mes->procstat = HTTPPROCE_STAT_COMPLETE;
		break;
	}

	return rv;
}

int UpSessionHttp2::appendHttp1Request(
		HttpMessagePtr&	mes)
{
	HttpRequest* req = dynamic_cast<HttpRequest*>(mes.get());

	// Http2-Settingsを反映
	HttpHeaderField* http2_setting = req->headers.get("Http2-Settings");
	if(http2_setting == nullptr)
	{
		return -1;
	}

	session_upgrade2(
				http2_setting->value,
				(req->method == "HEAD" ? 1 : 0));
	http2_setting = nullptr;


	// リクエストの前にコネクションプリフェイス(SETTINGフレーム)を送信するため予約する
	HttpMessagePtr setting = createRequest();
	setting->frame.hd.type = NGHTTP2_SETTINGS;
	setting->frame.hd.stream_id = 0;
	appendMessage(setting);

	// 受信したHTTP/1.1リクエストをHTTP/2に変換する
	bool setendheader = false;
	bool setendstream = false;

	if( req->trailer.size() > 0)
	{
		req->trailerframe.hd.stream_id = req->frame.hd.stream_id;

		req->trailerframe.hd.flags |= NGHTTP2_FLAG_END_HEADERS;
		setendheader = true;

		req->trailerframe.hd.flags |= NGHTTP2_FLAG_END_STREAM;
		setendstream = true;
	}

	if( req->payload.size() > 0)
	{
		req->dataframe.hd.stream_id = req->frame.hd.stream_id;
		if(setendstream == false)
		{
			req->dataframe.hd.flags |= NGHTTP2_FLAG_END_STREAM;
			setendstream = true;
		}
	}

	req->frame.hd.type = NGHTTP2_HEADERS;
	req->frame.hd.stream_id = 1;
	if( setendheader == false)
	{
		req->frame.hd.flags |= NGHTTP2_FLAG_END_HEADERS;
	}
	if( setendstream == false)
	{
		req->frame.hd.flags |= NGHTTP2_FLAG_END_STREAM;
	}

	// 転送しないヘッダ(接続固有ヘッダーフィールド)の削除
	HttpHeaderField* connfield = req->headers.get("Connection");
	if(connfield != nullptr)
	{
		// Connectionで指定されている内容(カンマ区切り)はすべて削除
		std::vector<std::string> revmovefields;
		util::split_string(connfield->value, ",", revmovefields);

		for( auto field : revmovefields)
		{
			util::trim(field);	// 前後の空白を除去
			req->headers.remove(field.c_str());
		}
		connfield = nullptr;
	}
	// Connectionを含む[RFC7540 8.1.2.2. 接続固有ヘッダーフィールド]を削除
	req->headers.remove("Connection");
	req->headers.remove("Upgrade");
	req->headers.remove("Keep-Alive");
	req->headers.remove("Proxy-Connection");
	req->headers.remove("Transfer-Encoding");

	// HTTP/2の擬似ヘッダーフィールド付与
	HttpHeaderBlock http2header;
	http2header.append(":method",req->method.c_str());
	http2header.append(":path",req->path.c_str());
	if( handler_.IsSSL())
	{
		http2header.append(":scheme","https");
	}
	else
	{
		http2header.append(":scheme","http");
	}

	std::string authority(req->host);
	if(req->port > 0)
	{
		authority += ":" + std::to_string(req->port );
	}
	http2header.append(":authority",authority.c_str());

	req->headers.prepend(http2header);

	int rv = 0;
	rv = deflate(mes.get());

	rv = deflateEnd(mes.get());


	HttpMessagePtr mesh2(mes.get());
	mes.release();

	appendMessage(mesh2);

	return 0;

}

