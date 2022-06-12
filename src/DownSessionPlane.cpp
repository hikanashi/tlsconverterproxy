#include "DownSessionPlane.h"
#include "ClientConnection.h"
#include "ServerConnection.h"

DownSessionPlane::DownSessionPlane(ClientConnection& handle)
	: DownSession(handle)
{

}

DownSessionPlane::~DownSessionPlane()
{
}


int DownSessionPlane::on_read(unsigned char *data, size_t datalen)
{;

  if (datalen == 0  /* || handler_->get_should_close_after_write()*/)
  {
    return 0;
  }

  int readlen = 0;
  readlen = handler_.getServer()->write(data,datalen);


  return readlen;
}

int DownSessionPlane::DoFlush()
{

	return 0;
}

int32_t DownSessionPlane::send(
		HttpMessagePtr&	message)
{
	return 0;
}


int DownSessionPlane::on_write()
{
  return 0;
}

int DownSessionPlane::on_event(short events)
{
  return 0;
}


