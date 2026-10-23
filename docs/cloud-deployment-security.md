# Pretzel 클라우드 공개 전 보안 점검 + GCP 구축 가이드

점검일: 2026-10-19 / 대상: `main` @ 74415c5 + 워킹트리

---

## 0. 한 줄 요약

**"진짜 심각한 크리덴셜 노출"은 없습니다.** 실제 API 키·비밀번호가 소스나 git 이력에 박혀 있는 건 0건이고,
암호화·세션·SQL·인증 게이트 같은 핵심 설계는 오히려 평균 이상입니다.

문제는 **"어플라이언스로 설계된 기본값을 그대로 인터넷에 올리면 즉사"** 하는 쪽입니다.
`admin/admin`, `pretzel/pretzel`, `padmin` 세 쌍의 개발용 기본 자격증명과,
`0.0.0.0`에 그대로 뜨는 pgAdmin·평문 HTTP API 리스너가 그것입니다.

| | 항목 | 수 |
|---|---|---|
| 🔴 P0 | 공개 전 반드시 고쳐야 함 | 4 |
| 🟠 P1 | 공개와 동시에 고쳐야 함 | 4 |
| 🟡 P2 | 공개 후 정리 | 5 |

---

## 1. 점검 결과 — 잘 되어 있는 것

이건 먼저 짚고 갑니다. 아래는 **고칠 필요 없음**:

| 영역 | 확인 내용 |
|---|---|
| **비밀 관리** | `.gitignore`가 `cert/*.key`, `secret/*.key`, `.env`, `startup-config.lab.json`을 정확히 차단. 커밋된 `startup-config.json`은 전부 플레이스홀더 |
| **자격증명 봉인** | AES-256-GCM, `base64(nonce‖tag‖ciphertext)`. 마스터키는 설치별 생성·root 전용 ([shared/util/Secret.h](../shared/util/Secret.h)). DB 사본만으로는 무용지물 |
| **세션 ID** | CSPRNG 256비트. 과거 `mt19937_64`(실효 32비트) 취약점을 이미 수정한 흔적이 주석에 남아 있음 ([mgmtd/service/auth/AuthService.cpp:304](../mgmtd/service/auth/AuthService.cpp#L304)) |
| **비밀번호 해시** | PBKDF2-HMAC-SHA256 + per-user salt, `pbkdf2$<iters>$<hex>` 포맷. 로그인 시 레거시 해시 자동 업그레이드 |
| **SQL 인젝션** | 전 데몬 파라미터 바인딩(`$1`). 동적으로 붙는 건 **내부 상수 테이블명뿐**이고 사용자 입력 경로 없음 — 오탐 아님, 확인함 |
| **경로 탈출** | 정적 파일은 사전 빌드된 캐시 맵 조회 방식(`StaticFileCache`). 파일시스템을 치지 않으므로 `../` 무력 ([WebService.cpp:541](../mgmtd/service/web/WebService.cpp#L541)) |
| **인증 게이트** | 라우팅 테이블 한 곳에 `Access::Authenticated` 집중, 정적 페이지도 미인증이면 302 → `/index.html`. Public은 login/logout/health/metric/SSO 5종뿐 |
| **아웃바운드 TLS** | `verifyCa` 또는 **인증서 지문 핀닝**. `verify_none`일 때도 핸드셰이크 직후 지문 검증 후에만 바디 전송 ([HttpClientSession.cpp:118](../shared/http/HttpClientSession.cpp#L118)) |
| **SSO** | SAML 서명 검증(`SamlClient::verifySignature`) + OIDC RS256 JWT 서명 검증(`OktaClient::verifySignatureRs256`) 모두 실제 구현됨 |
| **로그인 쓰로틀** | 연속 실패 시 백오프 + "어느 쪽이 틀렸는지" 비노출 |
| **하드코딩 스캔** | 실 API 키/토큰/내부 IP/사내 도메인 — **0건**. 검출된 IP는 전부 테스트 픽스처, 도메인은 전부 Palo Alto 공개 엔드포인트 |

---

## 2. 🔴 P0 — 공개 전 반드시

### P0-1. 소스에 박힌 개발용 기본 자격증명 3쌍

```
admin / admin            shared/config/Config.cpp:20-21   (웹 콘솔 관리자)
pretzel / pretzel        script/utils.py:123              (PostgreSQL 롤)
admin@pretzel.io / padmin  script/utils.py:100-101        (pgAdmin 관리자)
```

웹 콘솔 쪽은 `must_change=true`로 시드되지만([Config.cpp:428](../shared/config/Config.cpp#L428)),
**"첫 로그인 전까지 admin/admin이 유효하다"** 는 사실은 변하지 않습니다. 인터넷에 올린 직후
스캐너가 먼저 닿으면 그걸로 끝입니다. DB·pgAdmin 쪽은 `must_change`조차 없습니다.

**조치** — 환경변수 미설정 시 기본값으로 떨어지지 말고 **거부**:

```python
# script/utils.py — 현재
PG_DB_PASSWORD = os.environ.get("PZ_PG_PASSWORD", "pretzel")

# 이렇게
def _require(name):
    v = os.environ.get(name)
    if not v and os.environ.get("PZ_PROFILE") == "prod":
        sys.exit(f"[FATAL] {name} must be set when PZ_PROFILE=prod")
    return v or "pretzel"   # dev 기본값은 유지

PG_DB_PASSWORD = _require("PZ_PG_PASSWORD")
```

웹 콘솔 admin은 `PZ_SEED_ADMIN_PASSWORD` 환경변수를 읽어 시드하고, 미설정 시에는
**랜덤 32자를 생성해 `/var/lib/pretzel/initial-admin-password`(0600)에 쓰고 로그에는 경로만 남기는** 방식을 권장합니다.
클라우드라면 VM 기동 스크립트에서 Secret Manager로부터 주입하는 게 더 낫습니다(§5 참조).

### P0-2. pgAdmin이 `0.0.0.0:5050`으로 열림

```python
# config/pgadmin/config_local.py:22-23
PZ_PGADMIN_LISTEN_ADDRESS = "0.0.0.0"
PZ_PGADMIN_LISTEN_PORT = 5050
```

DB 전체를 쥐는 관리 콘솔이 모든 인터페이스에 붙습니다. 자격증명이 `padmin`인 것과 겹치면 **치명적**입니다.

**조치**: `127.0.0.1`로 변경. 접근은 IAP TCP 터널로(§6.4). GCP VPC 방화벽으로 막는 것만으로도 1차 방어는 되지만,
바인드 주소를 고치는 게 근본 조치입니다.

### P0-3. 북바운드 API가 평문 HTTP로 `0.0.0.0:8443`

```json
// config/startup-config.json → pretzel.api
"listen_address": "0.0.0.0", "listen_port": 8443, "tls_enabled": false
```

apid가 여는 ingest 엔드포인트입니다. 인증은 Bearer 토큰으로 제대로 걸려 있고
("빈 토큰 = 전부 거부"까지 올바르게 처리, [IngestRouting.cpp:32](../apid/service/ingest/IngestRouting.cpp#L32)),
**그 토큰이 평문으로 흐릅니다.** 한 번 캡처되면 끝입니다.

**조치**: `tls_enabled: true` + 콘솔과 같은 인증서. 외부에서 ingest를 쓰지 않는다면 `listen_address`를 `127.0.0.1`로.

### P0-4. 전 데몬이 root 실행, systemd 샌드박싱 0

`script/service/*.service` 10개 전부 `User=` 없음 → root. 격리 지시자도 없습니다.
mgmtd의 HTTP 파서(Boost.Beast)에서 메모리 버그 하나만 나와도 바로 root RCE입니다.

**조치** — 모든 유닛에 추가:

```ini
[Service]
User=pretzel
Group=pretzel
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ProtectHome=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectControlGroups=yes
RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6
RestrictSUIDSGID=yes
MemoryDenyWriteExecute=yes
ReadWritePaths=/var/lib/pretzel /var/log/pretzel /run/pretzel
```

단, 두 가지 걸림돌이 있습니다:

- **probed**는 ICMP raw 소켓이 필요 → `AmbientCapabilities=CAP_NET_RAW` + `CapabilityBoundingSet=CAP_NET_RAW`
- **mgmtd**는 443 바인드가 필요 → `AmbientCapabilities=CAP_NET_BIND_SERVICE`
  (클라우드에서는 LB가 443을 받으므로 백엔드는 8443으로 내려 이 권한 자체를 없애는 쪽이 낫습니다)
- `credentials.key`와 `cert/server.key`의 소유자를 `pretzel:pretzel` 0600으로 변경 필요

---

## 3. 🟠 P1 — 공개와 동시에

### P1-1. 세션 쿠키에 `Secure` 플래그 없음

```cpp
// mgmtd/service/web/controller/AuthController.cpp:90
resp.setCookie = "session=" + result.sessionId + "; HttpOnly; Path=/; SameSite=Strict";
//                                     ↑ Secure 없음
```

같은 문제가 [SsoController.cpp:276](../mgmtd/service/web/controller/SsoController.cpp#L276)에도 있습니다.
`HttpOnly`/`SameSite=Strict`는 잘 들어가 있어서 XSS 탈취와 CSRF는 막힙니다 — 빠진 건 `Secure` 하나뿐.
HTTP로 내려가는 요청이 하나라도 섞이면 세션이 평문에 실립니다.

**조치**: 두 줄 모두 `; Secure` 추가. 로그아웃 쿠키(`AuthController.cpp:99`)도 동일.

### P1-2. 보안 응답 헤더 전무

[shared/http/HttpBeast.h:44-64](../shared/http/HttpBeast.h#L44)에서 응답을 조립하는데
`Content-Security-Policy`, `Strict-Transport-Security`, `X-Frame-Options`,
`X-Content-Type-Options`, `Referrer-Policy`가 **하나도 없습니다**.

프론트엔드가 `innerHTML`을 147곳에서 쓰고 있어서(대부분 `esc()` 헬퍼를 거치긴 합니다)
CSP는 실수 한 건에 대한 최후 방어선으로 값어치가 큽니다.

**조치** — `HttpBeast.h`의 공통 조립부에:

```cpp
res->set(http::field::strict_transport_security, "max-age=31536000; includeSubDomains");
res->set(http::field::x_frame_options, "DENY");
res->set("X-Content-Type-Options", "nosniff");
res->set(http::field::referrer_policy, "no-referrer");
res->set(http::field::content_security_policy,
         "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
         "img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'");
```

> CSP는 먼저 `Content-Security-Policy-Report-Only`로 켜서 콘솔 위반을 확인한 뒤 전환하세요.
> 인라인 `<script>`가 남아 있으면 `script-src 'self'`에서 화면이 깨집니다.

### P1-3. 서버 TLS가 1.0/1.1을 허용

```cpp
// shared/http/HttpServer.cpp:67-68 — 서버: SSLv2/v3만 차단
no_sslv2 | no_sslv3 | single_dh_use

// shared/http/HttpClientSession.cpp:93-94 — 클라이언트: 1.0/1.1까지 차단 (이쪽이 맞음)
no_sslv2 | no_sslv3 | no_tlsv1 | no_tlsv1_1
```

서버 쪽만 뒤쳐져 있습니다. **조치**: 서버에도 `| no_tlsv1 | no_tlsv1_1`를 추가하거나,
`SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION)`로 명시. (클라우드에서는 LB가 종단하므로
외부 노출 면에서는 덜 급하지만, apid 8443·pgAdmin처럼 직접 종단하는 리스너에는 그대로 적용됩니다.)

### P1-4. 로그인 쓰로틀이 username 단위 · 인메모리

[AuthService.cpp:159-190](../mgmtd/service/auth/AuthService.cpp#L159) — `m_throttles`는 username을 키로 하는
메모리 맵입니다. 결과적으로:

- **IP 단위 제한이 없음** → 계정명을 돌려가며 치는 분산 추측에 무력
- **재시작 시 초기화** → `Restart=always`와 결합하면 공격자가 크래시를 유도해 리셋 가능
- `kMaxThrottled` 초과 시 `m_throttles.begin()`을 지움 → 맵을 채워서 **자기 차단을 밀어내는** 회피 가능

**조치**: 애플리케이션 수정은 선택이고, 클라우드에서는 **Cloud Armor rate-limit으로 IP 단위 제한을 앞단에 거는 것이 훨씬 효과적**입니다(§7). 그쪽을 먼저 하세요.

---

## 4. 🟡 P2 — 공개 후 정리

| # | 내용 | 비고 |
|---|---|---|
| P2-1 | **git 이력에 자가서명 개인키** — `cert/key.pem`이 `feec196`에 추가되고 `7e5e553`에서 삭제됨. `CN=172.17.108.19`, 2026-12 만료 | **현재 `cert/server.crt`와 공개키가 다름을 확인했습니다 — 실사용 키 아님.** 레포를 공개할 계획이면 `git filter-repo`로 세탁, 사내 유지면 방치 가능 |
| P2-2 | ingest 토큰 비교가 `!=` (비상수시간) — [IngestRouting.cpp:32](../apid/service/ingest/IngestRouting.cpp#L32) | 네트워크 지터에 묻히는 수준. `CRYPTO_memcmp`로 바꾸면 깔끔 |
| P2-3 | `/health`가 미인증 — 데몬 이름·상태·레이턴시 노출 | 핑거프린팅용. Cloud Armor에서 외부 차단(§7-R5). LB 헬스체크는 Armor를 우회하므로 영향 없음 |
| P2-4 | SNMP community 기본값 `"public"` — `startup-config.json` | 스캔 대상이 사설망이면 영향 적음 |
| P2-5 | 비밀번호 정책 없음 — 빈 문자열만 거부 | [AuthController.cpp:113](../mgmtd/service/web/controller/AuthController.cpp#L113). 최소 12자 + 길이 상한(PBKDF2 DoS 방지) 권장 |

> `/metric`(Prometheus)도 Public이지만 내용은 `pretzel_mgmtd_up`/`uptime_seconds`/`http_requests_total` 3개뿐이라 무해합니다. 그래도 외부에는 닫는 게 맞습니다.

---

## 5. GCP 아키텍처

Pretzel은 **9개 데몬 + PostgreSQL + systemd로 묶인 상태 있는 어플라이언스**입니다.
Cloud Run / GKE로 쪼개는 건 지금 구조에서 비용 대비 이득이 없습니다.
**Compute Engine 단일 VM(MIG 1대) + 글로벌 외부 ALB + Cloud Armor**가 정답입니다.

```
                    Internet
                       │
              ┌────────▼─────────┐
              │   Cloud DNS      │  pretzel.example.com
              └────────┬─────────┘
                       │
         ┌─────────────▼──────────────┐
         │ Global External ALB (443)  │  Google-managed TLS cert
         │  ├ Cloud Armor policy      │  WAF · rate-limit · geo · IP allowlist
         │  └ (옵션) IAP              │  Google 계정 인증을 앞단에 추가
         └─────────────┬──────────────┘
                       │ HTTPS :8443        VPC 방화벽: 35.191.0.0/16,
                       │                    130.211.0.0/22 에서만 허용
         ┌─────────────▼──────────────┐
         │  MIG (size 1)              │   subnet: 10.10.0.0/24 (private)
         │  e2-standard-4 / Ubuntu 22 │   외부 IP 없음
         │  ├ mgmtd  :8443  ← LB      │
         │  ├ apid   :8443→127.0.0.1  │   (P0-3 조치 후)
         │  ├ pgAdmin:5050→127.0.0.1  │   (P0-2 조치 후)
         │  ├ engined / ipcd / probed │
         │  ├ collectord / topologyd  │
         │  └ PostgreSQL :5432 (local)│
         └──────┬──────────────┬──────┘
                │              │
         ┌──────▼─────┐  ┌─────▼────────┐
         │ Cloud NAT  │  │ Secret Mgr   │  PZ_PG_PASSWORD
         │ (egress)   │  │              │  PZ_PGADMIN_PASSWORD
         └──────┬─────┘  │              │  PZ_SEED_ADMIN_PASSWORD
                │        │              │  credentials.key
    *.paloaltonetworks.com              │  server.crt / server.key
    (NGFW/SASE/AIRS API)                └──────────────┘

   운영 접근: IAP TCP forwarding (35.235.240.0/20) → SSH :22, pgAdmin :5050
              공인 22번 포트 없음
```

**핵심 결정 3가지**

1. **VM에 외부 IP를 주지 않습니다.** 인바운드는 LB만, 아웃바운드는 Cloud NAT만. 공격면이 LB 하나로 줄어듭니다.
2. **SSH는 IAP TCP forwarding으로만.** 22번을 인터넷에 열지 않습니다. 브루트포스 로그가 아예 안 생깁니다.
3. **pgAdmin은 절대 노출하지 않습니다.** IAP 터널로 로컬 포트포워딩해서 씁니다.

---

## 6. 구축 절차

### 6.0 변수

```bash
export PROJECT_ID="pretzel-prod"
export REGION="asia-northeast3"       # 서울
export ZONE="asia-northeast3-a"
export DOMAIN="pretzel.example.com"
export ADMIN_CIDR="203.0.113.10/32"   # 운영자 사무실 고정 IP로 교체

gcloud config set project "$PROJECT_ID"
gcloud services enable compute.googleapis.com iap.googleapis.com \
  secretmanager.googleapis.com dns.googleapis.com logging.googleapis.com
```

### 6.1 VPC · 서브넷 · NAT

```bash
gcloud compute networks create pretzel-vpc --subnet-mode=custom

gcloud compute networks subnets create pretzel-subnet \
  --network=pretzel-vpc --region="$REGION" --range=10.10.0.0/24 \
  --enable-private-ip-google-access      # 외부 IP 없이 Google API 접근
```

```bash
gcloud compute routers create pretzel-router \
  --network=pretzel-vpc --region="$REGION"

gcloud compute routers nats create pretzel-nat \
  --router=pretzel-router --region="$REGION" \
  --nat-all-subnet-ip-ranges --auto-allocate-nat-external-ips \
  --enable-logging --log-filter=ERRORS_ONLY
```

### 6.2 VPC 방화벽 — 기본 거부부터

순서가 중요합니다. **먼저 전면 거부를 깔고 필요한 것만 엽니다.**

```bash
# 1) 전면 거부 (최저 우선순위)
gcloud compute firewall-rules create pz-deny-all-ingress \
  --network=pretzel-vpc --direction=INGRESS --action=DENY --rules=all \
  --source-ranges=0.0.0.0/0 --priority=65534 \
  --enable-logging
```

```bash
# 2) LB 및 헬스체크 → 백엔드 8443. 이 두 대역이 GCP LB/헬스체커의 고정 출처입니다.
gcloud compute firewall-rules create pz-allow-lb-hc \
  --network=pretzel-vpc --direction=INGRESS --action=ALLOW \
  --rules=tcp:8443 --target-tags=pretzel \
  --source-ranges=35.191.0.0/16,130.211.0.0/22 --priority=1000
```

```bash
# 3) IAP TCP forwarding → SSH + pgAdmin. 35.235.240.0/20은 IAP 전용 고정 대역입니다.
gcloud compute firewall-rules create pz-allow-iap \
  --network=pretzel-vpc --direction=INGRESS --action=ALLOW \
  --rules=tcp:22,tcp:5050 --target-tags=pretzel \
  --source-ranges=35.235.240.0/20 --priority=1000
```

```bash
# 4) 이그레스: 443만 허용, 나머지 거부
gcloud compute firewall-rules create pz-allow-egress-https \
  --network=pretzel-vpc --direction=EGRESS --action=ALLOW \
  --rules=tcp:443,tcp:53,udp:53 --destination-ranges=0.0.0.0/0 \
  --target-tags=pretzel --priority=1000

gcloud compute firewall-rules create pz-deny-egress-all \
  --network=pretzel-vpc --direction=EGRESS --action=DENY --rules=all \
  --destination-ranges=0.0.0.0/0 --target-tags=pretzel --priority=65534 \
  --enable-logging
```

> **주의**: Palo Alto API는 443으로 나가므로 괜찮지만, probed의 **ICMP는 egress 443 룰에 걸려 막힙니다.**
> 사설망 장비를 Cloud VPN/Interconnect 너머로 핑한다면 그 대역에 대한 ICMP 허용 룰을 따로 추가하세요:
> ```bash
> gcloud compute firewall-rules create pz-allow-egress-icmp-onprem \
>   --network=pretzel-vpc --direction=EGRESS --action=ALLOW --rules=icmp \
>   --destination-ranges=10.0.0.0/8,172.16.0.0/12,192.168.0.0/16 \
>   --target-tags=pretzel --priority=900
> ```

### 6.3 Secret Manager

```bash
for s in PZ_PG_PASSWORD PZ_PGADMIN_PASSWORD PZ_SEED_ADMIN_PASSWORD; do
  openssl rand -base64 32 | tr -d '\n' | \
    gcloud secrets create "$s" --replication-policy=automatic --data-file=-
done

# 자격증명 마스터키 (32바이트 raw)
openssl rand 32 | gcloud secrets create PZ_CREDENTIALS_KEY \
  --replication-policy=automatic --data-file=-
```

```bash
gcloud iam service-accounts create pretzel-vm --display-name="Pretzel VM"

SA="pretzel-vm@${PROJECT_ID}.iam.gserviceaccount.com"
for s in PZ_PG_PASSWORD PZ_PGADMIN_PASSWORD PZ_SEED_ADMIN_PASSWORD PZ_CREDENTIALS_KEY; do
  gcloud secrets add-iam-policy-binding "$s" \
    --member="serviceAccount:${SA}" --role=roles/secretmanager.secretAccessor
done

gcloud projects add-iam-policy-binding "$PROJECT_ID" \
  --member="serviceAccount:${SA}" --role=roles/logging.logWriter
```

### 6.4 VM 기동 스크립트

```bash
cat > /tmp/pretzel-startup.sh <<'EOS'
#!/bin/bash
set -euo pipefail

sec() { gcloud secrets versions access latest --secret="$1"; }

export PZ_PROFILE=prod
export PZ_PG_PASSWORD="$(sec PZ_PG_PASSWORD)"
export PZ_PGADMIN_PASSWORD="$(sec PZ_PGADMIN_PASSWORD)"
export PZ_SEED_ADMIN_PASSWORD="$(sec PZ_SEED_ADMIN_PASSWORD)"

install -d -m 0700 /etc/pretzel
sec PZ_CREDENTIALS_KEY > /etc/pretzel/credentials.key
chmod 0600 /etc/pretzel/credentials.key
chown pretzel:pretzel /etc/pretzel/credentials.key    # P0-4 적용 후

cd /opt/pretzel-src && ./pretzel start
EOS

gcloud compute instances create pretzel-1 \
  --zone="$ZONE" --machine-type=e2-standard-4 \
  --subnet=pretzel-subnet --no-address \
  --image-family=ubuntu-2204-lts --image-project=ubuntu-os-cloud \
  --boot-disk-size=100GB --boot-disk-type=pd-balanced \
  --service-account="$SA" \
  --scopes=https://www.googleapis.com/auth/cloud-platform \
  --tags=pretzel \
  --shielded-secure-boot --shielded-vtpm --shielded-integrity-monitoring \
  --metadata-from-file=startup-script=/tmp/pretzel-startup.sh
```

> `--no-address`가 핵심입니다. 외부 IP가 없으므로 인터넷에서 VM에 직접 닿을 방법이 없습니다.

**SSH 접속** (22번이 공개돼 있지 않아도 됩니다):

```bash
gcloud compute ssh pretzel-1 --zone="$ZONE" --tunnel-through-iap
```

**pgAdmin 접속** (로컬 5050으로 터널링):

```bash
gcloud compute start-iap-tunnel pretzel-1 5050 --local-host-port=localhost:5050 --zone="$ZONE"
# → 브라우저에서 https://localhost:5050
```

### 6.5 MIG + 로드밸런서

```bash
gcloud compute instance-groups unmanaged create pretzel-ig --zone="$ZONE"
gcloud compute instance-groups unmanaged add-instances pretzel-ig \
  --zone="$ZONE" --instances=pretzel-1
gcloud compute instance-groups unmanaged set-named-ports pretzel-ig \
  --zone="$ZONE" --named-ports=https:8443
```

```bash
# 헬스체크 — /health가 미인증인 점을 여기서 활용합니다
gcloud compute health-checks create https pretzel-hc \
  --port=8443 --request-path=/health \
  --check-interval=10s --timeout=5s \
  --healthy-threshold=2 --unhealthy-threshold=3
```

```bash
gcloud compute backend-services create pretzel-backend \
  --global --protocol=HTTPS --port-name=https \
  --health-checks=pretzel-hc --timeout=120s \
  --enable-logging --logging-sample-rate=1.0

gcloud compute backend-services add-backend pretzel-backend \
  --global --instance-group=pretzel-ig --instance-group-zone="$ZONE"
```

> 백엔드 프로토콜을 HTTPS로 두면 LB→VM 구간도 암호화됩니다. 외부 ALB는 백엔드의
> 자체서명 인증서를 체인 검증하지 않으므로 `cert/server.crt`를 그대로 써도 동작합니다.

```bash
gcloud compute addresses create pretzel-ip --global
gcloud compute ssl-certificates create pretzel-cert --global --domains="$DOMAIN"

# TLS 1.2 이상만 (P1-3을 앞단에서도 강제). 프록시보다 먼저 만들어야 합니다.
gcloud compute ssl-policies create pretzel-ssl-policy \
  --profile=RESTRICTED --min-tls-version=1.2

gcloud compute url-maps create pretzel-urlmap --default-service=pretzel-backend
gcloud compute target-https-proxies create pretzel-https-proxy \
  --url-map=pretzel-urlmap --ssl-certificates=pretzel-cert \
  --ssl-policy=pretzel-ssl-policy
gcloud compute forwarding-rules create pretzel-fr \
  --global --address=pretzel-ip --target-https-proxy=pretzel-https-proxy --ports=443
```

```bash
# HTTP(80) → HTTPS 리디렉트
gcloud compute url-maps import pretzel-redirect --global --source=/dev/stdin <<'EOF'
name: pretzel-redirect
defaultUrlRedirect:
  httpsRedirect: true
  redirectResponseCode: MOVED_PERMANENTLY_DEFAULT
EOF
gcloud compute target-http-proxies create pretzel-http-proxy --url-map=pretzel-redirect
gcloud compute forwarding-rules create pretzel-fr-http \
  --global --address=pretzel-ip --target-http-proxy=pretzel-http-proxy --ports=80
```

DNS A 레코드를 `pretzel-ip`로 가리키면 Google-managed 인증서가 15~30분 내 `ACTIVE`가 됩니다.

---

## 7. Cloud Armor 정책

```bash
gcloud compute security-policies create pretzel-armor \
  --description="Pretzel console WAF"

gcloud compute security-policies update pretzel-armor \
  --log-level=VERBOSE
```

### R1 — 적응형 보호 (L7 DDoS 자동 탐지)

```bash
gcloud compute security-policies update pretzel-armor \
  --enable-layer7-ddos-defense \
  --layer7-ddos-defense-rule-visibility=STANDARD
```

### R2 — 로그인 엔드포인트 레이트 리밋 (P1-4의 실질적 해결책)

```bash
gcloud compute security-policies rules create 1000 \
  --security-policy=pretzel-armor \
  --expression="request.path.matches('/api/auth/login')" \
  --action=rate-based-ban \
  --rate-limit-threshold-count=10 \
  --rate-limit-threshold-interval-sec=60 \
  --ban-duration-sec=900 \
  --conform-action=allow --exceed-action=deny-429 \
  --enforce-on-key=IP \
  --description="login brute-force: 10/min per IP, 15min ban"
```

애플리케이션의 username 단위 쓰로틀이 못 막는 **IP 단위 분산 추측**을 여기서 잡습니다.

### R3 — 전역 레이트 리밋

```bash
gcloud compute security-policies rules create 1100 \
  --security-policy=pretzel-armor \
  --src-ip-ranges="*" \
  --action=throttle \
  --rate-limit-threshold-count=600 \
  --rate-limit-threshold-interval-sec=60 \
  --conform-action=allow --exceed-action=deny-429 \
  --enforce-on-key=IP \
  --description="global 600/min per IP"
```

### R4 — 관리 API는 사무실 IP에서만 (강력 권장)

```bash
gcloud compute security-policies rules create 900 \
  --security-policy=pretzel-armor \
  --expression="request.path.matches('/api/settings') || request.path.matches('/api/user')" \
  --action=deny-403 \
  --description="admin API: office only (deny by default)"

gcloud compute security-policies rules create 890 \
  --security-policy=pretzel-armor \
  --src-ip-ranges="$ADMIN_CIDR" \
  --action=allow \
  --description="office allowlist, evaluated before R4"
```

> 우선순위는 **숫자가 작을수록 먼저** 평가됩니다. 890(허용)이 900(거부)보다 앞서야 합니다.

### R5 — `/health`·`/metric` 외부 차단 (P2-3)

```bash
gcloud compute security-policies rules create 1200 \
  --security-policy=pretzel-armor \
  --expression="request.path.matches('/health') || request.path.matches('/metric')" \
  --action=deny-404 \
  --description="hide liveness/metrics from the internet"
```

**LB 헬스체크는 영향받지 않습니다.** GCP 헬스체커는 `35.191.0.0/16`/`130.211.0.0/22`에서
백엔드로 **직접** 가고 Cloud Armor를 통과하지 않습니다.

### R6 — 사전 구성 WAF 룰 (반드시 preview부터)

```bash
# SQLi / XSS / LFI / RCE — 민감도 1로 시작
for i in 1300:sqli-v33-stable 1310:xss-v33-stable \
         1320:lfi-v33-stable 1330:rce-v33-stable; do
  P="${i%%:*}"; R="${i##*:}"
  gcloud compute security-policies rules create "$P" \
    --security-policy=pretzel-armor \
    --expression="evaluatePreconfiguredWaf('${R}', {'sensitivity': 1})" \
    --action=deny-403 --preview \
    --description="WAF ${R} (preview)"
done
```

> **`--preview`를 반드시 붙여 시작하세요.** Pretzel은 설정 커밋에서 **JSON 본문을 통째로 POST**하고
> 기술문서(techdoc)·벤치테스트 업로드도 임의 텍스트를 실어 보냅니다. SQLi/XSS 룰이
> 정상 트래픽을 오탐할 가능성이 높습니다. 1~2주 로그를 본 뒤 룰별로 preview를 해제하거나,
> `/api/settings/commit`·`/api/benchtest/datasets` 경로를 예외로 빼세요.

```bash
# preview 해제 (오탐 확인 후)
gcloud compute security-policies rules update 1300 \
  --security-policy=pretzel-armor --no-preview
```

**오탐 확인 쿼리** (Logs Explorer):

```
resource.type="http_load_balancer"
jsonPayload.enforcedSecurityPolicy.name="pretzel-armor"
jsonPayload.previewSecurityPolicy.outcome="DENY"
```

### R7 — 지역 제한 (운영자가 국내에만 있다면)

```bash
gcloud compute security-policies rules create 800 \
  --security-policy=pretzel-armor \
  --expression="origin.region_code != 'KR'" \
  --action=deny-403 \
  --description="KR only"
```

단순하지만 **봇 트래픽의 90% 이상을 한 줄로 쳐냅니다.** 해외 출장 가능성이 있다면
VPN 경유를 전제하거나 이 룰은 빼세요.

### 정책 연결

```bash
gcloud compute backend-services update pretzel-backend \
  --global --security-policy=pretzel-armor
```

---

## 8. 선택 — IAP로 콘솔 앞단에 Google 로그인 추가

가장 강력한 조치입니다. Pretzel 로그인 화면에 닿기 **전에** Google 계정 인증을 요구합니다.
admin/admin이 남아 있어도 인터넷에서는 로그인 폼 자체를 볼 수 없습니다.

```bash
gcloud compute backend-services update pretzel-backend \
  --global --iap=enabled

gcloud iap web add-iam-policy-binding \
  --resource-type=backend-services --service=pretzel-backend \
  --member="user:jhkim000929@gmail.com" --role=roles/iap.httpsResourceAccessor
```

트레이드오프: SAML/OIDC SSO를 쓸 계획이면 인증이 2단이 되어 번거롭고,
apid ingest 같은 **머신 투 머신 호출이 막힙니다**(IAP용 서비스 계정 JWT가 따로 필요).
사람만 쓰는 콘솔이면 켜는 걸 권장합니다.

---

## 9. 작업 순서 체크리스트

### 코드 수정 (배포 전)

- [ ] **P0-1** `script/utils.py` — prod 프로파일에서 기본 비밀번호 거부
- [ ] **P0-1** `shared/config/Config.cpp:20-21` — admin 시드 비밀번호를 env/랜덤으로
- [ ] **P0-2** `config/pgadmin/config_local.py:22` — `0.0.0.0` → `127.0.0.1`
- [ ] **P0-3** `config/startup-config.json` — `pretzel.api.tls_enabled: true` (또는 `listen_address: 127.0.0.1`)
- [ ] **P0-3** `pretzel.console.listen_port: 443` → `8443` (LB가 443 종단)
- [ ] **P0-4** `script/service/*.service` 10개 — `User=pretzel` + 샌드박싱 지시자
- [ ] **P1-1** `AuthController.cpp:90,99` / `SsoController.cpp:276` — `; Secure` 추가
- [ ] **P1-2** `shared/http/HttpBeast.h` — 보안 헤더 5종 (CSP는 Report-Only로 먼저)
- [ ] **P1-3** `shared/http/HttpServer.cpp:67` — `no_tlsv1 | no_tlsv1_1` 추가

### 인프라 (순서대로)

- [ ] VPC + 서브넷 + Cloud NAT (§6.1)
- [ ] 방화벽 — **전면 거부부터** 깔고 4개 룰 (§6.2)
- [ ] Secret Manager + 서비스 계정 (§6.3)
- [ ] VM 생성 **`--no-address`** (§6.4)
- [ ] IAP SSH로 접속 확인, `./pretzel start` 수동 검증
- [ ] MIG + 백엔드 + LB + 관리형 인증서 (§6.5)
- [ ] DNS A 레코드 → 인증서 `ACTIVE` 대기
- [ ] Cloud Armor R1~R5, R7 적용 / **R6은 preview로** (§7)
- [ ] (권장) IAP 활성화 (§8)

### 배포 직후 반드시

- [ ] **admin 초기 비밀번호 즉시 변경** — 브라우저로 첫 로그인
- [ ] pgAdmin 비밀번호가 `padmin`이 아닌지 IAP 터널로 확인
- [ ] 외부에서 `nmap -Pn <LB IP>` — 443 외 열린 포트 0개 확인
- [ ] `curl https://$DOMAIN/health` → **404** (R5 동작 확인)
- [ ] `curl https://$DOMAIN/api/settings` → **403** (R4 동작 확인)
- [ ] 로그인 11회 연속 실패 → **429** (R2 동작 확인)
- [ ] 1주일 뒤 Cloud Armor preview 로그 검토 → WAF 룰 enforce 전환

### 지속 운영

- [ ] Cloud Logging 싱크 → BigQuery, `deny` 이벤트 알림 설정
- [ ] `credentials.key` 백업 — **이게 없으면 DB의 모든 벤더 API 키가 영구 복구 불능**
- [ ] 스냅샷 스케줄 (PostgreSQL 데이터 디스크)
- [ ] Google-managed 인증서는 자동 갱신이지만 `ACTIVE` 상태 모니터링

---

## 10. 비용 개략 (asia-northeast3, 월)

| 항목 | 대략 |
|---|---|
| e2-standard-4 (4 vCPU / 16GB, 상시) | ~$120 |
| pd-balanced 100GB | ~$12 |
| Global External ALB (전달 규칙 + 처리량) | ~$25 + 트래픽 |
| Cloud Armor (정책 $5 + 룰 $1 × 10) | ~$15 |
| Cloud NAT | ~$35 |
| Secret Manager / Logging | ~$5 |
| **합계** | **~$210/월** |

Cloud Armor 적응형 보호(Managed Protection Plus)를 켜면 월 $3,000부터라 소규모에는 과합니다.
위 구성은 **Standard** 기준입니다.

---

*점검 범위: 추적 중인 607개 파일 + 전체 git 이력. 동적 분석(DAST)·의존성 CVE 스캔(Boost/OpenSSL/gRPC 버전)은 포함하지 않았습니다.*
