#pragma once

#include <cstddef>
#include <cstdint>

// Streaming Ed25519 verifier for the signature trailer appended by
// scripts/sign_firmware.py. Required because orlp/ed25519's stock
// ed25519_verify wants the full message in RAM, and a 2 MB firmware image
// won't fit on an ESP32-C3 (≈300 KB usable heap).
//
// Trailer layout (68 bytes at EOF):
//
//     [ ... firmware body ... ]
//     [ signature (64 bytes)  ]
//     [ magic 'CPSG' (4 bytes)]
//
// SD-card manual update path does NOT call this verifier — see
// `docs/firmware-signature-migration.md` for the threat model.

namespace signature_verify {

enum class Result {
  OK,
  MISSING_TRAILER,    // file shorter than 68 bytes
  BAD_MAGIC,          // last 4 bytes != 'CPSG'
  INVALID_SIGNATURE,  // Ed25519 verify failed
  READ_FAIL,          // I/O error mid-stream
  OPEN_FAIL,          // couldn't open the file
};

// Verify the signature trailer of `sdPath` against the build-time public key
// embedded at `CrosspointPubKey.h`. On success, *bodySize is set to
// (fileSize - 68) so the caller can flash only the body bytes.
Result verifyTrailer(const char* sdPath, size_t* bodySize);

const char* resultName(Result r);

}  // namespace signature_verify
