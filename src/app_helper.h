#ifndef APP_HELPER_H
#define APP_HELPER_H

#include <stdlib.h>
#include <sys/time.h>
#include <poll.h>

#include <map>
#include <chrono>

#include <nghttp2/nghttp2.h>

namespace nghttp2 {

int verbose_on_header_callback(nghttp2_session *session,
                               const nghttp2_frame *frame, const uint8_t *name,
                               size_t namelen, const uint8_t *value,
                               size_t valuelen, uint8_t flags, void *user_data);

int verbose_on_frame_recv_callback(nghttp2_session *session,
                                   const nghttp2_frame *frame, void *user_data);

int verbose_on_invalid_frame_recv_callback(nghttp2_session *session,
                                           const nghttp2_frame *frame,
                                           int lib_error_code, void *user_data);

int verbose_on_frame_send_callback(nghttp2_session *session,
                                   const nghttp2_frame *frame, void *user_data);

int verbose_on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                        int32_t stream_id, const uint8_t *data,
                                        size_t len, void *user_data);

int verbose_error_callback(nghttp2_session *session, int lib_error_code,
                           const char *msg, size_t len, void *user_data);

// Returns difference between |a| and |b| in milliseconds, assuming
// |a| is more recent than |b|.
template <typename TimePoint>
std::chrono::milliseconds time_delta(const TimePoint &a, const TimePoint &b) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(a - b);
}

// Resets timer
void reset_timer();

// Returns the duration since timer reset.
std::chrono::milliseconds get_timer();

// Returns current time point.
std::chrono::steady_clock::time_point get_time();

void print_timer();

// Setting true will print characters with ANSI color escape codes
// when printing HTTP2 frames. This function changes a static
// variable.
void set_color_output(bool f);

// Set output file when printing HTTP2 frames. By default, stdout is
// used.
void set_output(FILE *file);


} // namespace nghttp2

#endif // APP_HELPER_H
