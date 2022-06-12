#ifndef TLSWRAPPER_H_
#define TLSWRAPPER_H_

#include "Common.h"
#include <vector>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/conf.h>

class TlsWrapper
{
public:
	TlsWrapper();
	virtual ~TlsWrapper();

	SSL_CTX* get_ssl_cts();
private:
	SSL_CTX *create_ssl_ctx();

public:
	static TlsWrapper*  tlsw;

private:
	std::vector<SSL_CTX*> ctx_list_;
};

#endif /* TLSWRAPPER_H_ */
