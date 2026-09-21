# 2026-09-21 — AI 모델 카탈로그 · 대화 저장 결함 · 콘솔 정리

짝 문서: `pretzel-ai` 저장소 `.claude/session_memory/2026-09-21-airs-live-token-param.md`

## 수행

### AI 모델 카탈로그 — 벤더에서 조회

**문제** — 콘솔이 모델 목록을 `mgmtd/www/js/ai-provider.js` 의 `VENDORS` 표에 하드코딩.
빌드 이후 출시된 모델은 다음 릴리스까지 선택 불가. 파일 주석 스스로
"This WILL go stale: vendors ship models on their own schedule and this file does not" 라고 인정.

**경로** — 기존 커넥터 테스트와 같은 세 축.

```
mgmtd (Operation ▸ AI Model 카드 · Update)
  → POST /api/ai/models/update {id}           202 + ticket
  → AiModelUpdateRequest (DeviceOp)           → collectord
       → AiCredentialStateRequest (Read)      → engined
       ← AiCredentialStateResponse             봉인된 키
       → credentials.key 로 unseal → 벤더 list API → 정제
  → AiModelUpdate (Write)                     → engined → ai_provider_model
  → AiModelUpdateResponse                     → mgmtd → ticket → 브라우저
```

collectord 경유 사유 — 아웃바운드 벤더 호출을 전부 소유하고, `io_context` 를 가진 유일한 데몬.
mgmtd 는 `io_context` 가 없음(전체 0건). `requestSync` 는 tick 루프 데몬에서 금지
(`shared/http/HttpClient.h` 주석).

**IPC 5개 신설** — 147 다음 빈 번호부터.

| cmd | 방향 | 분류 |
|---|---|---|
| `AiModelUpdateRequest = 148` | mgmtd → collectord | DeviceOp |
| `AiModelUpdateResponse = 149` | collectord → mgmtd | DeviceOp |
| `AiModelUpdate = 150` | collectord → engined | Write |
| `AiCredentialStateRequest = 151` | collectord → engined | Read |
| `AiCredentialStateResponse = 152` | engined → collectord | Read |

`ApiCredentialState{Request,Response}` 를 확장하지 않고 별도 쌍을 만든 사유 — 두 저장소가
다른 테이블의 다른 종류 자격증명. 한 응답에 둘을 실으면 어느 쪽 호출자에게든 상대의 키 재료가
전달됨.

**키를 캐시하지 않음** — `ApiCredentialState` 는 주기 수집이 매 폴마다 DB를 치는 것을 막으려
캐시. 이쪽은 운영자가 버튼을 누를 때만 실행되므로, 캐시는 "방금 입력한 키가 쓰이지 않는" 경우만
추가함.

**신규 파일**

| 파일 | 내용 |
|---|---|
| `collectord/service/api/controller/AiModelController.{h,cpp}` | 조회 2단계 + 벤더별 정제 |
| `mgmtd/www/js/ai-model-card.js` | Operation 카드 + step 기반 진행 창 |

**벤더별 정제**

| 벤더 | 필터 근거 | 레이블 |
|---|---|---|
| OpenAI | **휴리스틱** — `gpt` 접두사 + embedding/tts/whisper/audio/realtime/image/moderation 등 제외. 응답에 채팅 여부 필드 없음 | id |
| Google | `supportedGenerationMethods` 에 `generateContent` | `displayName` |
| Anthropic | 전부 채팅 모델 | `display_name` |

`token_param` 은 **벤더 단위 파생**(OpenAI `max_completion_tokens`, 나머지 `max_tokens`).
어느 벤더의 목록 API도 이 값을 보고하지 않음.

빈 목록은 전송하지 않음(collectord) + 수신 시에도 거부(engined). 벤더가 응답 형태를 바꿨을 때
운영자의 메뉴가 비는 것을 막음.

**engined 쓰기** — `BEGIN` → `DELETE WHERE provider` → `INSERT` → `COMMIT`.
`LogTailService.cpp` 의 트랜잭션 관용구(실패·COMMIT 실패 모두 `ROLLBACK`)를 따름.
벤더 단위 전체 교체 — 목록이 벤더의 답이므로, 사라진 모델도 함께 사라져야 함.

### 스키마

`shared/db/Database.cpp` `kSchemaDDL` 에 `ai_provider_model` 신설.

```sql
CREATE TABLE IF NOT EXISTS ai_provider_model (
    provider     TEXT        NOT NULL CHECK (provider IN ('openai', 'google', 'anthropic')),
    model_id     TEXT        NOT NULL,
    label        TEXT,
    token_param  TEXT,
    fetched_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (provider, model_id)
);
```

`oid` 없음 — 운영자가 만든 객체가 아니라 벤더 소유의 이름. 보존할 정체성이 없음.
config 가 아니라 state — 벤더가 모델을 출시할 때마다 설정 버전이 생기면 안 됨.

**`schema.sql` drift 해소** — 머리말이 "mirrored by kSchemaDDL … keep the two in sync" 라고
주장하나 `ai_` 테이블이 **하나도 없었음**(`ai_provider_credential_state`,
`ai_route_credential_state` 포함). `Database.cpp` 에서 블록을 프로그램으로 추출해 삽입.
두 파일의 해당 구간이 바이트 단위 동일함을 확인.

### chat_message 저장 결함 (측정 2026-09-21)

**증상** — `chat_session` 은 오늘도 갱신되는데 `chat_message` 는 **2026-09-02 이후 0건**.
engined 는 매번 `chat turn stored (session=…, messages=2)` 를 기록.

**원인 2개 중첩**

| | 내용 |
|---|---|
| 1 | `seq` 를 브라우저가 계산. `mgmtd/www/js/chatbot.js` 가 탭 메모리의 메시지 수로 산출 → `ChatController.cpp` 가 `input.value("seq", 0)` 로 받아 그대로 전달. DB가 이미 점유한 번호와 충돌 |
| 2 | `engined/service/chat/ChatService.cpp` 가 `ON CONFLICT DO NOTHING` **무-타깃**. `UNIQUE (session, seq)` 충돌을 재시도처럼 흡수. `db.exec` 는 0행 삽입도 `true` 를 반환하므로 `stored` 카운터가 올라감 |

무-타깃 `DO NOTHING` 은 의도된 변경이었음 — 주석에 원래 사유가 남아 있음: seq 충돌이 에러로
터져 반쪽 턴이 사라졌기 때문. **그 변경이 시끄러운 실패를 조용한 데이터 손실로 바꿈.**

**조치**

```sql
VALUES ($1, $2,
        (SELECT COALESCE(MAX(seq), -1) + 1 FROM chat_message WHERE session = $2),
        …)
ON CONFLICT (oid) DO NOTHING
RETURNING seq
```

- seq 를 engined 가 매김. conflict 타깃을 `(oid)` 로 좁힘 — 정당하게 반복되는 것은 oid 뿐
- `RETURNING` 으로 실제 삽입 행을 세어 로그를 정직하게 만듦
- `seq` 를 브라우저 → mgmtd → engined 체인 전체에서 제거
  (`chatbot.js` `turnMeta`, `MgmtdServiceManager.h` `ChatContext::seq`, `ChatController.cpp` 2곳)

`ChatContext` 주석이 이미 규칙을 말하고 있었음 — *"facts this side established and must not be
re-stated by a client that could say otherwise"*. `seq` 만 그 규칙의 예외였음.

**마이그레이션 불필요** — 기존 3개 세션 전부 seq 0부터 연속, NULL 0건. 컬럼은 `NOT NULL`.
새 문장이 세션별 `MAX(seq)+1` 이므로 이어짐. 9/2~9/21 대화는 DB에 도달한 적이 없어 복구 불가.

### token_param 전달 (proto)

`pretzel-ai` 가 provider 단위 `token_param` 을 도입했으나 **wire 에서 끊김**.

| 단계 | 사실 |
|---|---|
| 1 | `AiProvider` 에 해당 필드 없음 → `GrpcClient.cpp` 의 `JsonStringToMessage(ignore_unknown_fields=true)` 가 조용히 폐기 |
| 2 | `AiConfig.cpp` 가 entry 를 `{id, models}` 로 재구성 — provider 단위 필드가 복사되지 않음 |

조치 — `mgmtd/grpc/pretzel_ai.proto` 에 `token_param = 6`(4·5는 은퇴 번호, 재사용 금지),
`AiConfig.cpp` 가 값을 복사.

증상은 OpenAI 61개 전체가 `max_tokens` 로 나가 `gpt-6-astra` 가 400. 로그 없음.

### 콘솔 정리

**필수 필드 표시** — `.field-row > label.req::after` 하나로 통일. 판정은 각 모듈의 저장 검증과
C++ 스키마를 근거로 함.

| 탭 | 필수 |
|---|---|
| Sites | Site Name |
| Device | Name, Site, Device Type, 접속주소, TLS certificate, Copy API Command |
| API Credential | Name, Site, Device, Endpoint, Username, Password |
| Endpoints | Name, Endpoint(3종), API Credential |
| API Connectors | Name, Site, Device, API Credential, Endpoints |
| AI Provider | Provider, API key |
| AI Route | Service, Profile name |
| Users | Username·Password·Password Confirm(신규 시), Role |

Access Type 은 제외 — Device Type 이 정하는 파생 필드이고 `disabled`.
Users 의 3개는 `isNew` 조건부 — `saveEditor` 가 `editIdx == null` 일 때만 비밀번호를 요구.

**안내문 제거** — 예시 placeholder, "optional", 필드 아래 설명 문단 전부.
상태를 보고하는 것은 유지(`Sealed — type to replace`, `Not pinned`, `Create a site first`,
`Not supported yet.`).

**`enhanceSelects` 누락 2곳** — `config.js`(5곳 재렌더 → `paintEditor()` 헬퍼로 일원화),
`endpoints.js`(1곳). 두 모듈만 OS 기본 드롭다운이 나오고 있었음.

**기타**

| 대상 | 변경 |
|---|---|
| `api-keys.js` REFRESH | Manual/Auto 라디오 폐기. 장비 타입이 정책을 결정 — NGFW `manual` 고정(만료 없음), SASE `auto` 고정 + interval 1~10분 clamp(토큰 15분 만료) |
| `api-connectors.js` | `API Key` → `API Credential` 명칭 통일. `ENDPOINT CONTROL` 섹션 제거 |
| `ai-provider.js` 모델 picker | 검색 + 선택 칩 + Select all(검색 결과 한정) / Clear(전체). 선택 시 편집기 재렌더하지 않음 — 61행 재구성과 검색창 포커스 상실 방지 |
| `users.js` | 자물쇠 아이콘 → 연필(다른 테이블과 동일, 실제로 편집기 전체를 염). `Password` 컬럼 신설(마스크) |
| `table-tools.js` | 컬럼 너비 드래그 조절. 호스트 위임 + `table-layout: fixed` 전환 + 탭별 영속 |
| `operation.js` / `main.css` | 카드 2열 그리드(`.op-grid`) |
| `main.js` | `window.NMS.utils.icons` 공유 아이콘 세트 — `svg()` 래퍼가 3벌 복사돼 있었고 `techdoc.js` 는 아이콘이 없었음 |
| `main.css` `.op-btn-primary` | `.op-btn-load` 개명. Load 전용 이름이 Tech Doc Update 에도 붙어 있어 AI Model Update 에는 아무도 안 붙였음 |
| `main.css` 좌측 레일 | 폰트 12→11px, 아이콘 15→14px, 행 padding 5px, 섹션 헤딩 위 간격 16→12px |
| `.tgl` 스위치 | CSS에만 있고 사용처 0이던 컴포넌트를 커넥터 `Enable` 과 모델 picker 에 적용 |

## 처리한 결함

| 위치 | 내용 |
|---|---|
| `engined/service/chat/ChatService.cpp` | 무-타깃 `ON CONFLICT DO NOTHING` + `db.exec` 반환값을 삽입 성공으로 간주 → 19일간 조용한 데이터 손실 |
| `mgmtd/www/js/chatbot.js` + `ChatController.cpp` | `seq` 를 클라이언트가 결정 |
| `mgmtd/grpc/pretzel_ai.proto` + `AiConfig.cpp` | provider 단위 `token_param` 미전달 |
| `shared/db/schema.sql` | `ai_` 테이블 3개 누락. 머리말이 주장하는 미러가 아니었음 |
| `mgmtd/www/js/config.js` · `endpoints.js` | `enhanceSelects` 미호출 |
| `mgmtd/www/js/benchtest-card.js` | `modal().open()` 을 객체 인자로 호출 — `open(title, bodyHtml, footHtml)` 은 위치 인자. 제목 `[object Object]`, 본문 `undefined`. **미수정** |
| `mgmtd/www/js/api-connectors.js` | 커넥터 `name` 이 JS·C++ 어느 쪽에서도 검사되지 않았음. 다른 4개 객체는 전부 name 필수 |
| `mgmtd/www/js/ai-provider.js` | `activate()` 의 `catalog === null` 가드 때문에 빈 답이 캐시되면 재조회 불가 |
| `mgmtd/www/js/ai-provider.js` | `saveEditor` 가 `normalizeEntry` 를 거치지 않아 편집한 항목만 뚱뚱하게 저장 |
| `mgmtd/www/css/main.css` | `.ai-pick-row { display: grid }` 가 UA `[hidden]` 을 이겨 검색 필터가 한 번도 동작하지 않았음 |

## 검증

**빌드** — `pz-shared` `pz-collectord` `pz-engined` `pz-mgmtd` 포함 전체 타겟 통과.
새 `.cpp` 는 `GLOB_RECURSE` 때문에 `cmake -S . -B build` 재구성 필요.

**모델 조회 실행** (2026-09-21 11:19)

| 벤더 | 결과 |
|---|---|
| OpenAI | 61개 |
| Google | 41개 |
| Anthropic | 11개 |

**chat_message 수정 검증** — 실제 테이블에 `BEGIN … ROLLBACK` 으로 확인.

```
1번째 삽입(질문) → seq 11   INSERT 0 1
2번째 삽입(답변) → seq 12   INSERT 0 1
같은 oid 재시도   → 0 rows   INSERT 0 0
```

이후 실제 대화 1턴으로 seq 11·12 저장 확인. `scan=allow` 기록됨.

**측정 사실 (2026-09-21)**

| 사실 | 근거 |
|---|---|
| Google 필터가 느슨함 | 41개 중 `lyria-*`(음악) `gemma-*` `*-image` `*-tts` `gemini-3.5-transcribe` `deep-research-*` `antigravity-*` `gemini-robotics-*` `*-computer-use-*` 포함. 이미지·TTS 모델도 `generateContent` 를 지원한다고 응답하므로 그 필터로는 갈라지지 않음 |
| Gemini TTS 는 텍스트 턴 불가 | `The requested combination of response modalities (TEXT) is not supported by the model.` |
| C++ 스키마의 name 필수 여부 | site ✅ / device ❌ / api credential ✅ / endpoint ✅ / connector ❌ |
| `mgmtd` 는 아웃바운드 HTTP 클라이언트를 사용하지 않음 | `requestAsync`/`requestSync` 호출자는 collectord(5개 컨트롤러)와 authd(`OktaClient.cpp` 2곳)뿐 |
| `mgmtd` 에 `io_context` 없음 | 전체 0건. `HttpServer` 가 private 멤버로 하나 보유하나 접근자 없음 |

**가정한 것** — collectord 의 세 벤더 파서를 Gemini·Anthropic 실제 응답으로 처음 돌린 것은
이번 세션이며 성공했으나, 오류 경로(4xx/5xx, 페이지네이션 `has_more`)는 미검증.

## TODO

| 항목 | 이유 / 대상 파일 |
|---|---|
| Google 파서 필터 강화 | `generateContent` 만으로는 텍스트 턴 가능 여부를 가릴 수 없음. 이름 기반 제외 목록이 확실하나, 응답의 다른 필드(`outputTokenLimit` 등)에 더 나은 근거가 있는지 먼저 확인 필요. `collectord/service/api/controller/AiModelController.cpp` `parseGoogle()` |
| `validDevice` / `validApiConnector` 에 name 검사 | 콘솔은 요구하는데 C++ 스키마는 통과시킴. 콘솔 외 경로(API 직접 호출, 설정 Import)로 이름 없는 장비·커넥터가 들어올 수 있음. `mgmtd/service/web/controller/SettingsValidation.cpp` |
| `benchtest-card.js` 모달 2곳 | `open()` 객체 인자 호출. Import 거부 창과 Export 선택 창이 `[object Object]` / `undefined` 로 뜸. 154행·173행 |
| `IpcCmd::ChatTurnStore = 141` | 같은 파일 주석이 "141–144 retired … numbers are left unused rather than reassigned" 라고 선언한 번호를 재사용. 현재 양 데몬이 같은 헤더를 쓰므로 동작에는 무해. `shared/ipc/IpcProtocol.h` |
| `endpoints.js` 파라미터 행 placeholder | `name` / `value` 두 개가 라벨 없는 반복 행의 사실상 컬럼 헤더. 제거하려면 위에 헤더를 달아야 함 |
| `ai_provider_model.released_at` | OpenAI `created`(unix), Anthropic `created_at`(RFC3339), Google 없음. 최신 모델 식별용. 미착수 |
| 미커밋 | `pretzel` 워킹트리 30+ 파일. `topology.*` `main.js` `main.css` 일부는 이 세션 이전 변경 |
