#include "ipc/parcel.h"

#include <cstring>

namespace mini {

// Values are copied in host byte order. Fine here because both ends are the same
// machine; a network protocol would fix an endianness.

Parcel Parcel::fromBytes(std::vector<uint8_t> bytes) {
  Parcel p;
  p.buf_ = std::move(bytes);
  return p;
}

void Parcel::writeRaw(const void* p, size_t n) {
  const auto* b = static_cast<const uint8_t*>(p);
  buf_.insert(buf_.end(), b, b + n);
}

bool Parcel::readRaw(void* p, size_t n) {
  if (!ok_ || remaining() < n) {
    ok_ = false;
    std::memset(p, 0, n);
    return false;
  }
  std::memcpy(p, buf_.data() + pos_, n);
  pos_ += n;
  return true;
}

void Parcel::writeInt32(int32_t v) { writeRaw(&v, sizeof(v)); }
void Parcel::writeBool(bool v) { writeInt32(v ? 1 : 0); }
void Parcel::writeFloat(float v) { writeRaw(&v, sizeof(v)); }

void Parcel::writeString(std::string_view s) {
  writeInt32(static_cast<int32_t>(s.size()));
  writeRaw(s.data(), s.size());
}

void Parcel::append(const Parcel& other) { writeRaw(other.buf_.data(), other.buf_.size()); }

int32_t Parcel::readInt32() {
  int32_t v = 0;
  readRaw(&v, sizeof(v));
  return v;
}

bool Parcel::readBool() { return readInt32() != 0; }

float Parcel::readFloat() {
  float v = 0;
  readRaw(&v, sizeof(v));
  return v;
}

std::string Parcel::readString() {
  int32_t len = readInt32();
  // Never trust a length from the wire: check it against what is really there.
  if (!ok_ || len < 0 || static_cast<size_t>(len) > remaining()) {
    ok_ = false;
    return {};
  }
  std::string s(reinterpret_cast<const char*>(buf_.data() + pos_), static_cast<size_t>(len));
  pos_ += static_cast<size_t>(len);
  return s;
}

}  // namespace mini
