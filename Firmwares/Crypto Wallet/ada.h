#pragma once
//
// Cardano (ADA) support. Phase 1: address derivation.
//   - Icarus master key: PBKDF2-HMAC-SHA512(entropy, 4096, 96 bytes) then clamp.
//   - BIP32-Ed25519 (CIP-1852) derivation m/1852'/1815'/0'/{0,2}/i.
//   - Shelley base address = header || Blake2b224(paymentKey) || Blake2b224(stakeKey),
//     bech32-encoded as addr1... (mainnet) / addr_test1... (testnet).
//
// NOTE: Icarus derives from the raw entropy, so the optional BIP39 passphrase
// does NOT affect Cardano addresses (matches Yoroi/Eternl behaviour).

#include <Arduino.h>

namespace ada {

// Cardano Shelley base address for account 0, receive index `index`.
String address(const uint8_t *entropy, size_t entLen, const char *passphrase,
               uint32_t index, bool testnet);

// ---- Transaction signing ----------------------------------------------------
// Signs the Blake2b-256 hash of the CBOR tx body with the extended payment key.
// (On-device CBOR output display is a future enhancement; for now the tx hash is
// shown for host comparison.)  Interchange:  ADA|index|txBodyHex
bool     loadTx(const String &line);
String   txHashHex();     // Blake2b-256 tx-body hash (first bytes)
// Returns vkeyHex(64) || sigHex(128) so the host can build the witness. "" on fail.
String   signTx(const uint8_t *entropy, size_t entLen, const char *passphrase);
void     clearTx();

}  // namespace ada
