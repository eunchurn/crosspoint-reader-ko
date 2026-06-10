#include "SignatureVerifier.h"

#include <HalStorage.h>
#include <Logging.h>

#include <memory>

#include "CrosspointPubKey.h"

// orlp/ed25519 internal headers are plain C — wrap to suppress C++ name mangling.
extern "C" {
#include "ge.h"
#include "sc.h"
#include "sha512.h"
}

namespace {

constexpr size_t SIGNATURE_LEN = 64;
constexpr uint8_t MAGIC[4] = {'C', 'P', 'S', 'G'};
constexpr size_t TRAILER_LEN = SIGNATURE_LEN + sizeof(MAGIC);
// Chunk size for streaming the body through SHA-512. Tuned for ESP32-C3 heap
// budget — small enough not to fight the OTA download buffer (8 KiB) when both
// are alive briefly, large enough that file I/O dominates over SHA-512 cost.
constexpr size_t READ_CHUNK = 4096;

// Constant-time 32-byte comparator. Replicated from verify.c with the same
// hand-unrolled form — defensive against future compilers that might infer
// a short-circuit from a loop body and break the constant-time property.
int consttime_equal_32(const uint8_t* x, const uint8_t* y) {
  uint8_t r = x[0] ^ y[0];
#define F(i) r |= x[i] ^ y[i]
  F(1);
  F(2);
  F(3);
  F(4);
  F(5);
  F(6);
  F(7);
  F(8);
  F(9);
  F(10);
  F(11);
  F(12);
  F(13);
  F(14);
  F(15);
  F(16);
  F(17);
  F(18);
  F(19);
  F(20);
  F(21);
  F(22);
  F(23);
  F(24);
  F(25);
  F(26);
  F(27);
  F(28);
  F(29);
  F(30);
  F(31);
#undef F
  return r == 0;
}

}  // namespace

namespace signature_verify {

Result verifyTrailer(const char* sdPath, size_t* bodySize) {
  if (bodySize) *bodySize = 0;

  HalFile file;
  if (!Storage.openFileForRead("SIGVERIFY", sdPath, file) || !file) {
    LOG_ERR("SIGVERIFY", "open failed: %s", sdPath);
    return Result::OPEN_FAIL;
  }

  const size_t totalSize = file.fileSize();
  if (totalSize < TRAILER_LEN) {
    LOG_ERR("SIGVERIFY", "file too small (%u < %u)", static_cast<unsigned>(totalSize),
            static_cast<unsigned>(TRAILER_LEN));
    file.close();
    return Result::MISSING_TRAILER;
  }
  const size_t body_len = totalSize - TRAILER_LEN;

  // ── Magic check ────────────────────────────────────────────────────────
  uint8_t magic[sizeof(MAGIC)] = {0};
  if (!file.seekSet(totalSize - sizeof(MAGIC))) {
    LOG_ERR("SIGVERIFY", "seek to magic failed");
    file.close();
    return Result::READ_FAIL;
  }
  if (file.read(magic, sizeof(MAGIC)) != static_cast<int>(sizeof(MAGIC))) {
    LOG_ERR("SIGVERIFY", "read magic failed");
    file.close();
    return Result::READ_FAIL;
  }
  for (size_t i = 0; i < sizeof(MAGIC); ++i) {
    if (magic[i] != MAGIC[i]) {
      LOG_ERR("SIGVERIFY", "bad magic: %02x %02x %02x %02x", magic[0], magic[1], magic[2], magic[3]);
      file.close();
      return Result::BAD_MAGIC;
    }
  }

  // ── Signature read ─────────────────────────────────────────────────────
  uint8_t signature[SIGNATURE_LEN];
  if (!file.seekSet(body_len)) {
    LOG_ERR("SIGVERIFY", "seek to sig failed");
    file.close();
    return Result::READ_FAIL;
  }
  if (file.read(signature, SIGNATURE_LEN) != static_cast<int>(SIGNATURE_LEN)) {
    LOG_ERR("SIGVERIFY", "read sig failed");
    file.close();
    return Result::READ_FAIL;
  }

  // Same upfront rejection as orlp/ed25519's ed25519_verify: high 3 bits of
  // signature[63] must be zero (RFC 8032 §5.1.7 step 1 check).
  if (signature[63] & 224) {
    LOG_ERR("SIGVERIFY", "signature[63] high bits set");
    file.close();
    return Result::INVALID_SIGNATURE;
  }

  // ── Streaming body verification ────────────────────────────────────────
  // Replicates ed25519_verify(verify.c) but feeds the message in chunks so a
  // multi-MB firmware doesn't have to live in RAM at once. Public key /
  // signature halves come from the trailer; message is the file prefix.

  ge_p3 A;
  if (ge_frombytes_negate_vartime(&A, kCrosspointOtaPubKey) != 0) {
    LOG_ERR("SIGVERIFY", "ge_frombytes failed (bad public key?)");
    file.close();
    return Result::INVALID_SIGNATURE;
  }

  sha512_context hash;
  sha512_init(&hash);
  sha512_update(&hash, signature, 32);             // R component
  sha512_update(&hash, kCrosspointOtaPubKey, 32);  // A
  // M — stream from file
  auto buf = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[READ_CHUNK]);
  if (!buf) {
    LOG_ERR("SIGVERIFY", "OOM allocating read buffer");
    file.close();
    return Result::READ_FAIL;
  }
  if (!file.seekSet(0)) {
    LOG_ERR("SIGVERIFY", "seek to body failed");
    file.close();
    return Result::READ_FAIL;
  }
  size_t remaining = body_len;
  while (remaining > 0) {
    const size_t want = remaining < READ_CHUNK ? remaining : READ_CHUNK;
    const int got = file.read(buf.get(), want);
    if (got <= 0 || static_cast<size_t>(got) != want) {
      LOG_ERR("SIGVERIFY", "body read short: got=%d want=%u remaining=%u", got, static_cast<unsigned>(want),
              static_cast<unsigned>(remaining));
      file.close();
      return Result::READ_FAIL;
    }
    sha512_update(&hash, buf.get(), want);
    remaining -= want;
  }
  file.close();

  uint8_t h[64];
  sha512_final(&hash, h);

  sc_reduce(h);

  ge_p2 R;
  ge_double_scalarmult_vartime(&R, h, &A, signature + 32);
  uint8_t checker[32];
  ge_tobytes(checker, &R);

  if (!consttime_equal_32(checker, signature)) {
    LOG_ERR("SIGVERIFY", "signature mismatch");
    return Result::INVALID_SIGNATURE;
  }

  LOG_INF("SIGVERIFY", "signature OK (body=%u bytes)", static_cast<unsigned>(body_len));
  if (bodySize) *bodySize = body_len;
  return Result::OK;
}

const char* resultName(Result r) {
  switch (r) {
    case Result::OK:
      return "OK";
    case Result::MISSING_TRAILER:
      return "MISSING_TRAILER";
    case Result::BAD_MAGIC:
      return "BAD_MAGIC";
    case Result::INVALID_SIGNATURE:
      return "INVALID_SIGNATURE";
    case Result::READ_FAIL:
      return "READ_FAIL";
    case Result::OPEN_FAIL:
      return "OPEN_FAIL";
  }
  return "?";
}

}  // namespace signature_verify
