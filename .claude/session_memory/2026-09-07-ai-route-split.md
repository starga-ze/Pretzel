# 2026-09-07 — AI Route: 축 분리와 개명

짝 문서: `pretzel-ai` 저장소 `.claude/session_memory/2026-09-07-transport-guardrail-split.md`

## 수행

### 개명 — guardrail → route

| 계층 | 이전 | 이후 |
|---|---|---|
| 콘솔 페이지 | AI Guardrail | AI Route |
| JS | `www/js/ai-guardrail.js` | `www/js/ai-route.js` (`TAB` `DRAFT_KEY` `SECRET_KEY` `DOMAIN`) |
| running_config 도메인 | `pretzel-ai.guardrail` | `pretzel-ai.route` |
| 봉인 키 테이블 | `ai_guardrail_credential_state` | `ai_route_credential_state` |
| IPC scope 문자열 | `"guardrail"` | `"route"` |
| startup-config 주석 | `//guardrail1..8` | `//route1..10` |

사유 — 페이지가 두 번째 축(transport)을 갖게 되어, 검사자만 가리키는 이름이 페이지가 담는 것의
절반을 설명하지 못함. 저장 키까지 함께 옮긴 이유는 `AiConfig.cpp` 가 섹션을 이름으로 읽기 때문.
콘솔이 `route` 를 쓰고 리더가 `guardrail` 을 찾으면 운영자가 아무도 푸시하지 않는 섹션을 편집.

### 두 축

```
kTransports      { "direct", "ai_gateway" }        SettingsController.cpp
kGuardrailKinds  { "none", "api_application" }     ai_gateway 가 여기서 빠져 위로 이동
```

- `transport` 는 **기본값 없이 필수 검증**. 없는 행은 아무도 답하지 않은 행이고, 여기서 `direct`
  를 넣는 것은 mgmtd 가 고객 트래픽 경로를 결정하는 것
- `serviceDoc()` 도 두 축 모두 기본값 없이 행 그대로 전달. 미인식 값은 pretzel-ai 가 이름째 거부
  하고 그 거부가 유용한 결과. 이쪽에서 만든 기본값은 mgmtd 가 읽을 수 없는 행의 뜻을 정하는 것
- 용어 통일 — pretzel-ai 의 config·빌더·로그가 전부 `transport`. 콘솔 컬럼 헤더와 running_config
  필드명까지 같은 단어. 다르면 로그 한 줄과 리뷰 diff 를 서로 대조 불가

### 마이그레이션 2건

**1. running_config 키 개명** — `shared/config/Config.cpp` `seedStore()`

```sql
UPDATE running_config SET config_json = jsonb_set(
  config_json, '{pretzel-ai}',
  (config_json -> 'pretzel-ai') - 'guardrail'
    || jsonb_build_object('route', config_json -> 'pretzel-ai' -> 'guardrail'))
WHERE config_json -> 'pretzel-ai' ? 'guardrail'
  AND NOT (config_json -> 'pretzel-ai' ? 'route')
```

- **back-fill 보다 앞**에 위치해야 함. startup-config 가 이미 `route` 를 싣고 있어, back-fill 이
  먼저 돌면 빈 `route` 가 생김. 이 개명은 기존 `route` 를 덮지 않으므로(멱등성의 근거) 그때는
  no-op 가 되고, 운영자 행은 아무도 읽지 않는 `guardrail` 밑에 남음
- **모든 행**. 이력은 append-only 이고 재작성은 보통 잘못된 본능이지만, 설정 내용이 아니라 키
  철자만 바뀜. 옛 철자로 남은 버전은 이 빌드가 읽지 못하는 버전이고 리셋·롤백이 착지하는 곳
- 버전 범프 없음. 어느 데몬의 동작도 바뀌지 않으므로 수렴할 것이 없고, 범프는 개명된 키를
  배달하려고 fleet 을 재기동시키는 일

**2. 테이블 개명** — `shared/db/Database.cpp`

- `ALTER TABLE ... RENAME TO` + 제약 이름 2개. `CREATE TABLE IF NOT EXISTS` **앞**에 위치해야 함.
  뒤면 빈 테이블이 먼저 생겨 개명이 거부됨
- RENAME 채택 사유 — 행이 봉인된 키 자료. 재INSERT 하는 마이그레이션은 절반 성공 시 어플라이언스에
  더 이상 열 수 없는 키를 남김
- 양방향 가드로 신규 DB(옛 테이블 없음)·이미 마이그레이션된 DB(새 테이블 있음) 모두 no-op

### 벤더 id 폐쇄 + 고아 키 정리

- `ai_provider_credential_state.id` 에 `CHECK (id IN ('openai','google','anthropic'))` 추가.
  기존 위반 행은 ALTER 전에 DELETE — 제약 추가 실패는 `ensureSchema` 실패 → engined preflight
  실패 → 부팅 정지이고, 걸릴 행들은 제약이 말하는 정의상 무의미
- **측정 2026-09-07** — `'claude'` 행 1건 발견. 벤더가 `anthropic` 으로 개명되기 전 잔재. 콘솔은
  자격증명 엔드포인트를 같은 화이트리스트로 필터링하므로 화면에 나타나지 않았음. 즉 **운영자
  조작으로 도달 불가한 키 자료**
- 원인 — 옆 테이블(`ai_route_credential_state`)에는 CHECK 가 있고 이쪽에 없던 비대칭
- `ApiCredentialService::pruneAiCredentials()` 신설. `CommitService` 가 매 커밋 호출,
  `invalidateConfigCache()` **뒤**(prune 이 커밋된 문서를 읽어야 하고 그 위의 캐시는 대체된 사본)
- 어느 키가 살아남는지는 **축에서 따라옴** — 게이트웨이 키는 `transport`(그것 없이 연결 못 하는
  쪽), AIRS 키는 `guardrail`. 콘솔 `keysFor()` 와 같은 쌍이어야 하며, 어긋나면 Publish 가 방금
  유지한다고 표시한 키를 지움
- 매 커밋 실행 사유 — pretzel-ai 를 건드리지 않은 커밋은 지울 것을 못 찾을 뿐이고, 다른 이유로
  어긋난 어플라이언스(중단된 publish, 손편집, 복원)가 다음 커밋에 정렬됨. 특정 페이지를 누가
  편집할 때까지 틀린 채로 남지 않음
- 스토어당 1문장 + 살아남을 id 를 인자로 — 비교를 DB 가 하므로 read-then-write 창 없음

### 콘솔

- 컬럼 `Guardrail` 1개 → `Transport` / `Inspection` 2개
- `keyDraft` 스칼라 → 객체. 축마다 키가 달림
- `CREDENTIALS` 맵 신설(`airs` / `portkey`). 라벨을 축에 인라인하지 않음 — 한 축이 두 키를 갖거나
  두 축이 한 키를 공유해도 라벨이 한 곳
- `keysFor(e)` = transport 의 키 + guardrail 의 키. 미저장 경고가 어느 축 때문인지 지목
- transport 미인식 값은 `direct` 로 폴백. 에디터는 못 그리는 행을 운영자가 고칠 수 없게 남기면
  안 됨. **guardrail 축은 조용한 폴백 금지**
- `gateway.require_verdict` 제거

### AiConfig

- `describeService()` → `chat=direct/none(none)` 형식. 검사자만 적는 줄은 턴이 어느 경로로 가는지
  말할 수 없게 됨
- `api_application` 인 행에만 `profile=` 추가. api_application 서비스가 빌드를 거부하는 나머지 한
  조건이고, 가드레일만 적고 프로필을 비운 문서가 예고 없는 거부를 만들었음. 스캔하지 않는 배포는
  아무도 안 쓰는 프로필을 보고하지 않음

## 처리한 결함

| 위치 | 원인 · 처리 |
|---|---|
| `ai_provider_credential_state` | CHECK 부재로 고아 키 행 `'claude'` 1건. 콘솔이 같은 화이트리스트로 필터링해 비가시. CHECK 추가 + `pruneAiCredentials()` |
| 제약 이름 | `ai_gateway_credential_state_pkey` 가 테이블 개명 후에도 잔존. `\d` 출력이 개명이 절반만 된 것처럼 읽힘. `RENAME CONSTRAINT` |
| `.cfg-table-airoute` | `table-layout: fixed` 에서 Transport/Inspection 컬럼이 엔드포인트 URL 을 달고 있었음. 19% 컬럼에서 50자 무공백 문자열이 줄바꿈 못 하고 옆 셀 위에 덮어 그림. 짧은 이름만 남기고 폭 재배분, `td { overflow-wrap: anywhere }` 추가 |
| `main.css` nav 그룹 버튼 | `font` 단축 속성 제거 시 `line-height` 가 UA 기본(~1.2)에 남음. 형제 `<a>` 는 body 의 1.5 를 상속하여 그 행만 ~3px 낮고 Configuration ▸ System Operation 구간이 리듬 끊김으로 읽힘. `line-height: inherit` |
| `.field-row` 리듬 | 리듬을 margin-**bottom** 으로만 들고 있어, field-row 가 아닌 블록 다음의 라벨이 위 블록에 붙어 보임. 옵션 카드 + Endpoint, Fail open + AIRS key 두 곳. 형제 쌍으로 명시 |

## 검증

- 빌드·설치 2026-09-08 09:25. `pz-engined` `pz-mgmtd` 09:25:11 기동
- `ApplyConfig` 3회 도달(09:24:57 mgmtd ready / 09:25:11 fleet runtime start / 09:25:12 재연결).
  pretzel-ai 가 v21 로 chat 엔진 빌드 성공
- 푸시 문서에 두 축이 실림 — pretzel-ai 로그의 `services=[chat=direct/none(none)]` 이
  `describeService()` 출력 그대로
- running_config v21 이 `route` 도메인으로 읽힘. pretzel-ai 캐시 복원 로그의 version 21 과 일치

**미검증**
- 두 마이그레이션의 DB 상태 직접 확인 실패. psql 접근 권한 없음(`sudo: a password is required`).
  동작·로그상 정상이나 `\d ai_route_credential_state` 로 제약 이름까지 눈으로 확인 필요
- `transport=ai_gateway` 행. 게이트웨이 키 미저장(`gateway_key=none`)
- `pruneAiCredentials()` 의 실제 삭제. `'claude'` 행 발견은 코드 주석이 근거이고 본 세션에서
  재현하지 않음
- agent 행. running_config 에 없어 푸시에 미포함. 콘솔에서 추가 시 두 축 검증 재확인 필요

## TODO

| 항목 | 왜 안 됐나 | 어디를 |
|---|---|---|
| 잔존 주석 | "Five of the six combinations are deployments. The sixth — direct path, delegate to the gateway — ... disabled where it is offered". `delegated` 폐기로 조합은 2×2=4 이고 전부 유효, 비활성 조합 없음 | `mgmtd/www/js/ai-route.js:53` |
| 빈 줄 2개 | `transport` 검증 블록 뒤 | `mgmtd/service/web/controller/SettingsController.cpp` `validateServiceEntry` |
| verdict 와이어 | pretzel-ai 가 판정을 만들어도 proto `ChatResponse` 에 실을 필드가 없음. 짝 작업 | `mgmtd/grpc/pretzel_ai.proto` + 콘솔 |
| 마이그레이션 눈 확인 | psql 접근 권한 | `ai_route_credential_state` 제약 이름, running_config 전 행의 `route` 키 |
| 커밋 | 14개 파일 미커밋 | `git status` |
