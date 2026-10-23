# Advisory 수집 설계 — PAN 보안 어드바이저리 → `advisory`

2026-10-22 작성. 대상: `collectord` 공개(public) 엔드포인트 경로 + `advisory` 스키마.
구현 전 설계 문서이며, 아래 "측정" 표기가 붙은 값은 모두 실제로 호출해 확인한 것입니다.

---

## 0. 한 줄 요약

`security.paloaltonetworks.com`의 RSS를 collectord가 주기 폴링해 변경된 CVE만 `/json/<CVE>`로
상세 수집하고, engined가 `advisory` / `advisory_product`에 upsert 한다.
크리덴셜이 없는 공개 HTTPS 호출이라 **기존 connector 수집 파이프라인은 그대로 쓸 수 없다** — 전용
`AdvisoryService`를 두고, 엔드포인트 객체만 `ApiVendor::Public`으로 재사용한다.

---

## 1. 소스 결정

### 1.1 측정값

| URL | 결과 | 비고 |
|---|---|---|
| `GET /rss.xml` | 200, **8,338 B**, item 25개 | `pubDate` = 포털의 `updated`와 동일 (측정) |
| `GET /json/CVE-2026-109618` | 200, **15,410 B** | CVE JSON 5.0 레코드 |
| `GET /json/CVE-2026-0322` | 200, **46,886 B** | 영향 제품이 많은 어드바이저리 |
| `GET /json` | 200, **231,473 B** — **25건(기본값, 전체가 아님)** | 인덱스. `?limit` `?page` `?product` `?severity` 동작 (`offset`은 무시) |
| `GET /json?limit=100&page=1..6` | **총 587건** (6회 요청, 페이지당 681 KB) | 전체 이력. 최신순, 가장 오래된 것이 `CVE-2012-6602` |
| ID 구성 (587건) | `CVE-*` **478** + `PAN-SA-*` **109** | PAN 자체 공지 ID가 19%. `/json/PAN-SA-2025-0018` → 200, 5,740 B |
| `?limit=200` / `?limit=500` | 200 (1,131,529 B) / **HTTP 500** | 상한이 200~500 사이. limit=200은 IPC 프레임 상한(1 MB)도 넘는다 |
| `GET /.well-known/csaf/provider-metadata.json`, `/csaf/index.txt` | 404 | **CSAF 미제공** |
| `GET /api/v1/advisories`, `/CVE-xxx.json` | 404 | 그 외 공개 API 없음 |
| `robots.txt` | `Disallow: /json`, `/json/*` | `rss.xml`은 대상 아님 |
| User-Agent 없이 호출 | 200 | UA 요구 없음 (우리 클라이언트는 `pretzel`을 보냄) |
| `If-None-Match: <그 ETag>` | **200** (304 아님) | ETag는 주지만 조건부 GET을 지키지 않음 → 조건부 GET 설계 폐기 |
| `Cache-Control` | rss `max-age=120`, detail `max-age=300` | 폴링 하한의 근거 |

### 1.2 cve.org / NVD는 대체가 되지 않는다

공식 경로(`cveawg.mitre.org/api/cve/<ID>`)는 **PAN이 CNA인 CVE에만** 동일한 데이터를 준다.

- `CVE-2026-0322` → `assignerShortName: palo_alto`, `affected[].product = "PAN-OS"`,
  `versions[] = {version:"12.1.0", lessThan:"12.1.10-h2", changes:[{at:"12.1.10-h2", status:"unaffected"}]}`.
  포털과 **동일**. (측정)
- `CVE-2026-109618` → 같은 ID인데 **PHP Group의 레코드**가 온다. PAN은 제3자 CVE의 자사 제품
  영향도를 같은 ID로 공지하는데, 그 "PAN 제품 13종 × affected/fixed" 데이터는 **포털에만 있다**. (측정)

NVD(`sourceIdentifier=psirt@paloaltonetworks.com`, 총 498건 — 측정)도 같은 한계에 반영 지연이 더해진다.

여기에 더 결정적인 것: 포털 587건 중 **109건이 `PAN-SA-*`**로, CVE ID가 아니다(예:
`PAN-SA-2025-0018` — Prisma Browser의 월간 Chromium 취약점 묶음 공지). 상세 레코드는 CVE 5.0
형식 그대로 오고 `cveMetadata.cveId` 자리에 `PAN-SA-2025-0018`이 들어 있다. **cve.org도 NVD도
이 19%는 아예 가지고 있지 않다** — CVE가 아니니까.

**결론**: 포털 `/json/<ID>`가 진실의 원천(superset: `cpeApplicability`, `configurations`,
`exploits`, `timeline`, `solutions` 포함). cve.org는 `robots.txt`가 깨끗한 대체 소스이지만
`assignerShortName == "palo_alto"`인 레코드에서만 등가다. 설정으로 소스를 고를 수 있게 하되
기본값은 포털, 레코드에 어느 소스에서 왔는지 남긴다(`advisory.source`).

`robots.txt`는 크롤러 대상 규약이고 우리는 6시간에 수십 건을 읽는 설정 기반 폴러이지만, 공식 공개
API가 아니라는 신호이므로 — 예고 없이 바뀔 수 있다 — (a) 간격 하한을 두고, (b) 사이클당 상세 조회
수를 제한하고, (c) 소스 전환 스위치를 설정에 남긴다.

### 1.3 인덱스는 RSS로 간다

`/json`(231 KB)은 `kMaxSampleBody`(64 KB, `collectord/service/api/ApiUtil.h:104`)를 넘어 잘린다.
잘린 본문은 "작은 본문"이 아니라 파싱 불가능한 본문이다(그 상수의 주석이 말하는 그대로).
RSS는 8 KB로 여유가 있고, `pubDate`가 포털의 `updated`와 일치해 **변경 감지에 필요한 전부**를 준다.

주의: RSS와 `/json` 기본 응답은 둘 다 **최신 25건**이다. 전체 587건은 `?limit=100&page=1..6`으로만
얻어지고, 이것은 정상 운영 폴링이 아니라 일회성 백필의 모양이다(§6.1) — 정상 폴링은 RSS 25건으로
충분하다. 새 어드바이저리가 한 사이클(6h)에 25건 넘게 올라오는 일은 없다
(587건 / 2012년부터 = 연 40여 건).

---

## 2. 왜 기존 connector 파이프라인에 그대로 못 넣는가

4개 모두 코드에 박힌 전제다.

1. **발급 키 게이트** — `collectord/service/api/controller/ConnectorController.cpp:192`
   `issuedKey()`가 비면 폴을 무한 연기한다. 크리덴셜 없는 엔드포인트는 **영원히 호출되지 않는다**.
2. **커넥터는 인벤토리 객체를 요구** — `ApiService.cpp:240` `oid`/`object`/`auth_profile` 중 하나라도
   비면 커넥터를 버린다. 어드바이저리 피드는 장비도 크리덴셜도 없다.
   (설정 레벨 참조 검증 `shared/config/ApiRefs.cpp:70`은 `auth_profile`이 비어 있으면 통과시키므로,
   막는 것은 로더뿐이다.)
3. **엔드포인트 경로는 정적** — `ApiEndpoint`는 "path + params가 곧 하나의 호출"이라는 불변식을
   헤더 주석에 명시한다. CVE별 `/json/<ID>` 팬아웃은 런타임에 만들어지는 URL이고, 템플릿 필드를
   넣는 것은 그 불변식을 깨는 일이다. CVE 하나당 엔드포인트 객체를 만드는 것은 더 나쁘다.
4. **샘플 캡** — 상세 레코드는 측정 46.9 KB까지 나왔다. 64 KB 캡 아래지만 여유가 크지 않고,
   `api_collection`은 "스트림(커넥터+엔드포인트) 시계열"이 계약인 테이블이다. CVE 하나하나가
   스트림이 되면 `api_collection_stream_time` 인덱스와 `kBodiesPerStream` 보존 정책이 무의미해진다.

### 설계 결정 A — 전용 `AdvisoryService`

`collectord/service/advisory/`에 서비스를 하나 둔다. `api_collection`을 거치지 않는다.
선례가 있다: SASE 헬스 프로브(`StatusController`)도 수집 스트림 밖에서 자기 호출을 소유한다.

### 설계 결정 B — `ApiVendor::Public`은 그래도 추가한다

엔드포인트 객체(host / path / params / headers / TLS 정책)는 설정에서 선언·검증되는 1급 객체로
남겨야 한다. 피드 URL을 서비스 코드에 박으면 운영자가 소스를 바꿀 수 없다.

```cpp
enum class ApiVendor
{
    Ngfw,
    Sase,
    // 크리덴셜이 없는 공개 HTTPS. 장비도 테넌트도 아니고, 붙일 키가 없다 — host는
    // 엔드포인트가 직접 들고(SASE와 같은 이유), CA 체인으로 검증한다(핀 고정할 상대가 아님).
    Public,
};
```

- `ApiService::loadEndpoints` — `device_type`(레거시 키) / `vendor`가 `"public"`이면 `Public`.
  subtype은 `Public`에서 의미가 없으므로 읽지 않고 거부하지도 않는다.
- `ApiVendor::Public` 엔드포인트는 **커넥터가 참조할 수 없다**(§2-1·2의 전제를 건드리지 않기 위해).
  로더에서 거부하고 경고한다. 공개 피드를 `api_collection` 스트림으로도 돌리고 싶어지면 그때
  커넥터 쪽 완화를 별도로 하면 된다(§6, 선택).
- 호출 빌드는 `collectSaseOnce`에서 `Authorization` 헤더만 뺀 것과 동일하다.
  `req.host = ep.host; req.port = 443; req.verifyCa = true;`
  **`verifyCa = true`는 필수다** — `shared/http/HttpClientSession.cpp:212`에서
  `expectedFingerprint`가 비고 `verifyCa`가 false면 "첫 접촉" 경로로 들어가 요청을 보내지 않는다.

---

## 3. 수집 흐름

```
AdvisoryService (collectord)                       engined
  RunPeriodic (기본 6h)
    │
    ├─ 1단계: index endpoint GET /rss.xml  (8 KB, 최신 25건)
    │     파싱: <item>의 guid/link → 어드바이저리 ID, pubDate, title(severity 포함)
    │     ─────────────────────────────────── AdvisoryFeedState(156) ──▶ advisory_feed upsert
    │
    └─ 2단계: pubDate가 m_lastSeen[id]보다 새 것만
          GET /json/<ID>             (사이클당 최대 kMaxDetailPerCycle건)
          ─────────────────────────────────── AdvisoryRecord(155) ─────▶ advisory upsert
                                                                        advisory_product 재투영
```

- **"새 것" 판정은 collectord가 메모리에서 한다** (`m_lastSeen: advisory_id → pubDate`). engined에
  물어보는 왕복(`requestKeys` 같은)을 만들지 않는 쪽을 택했다: 재시작 시 한 번 25건을 다시 읽는
  비용이 IPC 왕복 하나를 영구히 유지하는 비용보다 싸고, engined의 upsert가 멱등이라 중복은 무해하다.
- 팬아웃은 사이클당 `kMaxDetailPerCycle`(기본 10)로 끊고 나머지는 다음 사이클로 넘긴다. 25건이
  동시에 갱신되는 일은 사실상 없지만, 상한 없는 팬아웃을 벤더 사이트에 쏘지 않는다.
- HTTP 클라이언트는 리다이렉트를 따라가지 않는다. 벤더가 호스트를 바꾸면 301이 `!ok`로 기록되고
  `advisory_feed.error`에 남는다 — 조용히 멈추는 것보다 낫다.

### 변경 지점

| 파일 | 변경 |
|---|---|
| `collectord/service/api/ApiService.h` | `ApiVendor::Public` 추가 |
| `collectord/service/api/ApiService.cpp` | `loadEndpoints` public 분기 / 커넥터 참조 거부 |
| `collectord/service/advisory/AdvisoryService.{h,cpp}` | 신규. 스케줄 + 2단계 수집 + `m_lastSeen` |
| `collectord/service/advisory/AdvisoryEvent.{h,cpp}` | 신규. `Setup` / `RunPeriodic` |
| `collectord/service/advisory/RssIndex.{h,cpp}` | 신규. RSS → `{advisory_id, pub_date, title}` 순수 함수 (단위 테스트 대상) |
| `collectord/service/CollectordServiceManager.{h,cpp}` | 서비스 등록 + `schedule()` 틱 |
| `collectord/CMakeLists.txt` | 소스 추가 |
| `shared/ipc/IpcProtocol.h` | `AdvisoryRecord = 155`, `AdvisoryFeedState = 156` (155 이상은 미사용 — 확인) |
| `shared/ipc/IpcProtocol.cpp` | `cmdToStr` + `classify()` → 둘 다 **Write**(= engined만 수신 가능) |
| `engined/event/EnginedEventFactory.cpp` | cmd → `AdvisoryEvent` |
| `engined/service/advisory/AdvisoryService.{h,cpp}` | 신규. 페이로드 검증 + upsert |
| `engined/service/advisory/AdvisoryProjection.{h,cpp}` | 신규. CVE 5.0 `affected[]` → `advisory_product` |
| `shared/db/Database.cpp` | DDL (§4) |
| `config/startup-config.json` | `collectord.service.advisory` 기본값 |

> `shared/db/schema.sql`은 어디에서도 실행되지 않고(grep 0건) 이미 뒤처져 있다
> (`pb_user_request` 없음, `api_collection.body_aged` 없음). 권위는 `Database.cpp`다.
> 문서용으로 같이 갱신할지는 선택.

### 설정 모양

```json
"advisory": {
  "enabled": false,
  "source": "pan_portal",
  "index_endpoint": "<endpoint oid — ApiVendor::Public>",
  "detail_endpoint": "<endpoint oid — path는 /json, CVE ID가 뒤에 붙는다>",
  "poll_interval_sec": 21600,
  "max_detail_per_cycle": 10
}
```

`detail_endpoint`의 `path`는 `/json`이고 서비스가 `/<CVE-ID>`를 붙인다. 엔드포인트 객체에
템플릿 문법을 넣지 않기 위한 선택이며, 이 접미사 규칙이 `source`가 존재하는 이유다:
`cve_org`는 host `cveawg.mitre.org`, path `/api/cve` + 같은 규칙으로 동일한 CVE 5.0을 돌려준다.

---

## 4. 스키마

`shared/db/Database.cpp`에 추가. 집 스타일(이유를 주석에, `IF NOT EXISTS`, 멱등 `ALTER`)을 따른다.

```sql
-- 벤더 보안 어드바이저리. 순수 state(시스템 산출, 운영자 선언 아님)이므로 running_config가 아니라
-- 여기 있고, 쓰는 것은 collectord의 IPC를 받는 engined뿐이다 — api_collection과 같은 분리.
--
-- api_collection의 body를 그때그때 읽지 않고 테이블을 두는 이유는 pb_user_request와 똑같다:
-- 샘플은 캡이 있고(64 KB), 보존 창을 지나면 payload가 해제되고(body_aged), 마지막 폴이 우연히
-- 담았던 것만 들고 있다. "우리 장비에 해당되는 미해결 어드바이저리"는 피드에서 이미 밀려난
-- 어드바이저리까지 봐야 답할 수 있으므로, 어드바이저리 자체는 어떤 샘플보다 오래 살아야 한다.
--
-- 키는 어드바이저리 ID. CVE ID가 아니라는 점이 중요하다: 587건 중 109건이 PAN-SA-*이고(측정),
-- 상세 레코드는 그 값을 cveMetadata.cveId 자리에 그대로 담아 보낸다. cve_id라고 이름 붙이면 19%는
-- 이름이 거짓말을 하는 행이 된다. 같은 어드바이저리가 수정 공지로 다시 올라오면 열 개의 비슷한
-- 행이 아니라 한 행이 바뀐다.
-- 컬럼은 질의·정렬하는 것만 승격하고 나머지는 record_json에 둔다 — 벤더가 필드를 더하는 일은
-- 정기적으로 일어나고, 기대한 필드를 이름 붙여 둔 스키마는 그때마다 마이그레이션을 요구한다.
--
--   source            이 레코드를 가져온 곳. 'pan_portal' | 'cve_org'. 등가가 아니기 때문에 남긴다:
--                     cve_org는 PAN이 CNA인 CVE에만 PAN 제품 영향도를 담는다 (제3자 CVE 영향 공지는
--                     같은 ID로 상위 CNA의 레코드를 돌려준다 — CVE-2026-109618, 2026-10-22 측정).
--   vendor_updated_at 포털의 updated(= RSS pubDate). 다시 가져올지 판단하는 유일한 근거이며,
--                     우리가 쓴 시각(updated_at)과 섞이면 안 된다.
--   record_json       받은 CVE 5.0 레코드 전체. 이 테이블의 진실이고, advisory_product는 파생이다.
CREATE TABLE IF NOT EXISTS advisory (
    advisory_id       TEXT        PRIMARY KEY,   -- 'CVE-2026-0322' | 'PAN-SA-2025-0018'
    source            TEXT        NOT NULL,
    assigner          TEXT,                  -- cveMetadata.assignerShortName ('palo_alto' 등)
    title             TEXT,
    severity          TEXT,                  -- CRITICAL | HIGH | MEDIUM | LOW | NONE
    base_score        NUMERIC(3,1),
    cvss_vector       TEXT,
    date_public       TIMESTAMPTZ,
    vendor_updated_at TIMESTAMPTZ,
    has_exploit       BOOLEAN     NOT NULL DEFAULT false,   -- containers.cna.exploits 존재
    record_json       JSONB       NOT NULL,
    first_seen_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at        TIMESTAMPTZ NOT NULL DEFAULT now()
);
-- 콘솔의 기본 읽기: 최신순 목록.
CREATE INDEX IF NOT EXISTS advisory_date ON advisory (date_public DESC);
-- "지금 조치해야 하는 것"만 보는 읽기. 부분 인덱스인 이유는 pb_user_request_sweep과 같다 —
-- 나머지는 이력이고, 건너뛰는 비용을 낼 이유가 없다.
CREATE INDEX IF NOT EXISTS advisory_actionable
    ON advisory (date_public DESC) WHERE severity IN ('CRITICAL', 'HIGH');

-- affected[] × versions[]를 평평하게 펼친 투영. advisory.record_json이 원본이고 이것은 파생이므로,
-- 레코드를 다시 받으면 해당 cve_id의 행을 통째로 지우고 다시 쓴다(부분 갱신하지 않는다).
--
-- 왜 펼치는가: 벤더의 affected는 제품 13개 × 버전 범위 n개의 중첩 배열이고, 우리가 물을 것은
-- "PAN-OS 11.1.6을 쓰는 장비에 해당되는 어드바이저리"라는 한 줄 질의다. JSONB 안에서 그걸 하면
-- 매 조회가 전체 테이블 전개가 된다.
--
--   status       affected | unaffected | unknown (벤더 표기 그대로)
--   version_from versions[].version — 범위의 시작
--   less_than    versions[].lessThan / lessThanOrEqual — 끝. 여기서부터 안전해지는 지점이기도 하다
--                (PAN은 changes[{at, status:'unaffected'}]로 같은 값을 다시 말한다)
--   le           less_than이 lessThanOrEqual이었는지. 경계 한 칸 차이로 오탐/누락이 갈린다
--   version_raw  이 행을 만든 versions[] 원소 그대로. 비교기가 못 읽는 표기를 나중에 복구하기 위해
CREATE TABLE IF NOT EXISTS advisory_product (
    oid          BIGSERIAL PRIMARY KEY,
    advisory_id  TEXT      NOT NULL REFERENCES advisory(advisory_id) ON DELETE CASCADE,
    vendor       TEXT,
    product      TEXT      NOT NULL,         -- 'PAN-OS', 'Prisma Access', 'GlobalProtect App' …
    status       TEXT      NOT NULL,
    version_from TEXT,
    less_than    TEXT,
    le           BOOLEAN   NOT NULL DEFAULT false,
    version_raw  JSONB
);
-- 장비 쪽에서 들어오는 조회: 이 제품의, 영향 있다고 말한 행만.
CREATE INDEX IF NOT EXISTS advisory_product_lookup
    ON advisory_product (product, advisory_id) WHERE status = 'affected';
-- 재투영(delete-then-insert)과 상세 화면의 조회.
CREATE INDEX IF NOT EXISTS advisory_product_parent ON advisory_product (advisory_id);

-- 피드 자체의 건강 상태. 한 소스당 한 행이고, 시계열이 아니다(마지막 결과만 의미가 있다).
-- api_collection에 샘플로 남기지 않기로 했기 때문에(설계 결정 A) "언제 마지막으로 성공했나"를
-- 답할 곳이 따로 필요하다. 조용히 멈춘 수집이 가장 비싼 실패 모드다.
CREATE TABLE IF NOT EXISTS advisory_feed (
    source       TEXT        PRIMARY KEY,
    last_poll_at TIMESTAMPTZ,
    last_ok_at   TIMESTAMPTZ,
    http_status  INT,
    latency_ms   INT,
    item_count   INT,
    error        TEXT
);
```

**보존 정책은 없다.** 의도된 결정이다. `api_collection`은 폴 하나당 한 행이 쌓이는 시계열이라
바닥(floor)이 필요했지만, 어드바이저리는 몇 년을 모아도 수천 행이고 "그때 우리가 노출돼 있었나"를
답하는 기록이다. 지워야 할 것은 벤더가 철회한 어드바이저리뿐이고, 그건 보존 기간의 문제가 아니다.

---

## 5. 장비 버전 매칭 (2단계 — 지금은 막혀 있음)

어드바이저리의 가치는 "우리 장비에 해당되나"이고, 그건 지금 **답할 수 없다**:
`sw_version` / `sw-version` / `software_version` — 트리 전체에 **0건**(측정). 장비는
`site.ngfw_devices[]` 설정 배열에 있고 `target` / `fingerprint`만 들고 있다
(`ConnectorController.cpp:86` `resolveDevice`).

선택지:

- **(a) 운영자가 선언** — `ngfw_devices[].sw_version`. 싸지만 사람이 업그레이드 후 갱신해야 하고,
  틀린 값은 오탐·누락으로 직결된다.
- **(b) 수집한다 (권장)** — PAN-OS XML API `type=op&cmd=<show><system><info/></system></show>`를
  `ApiSubtype::Xml` 엔드포인트로 커넥터에 붙이고, `PbUserRequestProjection`과 같은 방식으로
  `sw-version`을 뽑아 투영한다. **이미 있는 파이프라인 그대로**이고 설정 외에 새 전송 경로가 없다.
  장비 상태 테이블(또는 `ngfw_device_facts`)이 하나 필요하다.

**비교기는 새로 써야 한다.** PAN-OS 버전은 semver가 아니다 — 측정된 실제 값이
`12.1.10-h2`, `12.2.4`, `11.1.10-h2`이고 벤더도 `versionType: "custom"`이라고 말한다.
`major.minor.maint[-hN]` 4-튜플로 파싱해 사전식 비교하고, 파싱 실패는 "매칭 불가"로 기록한다
(조용히 "해당 없음"으로 떨어뜨리면 안 되는 쪽이다). 순수 함수로 두고 단위 테스트한다 —
`tests/`에 `test_panos_version.cpp`.

매칭 결과를 테이블로 둘지(`advisory_match`)는 2단계에서 결정한다. 질의로 충분하면 두지 않는다.

---

## 6. 단계

| 단계 | 내용 | 산출 |
|---|---|---|
| **1** | §4 스키마 + `ApiVendor::Public` + collectord `AdvisoryService` + IPC 155/156 + engined 수신/투영 | `advisory` 테이블이 채워진다. UI 없음, psql로 확인 |
| **1.5** | 일회성 백필 (§6.1) — 587건 전체 | 2012년부터의 이력이 들어온다 |
| **2** | 장비 버전 소스(§5 (b)) + PAN-OS 비교기 + 매칭 질의 | "이 장비에 해당되는 어드바이저리" 답 가능 |
| **3** | mgmtd `AdvisoryController` + Insight ▸ Advisory 페이지 | 콘솔 |

### 6.1 백필 — 1.5단계로 범위 안에 넣는다

처음에는 범위 밖으로 뒀는데, 측정해 보니 전체가 **587건**이고 `?limit=100&page=1..6` **6회
요청**으로 전부 열거된다. 2012년부터의 이력 전체이고, "이 장비가 지금까지 노출된 적 있나"는
어드바이저리 테이블의 존재 이유 중 하나다. 안 할 이유가 없다.

다만 정상 폴링 경로를 재사용하지 **않는다**:

- 인덱스 페이지는 **681 KB**로 `kMaxSampleBody`(64 KB)뿐 아니라 `IPC_MAX_FRAME_SIZE`(1 MB)에도
  위험하게 가깝다. 그래서 인덱스는 **IPC로 보내지 않는다** — collectord가 자기 안에서 파싱해
  ID 목록만 얻고, 레코드는 건당(≤ 47 KB) `AdvisoryRecord`로 보낸다.
- 인덱스 엔트리는 상세 레코드와 **다른 형태**다(`product[]`/`affected[]` 평행 배열에 콤마로 이어
  붙인 문자열). `record_json`에 그대로 넣을 수 없으므로 백필도 ID당 `/json/<ID>` 상세를 받는다.
- 587회 요청을 정상 사이클(10건/6h)에 태우면 15일이 걸린다. 운영자가 트리거하는 일회성 모드로
  분리하고 자체 레이트리밋(예: 초당 1건 → 약 10분)을 둔다. `advisory_feed`에 진행 상황을 남긴다.

범위 밖(의도적):

- 공개 엔드포인트를 `api_collection` 스트림으로 돌리기 — 커넥터의 object/auth 전제를 완화해야 하고,
  어드바이저리에는 필요하지 않다.
- 알림(메일/웹훅). 1단계가 끝나면 `advisory_actionable` 인덱스 위에서 자연스럽게 얹힌다.

---

## 7. 열린 질문

1. **`detail_endpoint` 접미사 규칙** — 엔드포인트 객체에 템플릿을 넣지 않기 위해 서비스가
   `/<CVE-ID>`를 붙이는 안이다. 이게 "엔드포인트는 완결된 호출"이라는 불변식을 다른 방식으로
   흐리는 것은 아닌지. 대안은 `AdvisoryService`가 host/path를 설정에서 직접 읽는 것(엔드포인트
   객체를 아예 안 쓰는 것) — 더 솔직하지만 엔드포인트 테스트 UI를 잃는다.
2. **소스 기본값** — 포털(`robots.txt` Disallow, 데이터는 superset) vs cve.org(공식). 측정 결과
   cve.org는 PAN이 CNA인 CVE에만 등가이고 `PAN-SA-*` 109건은 아예 없으므로, 전환 스위치는
   **동등한 대안이 아니라 포털이 막혔을 때의 열화 모드**다. 그렇게 문서에 못박을지, 아니면
   `source`별로 무엇이 빠지는지 UI에 노출할지.
3. **`advisory_feed`를 `api_collection`으로 대체할지** — 설계 결정 A를 뒤집고 커넥터 전제를
   완화하면 Insight ▸ API Collection의 health/cadence UI가 공짜로 따라온다. 지금은 안 하는 쪽.
