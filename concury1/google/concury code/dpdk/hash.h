/*!
 \file hash.h
 Describes hash functions used in this project.
 */

#pragma once

#include <functional>
#include <type_traits>
#include <cinttypes>
#include <string>
#include <iostream>
#include "farmhash.h"

//! \brief A hash function that hashes keyType to uint32_t. When SSE4.2 support is found, use sse4.2 instructions, otherwise use default hash function  std::hash.
template<class K>
class Hasher32 {
public:
  uint32_t s;    //!< hash s.
  
  Hasher32()
    : s(0xe2211) {
  }
  
  Hasher32(uint32_t _s)
    : s(_s) {
  }
  
  //! set bitmask and s
  void setSeed(uint32_t _s) {
    s = _s;
  }
  
  template<class K1>
  inline typename std::enable_if<!std::is_same<K1, std::string>::value, const uint64_t *>::type
  getBase(const K &k0) const {
    return (const uint64_t *) &k0;
  }
  
  template<class K1>
  inline typename std::enable_if<std::is_same<K1, std::string>::value, const uint64_t *>::type
  getBase(const K &k0) const {
    return (const uint64_t *) &k0[0];
  }
  
  template<class K1>
  inline typename std::enable_if<!std::is_same<K1, std::string>::value, uint16_t>::type
  getKeyByteLength(const K &k0) const {
    return sizeof(K);
  }
  
  template<class K1>
  inline typename std::enable_if<std::is_same<K1, std::string>::value, uint16_t>::type
  getKeyByteLength(const K &k0) const {
    return k0.length();
  }
  
  inline uint32_t operator()(const K &k0) const {
    static_assert(sizeof(K) <= 32, "K length should be 32/64/96/128/160/192/224/256 bits");
    
    uint32_t crc1 = ~0;
    const uint64_t *base = getBase<K>(k0);
    uint64_t *k = const_cast<uint64_t *>(base);
    uint32_t s1 = s;
    const uint16_t keyByteLength = getKeyByteLength<K>(k0);
    
    for (int i = 7; i < keyByteLength; i += 8) {
      asm(".byte 0xf2, 0x48, 0xf, 0x38, 0xf1, 0xf1;" :"=S"(crc1) :"0"(crc1), "c" ((*k) + s1));
      s1 = ((((uint64_t) s1) * s1 >> 16) ^ (s1 << 2));
      k++;
    }
    
    if ((keyByteLength & 7) == 4) {  // for faster process
      uint32_t *k32;
      k32 = (uint32_t *) k;
      asm( ".byte 0xf2, 0xf, 0x38, 0xf1, 0xf1;" :"=S"(crc1) :"0" (crc1), "c" ((*k32) + s1));
      s1 = ((((uint64_t) s1) * s1 >> 16) ^ (s1 << 2));
      k++;
    } else if (keyByteLength & 7) {
      uint64_t padded = *k;  // higher bits to zero
      padded = padded & (((unsigned long long) -1) >> (64 - (keyByteLength & 7) * 8));
      
      asm(".byte 0xf2, 0x48, 0xf, 0x38, 0xf1, 0xf1;" :"=S"(crc1) :"0"(crc1), "c" (padded + s1));
      s1 = ((((uint64_t) s1) * s1 >> 16) ^ (s1 << 2));
      k++;
    }

//    asm( ".byte 0xf2, 0xf, 0x38, 0xf1, 0xf1;" :"=S"(crc1) :"0" (crc1), "c" (s1));
    crc1 ^= (crc1 >> (32 ^ (7 & s1)));
    return crc1;
//    const uint64_t *base = getBase<K>(k0);
//    const uint16_t keyByteLength = getKeyByteLength<K>(k0);
//    return farmhash::Hash32WithSeed((char *) base, (size_t) keyByteLength, s);
  }
};
//
////! \brief A hash function that hashes keyType to uint32_t. When SSE4.2 support is found, use sse4.2 instructions, otherwise use default hash function  std::hash.
//template<class K>
//class Hasher64 {
//public:
//  uint64_t s;    //!< hash s.
//
//  Hasher64()
//    : s(0xe2211e2399) {
//  }
//
//  explicit Hasher64(uint64_t _s)
//    : s(_s) {
//  }
//
//  //! set bitmask and s
//  void setSeed(uint64_t _s) {
//    s = _s;
//  }
//
//  template<class K1>
//  inline typename std::enable_if<!std::is_same<K1, std::string>::value, const uint64_t *>::type
//  getBase(const K &k0) const {
//    return (const uint64_t *) &k0;
//  }
//
//  template<class K1>
//  inline typename std::enable_if<std::is_same<K1, std::string>::value, const uint64_t *>::type
//  getBase(const K &k0) const {
//    return (const uint64_t *) &k0[0];
//  }
//
//  template<class K1>
//  inline typename std::enable_if<!std::is_same<K1, std::string>::value, uint16_t>::type
//  getKeyByteLength(const K &k0) const {
//    return sizeof(K);
//  }
//
//  template<class K1>
//  inline typename std::enable_if<std::is_same<K1, std::string>::value, uint16_t>::type
//  getKeyByteLength(const K &k0) const {
//    return k0.length();
//  }
//
//  inline uint64_t operator()(const K &k0) const {
//    static_assert(sizeof(K) <= 32, "K length should be 32/64/96/128/160/192/224/256 bits");
//
//    const uint64_t *base = getBase<K>(k0);
//    const uint16_t keyByteLength = getKeyByteLength<K>(k0);
//    return farmhash::Hash64WithSeed((char *) base, (size_t) keyByteLength, s);
//  }
//};
