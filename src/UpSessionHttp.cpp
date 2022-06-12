#include "ServerConnection.h"
#include <string>
#include <sstream>
#include "UpSessionHttp.h"
#include "ClientConnection.h"


UpSessionHttp::UpSessionHttp(ServerConnection& handler)
	: UpSession(handler) , SessionHttp1Handler(HTTP_REQUEST)
	, request_()
{
}

UpSessionHttp::~UpSessionHttp()
{
}


int UpSessionHttp::on_read(unsigned char *data, size_t datalen)
{

	if (datalen == 0 /* || handler_->get_should_close_after_write()*/)
	{
		return 0;
	}

	size_t readlen = 0;

//	while( (datalen - readlen) > 0)
	{

		auto nread = recv(data + readlen, datalen - readlen);
		readlen += nread;
//		if(nread == 0)
//		{
//			break;
//		}

		auto& client = handler_.getClient();

		if (client == nullptr)
		{
			HttpRequest* req = dynamic_cast<HttpRequest*>(request_.get());

			if(req != nullptr &&
					req->host.size() > 0)
			{
				enum ConnectionProtocol protocol = ConnectionProtocol_HTTP1;
				if( req->upgrade_http2 == true )
				{
					protocol = ConnectionProtocol_HTTP2Both;
				}
				else if( req->methodnum == HTTP_CONNECT )
				{
#ifndef CONNECTHTTP2
					protocol = ConnectionProtocol_PLANE;
#else
					protocol = ConnectionProtocol_HTTP2;
#endif
				}
				else
				{
					protocol = ConnectionProtocol_HTTP1;
				}

				handler_.init_client(
						req->host,
						req->port,
						protocol);
			}
		}

		DoFlush();
	}

	return readlen;
}

int UpSessionHttp::htp_msg_begin(http_parser *htp)
{
	if(request_ == nullptr)
	{
		request_ = HttpRequestPtr(new HttpRequest);
		request_->procstat = HTTPPROCE_STAT_HEADER;
	}

	return 0;
}

int UpSessionHttp::htp_uricb(http_parser *htp, const char *data, size_t len)
{
	HttpRequest* req = dynamic_cast<HttpRequest*>(request_.get());

	if(req == nullptr)
	{
		return -1;
	}


	req->methodnum = htp->method;
	req->method = http_method_str((enum http_method)htp->method);

	if(req->methodnum == HTTP_CONNECT)
	{
		parseAuthority(
				data, len,
				req->host,
				req->port);
	}
	else
	{
		parseURI(data,
			len,
			req->scheme,
			req->host,
			req->port,
			req->path);
	}
	return 0;
}

int UpSessionHttp::htp_hdr_keycb(http_parser *htp, const char *data, size_t len)
{
	HttpHeaderField empty;
	curheader_ = empty;

	curheader_.name.assign(data,len);

	return 0;
}

int UpSessionHttp::htp_hdr_valcb(http_parser *htp, const char *data, size_t len)
{
	HttpRequest* req = dynamic_cast<HttpRequest*>(request_.get());
	if(req == nullptr)
	{
		return -1;
	}

	curheader_.value.assign(data,len);
	if(req->host.size() == 0 &&
		curheader_.name == "host")
	{
		req->host = curheader_.value;
	}

	if(curheader_.name ==  "upgrade" &&
		curheader_.value == "h2c")
	{
		req->upgrade_http2 = true;
	}

	if(req->procstat == HTTPPROCE_STAT_HEADER)
	{
		req->headers.append(
				(const uint8_t*)curheader_.name.c_str(), curheader_.name.size(),
				(const uint8_t*)curheader_.value.c_str(), curheader_.value.size());
	}
	else
	{
		req->trailer.append(
				(const uint8_t*)curheader_.name.c_str(), curheader_.name.size(),
				(const uint8_t*)curheader_.value.c_str(), curheader_.value.size());
	}

	return 0;
}

int UpSessionHttp::htp_hdrs_completecb(http_parser *htp)
{
	HttpRequest* req = dynamic_cast<HttpRequest*>(request_.get());
	if(req == nullptr)
	{
		return -1;
	}

	// TODO : メッセージ保存有無（圧縮有無）判定
	req->storemessage = IsStoreMessage(req);

	req->procstat = HTTPPROCE_STAT_BODY;
	return 0;
}

int UpSessionHttp::htp_bodycb(http_parser *htp, const char *data, size_t len)
{
	HttpRequest* req = dynamic_cast<HttpRequest*>(request_.get());
	if(req == nullptr)
	{
		return -1;
	}

	req->payload.add((const uint8_t*)data,len);
	return 0;
}

int UpSessionHttp::htp_msg_completecb(http_parser *htp)
{
	HttpRequest* req = dynamic_cast<HttpRequest*>(request_.get());
	if(req == nullptr)
	{
		return -1;
	}
	// TODO: 特定ホストのgzip圧縮&ヘッダ書き換え

	req->http_major = htp->http_major;
	req->http_minor = htp->http_minor;
	req->upgrade = htp->upgrade;

	req->procstat = HTTPPROCE_STAT_COMPLETE;

	return 0;
}

int32_t UpSessionHttp::send(
		HttpMessagePtr&	message)
{

	HttpResponse* res = dynamic_cast<HttpResponse*>(message.get());
	if(res == nullptr)
	{
		return 0;
	}

	std::stringstream sendbuf;

	sendbuf << "HTTP/" << res->http_major << "." << res->http_minor << " "
			<< res->status_code << " " << res->reason << "\r\n";
	for(auto& headfield : res->headers.getFields() )
	{
		sendbuf << headfield.name.c_str() << ":" << headfield.value << "\r\n";
	}
	sendbuf << "\r\n";

	std::string buffer = sendbuf.str();
	handler_.write((uint8_t*)buffer.c_str(), buffer.size());
	warnx("%s", buffer.c_str());

	if(res->payload.size() > 0)
	{
		std::string derimiter = "\r\n";
		res->payload.add((uint8_t*)derimiter.c_str(), derimiter.size());
		size_t writelen = handler_.write(res->payload.pos(), res->payload.size());
		warnx("%s", res->payload.pos());
		res->payload.drain(writelen);
	}

	if(res->payload.size() <= 0 &&
		res->trailer.size() > 0)
	{
		sendbuf.str(""); // バッファをクリアする。
		sendbuf.clear(std::stringstream::goodbit);	//ストリームの状態をクリアする。この行がないと意図通りに動作しない

		for(auto& headfield : res->trailer.getFields() )
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

size_t UpSessionHttp::sendSwichingProtocol()
{
	std::string res ="HTTP/1.1 101 Switching Protocols\r\n"
						"Connection: Upgrade\r\n"
						"Upgrade: h2c\r\n"
						"\r\n";

	size_t writelen = 0;
	writelen = handler_.write((const uint8_t*)res.c_str(),res.size());

	warnx("send Upgrade Message(%d) ", writelen);
	warnx("%s",res.c_str());

	return writelen;
}

size_t UpSessionHttp::sendConnectResult()
{
	std::string res ="HTTP/1.1 200 OK\r\n"
						"\r\n";

	size_t writelen = 0;
	writelen = handler_.write((const uint8_t*)res.c_str(),res.size());

	warnx("send OK Message(%d) ", writelen);
	warnx("%s",res.c_str());

	return writelen;
}

int UpSessionHttp::on_write()
{
  return 0;
}

int UpSessionHttp::on_event()
{
  return 0;
}

int UpSessionHttp::DoFlush()
{
	HttpRequest* req = dynamic_cast<HttpRequest*>(request_.get());
	if(req == nullptr)
	{
		return 0;
	}

	if(req->upgrade_http2 == true &&
		handler_.IsSSL() == true)
	{
		return 0;
	}

	auto& client = handler_.getClient();
	if(client == nullptr)
	{
		return -1;
	}

	if( client->IsConnected() != true )
	{
		return -1;
	}

	// メッセージが完成していないフレームは送信しない(受信中のDATAフレームなど)
	if(req->procstat != HTTPPROCE_STAT_COMPLETE)
	{
		return 0;
	}

	client->submit_message(request_);
	HttpMessagePtr empty;
	request_.swap(empty);	// ポインタの所有権を移動してdeleteする
	return 0;
}
