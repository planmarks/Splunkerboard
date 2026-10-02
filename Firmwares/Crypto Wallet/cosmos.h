#pragma once
//
// Cosmos Hub (ATOM) support.
//   Address: BIP44 m/44'/118'/0'/0/i, secp256k1.
//            accountId = RIPEMD160(SHA256(compressedPubKey));
//            address   = bech32("cosmos", accountId).
//   Signing: SIGN_MODE_DIRECT — the host builds the protobuf SignDoc and sends
//            its bytes; the device signs SHA256(SignDoc) with secp256k1 (low-s)
//            and returns the 64-byte compact signature (r||s). Blind-sign: the
//            device shows the SHA256 digest for on-host verification.
//            App sends:    ATOM|acct|signDocHex
//            Device sends: ATOM-SIG> <64-byte r||s hex>
//
#include <Arduino.h>

namespace cosmos {

// Cosmos (cosmos1...) address for BIP44 account `index`.
String address(const uint8_t *entropy, size_t entLen,
               const char *passphrase, uint32_t index);

// Parse a signing request "ATOM|acct|signDocHex". Returns true if well-formed.
bool loadTx(const String &line);

// First 8 hex chars of the SHA256(SignDoc) digest (for on-screen verification).
String txHashHex();

// Sign the loaded SignDoc. Returns 64-byte r||s hex, "" on failure.
String signTx(const uint8_t *entropy, size_t entLen, const char *passphrase);

void clearTx();

}  // namespace cosmos
