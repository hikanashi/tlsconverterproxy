#include "ClientConnection.h"
#include "DownSessionHttp.h"
#include "ServerConnection.h"
#include <sstream>

DownSessionHttp::DownSessionHttp(ClientConnection& handle)
	: DownSession(handle) , SessionHttp1Handler(HTTP_RESPONSE)
	, message_()
	, curheader_()
	, protocoloption_()
{

}

DownSessionHttp::~DownSessionHttp()
{
}

int DownSessionHttp::on_read(unsigned char *data, size_t datalen)
{;

	if (datalen == 0 /* || handler_->get_should_close_after_write()*/)
	{
		return 0;
	}

	size_t readlen = 0;

//	while( (datalen - readlen) > 0)
	{

		auto nread = recv(data, datalen);
		readlen += nread;
//		if(nread == 0)
//		{
//			break;
//		}

		DoFlush();
	}

	return readlen;
}

int DownSessionHttp::DoFlush()
{
	ServerConnection* server = handler_.getServer();
	if(server == nullptr)
	{
		return -1;
	}

	// メッセージが完成していないフレームは送信しない(受信中のDATAフレームなど)
	if(message_->procstat != HTTPPROCE_STAT_COMPLETE)
	{
		return 0;
	}

	server->submit_message(message_);
	HttpMessagePtr empty;
	message_.swap(empty);	// ポインタの所有権を移動してdeleteする

	return 0;
}


int DownSessionHttp::htp_msg_begin(http_parser *htp)
{
	if(message_ == nullptr)
	{
		message_ = HttpMessagePtr(new HttpResponse);
		message_->procstat = HTTPPROCE_STAT_HEADER;
	}

	return 0;
}

int DownSessionHttp::htp_statuscb(http_parser *htp, const char *data, size_t len)
{
	HttpResponse* res = dynamic_cast<HttpResponse*>(message_.get());

	if(res == nullptr)
	{
		return -1;
	}

	res->status_code = htp->status_code;
	res->reason.assign(data,len);
	return 0;
}

int DownSessionHttp::htp_hdr_keycb(http_parser *htp, const char *data, size_t len)
{
	HttpHeaderField empty;
	curheader_ = empty;

	curheader_.name.assign(data,len);

	return 0;
}

int DownSessionHttp::htp_hdr_valcb(http_parser *htp, const char *data, size_t len)
{
	HttpResponse* res = dynamic_cast<HttpResponse*>(message_.get());
	if(res == nullptr)
	{
		return -1;
	}

	curheader_.value.assign(data,len);

	if(res->procstat == HTTPPROCE_STAT_HEADER)
	{
		res->headers.append(
				(const uint8_t*)curheader_.name.c_str(), curheader_.name.size(),
				(const uint8_t*)curheader_.value.c_str(), curheader_.value.size());
	}
	else
	{
		res->trailer.append(
				(const uint8_t*)curheader_.name.c_str(), curheader_.name.size(),
				(const uint8_t*)curheader_.value.c_str(), curheader_.value.size());
	}

	return 0;
}

int DownSessionHttp::htp_hdrs_completecb(http_parser *htp)
{
	HttpResponse* res = dynamic_cast<HttpResponse*>(message_.get());
	if(res == nullptr)
	{
		return -1;
	}

	// TODO : メッセージ保存有無（圧縮有無）判定
	res->storemessage = IsStoreMessage(res);

	res->procstat = HTTPPROCE_STAT_BODY;
	return 0;
}

int DownSessionHttp::htp_bodycb(http_parser *htp, const char *data, size_t len)
{
	HttpResponse* res = dynamic_cast<HttpResponse*>(message_.get());
	if(res == nullptr)
	{
		return -1;
	}

	res->payload.add((const uint8_t*)data,len);
	return 0;
}

int DownSessionHttp::htp_msg_completecb(http_parser *htp)
{
	HttpResponse* res = dynamic_cast<HttpResponse*>(message_.get());
	if(res == nullptr)
	{
		return -1;
	}
	// TODO: 特定ホストのgzip圧縮&ヘッダ書き換え

	res->http_major = htp->http_major;
	res->http_minor = htp->http_minor;

	res->procstat = HTTPPROCE_STAT_COMPLETE;


	if(res->status_code == HTTP_STATUS_SWITCHING_PROTOCOLS )
	{
		HttpHeaderField* upgradefield = res->headers.get("Upgrade");
		if( upgradefield != nullptr )
		{
			handler_.setSwitchingProtocol(upgradefield->value, protocoloption_);
			protocoloption_.clear();
		}

	}

	return 0;
}


int32_t DownSessionHttp::send(
		HttpMessagePtr&	message)
{
	HttpRequest* req = dynamic_cast<HttpRequest*>(message.get());
	if(req == nullptr)
	{
		return 0;
	}

	std::stringstream sendbuf;

	sendbuf << req->method << " " << req->path << " HTTP/"
			<< req->http_major << "." << req->http_minor << "\r\n";
	for(auto& headfield : req->headers.getFields() )
	{
		sendbuf << headfield.name.c_str() << ":" << headfield.value << "\r\n";

		if(headfield.name == "HTTP2-Settings")
		{
			protocoloption_ = headfield.value;
		}

	}
	sendbuf << "\r\n";

	std::string buffer = sendbuf.str();
	handler_.write((uint8_t*)buffer.c_str(), buffer.size());
	warnx("%s", buffer.c_str());

	if(req->payload.size() > 0)
	{
		std::string derimiter = "\r\n";
		req->payload.add((uint8_t*)derimiter.c_str(), derimiter.size());
		size_t writelen = handler_.write(req->payload.pos(), req->payload.size());
		warnx("%s", req->payload.pos());
		req->payload.drain(writelen);
	}

	if(req->payload.size() <= 0 &&
		req->trailer.size() > 0)
	{
		sendbuf.str(""); // バッファをクリアする。
		sendbuf.clear(std::stringstream::goodbit);	//ストリームの状態をクリアする。この行がないと意図通りに動作しない

		for(auto& headfield : req->trailer.getFields() )
		{
			sendbuf << headfield.name.c_str() << ":" << headfield.value << "\r\n";
		}
		sendbuf << "\r\n";

		buffer = sendbuf.str();
		handler_.write((uint8_t*)buffer.c_str(), buffer.size());
		warnx("%s", buffer.c_str());
	}

	return 0;
}


int DownSessionHttp::on_write()
{
  return 0;
}

int DownSessionHttp::on_event(short events)
{
  return 0;
}
