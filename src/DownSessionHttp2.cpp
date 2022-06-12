#include "ClientConnection.h"
#include "DownSessionHttp2.h"
#include "ServerConnection.h"
#include "UpSessionHttp2.h"
#include <event2/bufferevent_ssl.h>
#include "GzipInflater.h"


DownSessionHttp2::DownSessionHttp2(ClientConnection& handle)
	: DownSession(handle) , SessionHttp2Handler()
	, curResponse_()
{
	create_client();

}

DownSessionHttp2::~DownSessionHttp2()
{
	DoFlush();
}

int DownSessionHttp2::on_read(unsigned char *data, size_t datalen)
{
	if (datalen == 0 /* || handler_->get_should_close_after_write()*/)
	{
		return 0;
	}

	size_t readlen = 0;
	uint8_t* readpos = data;


	{
		readlen += recv(data, datalen - readlen);
		readpos += readlen;
	}

	DoFlush();
	return readlen;
}

int DownSessionHttp2::DoFlush()
{
	ServerConnection* server = handler_.getServer();
	if(server == nullptr)
	{
		return -1;
	}

	auto it = messages_.begin();
	while (it != messages_.end())
	{
		HttpMessagePtr& mes = *it;
		bool sentsetting = server->IsSentSetting();

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

		server->submit_message(mes);
		it = messages_.erase(it);
	}

	session_send();
	return 0;
}


int32_t DownSessionHttp2::send(
		HttpMessagePtr&	message)
{
	int32_t rv = 0;
	rv = submit_message(message);
	return rv;
}


ssize_t DownSessionHttp2::send_callback(
			const uint8_t *data,
			size_t length,
			int flags)
{
	(int)flags; // The flags is currently not used and always 0.
	size_t writelen = 0;
	writelen = handler_.write(data,length);
	return (ssize_t)writelen;
}

int DownSessionHttp2::on_begin_frame_callback(
                    const nghttp2_frame_hd *hd)
{

	warnx("on_begin_frame_callback type=%d flags=%x, length=%d, streamid=%d",
			hd->type, hd->flags, hd->length, hd->stream_id);

	HttpMessagePtr& mes = getMessage(*hd, true);
	if( mes == nullptr )
	{
		warnx(">>>>>>>>>>>>>>>>>>>>> Create type=%d flags=%x, length=%d, streamid=%d",
				hd->type, hd->flags, hd->length, hd->stream_id);
		mes = HttpMessagePtr(new HttpResponse);
		mes->frame.hd = *hd;
		mes->procstat = HTTPPROCE_STAT_HEADER;
		appendMessage(mes);
	}

	return 0;
}




int DownSessionHttp2::on_header_callback2(
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

int DownSessionHttp2::on_data_chunk_recv_callback(uint8_t flags,
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

int DownSessionHttp2::on_frame_recv_callback(
                    const nghttp2_frame *frame)
{
	nghttp2_frame_hd searchhd;
	searchhd = frame->hd;
	HttpMessagePtr& mes = getMessage(searchhd,true);
	int rv = 0;

	switch(frame->hd.type)
	{
	case NGHTTP2_HEADERS:
		mes->storemessage = IsStoreMessage(mes.get());
		if(mes->storemessage == true)
		{
			// TODO: 特定ホストから受信したデータはgzip圧縮されている場合の展開&ヘッダ書き換え

		}

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
			rv = inflateEnd(mes.get());
			// TODO: エラー発生時のRST_STAREM送信
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

		rv = inflate(mes.get());
		// TODO: エラー発生時のRST_STAREM送信

		if( mes->storemessage == false ||
			(frame->hd.flags & NGHTTP2_FLAG_END_STREAM ) != 0 )
		{
			mes->procstat = HTTPPROCE_STAT_COMPLETE;
			rv = inflateEnd(mes.get());
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

int DownSessionHttp2::on_write()
{
  return 0;
}

int DownSessionHttp2::on_event(short events)
{

	if (events & BEV_EVENT_CONNECTED)
	{
		return 0;

	}
	return 0;
}
