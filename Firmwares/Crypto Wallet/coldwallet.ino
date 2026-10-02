//
// Sentinel — Phase 6: optional BLE transport
//
// Cumulative features:
//  * BIP39 seed generation OR restore (enter an existing 12-word phrase via
//    letter-prefix autocomplete) — recovery after a wrong-PIN self-wipe.
//  * Optional BIP39 passphrase (25th word), stored encrypted with the seed.
//  * AES-256-GCM encrypted storage keyed by PBKDF2(PIN).
//  * Boot unlock; wrong-PIN self-wipe counter (survives power-cycle).
//  * Receive: BIP84 native-segwit addresses (m/84'/coin'/0'/0/i) as text + QR.
//  * Sign Tx: PSBT in over serial OR BLE, review outputs+fee on-device, hold
//    SELECT to confirm, device signs offline and returns the signed PSBT.
//  * Multi-coin: main-menu Coin cycles BTC / ETH / SOL / XLM / ADA, each with
//    receive + signing. ed25519 (SOL/XLM/ADA) via vendored TweetNaCl; Cardano
//    adds Icarus + BIP32-Ed25519 + Blake2b (vendored). ADA signing is blind-sign.
//  * Settings: coin, Testnet/Mainnet, BLE on/off (default off), export xpub, wipe.
//
// BLE is only a transport: a PSBT over BLE still needs on-device confirmation,
// and the radio only comes up after unlock. Secrets live only in RAM and are
// zeroized after use. The mnemonic is shown only on the OLED, never on Serial.
//
// Target: Seeed XIAO ESP32-C5  |  Arduino-ESP32 core 3.3.5+
// Requires libraries: U8g2, uBitcoin, NimBLE-Arduino (QRCode vendored as qrcodegen.*)
//
#include "config.h"
#include "display.h"
#include "buttons.h"
#include "wallet.h"
#include "store.h"
#include "signer.h"
#include "ble.h"
#include "eth.h"
#include "bnb.h"
#include "xrp.h"
#include "sol.h"
#include "xlm.h"
#include "ada.h"
#include "cosmos.h"
#include "tron.h"
#include "ton.h"

// ESP-IDF sleep APIs for the low-power idle state (CPU light sleep + GPIO wake).
#include "esp_sleep.h"
#include "driver/gpio.h"

// The default Arduino loop task stack (8 KB) is too small for the ed25519
// scalar multiplication (Solana/Stellar) on top of the UI/crypto call chain.
// Enlarge it so key derivation doesn't overflow the stack and reboot the device.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

Display display;
Buttons  buttons;

// ---- Unlocked wallet state (RAM only) --------------------------------------
static uint8_t g_entropy[32];
static size_t  g_entropyLen = 0;
static char    g_passphrase[store::MAX_PASSPHRASE + 1] = {0};  // BIP39 passphrase (may be empty)
static bool    g_unlocked   = false;

// Timestamp of the last user interaction (any button). Drives the inactivity
// auto-lock; updated wherever a button event is consumed.
static uint32_t g_lastActivityMs = 0;

static void lockWallet() {
  wallet::zeroize(g_entropy, sizeof(g_entropy));
  wallet::zeroize(g_passphrase, sizeof(g_passphrase));
  g_entropyLen = 0;
  g_unlocked   = false;
}

// ---- Menus ------------------------------------------------------------------
static const char *coinName(uint8_t c) {
  switch (c) {
    case COIN_ETH:  return "ETH";
    case COIN_BNB:  return "BNB";
    case COIN_XRP:  return "XRP";
    case COIN_USDT: return "USDT";
    case COIN_USDC: return "USDC";
    case COIN_AVAX: return "AVAX";
    case COIN_BASE: return "BASE";
    case COIN_POL:  return "POL";
    case COIN_CRO:  return "CRO";
    case COIN_ATOM: return "ATOM";
    case COIN_TRX:  return "TRX";
    case COIN_TON:  return "TON";
    case COIN_SOL:  return "SOL";
    case COIN_XLM:  return "XLM";
    case COIN_ADA:  return "ADA";
    default:        return "BTC";
  }
}

// EVM chains all share Ethereum's key/address and EIP-1559 signing (eth.cpp);
// only the chainId (carried in the tx line) differs. Grouping them here keeps the
// per-screen routing in one place — adding another EVM chain is a one-line change.
static bool isEvmCoin(uint8_t c) {
  return c == COIN_ETH  || c == COIN_BNB  || c == COIN_USDT || c == COIN_USDC ||
         c == COIN_AVAX || c == COIN_BASE || c == COIN_POL  || c == COIN_CRO;
}

// Main menu is built dynamically because the coin item shows live state.
static const uint8_t kMenuCount = 5;
static char          mainCoinLabel[24];
static const char   *mainItems[kMenuCount];
static void buildMainMenu() {
  snprintf(mainCoinLabel, sizeof(mainCoinLabel), "Coin: <%s>", coinName(store::coin()));  // <>=Left/Right
  mainItems[0] = mainCoinLabel;   // currency picker on top
  mainItems[1] = "Receive";
  mainItems[2] = "Sign Tx";
  mainItems[3] = "Settings";
  mainItems[4] = "About";
}

// Settings menu is built dynamically because the network/BLE items show live state.
static const uint8_t kSettingsCount = 7;
static char          netItemLabel[24];
static char          bleItemLabel[24];
static const char   *settingsItems[kSettingsCount];
static void buildSettings() {
  snprintf(netItemLabel, sizeof(netItemLabel), "Net: %s",
           store::isTestnet() ? "TESTNET" : "MAINNET");
  snprintf(bleItemLabel, sizeof(bleItemLabel), "BLE: %s",
           store::bleEnabled() ? "ON" : "OFF");
  settingsItems[0] = "Generate Seed";
  settingsItems[1] = "Restore Wallet";
  settingsItems[2] = netItemLabel;
  settingsItems[3] = bleItemLabel;
  settingsItems[4] = "Export xpub";
  settingsItems[5] = "Wipe wallet";
  settingsItems[6] = "Back";
}

enum Screen {
  SCREEN_MENU, SCREEN_SETTINGS, SCREEN_SEED, SCREEN_SEED_DONE, SCREEN_STUB,
  SCREEN_RECEIVE, SCREEN_RECEIVE_QR, SCREEN_SIGN_REVIEW, SCREEN_SIGN_DONE, SCREEN_XPUB
};
static Screen  screen      = SCREEN_MENU;
static uint8_t selected     = 0;   // main menu cursor
static uint8_t settingsSel  = 0;   // settings menu cursor
static const char *stubTitle = "";
static const char *stubBody  = "";

// ---- Receive view state -----------------------------------------------------
static uint32_t g_recvIndex = 0;
static String   g_recvAddr;
static String   g_recvPath;
static void updateReceive() {
  uint8_t c = store::coin();
  if (isEvmCoin(c))                 // ETH/BNB/USDT/USDC/AVAX/BASE/POL/CRO share the ETH address
    g_recvAddr = eth::address(g_entropy, g_entropyLen, g_passphrase, g_recvIndex);
  else if (c == COIN_XRP)
    g_recvAddr = xrp::address(g_entropy, g_entropyLen, g_passphrase, g_recvIndex);
  else if (c == COIN_ATOM)
    g_recvAddr = cosmos::address(g_entropy, g_entropyLen, g_passphrase, g_recvIndex);
  else if (c == COIN_TRX)
    g_recvAddr = tron::address(g_entropy, g_entropyLen, g_passphrase, g_recvIndex);
  else if (c == COIN_TON)
    g_recvAddr = ton::address(g_entropy, g_entropyLen, g_passphrase, g_recvIndex);
  else if (c == COIN_SOL)
    g_recvAddr = sol::address(g_entropy, g_entropyLen, g_passphrase, g_recvIndex);
  else if (c == COIN_XLM)
    g_recvAddr = xlm::address(g_entropy, g_entropyLen, g_passphrase, g_recvIndex);
  else if (c == COIN_ADA)
    g_recvAddr = ada::address(g_entropy, g_entropyLen, g_passphrase, g_recvIndex, store::isTestnet());
  else
    g_recvAddr = wallet::receiveAddress(g_recvIndex, &g_recvPath);

  // Echo to Serial and BLE with coin tag so the app routes correctly.
  const char* coinTag = coinName(store::coin());
  String addrLine = String("addr #") + g_recvIndex + " " + coinTag + ": " + g_recvAddr;
  Serial.println(addrLine);
  if (ble::isConnected()) ble::sendLine(addrLine);
}

// ---- Sign view state --------------------------------------------------------
static uint8_t g_signPage = 0;   // 0..storedOutputs()-1 = outputs; last = summary
static SignSource g_signSource = SRC_SERIAL;   // where to return the signed PSBT (enum in config.h)

// ---- xpub export state ------------------------------------------------------
static String g_xpub;

// Format satoshis as a BTC decimal string.
static String satToBtc(uint64_t sat) {
  char b[24];
  snprintf(b, sizeof(b), "%llu.%08llu",
           (unsigned long long)(sat / 100000000ULL),
           (unsigned long long)(sat % 100000000ULL));
  return String(b);
}

// ---- Seed backup view -------------------------------------------------------
static const uint8_t WORDS_PER_PAGE = 4;
static String  g_seedWords[24];
static uint8_t g_seedCount = 0;
static uint8_t g_seedPage  = 0;

static void clearSeedState() {
  for (uint8_t i = 0; i < 24; i++) g_seedWords[i] = "";
  g_seedCount = 0;
  g_seedPage  = 0;
}

static void splitMnemonic(const String &m) {
  clearSeedState();
  int start = 0;
  while (start < (int)m.length() && g_seedCount < 24) {
    int sp = m.indexOf(' ', start);
    if (sp < 0) { g_seedWords[g_seedCount++] = m.substring(start); break; }
    g_seedWords[g_seedCount++] = m.substring(start, sp);
    start = sp + 1;
  }
}

static uint8_t seedPageCount() {
  return (g_seedCount + WORDS_PER_PAGE - 1) / WORDS_PER_PAGE;
}

// ---- Blocking button helper -------------------------------------------------
static ButtonAction waitButton() {
  ButtonAction a;
  while (!buttons.poll(a)) delay(2);
  g_lastActivityMs = millis();   // interaction in a modal counts as activity
  return a;
}

static void waitForWakeButton();   // fwd decl (defined near auto-lock)

// ---- Modal: PIN entry -------------------------------------------------------
// Up/Down change the current digit, Select commits it, hold Select submits
// (needs >= PIN_MIN_DIGITS), Back deletes the last digit (or cancels if empty).
// Returns the digit count, or -1 if cancelled. `out` must hold PIN_MAX_DIGITS+1.
// When dimOnIdle is set (the unlock screen), the OLED blanks + the CPU light-
// sleeps after AUTO_LOCK_MS of no input, waking back to the PIN screen on a press.
static int pinEntryModal(const char *title, char *out, size_t maxDigits, bool dimOnIdle) {
  size_t n = 0;
  int digit = 0;
  uint32_t idleStart = millis();
  for (;;) {
    char stars[PIN_MAX_DIGITS + 1];
    size_t i = 0;
    for (; i < n && i < maxDigits; i++) stars[i] = '*';
    stars[i] = '\0';

    char body[48];
    if (n < maxDigits)
      snprintf(body, sizeof(body), "PIN: %s%d\nhold SEL=ok BK=del", stars, digit);
    else
      snprintf(body, sizeof(body), "PIN: %s\nhold SEL=ok BK=del", stars);
    display.message(title, body);

    // Wait for a button. If dimOnIdle and no input for AUTO_LOCK_MS, blank the
    // OLED and light-sleep (same idle behaviour as the unlocked auto-lock); the
    // wake press is consumed and we just redraw.
    ButtonAction a; bool got = false;
    for (;;) {
      if (buttons.poll(a)) { g_lastActivityMs = millis(); got = true; break; }
      if (dimOnIdle && (uint32_t)(millis() - idleStart) >= AUTO_LOCK_MS) {
        display.sleep();
        waitForWakeButton();
        display.wake();
        idleStart = millis();
        break;
      }
      delay(2);
    }
    if (!got) continue;            // woke from dim → redraw and keep waiting
    idleStart = millis();

    if (a.id == BTN_UP) {
      digit = (digit + 9) % 10;
    } else if (a.id == BTN_DOWN) {
      digit = (digit + 1) % 10;
    } else if (a.id == BTN_SELECT && a.event == EV_PRESS) {
      if (n < maxDigits) { out[n++] = (char)('0' + digit); digit = 0; }
    } else if (a.id == BTN_SELECT && a.event == EV_LONGPRESS) {
      if (n >= PIN_MIN_DIGITS) { out[n] = '\0'; return (int)n; }
    } else if (a.id == BTN_BACK) {
      if (n > 0) n--; else return -1;
    }
  }
}

// ---- Modal: hold-to-confirm -------------------------------------------------
static bool confirmModal(const char *title, const char *body) {
  display.message(title, body);
  for (;;) {
    ButtonAction a = waitButton();
    if (a.id == BTN_SELECT && a.event == EV_LONGPRESS) return true;
    if (a.id == BTN_BACK) return false;
  }
}

// Modal: enter a free-text passphrase by character selection. Up/Down cycle the
// charset, Select appends, hold Select finishes (empty allowed), Back deletes a
// char (or cancels if empty). Returns the length, or -1 if cancelled. The typed
// text is shown (this is a trusted setup screen). `out` must hold maxLen+1.
static int enterPassphraseModal(char *out, size_t maxLen) {
  static const char CH[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .-_!@#$";
  const int NCH = sizeof(CH) - 1;
  size_t n = 0;
  out[0] = '\0';
  int ci = 0;

  for (;;) {
    size_t len = strlen(out);
    const char *shown = (len > 14) ? out + (len - 14) : out;   // show the tail
    char body[48];
    snprintf(body, sizeof(body), "%s[%c]\nhold SEL=done BK=del", shown, CH[ci]);
    display.message("Passphrase", body);

    ButtonAction a = waitButton();
    if (a.id == BTN_UP) {
      ci = (ci + NCH - 1) % NCH;
    } else if (a.id == BTN_DOWN) {
      ci = (ci + 1) % NCH;
    } else if (a.id == BTN_SELECT && a.event == EV_PRESS) {
      if (n < maxLen) { out[n++] = CH[ci]; out[n] = '\0'; }
    } else if (a.id == BTN_SELECT && a.event == EV_LONGPRESS) {
      return (int)n;   // done (may be empty)
    } else if (a.id == BTN_BACK) {
      if (n > 0) { out[--n] = '\0'; } else return -1;
    }
  }
}

// ---- Flow: unlock existing wallet at boot -----------------------------------
// Returns true if unlocked; false if the wallet was wiped during the attempts.
static bool unlockFlow() {
  for (;;) {
    char pin[PIN_MAX_DIGITS + 1];
    int n = pinEntryModal("Unlock", pin, PIN_MAX_DIGITS, true);   // dim the PIN screen on idle
    if (n < 0) { continue; }   // cannot cancel out of the lock screen

    uint8_t left = 0;
    size_t  elen = 0;
    store::LoadResult r = store::loadSeed(pin, g_entropy, &elen,
                                          g_passphrase, sizeof(g_passphrase), &left);
    wallet::zeroize(pin, sizeof(pin));

    if (r == store::LOAD_OK) {
      g_entropyLen = elen;
      g_unlocked   = true;
      return true;
    }
    if (r == store::LOAD_WIPED) {
      display.message("Wiped", "Too many tries");
      delay(2500);
      lockWallet();
      return false;
    }
    if (r == store::LOAD_WRONG_PIN) {
      char m[40];
      snprintf(m, sizeof(m), "Wrong PIN\n%u tries left", left);
      display.message("Denied", m);
      delay(1500);
      continue;
    }
    display.message("Error", "storage fault");
    delay(1500);
  }
}

// ---- Flow: set PIN and save the seed (used by Generate and Restore) ---------
static bool setPinAndSaveFlow() {
  // Optional BIP39 passphrase (25th word) — collected once, before the PIN.
  char pass[store::MAX_PASSPHRASE + 1];
  pass[0] = '\0';
  if (confirmModal("Passphrase?", "hold SEL=add\nBK=skip")) {
    if (enterPassphraseModal(pass, store::MAX_PASSPHRASE) < 0) pass[0] = '\0';
  }

  for (;;) {
    char p1[PIN_MAX_DIGITS + 1];
    int n1 = pinEntryModal("Set PIN", p1, PIN_MAX_DIGITS, false);
    if (n1 < 0) {
      wallet::zeroize(p1, sizeof(p1));
      wallet::zeroize(pass, sizeof(pass));
      return false;  // abort
    }

    char p2[PIN_MAX_DIGITS + 1];
    int n2 = pinEntryModal("Confirm PIN", p2, PIN_MAX_DIGITS, false);
    bool match = (n2 >= 0) && (strcmp(p1, p2) == 0);
    wallet::zeroize(p2, sizeof(p2));

    if (!match) {
      wallet::zeroize(p1, sizeof(p1));
      display.message("Mismatch", "try again");
      delay(1500);
      continue;
    }

    // Rebuild the mnemonic and convert it back to raw entropy for storage.
    String m;
    for (uint8_t i = 0; i < g_seedCount; i++) { if (i) m += ' '; m += g_seedWords[i]; }
    uint8_t ent[32];
    size_t el = wallet::seedPhraseToEntropy(m, ent, sizeof(ent));
    m = "";

    bool ok = (el > 0) && store::saveSeed(ent, el, pass, p1);
    if (ok) {
      memcpy(g_entropy, ent, el);
      g_entropyLen = el;
      strncpy(g_passphrase, pass, sizeof(g_passphrase) - 1);
      g_passphrase[sizeof(g_passphrase) - 1] = '\0';
      g_unlocked = true;
    }
    wallet::zeroize(ent, sizeof(ent));
    wallet::zeroize(p1, sizeof(p1));
    wallet::zeroize(pass, sizeof(pass));

    display.message(ok ? "Saved" : "Error", ok ? "PIN set" : "save failed");
    delay(1400);
    return ok;
  }
}

// ---- Actions ----------------------------------------------------------------
static void showStub(const char *title, const char *body) {
  stubTitle = title;
  stubBody  = body;
  screen    = SCREEN_STUB;
}

static void doGenerateSeed() {
  if (store::hasSeed()) {
    showStub("Generate Seed", "Seed exists.\nWipe in Settings");
    return;
  }
  display.message("Generate Seed", "Working...");
  String phrase = wallet::generateSeedPhrase(24);
  if (phrase.length() == 0 || !wallet::validateMnemonic(phrase)) {
    Serial.println("seed generation FAILED");
    display.message("Error", "RNG failed");
    delay(1500);
    screen = SCREEN_MENU;
    return;
  }
  Serial.println("seed generated (shown on display only)");
  splitMnemonic(phrase);
  phrase = "";
  g_seedPage = 0;
  screen = SCREEN_SEED;
}

// Modal: enter one BIP39 word by letter-prefix autocomplete.
// Up/Down cycle the current letter, Select commits it (narrowing the match),
// hold Select picks the shown word, Back deletes a letter (or cancels the whole
// restore if the prefix is empty). Returns the wordlist index, or -1 to cancel.
static int enterWordModal(uint8_t pos, uint8_t total) {
  char prefix[9];
  prefix[0] = '\0';
  int letter = 0;  // 0..25 -> 'a'..'z'

  for (;;) {
    size_t pl = strlen(prefix);
    char cand[10];
    memcpy(cand, prefix, pl);
    cand[pl] = (char)('a' + letter);
    cand[pl + 1] = '\0';

    int count = 0;
    int first = wallet::wordlistMatch(cand, &count);
    const char *top = (count > 0) ? wallet::wordlistAt(first) : "";

    char title[20];
    snprintf(title, sizeof(title), "Word %u/%u", pos, total);
    char body[64];
    if (count > 0)
      snprintf(body, sizeof(body), "%s_\n%s (%d)\nSEL add  hold pick", cand, top, count);
    else
      snprintf(body, sizeof(body), "%s_\nno match\nBK=del", cand);
    display.message(title, body);

    ButtonAction a = waitButton();
    if (a.id == BTN_UP) {
      letter = (letter + 25) % 26;
    } else if (a.id == BTN_DOWN) {
      letter = (letter + 1) % 26;
    } else if (a.id == BTN_SELECT && a.event == EV_PRESS) {
      if (count > 0 && pl < sizeof(prefix) - 2) {
        prefix[pl] = (char)('a' + letter);
        prefix[pl + 1] = '\0';
        letter = 0;
      }
    } else if (a.id == BTN_SELECT && a.event == EV_LONGPRESS) {
      if (count > 0) return first;
    } else if (a.id == BTN_BACK) {
      if (pl > 0) { prefix[pl - 1] = '\0'; letter = 0; }
      else return -1;
    }
  }
}

// Modal: choose a 12- or 24-word restore. Up/Down toggles, hold SEL confirms,
// BK cancels. Returns 12 or 24, or 0 if cancelled.
static int chooseWordCount() {
  int wc = 24;   // 24 is the new default (matches Generate Seed)
  for (;;) {
    char body[48];
    snprintf(body, sizeof(body), "%d words\nUp/Dn change\nhold SEL=ok BK=x", wc);
    display.message("Restore", body);
    ButtonAction a = waitButton();
    if (a.id == BTN_UP || a.id == BTN_DOWN) wc = (wc == 12) ? 24 : 12;
    else if (a.id == BTN_SELECT && a.event == EV_LONGPRESS) return wc;
    else if (a.id == BTN_BACK) return 0;
  }
}

static void doRestoreSeed() {
  if (store::hasSeed()) { showStub("Restore", "Wipe first"); return; }

  int wc = chooseWordCount();          // 12 or 24 (existing wallets may be 12)
  if (wc == 0) { screen = SCREEN_MENU; return; }

  clearSeedState();
  for (uint8_t i = 0; i < wc; i++) {
    int idx = enterWordModal(i + 1, (uint8_t)wc);
    if (idx < 0) { clearSeedState(); screen = SCREEN_MENU; return; }  // cancelled
    g_seedWords[i] = wallet::wordlistAt(idx);
    g_seedCount = i + 1;
  }

  // Validate the BIP39 checksum before accepting.
  String m;
  for (uint8_t i = 0; i < wc; i++) { if (i) m += ' '; m += g_seedWords[i]; }
  bool valid = wallet::validateMnemonic(m);
  m = "";
  if (!valid) {
    display.message("Restore", "Bad checksum\ncheck words");
    delay(2200);
    clearSeedState();
    screen = SCREEN_MENU;
    return;
  }

  // Reuse the PIN-set + encrypted-save path (operates on g_seedWords).
  bool ok = setPinAndSaveFlow();
  clearSeedState();
  if (ok) { display.message("Restored", "wallet ready"); delay(1300); }
  screen = SCREEN_MENU;
}

static void doReceive() {
  if (!g_unlocked) { showStub("Receive", "Unlock first"); return; }
  display.message("Receive", "Deriving...");

  if (store::coin() != COIN_BTC) {         // non-BTC: derive per-index directly
    g_recvIndex = 0;
    updateReceive();
    if (g_recvAddr.length() == 0) { showStub("Receive", "derive error"); return; }
    screen = SCREEN_RECEIVE;
    return;
  }

  // BTC: cache the master key so paging is fast.
  if (wallet::beginReceive(g_entropy, g_entropyLen, g_passphrase, store::isTestnet())) {
    g_recvIndex = 0;
    updateReceive();
    screen = SCREEN_RECEIVE;
  } else {
    showStub("Receive", "derive error");
  }
}

// Read one line of base64 from Serial. BACK cancels (returns ""). Caps length
// to avoid unbounded growth.
static String readSerialLineOrCancel() {
  String s;
  for (;;) {
    ButtonAction a;
    if (buttons.poll(a) && a.id == BTN_BACK) return String("");
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') { if (s.length() > 0) return s; }
      else {
        s += c;
        if (s.length() > 16384) return s;   // safety cap
      }
    }
    delay(2);
  }
}

// Parse a base64 PSBT and enter the on-device review flow. `src` records where
// the signed result should be returned.
static void startSignFromB64(const String &b64, SignSource src) {
  display.message("Sign Tx", "Parsing...");
  if (!signer::load(b64, g_entropy, g_entropyLen, g_passphrase, store::isTestnet())) {
    display.message("Sign Tx", "Invalid PSBT");
    delay(1800);
    screen = SCREEN_MENU;
    return;
  }
  g_signSource = src;
  g_signPage = 0;
  screen = SCREEN_SIGN_REVIEW;
}

static void doSignTx() {
  if (!g_unlocked) { showStub("Sign Tx", "Unlock first"); return; }

  if (store::coin() == COIN_ADA) {
    display.message("Sign Tx", "Paste ADA tx on\nserial + <enter>\nBK=cancel");
    Serial.println("ADA> paste tx line (ADA|index|txBodyHex):");
    String line = readSerialLineOrCancel();
    if (line.length() == 0) { screen = SCREEN_MENU; return; }
    display.message("Sign Tx", "Parsing...");
    bool ok = ada::loadTx(line);
    line = "";
    if (!ok) { display.message("Sign Tx", "Bad ADA tx"); delay(1800); screen = SCREEN_MENU; return; }
    g_signSource = SRC_SERIAL;
    g_signPage = 0;
    screen = SCREEN_SIGN_REVIEW;
    return;
  }

  if (store::coin() == COIN_XLM) {
    display.message("Sign Tx", "Paste XLM tx on\nserial + <enter>\nBK=cancel");
    Serial.println("XLM> paste tx line (XLM|acct|signatureBaseHex):");
    String line = readSerialLineOrCancel();
    if (line.length() == 0) { screen = SCREEN_MENU; return; }
    display.message("Sign Tx", "Parsing...");
    bool ok = xlm::loadTx(line);
    line = "";
    if (!ok) { display.message("Sign Tx", "Bad XLM tx"); delay(1800); screen = SCREEN_MENU; return; }
    g_signSource = SRC_SERIAL;
    g_signPage = 0;
    screen = SCREEN_SIGN_REVIEW;
    return;
  }

  if (store::coin() == COIN_SOL) {
    display.message("Sign Tx", "Paste SOL tx on\nserial + <enter>\nBK=cancel");
    Serial.println("SOL> paste tx line (SOL|acct|messageHex):");
    String line = readSerialLineOrCancel();
    if (line.length() == 0) { screen = SCREEN_MENU; return; }
    display.message("Sign Tx", "Parsing...");
    bool ok = sol::loadTx(line);
    line = "";
    if (!ok) { display.message("Sign Tx", "Bad SOL tx"); delay(1800); screen = SCREEN_MENU; return; }
    g_signSource = SRC_SERIAL;
    g_signPage = 0;
    screen = SCREEN_SIGN_REVIEW;
    return;
  }

  if (store::coin() == COIN_ETH) {
    display.message("Sign Tx", "Paste ETH tx on\nserial + <enter>\nBK=cancel");
    Serial.println("ETH> paste tx line (ETH|acct|chainId|nonce|maxPrio|maxFee|gas|to|value|data):");
    String line = readSerialLineOrCancel();
    if (line.length() == 0) { screen = SCREEN_MENU; return; }
    display.message("Sign Tx", "Parsing...");
    bool ok = eth::loadTx(line);
    line = "";
    if (!ok) { display.message("Sign Tx", "Bad ETH tx"); delay(1800); screen = SCREEN_MENU; return; }
    g_signSource = SRC_SERIAL;
    g_signPage = 0;
    screen = SCREEN_SIGN_REVIEW;
    return;
  }

  if (store::coin() == COIN_BNB) {
    display.message("Sign Tx", "Paste BNB tx on\nserial + <enter>\nBK=cancel");
    Serial.println("BNB> paste tx line (BNB|acct|chainId|nonce|maxPrio|maxFee|gas|to|value|data):");
    String line = readSerialLineOrCancel();
    if (line.length() == 0) { screen = SCREEN_MENU; return; }
    display.message("Sign Tx", "Parsing...");
    bool ok = eth::loadTx(line);
    line = "";
    if (!ok) { display.message("Sign Tx", "Bad BNB tx"); delay(1800); screen = SCREEN_MENU; return; }
    g_signSource = SRC_SERIAL;
    g_signPage = 0;
    screen = SCREEN_SIGN_REVIEW;
    return;
  }

  if (store::coin() == COIN_USDT || store::coin() == COIN_USDC) {
    const char *tok = coinName(store::coin());
    char msg[48]; snprintf(msg, sizeof(msg), "Paste %s tx on\nserial + <enter>\nBK=cancel", tok);
    display.message("Sign Tx", msg);
    char prompt[80];
    snprintf(prompt, sizeof(prompt), "%s> paste tx line (%s|acct|chainId|nonce|maxPrio|maxFee|gas|to|value|data):", tok, tok);
    Serial.println(prompt);
    String line = readSerialLineOrCancel();
    if (line.length() == 0) { screen = SCREEN_MENU; return; }
    display.message("Sign Tx", "Parsing...");
    bool ok = eth::loadTx(line);
    line = "";
    if (!ok) { display.message("Sign Tx", "Bad tx"); delay(1800); screen = SCREEN_MENU; return; }
    g_signSource = SRC_SERIAL;
    g_signPage = 0;
    screen = SCREEN_SIGN_REVIEW;
    return;
  }

  if (store::coin() == COIN_XRP) {
    display.message("Sign Tx", "Paste XRP tx on\nserial + <enter>\nBK=cancel");
    Serial.println("XRP> paste tx line (XRP|acct|txBodyHex):");
    String line = readSerialLineOrCancel();
    if (line.length() == 0) { screen = SCREEN_MENU; return; }
    display.message("Sign Tx", "Parsing...");
    bool ok = xrp::loadTx(line);
    line = "";
    if (!ok) { display.message("Sign Tx", "Bad XRP tx"); delay(1800); screen = SCREEN_MENU; return; }
    g_signSource = SRC_SERIAL;
    g_signPage = 0;
    screen = SCREEN_SIGN_REVIEW;
    return;
  }

  if (store::coin() == COIN_ATOM) {
    display.message("Sign Tx", "Paste ATOM tx on\nserial + <enter>\nBK=cancel");
    Serial.println("ATOM> paste tx line (ATOM|acct|signDocHex):");
    String line = readSerialLineOrCancel();
    if (line.length() == 0) { screen = SCREEN_MENU; return; }
    display.message("Sign Tx", "Parsing...");
    bool ok = cosmos::loadTx(line);
    line = "";
    if (!ok) { display.message("Sign Tx", "Bad ATOM tx"); delay(1800); screen = SCREEN_MENU; return; }
    g_signSource = SRC_SERIAL;
    g_signPage = 0;
    screen = SCREEN_SIGN_REVIEW;
    return;
  }

  if (store::coin() == COIN_TRX) {
    display.message("Sign Tx", "Paste TRX tx on\nserial + <enter>\nBK=cancel");
    Serial.println("TRX> paste tx line (TRX|acct|rawDataHex):");
    String line = readSerialLineOrCancel();
    if (line.length() == 0) { screen = SCREEN_MENU; return; }
    display.message("Sign Tx", "Parsing...");
    bool ok = tron::loadTx(line);
    line = "";
    if (!ok) { display.message("Sign Tx", "Bad TRX tx"); delay(1800); screen = SCREEN_MENU; return; }
    g_signSource = SRC_SERIAL;
    g_signPage = 0;
    screen = SCREEN_SIGN_REVIEW;
    return;
  }

  if (store::coin() == COIN_TON) {
    display.message("Sign Tx", "Paste TON tx on\nserial + <enter>\nBK=cancel");
    Serial.println("TON> paste tx line (TON|acct|msgHashHex):");
    String line = readSerialLineOrCancel();
    if (line.length() == 0) { screen = SCREEN_MENU; return; }
    display.message("Sign Tx", "Parsing...");
    bool ok = ton::loadTx(line);
    line = "";
    if (!ok) { display.message("Sign Tx", "Bad TON tx"); delay(1800); screen = SCREEN_MENU; return; }
    g_signSource = SRC_SERIAL;
    g_signPage = 0;
    screen = SCREEN_SIGN_REVIEW;
    return;
  }

  if (store::coin() == COIN_AVAX || store::coin() == COIN_BASE ||
      store::coin() == COIN_POL  || store::coin() == COIN_CRO) {
    const char *tok = coinName(store::coin());
    char msg[48]; snprintf(msg, sizeof(msg), "Paste %s tx on\nserial + <enter>\nBK=cancel", tok);
    display.message("Sign Tx", msg);
    char prompt[96];
    snprintf(prompt, sizeof(prompt), "%s> paste tx line (%s|acct|chainId|nonce|maxPrio|maxFee|gas|to|value|data):", tok, tok);
    Serial.println(prompt);
    String line = readSerialLineOrCancel();
    if (line.length() == 0) { screen = SCREEN_MENU; return; }
    display.message("Sign Tx", "Parsing...");
    bool ok = eth::loadTx(line);
    line = "";
    if (!ok) { display.message("Sign Tx", "Bad tx"); delay(1800); screen = SCREEN_MENU; return; }
    g_signSource = SRC_SERIAL;
    g_signPage = 0;
    screen = SCREEN_SIGN_REVIEW;
    return;
  }

  display.message("Sign Tx", "Paste PSBT on\nserial + <enter>\nBK=cancel");
  Serial.println("PSBT> paste base64 PSBT (one line, end with newline):");
  String b64 = readSerialLineOrCancel();
  if (b64.length() == 0) { screen = SCREEN_MENU; return; }
  startSignFromB64(b64, SRC_SERIAL);
}

static void doSign() {
  display.message("Sign Tx", "Signing...");
  String out;
  bool ok;
  uint8_t c = store::coin();
  const char *label = "PSBT-SIGNED>";

  if (c == COIN_ADA) {
    out = ada::signTx(g_entropy, g_entropyLen, g_passphrase);
    ok = out.length() > 0;
    ada::clearTx();
    label = "ADA-SIGNED>";
  } else if (c == COIN_XLM) {
    out = xlm::signTx(g_entropy, g_entropyLen, g_passphrase);
    ok = out.length() > 0;
    xlm::clearTx();
    label = "XLM-SIGNED>";
  } else if (c == COIN_SOL) {
    out = sol::signTx(g_entropy, g_entropyLen, g_passphrase);
    ok = out.length() > 0;
    sol::clearTx();
    label = "SOL-SIGNED>";
  } else if (isEvmCoin(c)) {
    out = eth::signTx(g_entropy, g_entropyLen, g_passphrase);
    ok = out.length() > 0;
    eth::clearTx();
    label = (c == COIN_BNB)  ? "BNB-SIGNED>"  :
            (c == COIN_USDT) ? "USDT-SIGNED>" :
            (c == COIN_USDC) ? "USDC-SIGNED>" :
            (c == COIN_AVAX) ? "AVAX-SIGNED>" :
            (c == COIN_BASE) ? "BASE-SIGNED>" :
            (c == COIN_POL)  ? "POL-SIGNED>"  :
            (c == COIN_CRO)  ? "CRO-SIGNED>"  : "ETH-SIGNED>";
  } else if (c == COIN_XRP) {
    out = xrp::signTx(g_entropy, g_entropyLen, g_passphrase);
    ok = out.length() > 0;
    xrp::clearTx();
    label = "XRP-SIG>";
  } else if (c == COIN_ATOM) {
    out = cosmos::signTx(g_entropy, g_entropyLen, g_passphrase);
    ok = out.length() > 0;
    cosmos::clearTx();
    label = "ATOM-SIG>";
  } else if (c == COIN_TRX) {
    out = tron::signTx(g_entropy, g_entropyLen, g_passphrase);
    ok = out.length() > 0;
    tron::clearTx();
    label = "TRX-SIG>";
  } else if (c == COIN_TON) {
    out = ton::signTx(g_entropy, g_entropyLen, g_passphrase);
    ok = out.length() > 0;
    ton::clearTx();
    label = "TON-SIG>";
  } else {
    int n = signer::sign(g_entropy, g_entropyLen, g_passphrase, out);
    ok = (n > 0);
    signer::clear();
  }

  if (!ok) {
    out = "";
    display.message("Sign Tx", "Sign failed");
    delay(2000);
    screen = SCREEN_MENU;
    return;
  }

  Serial.println(label);
  Serial.println(out);      // signed tx (public data — safe on serial)
  if (g_signSource == SRC_BLE) ble::sendLine(out);
  out = "";
  display.message("Signed OK", g_signSource == SRC_BLE ? "sent via BLE" : "see serial");
  screen = SCREEN_SIGN_DONE;
}

static void doExportXpub() {
  if (!g_unlocked) { showStub("Export xpub", "Unlock first"); return; }
  if (store::coin() != COIN_BTC) { showStub("Export xpub", "BTC only"); return; }
  if (!wallet::beginReceive(g_entropy, g_entropyLen, g_passphrase, store::isTestnet())) {
    showStub("Export xpub", "derive error");
    return;
  }
  g_xpub = wallet::accountXpub();
  String fp = wallet::masterFingerprintHex();
  wallet::endReceive();
  if (g_xpub.length() == 0 || fp.length() == 0) { showStub("Export xpub", "error"); return; }

  // Build a BIP84 output descriptor with key origin so a watch-only wallet's
  // PSBTs carry our master fingerprint + full path (required for signing).
  uint32_t coin = store::isTestnet() ? 1 : 0;
  String desc = "wpkh([" + fp + "/84h/" + String(coin) + "h/0h]" + g_xpub + "/<0;1>/*)";

  // All public data — safe to print on serial.
  Serial.println("XPUB>");        Serial.println(g_xpub);
  Serial.println("FINGERPRINT>"); Serial.println(fp);
  Serial.println("DESCRIPTOR>");  Serial.println(desc);
  screen = SCREEN_XPUB;
}

static void doToggleBle() {
  bool on = !store::bleEnabled();
  store::setBleEnabled(on);
  if (on) {
    ble::enable(BLE_DEVICE_NAME);
    char body[40];
    snprintf(body, sizeof(body), "ON as\n%s", ble::advName());   // e.g. "Sentinel-A1B2"
    display.message("BLE", body);
  } else {
    ble::disable();
    display.message("BLE", "OFF");
  }
  delay(1200);
}

static void doWipe() {
  if (!confirmModal("Wipe wallet?", "hold SEL=yes\nBK=no")) return;
  ble::disable();          // drop the radio; wipe also clears the BLE setting
  store::wipe();
  lockWallet();
  display.message("Wiped", "wallet erased");
  delay(1500);
  selected = 0;
  screen = SCREEN_MENU;
}

// ---- Rendering --------------------------------------------------------------
static void renderSeed() {
  uint8_t startIdx = g_seedPage * WORDS_PER_PAGE;
  uint8_t endIdx   = min<uint8_t>(startIdx + WORDS_PER_PAGE, g_seedCount);
  char title[24];
  snprintf(title, sizeof(title), "Backup %u-%u/%u", startIdx + 1, endIdx, g_seedCount);
  String body;
  for (uint8_t i = startIdx; i < endIdx; i++) {
    char line[24];
    snprintf(line, sizeof(line), "%2u. %s", i + 1, g_seedWords[i].c_str());
    body += line;
    if (i + 1 < endIdx) body += "\n";
  }
  display.message(title, body.c_str());
}

static void renderReceive() {
  if (g_recvAddr.length() == 0) { display.message("Receive", "derive error"); return; }
  char title[24];
  snprintf(title, sizeof(title), "%s #%lu", coinName(store::coin()), (unsigned long)g_recvIndex);
  String body;
  const int W = 21;   // chars per line at the 6x12 font
  for (int i = 0; i < (int)g_recvAddr.length(); i += W) {
    body += g_recvAddr.substring(i, min(i + W, (int)g_recvAddr.length()));
    body += "\n";
  }
  body += "SEL=QR  Up/Dn=idx";
  display.message(title, body.c_str());
}

static void renderEthReview() {
  if (g_signPage == 0) {
    // Page 0: what + how much.
    String body = eth::txAmountStr() + " " + eth::txSymbol() + "\n\nDn=recipient";
    String title = eth::txIsErc20() ? String("Send token") : ("Send " + eth::txSymbol());
    display.message(title.c_str(), body.c_str());
  } else if (g_signPage == 1) {
    // Page 1: the recipient address, wrapped.
    String body;
    String r = eth::txRecipient();
    const int W = 21;
    for (int i = 0; i < (int)r.length(); i += W) {
      body += r.substring(i, min(i + W, (int)r.length()));
      body += "\n";
    }
    display.message("Recipient", body.c_str());
  } else {
    // Page 2: fee + chain + confirm.
    String body = "Fee<=" + eth::txFeeStr() + " " + eth::txNativeSymbol() + "\nChain: " +
                  String((unsigned long)eth::txChainId()) + "\nhold SEL=SIGN\nBK=cancel";
    display.message("Confirm", body.c_str());
  }
}

static void renderSolReview() {
  if (g_signPage == 0) {
    if (sol::txHasTransfer()) {
      String body = sol::txAmountStr() + " SOL\n\nDn=recipient";
      display.message("Send SOL", body.c_str());
    } else {
      char body[48];
      snprintf(body, sizeof(body), "BLIND SIGN\n%lu bytes\nverify on host",
               (unsigned long)sol::txMsgLen());
      display.message("Sign SOL", body);
    }
  } else if (g_signPage == 1 && sol::txHasTransfer()) {
    String body;
    String r = sol::txRecipient();
    const int W = 21;
    for (int i = 0; i < (int)r.length(); i += W) {
      body += r.substring(i, min(i + W, (int)r.length()));
      body += "\n";
    }
    display.message("Recipient", body.c_str());
  } else {
    display.message("Confirm", "hold SEL=SIGN\nBK=cancel");
  }
}

static void renderXlmReview() {
  if (g_signPage == 0) {
    if (xlm::txHasPayment()) {
      String body = xlm::txAmountStr() + " XLM\n\nDn=recipient";
      display.message("Send XLM", body.c_str());
    } else {
      String body = "BLIND SIGN\nhash: " + xlm::txHashHex() + "\nverify on host";
      display.message("Sign XLM", body.c_str());
    }
  } else if (g_signPage == 1 && xlm::txHasPayment()) {
    String body;
    String r = xlm::txRecipient();
    const int W = 21;
    for (int i = 0; i < (int)r.length(); i += W) {
      body += r.substring(i, min(i + W, (int)r.length()));
      body += "\n";
    }
    display.message("Recipient", body.c_str());
  } else {
    display.message("Confirm", "hold SEL=SIGN\nBK=cancel");
  }
}

static void renderAdaReview() {
  // ADA on-device CBOR output display is a future enhancement; blind-sign for now.
  if (g_signPage == 0) {
    String body = "BLIND SIGN\nhash: " + ada::txHashHex() + "\nverify on host";
    display.message("Sign ADA", body.c_str());
  } else {
    display.message("Confirm", "hold SEL=SIGN\nBK=cancel");
  }
}

static void renderXrpReview() {
  if (g_signPage == 0) {
    String body = "BLIND SIGN\nhash: " + xrp::txHashHex() + "..\nverify on host";
    display.message("Sign XRP", body.c_str());
  } else {
    display.message("Confirm", "hold SEL=SIGN\nBK=cancel");
  }
}

static void renderCosmosReview() {
  if (g_signPage == 0) {
    String body = "BLIND SIGN\nhash: " + cosmos::txHashHex() + "..\nverify on host";
    display.message("Sign ATOM", body.c_str());
  } else {
    display.message("Confirm", "hold SEL=SIGN\nBK=cancel");
  }
}

static void renderTronReview() {
  if (g_signPage == 0) {
    String body = "BLIND SIGN\nhash: " + tron::txHashHex() + "..\nverify on host";
    display.message("Sign TRX", body.c_str());
  } else {
    display.message("Confirm", "hold SEL=SIGN\nBK=cancel");
  }
}

static void renderTonReview() {
  if (g_signPage == 0) {
    String body = "BLIND SIGN\nhash: " + ton::txHashHex() + "..\nverify on host";
    display.message("Sign TON", body.c_str());
  } else {
    display.message("Confirm", "hold SEL=SIGN\nBK=cancel");
  }
}

static void renderSignReview() {
  if (store::coin() == COIN_ADA) { renderAdaReview(); return; }
  if (store::coin() == COIN_XLM) { renderXlmReview(); return; }
  if (store::coin() == COIN_SOL) { renderSolReview(); return; }
  if (isEvmCoin(store::coin())) { renderEthReview(); return; }
  if (store::coin() == COIN_XRP) { renderXrpReview(); return; }
  if (store::coin() == COIN_ATOM) { renderCosmosReview(); return; }
  if (store::coin() == COIN_TRX) { renderTronReview(); return; }
  if (store::coin() == COIN_TON) { renderTonReview(); return; }
  uint8_t nOut = signer::storedOutputs();
  if (g_signPage < nOut) {
    // One output per page: amount, change tag, and wrapped address.
    char title[24];
    snprintf(title, sizeof(title), "Out %u/%u%s",
             g_signPage + 1, signer::totalOutputs(),
             signer::outputIsMine(g_signPage) ? " chg" : "");
    String body = satToBtc(signer::outputAmountSat(g_signPage)) + " BTC\n";
    const char *addr = signer::outputAddress(g_signPage);
    const int W = 21;
    for (int i = 0; i < (int)strlen(addr); i += W) {
      body += String(addr).substring(i, min(i + W, (int)strlen(addr)));
      body += "\n";
    }
    display.message(title, body.c_str());
  } else {
    // Summary / confirm page.
    String body = "Fee: " + satToBtc(signer::feeSat()) + "\n";
    if (signer::truncated()) body += "(+more outs)\n";
    body += "hold SEL=SIGN\nBK=cancel";
    display.message("Confirm", body.c_str());
  }
}

static void render() {
  switch (screen) {
    case SCREEN_MENU:        buildMainMenu();
                             display.menu(FW_NAME, mainItems, kMenuCount, selected); break;
    case SCREEN_SETTINGS:    buildSettings();
                             display.menu("Settings", settingsItems, kSettingsCount, settingsSel); break;
    case SCREEN_SEED:        renderSeed(); break;
    case SCREEN_SEED_DONE:   display.message("Backed up?", "SEL=set PIN\nBK=review"); break;
    case SCREEN_RECEIVE:     renderReceive(); break;
    case SCREEN_RECEIVE_QR:  display.qr(g_recvAddr.c_str()); break;
    case SCREEN_SIGN_REVIEW: renderSignReview(); break;
    case SCREEN_SIGN_DONE:   display.message("Signed OK", "any key = menu"); break;
    case SCREEN_XPUB:        display.qr(g_xpub.c_str()); break;
    case SCREEN_STUB:        display.message(stubTitle, stubBody); break;
  }
}

// Wait (OLED already blanked by the caller) until any button is pressed, then
// return so the caller can wake the screen.
//
// NOTE: this is a plain polling wait, NOT CPU light sleep. The esp_light_sleep_
// start() path was removed because it caused a reboot after unlocking — the
// light-sleep -> heavy-crypto/NVS (PBKDF2 + AES-GCM in loadSeed) path crashes on
// the C5. The OLED-off already saves most of the idle power; restoring true CPU
// light sleep needs a proper fix (see the panic backtrace) before it's safe.
static void waitForWakeButton() {
  ButtonAction a;
  while (buttons.poll(a)) { }           // drain any stale events
  while (!buttons.poll(a)) delay(10);   // stay here (screen dark) until a press
}

// ---- Auto-lock --------------------------------------------------------------
// Called from loop() after AUTO_LOCK_MS of inactivity. Wipes the keys from RAM,
// turns BLE off, blanks the OLED for low power, and stays dark until a button
// wakes it — then blocks on the PIN screen until the user unlocks again.
static void autoLock() {
  ble::disable();                 // turn the radio off while locked (security)
  lockWallet();                   // zeroize entropy/passphrase; g_unlocked = false
  display.message("Locked", "Idle timeout");
  delay(700);

  display.sleep();                // OLED off — low-power idle
  waitForWakeButton();            // stay dark until any button is pressed
  display.wake();                 // OLED back on

  if (store::hasSeed()) {
    unlockFlow();                 // blocks until re-unlocked (or wiped)
  }
  // Restore BLE per the saved setting once unlocked again (mirrors boot).
  if (g_unlocked && store::bleEnabled()) ble::enable(BLE_DEVICE_NAME);

  selected = 0;
  settingsSel = 0;
  screen = SCREEN_MENU;
  g_lastActivityMs = millis();
  render();
}

// ---- Setup / loop -----------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(50);
  Serial.println();
  Serial.println(FW_NAME " " FW_VERSION " — Phase 6 (optional BLE)");

  display.begin();
  buttons.begin();
  display.splash(FW_NAME, FW_VERSION);

  // Diagnostic: verify HD derivation against the public BIP84 test vector.
  {
    String got;
    bool ok = wallet::selfTestBip84(&got);
    Serial.print("bip84 self-test (idx0 | idx1): "); Serial.println(ok ? "PASS" : "FAIL");
    Serial.print("  got: "); Serial.println(got);
    Serial.println("  exp: bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu | bc1qnjg0jd8228aq7egyzacy8cys3knf9xvrerkf9g");
  }

  delay(800);

  if (store::hasSeed()) {
    Serial.println("wallet present — unlocking");
    unlockFlow();   // blocks until unlocked or wiped
  } else {
    Serial.println("no wallet — first run");
  }

  // BLE only comes up after a successful unlock, and only if enabled.
  ble::begin();
  if (g_unlocked && store::bleEnabled()) {
    ble::enable(BLE_DEVICE_NAME);
    Serial.println("BLE enabled (advertising)");
  }

  g_lastActivityMs = millis();   // start the inactivity clock fresh
  render();
}

void loop() {
  // Auto-lock: after AUTO_LOCK_MS with no interaction, re-lock and return to the
  // PIN screen (BLE is turned off inside autoLock). Unsigned subtraction handles
  // millis() rollover correctly.
  if (g_unlocked && (uint32_t)(millis() - g_lastActivityMs) >= AUTO_LOCK_MS) {
    autoLock();
    return;
  }

  // Accept a signable payload over BLE while idle at the menu and unlocked.
  if (g_unlocked && screen == SCREEN_MENU) {
    String line;
    if (ble::takePsbt(line)) {   // NUS line: PSBT (BTC), ETH tx, or SOL tx line
      g_lastActivityMs = millis();   // an incoming tx counts as activity
      uint8_t c = store::coin();
      if (isEvmCoin(c)) {
        if (eth::loadTx(line)) { g_signSource = SRC_BLE; g_signPage = 0; screen = SCREEN_SIGN_REVIEW; }
      } else if (c == COIN_XRP) {
        if (xrp::loadTx(line)) { g_signSource = SRC_BLE; g_signPage = 0; screen = SCREEN_SIGN_REVIEW; }
      } else if (c == COIN_ATOM) {
        if (cosmos::loadTx(line)) { g_signSource = SRC_BLE; g_signPage = 0; screen = SCREEN_SIGN_REVIEW; }
      } else if (c == COIN_TRX) {
        if (tron::loadTx(line)) { g_signSource = SRC_BLE; g_signPage = 0; screen = SCREEN_SIGN_REVIEW; }
      } else if (c == COIN_TON) {
        if (ton::loadTx(line)) { g_signSource = SRC_BLE; g_signPage = 0; screen = SCREEN_SIGN_REVIEW; }
      } else if (c == COIN_SOL) {
        if (sol::loadTx(line)) { g_signSource = SRC_BLE; g_signPage = 0; screen = SCREEN_SIGN_REVIEW; }
      } else if (c == COIN_XLM) {
        if (xlm::loadTx(line)) { g_signSource = SRC_BLE; g_signPage = 0; screen = SCREEN_SIGN_REVIEW; }
      } else if (c == COIN_ADA) {
        if (ada::loadTx(line)) { g_signSource = SRC_BLE; g_signPage = 0; screen = SCREEN_SIGN_REVIEW; }
      } else if (c == COIN_BTC) {
        startSignFromB64(line, SRC_BLE);
      }
      render();
      return;
    }
  }

  ButtonAction a;
  if (!buttons.poll(a)) { delay(2); return; }
  g_lastActivityMs = millis();   // any button press resets the inactivity clock

  switch (screen) {
    case SCREEN_MENU:
      if (a.id == BTN_UP)   selected = (selected == 0) ? kMenuCount - 1 : selected - 1;
      if (a.id == BTN_DOWN) selected = (selected + 1) % kMenuCount;
      // Left/Right change the coin while the cursor is on the Coin item (top).
      // D8 (Left) advances through the list, D9 (Right) steps back.
      if (selected == 0) {
        if (a.id == BTN_LEFT)  store::setCoin((store::coin() + 1) % COIN_COUNT);
        if (a.id == BTN_RIGHT) store::setCoin((store::coin() + COIN_COUNT - 1) % COIN_COUNT);
      }
      if (a.id == BTN_SELECT && a.event == EV_PRESS) {
        switch (selected) {
          case 0: break;   // coin is changed with Left/Right, not OK
          case 1: doReceive(); break;
          case 2: doSignTx(); break;
          case 3: settingsSel = 0; screen = SCREEN_SETTINGS; break;
          case 4: showStub("About",
                    "Sentinel Wallet\nV0.9B\nhttps://\nsentinelwallet.io"); break;
        }
      }
      break;

    case SCREEN_SETTINGS:
      if (a.id == BTN_UP)   settingsSel = (settingsSel == 0) ? kSettingsCount - 1 : settingsSel - 1;
      if (a.id == BTN_DOWN) settingsSel = (settingsSel + 1) % kSettingsCount;
      if (a.id == BTN_BACK) screen = SCREEN_MENU;
      if (a.id == BTN_SELECT && a.event == EV_PRESS) {
        switch (settingsSel) {
          case 0: doGenerateSeed(); break;
          case 1: doRestoreSeed(); break;
          case 2: store::setTestnet(!store::isTestnet()); break;  // toggle network
          case 3: doToggleBle(); break;
          case 4: doExportXpub(); break;
          case 5: doWipe(); break;
          case 6: screen = SCREEN_MENU; break;
        }
      }
      break;

    case SCREEN_SEED:
      if (a.id == BTN_UP && g_seedPage > 0) g_seedPage--;
      else if (a.id == BTN_DOWN || (a.id == BTN_SELECT && a.event == EV_PRESS)) {
        if (g_seedPage + 1 < seedPageCount()) g_seedPage++;
        else screen = SCREEN_SEED_DONE;
      } else if (a.id == BTN_BACK) {
        clearSeedState();
        screen = SCREEN_MENU;
      }
      break;

    case SCREEN_SEED_DONE:
      if (a.id == BTN_BACK) { g_seedPage = seedPageCount() - 1; screen = SCREEN_SEED; }
      else if (a.id == BTN_SELECT && a.event == EV_PRESS) {
        bool ok = setPinAndSaveFlow();
        clearSeedState();
        screen = SCREEN_MENU;
        (void)ok;
      }
      break;

    case SCREEN_RECEIVE:
      if (a.id == BTN_UP && g_recvIndex > 0)   { g_recvIndex--; updateReceive(); }
      else if (a.id == BTN_DOWN && g_recvIndex < 999) { g_recvIndex++; updateReceive(); }
      else if (a.id == BTN_SELECT && a.event == EV_PRESS) screen = SCREEN_RECEIVE_QR;
      else if (a.id == BTN_BACK) {
        wallet::endReceive();
        g_recvAddr = ""; g_recvPath = "";
        screen = SCREEN_MENU;
      }
      break;

    case SCREEN_RECEIVE_QR:
      if (a.id == BTN_BACK || a.id == BTN_SELECT) screen = SCREEN_RECEIVE;
      break;

    case SCREEN_SIGN_REVIEW: {
      // Confirm page index per coin.
      uint8_t lastPage;
      if (store::coin() == COIN_ADA)                        lastPage = 1;
      else if (store::coin() == COIN_XLM)                   lastPage = xlm::txHasPayment() ? 2 : 1;
      else if (store::coin() == COIN_SOL)                   lastPage = sol::txHasTransfer() ? 2 : 1;
      else if (isEvmCoin(store::coin()))                    lastPage = 2;
      else if (store::coin() == COIN_XRP)                   lastPage = 1;
      else if (store::coin() == COIN_ATOM)                  lastPage = 1;
      else if (store::coin() == COIN_TRX)                   lastPage = 1;
      else if (store::coin() == COIN_TON)                   lastPage = 1;
      else                                                   lastPage = signer::storedOutputs();

      if (a.id == BTN_UP && g_signPage > 0) g_signPage--;
      else if (a.id == BTN_DOWN && g_signPage < lastPage) g_signPage++;
      else if (a.id == BTN_BACK) {
        if (store::coin() == COIN_ADA)                        ada::clearTx();
        else if (store::coin() == COIN_XLM)                   xlm::clearTx();
        else if (store::coin() == COIN_SOL)                   sol::clearTx();
        else if (isEvmCoin(store::coin()))                    eth::clearTx();
        else if (store::coin() == COIN_XRP)                   xrp::clearTx();
        else if (store::coin() == COIN_ATOM)                  cosmos::clearTx();
        else if (store::coin() == COIN_TRX)                   tron::clearTx();
        else if (store::coin() == COIN_TON)                   ton::clearTx();
        else                                                   signer::clear();
        screen = SCREEN_MENU;
      }
      else if (a.id == BTN_SELECT && a.event == EV_LONGPRESS && g_signPage == lastPage) {
        doSign();
      }
      break;
    }

    case SCREEN_SIGN_DONE:
      if (a.id == BTN_BACK || a.id == BTN_SELECT) screen = SCREEN_MENU;
      break;

    case SCREEN_XPUB:
      if (a.id == BTN_BACK || a.id == BTN_SELECT) { g_xpub = ""; screen = SCREEN_SETTINGS; }
      break;

    case SCREEN_STUB:
      if (a.id == BTN_BACK || a.id == BTN_SELECT) screen = SCREEN_MENU;
      break;
  }

  render();
}
