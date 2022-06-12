#include "TlsWrapper.h"

TlsWrapper* TlsWrapper::tlsw = NULL;

TlsWrapper::TlsWrapper()
	: ctx_list_(1)
{
  SSL_load_error_strings();
  SSL_library_init();

  SSL_CTX* ssl_ctx = create_ssl_ctx();

  ctx_list_[0] = ssl_ctx;
}

TlsWrapper::~TlsWrapper() {
	std::vector<SSL_CTX*>::iterator it = ctx_list_.begin();
	while (it != ctx_list_.end())
	{
	  SSL_CTX_free(*it);
	  ctx_list_.erase(it);
	}
}

SSL_CTX* TlsWrapper::get_ssl_cts()
{
	return ctx_list_[0];
}


/* Create SSL_CTX. */
SSL_CTX *TlsWrapper::create_ssl_ctx()
{
  SSL_CTX *ssl_ctx;
  ssl_ctx = SSL_CTX_new(SSLv23_client_method());
  if (!ssl_ctx) {
    errx(1, "Could not create SSL/TLS context: %s",
         ERR_error_string(ERR_get_error(), NULL));
  }

  SSL_CTX_set_options(ssl_ctx,
                      SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
                          SSL_OP_NO_COMPRESSION |
                          SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);

  return ssl_ctx;
}
