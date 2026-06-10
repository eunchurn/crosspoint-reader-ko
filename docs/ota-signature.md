# OTA Firmware Signature

Ed25519로 서명된 펌웨어만 OTA 경로로 받아들이는 신뢰 모델.
TLS 인증서가 아닌 펌웨어 자체의 서명을 신뢰 앵커로 사용.

## 동작

```
[릴리스]
  pio run → firmware.bin
       ↓
  scripts/sign_firmware.py firmware.bin   (CI)
       ↓
  firmware.bin = [body][64B Ed25519 sig][4B 'CPSG']
       ↓
  GitHub Release asset

[디바이스 OTA]
  GitHub Release 조회 → firmware.bin 다운로드 (TLS 자체서명 OK)
       ↓
  SignatureVerifier::verifyTrailer (Ed25519 streaming verify)
       ↓
  flashFromSdPath(bodySize = file_size - 68)   // trailer 제외
```

## 신뢰 경계

| 경로 | 서명 검증 | 비고 |
|---|---|---|
| OTA 다운로드 | 강제 | 검증 실패 시 `OtaUpdaterError::SIGNATURE_ERROR` |
| SD카드 수동 업데이트 | **없음** | escape hatch — 옛 펌웨어 사용자 / 자체 빌드 테스트용 |
| 전체 reflash (esptool merge_bin) | 해당 없음 | 부트로더 단계, OTA 흐름 밖 |

## 코드 위치

- 공개키 임베드: `src/network/CrosspointPubKey.h` (32바이트)
- 검증 모듈: `src/network/SignatureVerifier.{h,cpp}`
- OTA 통합: `src/network/OtaUpdater.cpp::installUpdate`
- Ed25519 구현: `lib/Ed25519/` (orlp/ed25519 verify-only subset, zlib license)
- 서명 도구: `scripts/sign_firmware.py`
- 검증 도구: `scripts/verify_firmware_signature.py`
- 키 생성 도구: `scripts/gen_ota_keypair.py`
- CI 통합: `.github/workflows/release.yml` (Sign firmware.bin for OTA 단계)

## 키 관리

- **비밀키**: GitHub Actions Secret `ED25519_PRIVATE_KEY` (PEM PKCS#8 Ed25519).
  메인테이너 1Password 백업 보유. 로컬에 평문 보관 금지.
- **공개키**: `CrosspointPubKey.h`에 32바이트 raw로 임베드, git에 커밋. 노출 OK.

## TLS 변경

OTA 클라이언트 두 곳 모두 `crt_bundle_attach` 제거 + `skip_cert_common_name_check = true`:

- `OtaUpdater::checkForUpdate` — api.github.com 조회 (DNS 스푸핑)
- `OtaUpdater::installUpdate` — 펌웨어 .bin 다운로드 (DNS 스푸핑)

`crt_bundle_attach`가 NULL이면 esp_tls는 `MBEDTLS_SSL_VERIFY_NONE`로 폴백.
펌웨어 시그니처가 무결성/진본성 보장하므로 TLS 검증 없어도 안전.

## 키 회전 절차

키 손상 또는 정기 회전 시:

1. `python scripts/gen_ota_keypair.py --force` → 새 키 페어 + 새 헤더
2. `CrosspointPubKey.h`를 변경된 상태로 두되, **추가로 옛 공개키도 임베드**
   (dual-trust 헤더로 수정, `kCrosspointOtaPubKey[]` + `kCrosspointOtaPubKeyPrev[]`)
3. `SignatureVerifier::verifyTrailer`를 두 키 모두로 시도하게 수정
4. 옛 키로 서명된 펌웨어 한 차례 릴리스 (배포 단계)
5. 사용자 업데이트 대기 (≥1개월)
6. 새 키로 서명 시작
7. 다다음 릴리스부터 dual-trust 제거, 옛 키 헤더에서 삭제

## 사용자 영향

- **신 펌웨어 사용자**: OTA 무한 가능 (cert 만료 영향 없음)
- **옛 펌웨어 사용자 (cert 만료 전)**: OTA로 신 펌웨어 1회 업데이트 가능
- **옛 펌웨어 사용자 (cert 만료 후)**: OTA 실패 → SD카드 경로로 신 펌웨어 수동 설치 필요
- **자체 빌드 개발자**: SD카드 경로로 unsigned bin 그대로 flash 가능

## 관련 문서

- 마이그레이션 계획: `xteink-unlocker/docs/firmware-signature-migration.md`
