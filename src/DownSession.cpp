#include "DownSession.h"
#include "ClientConnection.h"
#include <sstream>

DownSession::DownSession(ClientConnection& handle)
	: handler_(handle)
{

}

DownSession::~DownSession()
{

}

bool DownSession::IsStoreMessage(
		HttpMessage*	message)
{
	bool store = true;
	HttpResponse* res = dynamic_cast<HttpResponse*>(message);
	if(res == nullptr)
	{
		return true;
	}

// TODO : メッセージ保存有無（圧縮有無）判定

	// 接続先判定
	std::string host = handler_.getHost();


	// contents-encording判定
	HttpHeaderField* cefield = res->headers.get("Content-Encoding");
	if( cefield == nullptr )
	{
		return false;
	}
	if( cefield->value != "gzip")
	{
		return false;
	}

	return true;
}


int DownSession::inflate(
		HttpMessage*	message)
{
	HttpResponse* res = dynamic_cast<HttpResponse*>(message);
	if(res == nullptr)
	{
		return -1;
	}

	if(res->storemessage == false)
	{
		return 0;
	}

	size_t payloadsize = res->payload.size();
	if( payloadsize <= 0)
	{
		return 0;
	}

	if(res->inflater == nullptr)
	{
		res->inflater = std::unique_ptr<GzipInflater>(new GzipInflater(16384,16384));
	}

	int rv = 0;
	while(res->payload.size() > 0)
	{
		rv = res->inflater->inflate(
						res->payload.pos(),
						&payloadsize);
		if( rv != 0 )
		{
			return -1;
		}
		res->payload.drain(payloadsize);
	}

	return rv;
}

int DownSession::inflateEnd(
		HttpMessage*	message)
{
	HttpResponse* res = dynamic_cast<HttpResponse*>(message);
	if(res == nullptr)
	{
		return -1;
	}

	if(res->storemessage == false)
	{
		return 0;
	}

	if(res->inflater == nullptr)
	{
		return 0;
	}


	int rv = 0;
	// 全消去
	size_t payloadsize = res->payload.size();
	while(res->payload.size() > 0)
	{
		rv = res->inflater->inflate(
						res->payload.pos(),
						&payloadsize);
		if( rv != 0 )
		{
			return -1;
		}
		res->payload.drain(payloadsize);
	}

	// ペイロードを圧縮データに入れ替える
	res->payload.add(res->inflater->data(), res->inflater->size());

	// ヘッダを削除する
	res->headers.remove("Content-Encoding");
	res->headers.remove("Transfer-Encoding");

	// サイズの書き換え
	std::stringstream contentlength;
	contentlength << res->payload.size();
	HttpHeaderField* clfield = res->headers.get("Content-Length");
	if( clfield == nullptr )
	{
		res->headers.append("Content-Length", contentlength.str().c_str());
	}
	else
	{
		clfield->value = contentlength.str();
	}

	return rv;
}
