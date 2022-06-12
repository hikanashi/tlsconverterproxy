#ifndef SESSIONHTTP2HANDLER_H_
#define SESSIONHTTP2HANDLER_H_

#include "Common.h"
#include "MemBuff.h"
#include "HttpHeader.h"

class SessionHttp2Handler
{
public:
	SessionHttp2Handler();
	virtual ~SessionHttp2Handler();

	ssize_t recv(
			const uint8_t *in,
            size_t inlen);

	int32_t submit_message(
					HttpMessagePtr& 		message);

	void submit_defaultsetting();
	void submit_goaway(
					int32_t last_stream_id,
					uint32_t error_code,
					std::string& opaque_data);

	int32_t get_last_proc_stream_id();
	int		session_send();


	virtual ssize_t send_callback(
						const uint8_t *data,
	                    size_t length,
						int flags);
	virtual int on_begin_frame_callback(
	                    const nghttp2_frame_hd *frame);
	virtual int on_frame_recv_callback(
	                    const nghttp2_frame *frame);
	virtual int on_data_chunk_recv_callback(uint8_t flags,
							int32_t stream_id, const uint8_t *data,
							size_t len);
	virtual int on_stream_close_callback(int32_t stream_id,
	                                    uint32_t error_code);
	virtual int on_header_callback2(
				const nghttp2_frame *frame,
				nghttp2_rcbuf *name, nghttp2_rcbuf *value,
				uint8_t flags);
	virtual int on_invalid_header_callback2(
			const nghttp2_frame *frame,
			nghttp2_rcbuf *name, nghttp2_rcbuf *value,
			uint8_t flags);
	virtual int on_frame_send_callback(
			const nghttp2_frame *frame);
	ssize_t select_padding_callback(
			const nghttp2_frame *frame, size_t max_payloadlen);

	virtual int on_begin_headers_callback(
            const nghttp2_frame *frame);
	virtual int before_frame_send_callback(
            const nghttp2_frame *frame);


	bool	IsSentSetting() { return sendsettingframe_; }
	bool	IsUpgradeing() { return upgrading_; }
	void	setUpgrading(bool upgrade) { upgrading_ = upgrade; }

	int session_upgrade2(
			const std::string& settings_payload,
			int head_request);

protected:
	void create_server();
	void create_client();
	void setcallbacks(nghttp2_session_callbacks *callbacks);


	int		set_stream_user_data(int32_t stream_id,
	                             void *stream_user_data);

	HttpMessagePtr&	getMessage(const nghttp2_frame_hd& hd,
								bool  exceptcomplete);
	HttpMessagePtr&	appendMessage(HttpMessagePtr& message);

	virtual int32_t submit_headers(
					HttpMessagePtr& 		message);

	int32_t set_next_stream_id(
						int32_t stream_id );

	int submit_settings(
	        uint8_t flags,
	        const nghttp2_settings_entry *iv,
	        size_t niv);
	int32_t submit_push_promise(
			uint8_t flags, int32_t stream_id,
			const nghttp2_nv *nva, size_t nvlen, void *promised_stream_user_data);
	int32_t submit_headers(
			uint8_t flags, int32_t stream_id,
			const nghttp2_priority_spec *pri_spec, const nghttp2_nv *nva, size_t nvlen,
			void *stream_user_data);
	int submit_data(
			uint8_t flags,
	        int32_t stream_id,
			MemBuff& body);
	int32_t submit_request(
			int32_t stream_id,
			const nghttp2_priority_spec *pri_spec, const nghttp2_nv *nva, size_t nvlen,
			MemBuff& body, void *stream_user_data);
	int submit_response(
			int32_t stream_id,
			const nghttp2_nv *nva, size_t nvlen,
			MemBuff& body);
	int submit_trailer(
				int32_t stream_id,
	            const nghttp2_nv *nva, size_t nvlen);
	int submit_ping(
			uint8_t flags,
			const uint8_t *opaque_data);
	int submit_window_update(
	        uint8_t flags,
	        int32_t stream_id,
	        int32_t window_size_increment);
	int submit_priority(
	        uint8_t flags,
	        int32_t stream_id,
			const nghttp2_priority_spec *pri_spec);
	int submit_rst_stream(
	        uint8_t flags, int32_t stream_id,
	        uint32_t error_code);
	int submit_goaway(
	        uint8_t flags, int32_t last_stream_id,
	        uint32_t error_code,
	        const uint8_t *opaque_data,
	        size_t opaque_data_len);
	int submit_shutdown_notice();


protected:
	nghttp2_session*	session_;
	nghttp2_option *	option_;
	std::vector<HttpMessagePtr> messages_;
	bool	sendsettingframe_;
	bool	upgrading_;
};

#endif /* SESSIONHTTP2HANDLER_H_ */
