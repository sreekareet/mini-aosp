// Parcel: a flat byte buffer with typed write/read, the unit of data sent over IPC.
// Real AOSP: android::Parcel (also carries file descriptors and Binder objects).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mini {

// Writes append at the end; reads consume from a cursor at the front.
//
// Error style: reads never throw. Reading past the end (or a nonsense length)
// returns a zero value and sets a sticky "bad" flag, like std::istream. Callers
// read all their fields, then check ok() once. Simple to write and hard to misuse:
// once bad, a Parcel stays bad.
class Parcel {
 public:
  Parcel() = default;
  static Parcel fromBytes(std::vector<uint8_t> bytes);

  void writeInt32(int32_t v);
  void writeBool(bool v);
  void writeFloat(float v);
  void writeString(std::string_view s);  // int32 length + bytes
  void append(const Parcel& other);      // all of other's bytes, ignoring its cursor

  int32_t readInt32();
  bool readBool();
  float readFloat();
  std::string readString();

  bool ok() const { return ok_; }
  void markBad() { ok_ = false; }  // for decoders that find semantically invalid data
  const std::vector<uint8_t>& bytes() const { return buf_; }
  size_t size() const { return buf_.size(); }
  size_t remaining() const { return buf_.size() - pos_; }

 private:
  void writeRaw(const void* p, size_t n);
  bool readRaw(void* p, size_t n);

  std::vector<uint8_t> buf_;
  size_t pos_ = 0;
  bool ok_ = true;
};

}  // namespace mini
