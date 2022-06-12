#ifndef HTTPHEADER_H_
#define HTTPHEADER_H_

#include "Common.h"
#include <string>
#include <string.h>
#include <vector>
#include "MemBuff.h"
#include "GzipInflater.h"
#include "GzipDeflater.h"

// HTTPヘッダは大文字小文字を区別しないので、大文字小文字を区別しないで比較する文字列型を定義
struct ci_char_traits : public std::char_traits < char >
{
	static int compare(const char *s1, const char *s2, size_t n)
	{
		return strncasecmp(s1, s2, n);
	}
};

typedef std::basic_string<char, ci_char_traits> cistring;

typedef struct HttpHeaderField
{
	cistring name;
	std::string value;
} HttpHeaderField;

typedef struct HttpHeaderBlock
{
	HttpHeaderField* get(const char* name)
	{
		for(HttpHeaderField& header : headerblock)
		{
			if(header.name ==  name )
			{
				return &header;
			}
		}
		return nullptr;
	}

	void update(
			HttpHeaderField* header)
	{
		if(header == nullptr)
		{
			return;
		}

		HttpHeaderField* hd = get(header->name.c_str());
		updateheader = true;

		if( hd != nullptr)
		{
			hd->value = header->value;
			return;
		}

		HttpHeaderField addheader;

		addheader.name = header->name;
		addheader.value = header->value;

		headerblock.push_back(addheader);
	}

	void append(
			const uint8_t *name, size_t namelen,
			const uint8_t *value,size_t valuelen)
	{
		HttpHeaderField header;
		header.name.assign((char*)name,namelen);
		header.value.assign((char*)value,valuelen);

		updateheader = true;
		headerblock.push_back(header);
	}

	void append(
			const char *name,
			const char *value)
	{
		HttpHeaderField header;
		header.name.assign(name);
		header.value.assign(value);

		updateheader = true;
		headerblock.push_back(header);
	}

	void prepend(
			HttpHeaderBlock& header)
	{
		headerblock.reserve(headerblock.size()+ header.size());
		headerblock.insert(
				headerblock.begin(),
				header.headerblock.begin(),
				header.headerblock.end());
//		std::copy(header.headerblock.begin(),header.headerblock.end(),std::front_inserter(headerblock));
		updateheader = true;
	}


	void remove(
			const char* name)
	{
		auto it = headerblock.begin();
		while (it != headerblock.end())
		{
			auto& header = *it;
			if(header.name == name)
			{
				it = headerblock.erase(it);
			}
			else
			{
				it++;
			}
		}

		updateheader = true;
	}

	const nghttp2_nv* getnv(size_t& nvlen)
	{
		if(updateheader == true)
		{
			nvheaders.clear();
			for(HttpHeaderField& header : headerblock)
			{
				nghttp2_nv nv;
				nv.name = (uint8_t*)header.name.c_str();
				nv.namelen = header.name.size();
				nv.value = (uint8_t*)header.value.c_str();
				nv.valuelen = header.value.size();
				nvheaders.push_back(nv);
			}
		}
		nvlen = nvheaders.size();
		return nvheaders.data();
	}

	void setnv(
			const nghttp2_nv *nva,
			size_t nvlen)
	{
		if(nva == nullptr && nvlen > 0 )
		{
			return;
		}

		updateheader = true;
		headerblock.clear();

		for(size_t idx=0; idx < nvlen; idx++)
		{
			HttpHeaderField header;
			const nghttp2_nv*	nv = &nva[idx];

			header.name.assign((char*)nv->name,nv->namelen);
			header.value.assign((char*)nv->value,nv->valuelen);
			headerblock.push_back(header);
		}
	}

	size_t size()
	{
		return headerblock.size();
	}

	const std::vector<HttpHeaderField>& getFields()
	{
		return headerblock;
	}

private:
	std::vector<HttpHeaderField> headerblock;
	std::vector<nghttp2_nv> nvheaders;
	bool updateheader;
} HttpHeaderBlock;


enum HTTPPROCSTAT
{
	HTTPPROCE_STAT_INIT = 0,
	HTTPPROCE_STAT_HEADER,
	HTTPPROCE_STAT_BODY,
	HTTPPROCE_STAT_TRAILER,
	HTTPPROCE_STAT_COMPLETE
};

typedef struct HttpMessage
{
	HTTPPROCSTAT procstat;
	HttpHeaderBlock headers;
	MemBuff	payload;	// DATA body(HTTP/1.x,HTTP2) / SETTING blocks(HTTP2) / GOAWAY opaque data(HTTP2)
	HttpHeaderBlock trailer;
	nghttp2_frame frame;	// HTTP2 frame(HTTP2 only)
	nghttp2_frame dataframe;	// HTTP2 frame(HTTP2 DATA frame only)
	nghttp2_frame trailerframe;	// HTTP2 frame(HTTP2 HEADER(trailer) frame only)
	unsigned short http_major;
	unsigned short http_minor;
	bool storemessage;

	HttpMessage()
		: procstat(HTTPPROCE_STAT_INIT)
		, headers()
		, payload()
		, trailer()
		, frame{0}
		, dataframe{0}
		, trailerframe{0}
		, http_major(0)
		, http_minor(0)
		, storemessage(false)
	{}
	virtual ~HttpMessage() {}
} HttpMessage;



typedef struct HttpRequest : public HttpMessage
{
	std::string host;
	uint16_t port;
	unsigned int methodnum;
	std::string method;
	std::string scheme;
	std::string authority;
	std::string path;
	unsigned int upgrade;
	bool upgrade_http2;
	std::unique_ptr<GzipDeflater> deflater;
	HttpRequest()
		: host()
		, port(0)
		, methodnum{0}
		, method{}
		, scheme()
		, authority()
		, path()
		, upgrade{0}
		, upgrade_http2(false)
		, deflater()
	{}
	virtual ~HttpRequest() {}
} HttpRequest;


typedef struct HttpResponse : public HttpMessage
{
	unsigned int status_code;
	std::string reason;
	std::unique_ptr<GzipInflater> inflater;
	HttpResponse()
		: status_code(0)
		, reason()
		, inflater()
	{}
	virtual ~HttpResponse() {}
} HttpResponse;

typedef std::unique_ptr<HttpMessage> HttpMessagePtr;
typedef std::unique_ptr<HttpRequest> HttpRequestPtr;



#endif /* HTTPHEADER_H_ */
