# Running-config 이식성 검토

검토일: 2026-11-03. 대상은 현재 작업 트리(미커밋 Notification/Scheduling 변경 포함)와 사용자가 첨부한 PAN-OS XML이다. 운영 DB의 실제 running-config를 조회하거나 운영 장비에 Load하지 않았다. 아래는 소스 추적과 제한된 재현 결과이며, 설치된 바이너리가 현재 소스와 같은지는 별도 확인 대상이다. 애플리케이션 코드는 변경하지 않았다.

## 결론

현재 running-config는 운영 의도 일부를 담는 버전 문서이지, 새 어플라이언스의 동작을 재구성하는 완결된 복원 패키지는 아니다. 내부 파라미터 제거만으로 해결되지 않는다.

1. 현재 Save 결과를 Load할 프론트 변환기가 구형 스키마에 머물러 있다.
2. Load를 수정하더라도 현재 commit은 전체 교체가 아닌 merge다.
3. 계정, 영구 인증정보, 인증서, 배포 연결정보의 복원 계약이 없다.
4. 새 DB 초기화와 관리자 선언에 결함이 있다.
5. 적용 성공, 실패 복구, 동시 수정, 버전 호환성을 끝까지 보장하지 않는다.

권장 결과물은 사람이 검토하는 비밀정보 없는 설정과, 실제 이관에 쓰는 암호화된 복원 패키지다. 한 파일로 이관하는 UX를 제공하더라도 내부에는 config, secret bundle, certificate assets, manifest가 분리되어야 한다.

## PAN-OS 첨부 파일에서 확인한 것

원본 값이나 비밀값은 이 문서에 복사하지 않았다.

- 루트는 `<config version="9.1.0" urldb="paloaltonetworks">`다. 적어도 해석할 설정 버전에 대한 정보가 있다.
- `mgt-config`에 사용자와 password-complexity, 비어 있지 않은 `phash`가 있다.
- `shared`에는 application/service와 그룹 등 공유 객체가 있다.
- `devices/entry/deviceconfig`에는 system/setting이 있다.
- `network`에는 interface, virtual-router, ike, qos 등이 있다.
- `vsys/entry`에는 zone, address, rulebase, import 등 정책과 객체 참조가 있다.
- 이 파일에서 certificate/private-key/master-key 태그는 발견되지 않았다. 이 샘플만으로 인증서·모든 비밀의 이관 가능성을 입증할 수는 없다.

배울 점은 시스템 설정을 무조건 없애는 것이 아니라, 동작에 필요한 선언과 객체 관계를 버전 있는 문서로 표현하고 복원 절차가 이를 이해한다는 것이다. PAN-OS도 Load는 candidate 교체이고 실제 running 반영에는 commit이 필요하다. 암호화된 설정의 이관에는 master key 처리도 고려한다.

공식 참고: [Operations](https://docs.paloaltonetworks.com/ngfw/help/10-1/device/device-setup-operations), [Revert configuration](https://docs.paloaltonetworks.com/ngfw/administration/firewall-administration/manage-configuration-backups/revert-firewall-configuration-changes).

## 현재 경로

```text
config/startup-config.json
  → script/start.py: 배포 시 DB password 주입
  → /etc/pretzel/startup-config.json: DB 연결 + 초기 seed
  → Config::seedStore(): running_config 최초 생성, 일부 redaction

브라우저 sessionStorage candidate
  → POST /api/settings/commit: scope/domain별 검증
  → IPC SettingsCommitRequest
  → engined CommitService: 기존 root에 merge_patch
  → Config::commitConfig(): 새 버전을 바로 active로 기록
  → 일부 daemon 재시작 및 버전 수렴 확인
  → mgmtd cache 무효화 + 별도 AI ApplyConfig

Save → active config_json 파일 저장
Export → 저장 파일 다운로드
Import → JSON 문법 검사 후 저장 파일 생성
Load → 브라우저에서 구형 문서를 change 배열로 변환 → 일반 commit
```

## 주요 발견과 수정안

### F1. [P1 / 재현] Save → Load 왕복이 깨져 있다

근거: `mgmtd/www/js/operation.js:108`, `SettingsController.cpp:175` 부근의 scope 검증.

현재 문서는 `pretzel.site`, `pretzel.connector`, `pretzel-ai.providers` 등의 구조인데 `toChanges()`는 `config[daemon].service`만 탐색한다. 현재 baseline에 원본 JS 함수를 실행한 결과 change 수는 **0**이었다. 구형 파일로 실행하면 `{daemon, domain, values}`를 만들지만 API는 `{scope, domain, values}`를 요구한다. 따라서 현재 구조와 구형 구조 모두 정상적인 Load가 안 된다.

수정: 단순 필드 변경으로 끝내지 말고 서버 측 import/restore parser를 만들고, 지원하는 구형 포맷만 명시적으로 변환한다. 프론트가 파일 구조를 임의로 분해하지 않게 한다.

### F2. [P1 / 코드 확인] 전체 복원이 아니라 부분 merge다

근거: `engined/service/commit/CommitService.cpp:231`.

입력에 없는 domain/key는 남는다. 예를 들어 과거 백업에 notification이 없더라도 이후 추가된 알림 프로필이 남을 수 있다. auth의 한 방식으로 되돌려도 다른 하위 설정이 남을 수 있다. 배열은 교체되지만 객체는 merge되므로 삭제 의미도 일관되지 않다.

수정: `replace`와 `merge` 계약을 분리한다. 전체 복원은 portable 영역을 정규화한 목표 문서로 교체하고, 빠진 선택 영역은 해당 스키마 기본값으로 해석한다. 대상 장비의 bootstrap 영역 보존은 명시적 정책으로 처리한다.

### F3. [P1 / 코드 확인] 설정 파일에는 인증에 필요한 영구 자산이 없다

근거: `shared/db/Database.cpp`, `shared/util/Secret.cpp:51`, `engined/service/admin/AdminService.cpp:106`, 각 UI 모듈의 credential API 경로.

| 자산 | 실제 위치 | config-only 복원 결과 |
|---|---|---|
| 로컬 사용자 hash/salt/OID | local_users | 사용자 선언만 존재하고 원래 비밀번호로 로그인 불가 |
| NGFW/SASE 계정 비밀 | api_credential_state.id_enc/pw_enc | 재발급에 필요한 원본 credential 누락 |
| 발급된 vendor key | api_credential_state.key_enc | 재발급 또는 선택 이관 필요 |
| 수동 등록 SASE key | sase_device.api_key_enc | 별도 원본 비밀 없이 자동 재생성 불가할 수 있음 |
| AI provider / AIRS / gateway key | ai_provider_credential_state, ai_route_credential_state | AI 경로 선언만 복원됨 |
| SMTP password | notification_credential_state | 인증 relay 사용 시 발송 실패 |
| console certificate/private key | /etc/pretzel/cert 등 파일 | 경로만 복원, 원래 TLS 정체성은 복원되지 않음 |
| credential 암호화 root key | /etc/pretzel/credentials.key | 기존 DB ciphertext만 복사해도 새 키로 복호화 불가 |

수정: 영구 비밀과 임시 상태를 분리해 inventory를 정의한다. 암호화된 이관 패키지를 가져올 때 source secret을 복호화하고 target 장비의 키로 재암호화한다. 원본 장비의 master key를 모든 대상에 그대로 복제하는 방식을 기본으로 삼지 않는다. 비밀번호 hash는 평문 비밀번호가 아니라 hash 알고리즘/파라미터/salt/OID와 함께 보호하여 이관한다. bearer token, session, 임시 OAuth transaction은 새로 만든다.

### F4. [P1 / 코드 확인] Fresh DB의 장비 테이블 생성 경로가 빠져 있다

근거: `shared/db/Database.cpp:12`의 실제 kSchemaDDL, `Database::ensureSchema():607`, `shared/db/schema.sql:140`, `:157`.

실행되는 DDL에 ngfw_device/sase_device CREATE TABLE이 없다. 해당 CREATE는 별도 schema.sql에만 있고 저장소에서 그 파일을 실행하는 설치·부팅 호출을 찾지 못했다. 반면 `ProbeService::projectInventory()`는 두 테이블에 upsert한다. 기존 DB가 있는 개발 환경에서 가려지고, 빈 DB에서 깨질 수 있는 이관 차단 요인이다.

수정: 설치와 부팅이 같은 migration registry를 실행하도록 단일화한다. 빈 DB → 전체 migration → inventory projection을 필수 인수 테스트로 둔다. 이 판단은 소스 기준이며 실제 빈 PostgreSQL 인스턴스에서의 통합 재현은 미실시했다.

### F5. [P1 / 코드 확인] 최초 관리자 선언이 자동 완성되지 않는다

근거: `Config.cpp:249`, `:285`, `:428`; `Database.cpp:494–526`; `mgmtd/service/auth/AuthService.cpp:56`.

preflight는 ensureSchema 이후 seedStore를 실행한다. 사용자 선언 backfill SQL은 앞의 ensureSchema에 있고, `user` domain이 없을 때만 실행된다. 현재 baseline은 이미 `user: {list: []}`를 갖는다. 이후 local_users에 admin을 만들더라도 선언은 빈 배열로 남으며, 다음 실행에도 domain이 존재하므로 backfill하지 않는다. declaredRole은 선언 없는 계정을 user로 취급한다.

수정: 새 장비 seed 시 최초 관리자 credential과 선언/OID를 한 transaction에서 만든다. 복원 시에는 이름뿐 아니라 OID 충돌과 실제 인증 가능한 관리자 존재를 검사한다. 기존 admin 이름에 다른 OID를 덮어쓰는 규칙도 명시해야 한다.

### F6. [P1 / 코드 확인] 적용 실패한 버전도 active로 남는다

근거: `Config.cpp:461`, `CommitService.cpp:259`, `mgmtd/service/bootstrap/BootstrapService.cpp:225` 이후.

DB enum에는 pending이 있으나 commitConfig는 이전 active를 superseded로 만들고 새 버전을 즉시 active로 쓴다. 이후 daemon reload가 실패해도 active를 되돌리는 경로가 없다. mgmtd 코드 역시 이 상황을 전제로 AI 설정을 계속 push한다. README의 pending → active 및 last-known-good 설명과 실제 동작이 다르다.

수정: durable apply job과 target version을 만들고 pending 문서를 대상으로 prepare/apply를 수행한다. pending을 데몬이 명시적으로 읽을 방법도 함께 필요하다. 필수 서비스 성공 후 active를 전환하고, 실패 시 이전 버전 재적용과 복구 결과를 기록한다. 이미 발송한 이메일 같은 외부 부작용은 rollback으로 취소되지 않으므로 검증 중 스케줄 실행을 막는다.

### F7. [P1 / 코드 확인] 모든 영역의 실제 적용을 확인하지 않는다

근거: `CommitService.cpp:259`, `mgmtd/core/MgmtdCore.cpp:177`, `engined/service/bootstrap/BootstrapService.cpp:409`.

reload 대상은 authd/probed/collectord/topologyd/apid이며 mgmtd와 ipcd는 제외된다. console listener/TLS는 mgmtd 시작 시 읽고, commit 완료 시 cache 무효화만으로 기존 HTTP server가 재구성되지는 않는다. AI는 별도 gRPC push이고 fleet 수렴 대상에 없다. DB 설정은 running이 아니라 startup 파일에서 읽는다.

수정: domain마다 소비자, live reload 가능 여부, restart 필요 여부, readiness 검사, 실패 복구 방식을 선언한다. console 접근 주소 변경에는 새 주소 확인 및 이전 접근 경로 복구가 필요하다. 설정에 기록됐다는 사실과 실제 적용됐다는 사실을 UI에서 구분한다.

### F8. [P1 / 코드 확인] Import와 commit의 검증 범위가 복원용으로 부족하다

근거: `SettingsController.cpp:385`, `SettingsValidation.cpp:953`, `shared/config/ApiRefs.cpp:31`.

Import는 JSON parse 성공만 검사하므로 `[]`, 숫자, 무관한 JSON도 저장 가능하다. schema_version이나 지원 release 판단이 없다. known scope의 unknown domain은 허용되고, 일부 domain만 shape 검사를 갖는다. connector 참조 검사는 endpoint/auth_profile 중심이며 site/device/rack/credential-device 일치 등 전체 그래프 검사가 아니다. 검사와 실제 write도 다른 시점·프로세스다.

수정: manifest/format/schema 검사 → 버전 migration → 전체 목표 config shape → 전체 참조 그래프 → secret/asset 결합 → 대상 환경 preflight 순으로 검증한다. 타입 불일치는 예외가 HTTP 요청 처리를 깨뜨리지 않도록 path별 오류로 반환한다. commit worker도 최종 목표 문서를 다시 검증한다.

### F9. [P1 / 코드 확인] “running-config에는 secret이 없다”는 보장이 전체에 적용되지 않는다

근거: `Config.cpp:125`, `authd/service/auth/AuthService.cpp:45`, `SettingsValidation.cpp:1003`.

redactSecretsForPersist는 pretzel.database.password와 console.admin만 지운다. authd는 auth.oidc.client_secret을 읽는데 auth domain에는 일반 commit의 비밀 필드 거부 규칙이 없다. 따라서 해당 값을 설정하면 일반 View/Save/Export로 나갈 수 있다. 실제 운영 설정에 값이 들어 있는지는 조회하지 않았다. Import 저장 경로는 redaction도 없이 전달받은 JSON 원문을 저장한다.

수정: OIDC client secret을 별도 secret store로 이동하고 secret_ref를 둔다. domain/schema에서 허용 필드를 선언하고 sensitive 자산을 전역적으로 분류한다. 비밀정보 없는 export와 비밀 포함 암호화 export를 별도 계약으로 제공하며, 가져온 임의 JSON을 곧바로 공개 설정 저장소에 넣지 않는다.

### F10. [P2 / 코드 확인] Load/Publish의 진행·동시성·candidate 처리 불완전

근거: `operation.js:124`, `settings/review.js:137`, `settings/draft.js:73`, `SettingsController.cpp:280`.

- API의 applied 응답은 engined에 IPC 요청을 보냈다는 뜻이고 DB commit 완료가 아니다.
- Operation Load는 일반 Publish의 완료 polling 없이 즉시 Applied로 표시하고 재조회한다.
- 일반 Publish도 완료 전에 onPublished를 호출하고 비밀정보 저장을 추가 요청으로 시작한다. config와 credential이 하나의 원자적 작업이 아니다.
- Load는 browser candidate와 diff/review를 공유하지 않는다. 기존 sessionStorage draft를 무효화/재기반하지 않는다.
- draft는 version이 감소할 때만 폐기한다. Load/restore가 새 버전으로 증가하면 예전 draft가 남아 재발행될 수 있다.
- commit 요청에 base_version/CAS가 없다. 두 운영자의 배열 편집이나 validation 이후 대기 큐의 변경이 겹치면 앞선 결과를 덮어쓸 수 있다.

수정: 서버 candidate에 base_version을 두고 충돌을 409로 보고한다. Load는 candidate를 만들고 diff, 누락 자산, restart 항목을 보여준다. apply는 202 + durable job_id를 반환하며 UI는 최종 job 상태를 따른다. credential/asset 준비 결과까지 한 apply job에 포함한다.

### F11. [P2 / 코드 확인] 포맷 호환성과 이력 migration이 임시 처리에 의존

근거: `Config.cpp:317`, `:404`, `Database.cpp:507`.

문서에 schema_version이 없고 startup default로 scope/domain을 backfill하거나 history 행을 직접 바꾼다. 따라서 같은 config version의 내용이 부팅/업그레이드 전후에 달라질 수 있다. 과거 파일의 guardrail → route 같은 변환도 import용 계약으로 정리되어 있지 않다.

수정: config schema migration과 DB migration을 분리해 버전 관리한다. 백업에는 format/schema/product version과 호환 조건을 기록하고, 원본 archive는 보존한 채 변환 결과를 새 revision으로 만든다. 미래 포맷은 조용히 무시하지 않고 거부한다.

### F12. [P2 / 코드 확인] config rollback만으로 secret rollback이 되지 않는다

근거: `CommitService.cpp:287`, `CredentialService.cpp:215`.

AI credential prune이 commit 후 즉시 실행된다. 이전 config에 있던 provider/route를 제거하면 해당 비밀도 삭제될 수 있고, 이전 config를 다시 Load해도 복구되지 않는다. 사용자 비밀번호와 SMTP 비밀번호 변경도 configuration version과 같은 이력 계약이 없다.

수정: 설정 이력과 credential 수명 정책을 분리하되 복원 가능 기간을 정의한다. active/pending/복구용 revision에서 참조하는 secret revision은 유지하고, 최종 성공 이후 GC한다. 보안상 비밀번호 변경을 자동 rollback할지 여부는 자산 종류별로 정한다.

## 설정에서 걷어내거나 변경할 항목

| 현재 항목 | 처리 | 이유 |
|---|---|---|
| global.ipc socket/buffer/retry/max_connections | bootstrap 또는 코드 기본값으로 이동 | 프로세스 통신 구현·배포 경로이며 portable 운영 의도 아님 |
| global.logger.dir | 장비 로컬 경로로 이동 | 파일 경로는 대상 장비가 결정 |
| logger retention/size | 제품 정책이면 logging 도메인으로 유지 | 운영자가 결정하는 보존 정책은 의미 있는 설정 |
| pretzel.database 전체 | bootstrap으로 이동 | 실제 DB 접속도 startup 파일에서 결정되며 portable config로 바꿀 수 없음 |
| console.cert_file/key_file | certificate_ref로 대체 | 경로 대신 복원 가능한 certificate 자산을 참조 |
| console/API listen address·port·TLS | management 도메인 + 이관 옵션 | 운영 정책이지만 대상 네트워크에 의존; 무조건 삭제 대상 아님 |
| pretzel.router.allow_unknown_dst | 제거 후보 | 현재 C++/JS 소비 경로를 찾지 못함; 호환 검토 후 제거 |
| pretzel.scan.community/v3_devices | 제거 또는 정식 SNMP 도메인 구현 | 현재 소비 경로를 찾지 못함; 비밀값이라면 별도 secret 자산 필요 |
| pretzel.rack.racks | 구현 여부 확정 후 유지/제거 | 현재 runtime 소비 경로를 찾지 못함 |
| site/devices/endpoints/connectors | 유지 | 핵심 운영 의도와 객체 관계 |
| NGFW fingerprint | trust 설정으로 유지 | 관측 캐시가 아니라 상대 서버를 신뢰하는 근거; 바뀐 대상에 자동 재신뢰 금지 |
| auth IdP, SP ID, ACS, redirect URI | 유지 + 이관 preflight | 대상 hostname 변경 시 IdP 등록도 맞아야 함 |
| user 선언 | 유지 + credential 참조 결합 | 선언만으로는 계정이 작동하지 않음 |
| notification/scheduling | 유지 + secret 결합 | 알림 정책은 portable 운영 의도 |
| pretzel-ai providers/route | 유지 + secret 참조 | 외부 AI 서비스 접속과 자산 준비 검증도 필요 |
| template // 주석 키 | factory template에만 유지 | start.py는 최상위 주석만 제거해 중첩 주석이 persist될 수 있음 |

PZ_PRETZEL_AI_TARGET, PZ_APID_TOKEN 및 서비스 실행 환경은 별도 dependency inventory에 포함한다. AI target은 내부 co-location이면 bootstrap으로, 운영자가 외부 AI를 선택하는 기능이면 명시적 service connection 설정으로 만든다. OS IP/DNS/NTP/route, firewall, service units, 외부 DB/AI corpus는 현재 config가 복원한다고 볼 수 없다. 제품이 지원할 범위를 manifest에서 명확히 해야 한다.

## 권장 복원 패키지와 UI/API

```text
appliance-backup
  manifest.json       format/schema/product version, asset 목록, 호환 조건
  config.json         portable 선언 + 안정적 object id + secret/certificate ref
  protected-assets    암호화된 영구 비밀, 선택한 account hashes/certificates
  optional-data       운영 이력 보존을 선택한 경우만 포함
```

config, manifest, 자산의 무결성도 함께 보호해야 하며 체크섬만으로 출처 인증이 되지는 않는다. 비밀 없는 export에서는 빠진 참조를 누락 목록으로 표시하고, 복원 완료라고 보고하지 않는다. API 토큰·세션·probe 상태·last_seen·로그는 기본 config에서 제외한다. 알림 발송 checkpoint는 단순 캐시와 다르게 재발송에 영향을 주므로 DR에서는 이관하거나 명시적 기준 시각으로 초기화한다. 복제 장비가 기존 장비와 동시에 수집·발송하지 않도록 activation 단계를 둔다.

제안 API(현재 구현 아님):

| API | 역할 |
|---|---|
| POST /api/config/exports | config-only 또는 암호화 migration bundle 생성 |
| POST /api/config/imports | 임시 업로드, 포맷·호환성·자산 검사 |
| POST /api/config/imports/{id}/validate | replace/merge, 대상 management 보존 옵션으로 최종 목표 검증 |
| GET /api/config/imports/{id}/diff | 추가·변경·삭제, 누락 비밀, 재시작 및 외부 의존성 |
| POST /api/config/imports/{id}/load | 서버 candidate 생성, 운영에는 아직 미적용 |
| POST /api/config/candidates/{id}/commit | base_version 검사 후 apply job 생성 |
| GET /api/config/jobs/{id} | 구성·자산·각 서비스 적용 결과와 recovery 결과 |

UI는 Import → 검증/변환 → Load candidate → diff 및 환경 매핑 → Commit → 실제 준비 상태 순으로 만든다. 기존 후보가 있으면 교체 또는 취소를 명시적으로 선택한다. schema mismatch, missing secret, unreachable dependency, restart required를 서로 다른 상태로 보여준다. 적용과 복구 작업은 전용 관리자 권한으로 제한한다.

## 우선순위 및 인수 기준

1. 먼저 Load 포맷 오류, fresh DB migration, 최초 admin seed, secret 노출 경로를 수정한다.
2. portable/bootstrap/secret/runtime 분류표를 실제 schema registry로 구현하고 format version을 도입한다.
3. replace 기반 server candidate와 전체 문서 검증, base_version/CAS, durable job을 구현한다.
4. 암호화 자산의 export/import 및 인증서·계정 결합을 구현한다.
5. 서비스별 apply와 last-known-good recovery, UI 완료 판정을 연결한다.

필수 테스트:

- 빈 DB + 새 credentials.key + 다른 장비 환경에서 설치와 관리자 초기 접근 성공.
- 원본 A → export → 빈 B → import/validate/load/commit → 정규화된 portable config 동등성.
- 원래 local account 로그인, vendor 인증/수집, AI 호출, SMTP 테스트가 실제 성공.
- 백업에 없는 객체가 replace 후 남지 않으며, 참조가 잘못된 파일은 운영 변경 없이 거부.
- 비밀 없는 설정과 잘못된 복호화 키는 ready로 보고되지 않음.
- 중간 daemon 실패, 프로세스 종료, DB 오류에서 이전 정상 설정과 자산 복구.
- 이전/현재/미래 schema, concurrent editor, 남아 있는 browser draft 처리.
- console 주소·TLS 변경의 접근 복구 및 별도 AI 실패 결과 표시.
- migration/clone 이후 중복 알림·과거 advisory 대량 재발송 방지.

## 검증 범위

원본 operation.js의 toChanges 함수를 Node VM으로 추출해 현 baseline과 구형 구조에 실행했다. 결과는 현 baseline 0 changes, 구형 출력 scope 없음이었다. 나머지 결함은 source/data-flow 확인이다. PAN-OS XML은 구조와 태그 존재만 검사했다. 운영 DB·메일·장비·설정은 변경하지 않았고 새 장비 복원 E2E는 실행하지 않았다.
