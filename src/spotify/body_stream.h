#pragma once
#include <Arduino.h>

// An HTTP response body as a Stream, for ArduinoJson to parse without buffering it.
// HTTPClient::getStream() is the raw connection: with "Transfer-Encoding: chunked" the chunk
// size lines are mixed into the data, and with Content-Length nothing marks where the body
// ends. This handles both, and drain() consumes whatever the parser left, so the keep-alive
// connection is clean for the next request.
class BodyStream : public Stream {
 public:
  BodyStream(Stream& in, bool chunked, int contentLength)
      : in_(in), chunked_(chunked), remain_(chunked ? 0 : contentLength), done_(!chunked && contentLength <= 0) {}

  int available() override {
    if (done_) return 0;
    return in_.available() > 0 ? 1 : 0;
  }

  int read() override {
    if (!ensure()) return -1;
    const int c = timedRead();
    if (c < 0) {
      done_ = true;  // timeout or closed: give up on this body
      return -1;
    }
    if (--remain_ == 0 && !chunked_) done_ = true;
    return c;
  }

  int peek() override {
    if (!ensure()) return -1;
    const uint32_t start = millis();
    while (in_.available() <= 0) {
      if (millis() - start > _timeout) return -1;
      delay(1);
    }
    return in_.peek();
  }

  size_t write(uint8_t) override { return 0; }

  // Read and discard the rest of the body.
  void drain() {
    while (read() >= 0) {
    }
  }

 private:
  // True when there is body data left to read (moving to the next chunk if needed).
  bool ensure() {
    if (done_) return false;
    if (remain_ > 0) return true;
    if (!chunked_) {
      done_ = true;
      return false;
    }
    if (started_) readLine();  // the CRLF that ends the previous chunk's data
    started_ = true;
    const String size = readLine();
    remain_ = strtol(size.c_str(), nullptr, 16);
    if (remain_ <= 0) {
      readLine();  // the empty line after the last chunk (no trailers from Spotify)
      done_ = true;
      return false;
    }
    return true;
  }

  int timedRead() {
    const uint32_t start = millis();
    while (true) {
      const int c = in_.read();
      if (c >= 0) return c;
      if (millis() - start > _timeout) return -1;
      delay(1);
    }
  }

  String readLine() {
    String s;
    while (true) {
      const int c = timedRead();
      if (c < 0 || c == '\n') break;
      if (c != '\r') s += (char)c;
      if (s.length() > 32) break;  // a size line is short; anything else is garbage
    }
    return s;
  }

  Stream& in_;
  bool chunked_;
  long remain_;
  bool done_;
  bool started_ = false;
};
