#ifndef COMMON_H_
#define COMMON_H_

#include <stdint.h>
#include <err.h>
#include <memory>
#include <utility>
#include <nghttp2/nghttp2.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/conf.h>

typedef enum ConnectionProtocol
{
	ConnectionProtocol_PLANE = 0,
	ConnectionProtocol_HTTP1,
	ConnectionProtocol_HTTP2,
	ConnectionProtocol_HTTP2Both
} ConnectionProtocol;


#endif /* COMMON_H_ */
