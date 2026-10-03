#pragma once
#include <Arduino.h>

class DSHA1 {
 public:
  static const size_t OUTPUT_SIZE = 20;

  DSHA1() { reset(); }

  DSHA1 &write(const unsigned char *data, size_t len) {
    size_t bufsize = bytes % 64;
    if (bufsize && bufsize + len >= 64) {
      size_t n = 64 - bufsize;
      memcpy(buf + bufsize, data, n);
      bytes += n;
      data += n;
      len -= n;
      transform(s, buf);
      bufsize = 0;
    }
    while (len >= 64) {
      transform(s, data);
      bytes += 64;
      data += 64;
      len -= 64;
    }
    if (len > 0) {
      memcpy(buf + bufsize, data, len);
      bytes += len;
    }
    return *this;
  }

  void finalize(unsigned char hash[OUTPUT_SIZE]) {
    const unsigned char pad[64] = {0x80};
    unsigned char sizedesc[8];
    writeBE64(sizedesc, bytes << 3);
    write(pad, 1 + ((119 - (bytes % 64)) % 64));
    write(sizedesc, 8);
    writeBE32(hash, s[0]);
    writeBE32(hash + 4, s[1]);
    writeBE32(hash + 8, s[2]);
    writeBE32(hash + 12, s[3]);
    writeBE32(hash + 16, s[4]);
  }

  DSHA1 &reset() {
    bytes = 0;
    initialize(s);
    memset(buf, 0, sizeof(buf));
    return *this;
  }

  DSHA1 &warmup() {
    static const unsigned char warm[] = "warmup-warmup-warmup";
    uint8_t out[20];
    reset().write(warm, sizeof(warm) - 1).finalize(out);
    return reset();
  }

 private:
  uint32_t s[5];
  unsigned char buf[64];
  uint64_t bytes = 0;

  static constexpr uint32_t k1 = 0x5A827999ul;
  static constexpr uint32_t k2 = 0x6ED9EBA1ul;
  static constexpr uint32_t k3 = 0x8F1BBCDCul;
  static constexpr uint32_t k4 = 0xCA62C1D6ul;

  static inline uint32_t f1(uint32_t b, uint32_t c, uint32_t d) { return d ^ (b & (c ^ d)); }
  static inline uint32_t f2(uint32_t b, uint32_t c, uint32_t d) { return b ^ c ^ d; }
  static inline uint32_t f3(uint32_t b, uint32_t c, uint32_t d) { return (b & c) | (d & (b | c)); }
  static inline uint32_t left(uint32_t x) { return (x << 1) | (x >> 31); }

  static inline void Round(uint32_t a, uint32_t &b, uint32_t c, uint32_t d, uint32_t &e,
                           uint32_t f, uint32_t k, uint32_t w) {
    e += ((a << 5) | (a >> 27)) + f + k + w;
    b = (b << 30) | (b >> 2);
  }

  static void initialize(uint32_t state[5]) {
    state[0] = 0x67452301ul;
    state[1] = 0xEFCDAB89ul;
    state[2] = 0x98BADCFEul;
    state[3] = 0x10325476ul;
    state[4] = 0xC3D2E1F0ul;
  }

  static inline uint32_t readBE32(const unsigned char *ptr) {
    uint32_t v;
    memcpy(&v, ptr, sizeof(v));
    return __builtin_bswap32(v);
  }

  static inline void writeBE32(unsigned char *ptr, uint32_t x) {
    x = __builtin_bswap32(x);
    memcpy(ptr, &x, sizeof(x));
  }

  static inline void writeBE64(unsigned char *ptr, uint64_t x) {
    x = __builtin_bswap64(x);
    memcpy(ptr, &x, sizeof(x));
  }

  static void transform(uint32_t *s, const unsigned char *chunk) {
    uint32_t a=s[0], b=s[1], c=s[2], d=s[3], e=s[4];
    uint32_t w[80];
    for (int i=0;i<16;i++) w[i]=readBE32(chunk+i*4);
    for (int i=16;i<80;i++) w[i]=left(w[i-3]^w[i-8]^w[i-14]^w[i-16]);
    for (int i=0;i<80;i++) {
      uint32_t f,k;
      if (i<20) { f=f1(b,c,d); k=k1; }
      else if (i<40) { f=f2(b,c,d); k=k2; }
      else if (i<60) { f=f3(b,c,d); k=k3; }
      else { f=f2(b,c,d); k=k4; }
      uint32_t temp=((a<<5)|(a>>27))+f+e+k+w[i];
      e=d; d=c; c=(b<<30)|(b>>2); b=a; a=temp;
    }
    s[0]+=a; s[1]+=b; s[2]+=c; s[3]+=d; s[4]+=e;
  }
};
