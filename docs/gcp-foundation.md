# GCP 기초 구축 가이드 — Prisma Access Service Connection 연동

전제: GCP 완전 처음 / 목표 토폴로지는 아래

```
사용자(GlobalProtect) ──▶ Prisma Access ──IPsec(Service Connection)──▶ GCP VPC ──▶ Pretzel
```

---

## 0. 먼저 — 이 토폴로지는 앞 가이드를 상당 부분 **무효화**합니다 (좋은 쪽으로)

`docs/cloud-deployment-security.md`는 "인터넷에 공개하는 웹 콘솔"을 전제로 썼습니다.
Service Connection 방식은 전제가 다릅니다.

| | 앞 가이드 (공개 LB) | **이번 (SASE 경유)** |
|---|---|---|
| 접근 경로 | 인터넷 → 공개 LB → VM | 사용자 → Prisma Access → IPsec → **사설 IP** |
| 공인 IP | LB에 1개 필요 | **Pretzel 쪽 0개** (VPN 게이트웨이만) |
| Cloud Armor | 필수 | **거의 불필요** (공개 L7 표면이 없음) |
| 인증 | Pretzel 로그인이 1차 방어선 | **GlobalProtect 인증 + Prisma Access 정책이 앞단** |
| 공격 표면 | 전 세계 | **Prisma Access 테넌트 내부** |
| 무차별 대입 | 상시 발생 | 인터넷에서 **도달 자체 불가** |

제가 "클라우드에 올리기 위험하다"에 대해 드린 답의 핵심이 "자산 가치가 커서"였는데,
**이 토폴로지는 그 위험을 구조적으로 가장 크게 줄이는 선택**입니다. 좋은 판단이셨습니다.

> **여전히 유효한 것**: P0 4건(기본 자격증명·pgAdmin 바인드·평문 API·root 실행)과 P1 4건은
> 그대로 고쳐야 합니다. 터널 안에 있다고 `admin/admin`이 안전해지지 않습니다.
> 내부자·단말 감염·측면 이동은 전부 터널 **안쪽**에서 일어납니다.

---

## 1. GCP 개념 10분 — 이것만 알면 됩니다

### 1.1 리소스 계층

```
조직 (Organization)          ← Workspace/Cloud Identity 있을 때만. 없으면 건너뜀
└── 폴더 (Folder)            ← 선택
    └── 프로젝트 (Project)   ★ 모든 것의 단위. 과금·IAM·격리 경계
        └── 리소스 (VPC, VM, 디스크…)
```

**프로젝트가 핵심 단위**입니다. 프로젝트가 다르면 네트워크도 과금도 권한도 기본 분리됩니다.

### 1.2 스크린샷의 "My First Project"를 쓰지 마세요

지금 보고 계신 기본 프로젝트는 이름도 못 바꾸고(표시 이름만 가능) 나중에 정리가 어렵습니다.
**전용 프로젝트를 새로 만들어 시작하는 게 맞습니다.** 잘못되면 프로젝트째 삭제하면 되니까요.

### 1.3 리전과 존

- **리전(region)** = 도시 단위. 서울은 `asia-northeast3`
- **존(zone)** = 리전 안의 물리 구획. `asia-northeast3-a`, `-b`, `-c`
- VM은 존에, 서브넷은 리전에 속합니다

### 1.4 ★ VPC는 글로벌, 서브넷은 리전

이게 AWS와 가장 다른 점이고 처음에 제일 헷갈립니다.

```
VPC "pretzel-vpc"  ← 전 세계 하나. 리전 개념 없음
 ├ 서브넷 A  asia-northeast3 (서울)   10.100.10.0/24
 ├ 서브넷 B  us-central1    (아이오와) 10.100.20.0/24
 └ 서브넷 C  europe-west1   (벨기에)   10.100.30.0/24
      ↑ 이 셋은 추가 설정 없이 서로 사설 IP로 통신됩니다 (피어링 불필요)
```

AWS처럼 리전마다 VPC를 만들고 피어링할 필요가 없습니다.

### 1.5 방화벽의 암묵적 규칙

VPC를 만들면 보이지 않는 두 규칙이 항상 맨 뒤(우선순위 65535)에 깔립니다:

| 방향 | 동작 | 의미 |
|---|---|---|
| 인그레스 | **전부 거부** | 명시적으로 허용해야 들어옴 |
| 이그레스 | **전부 허용** | ← 앞서 지적한 exfil 문제의 출발점 |

그래서 egress 거부 규칙을 **따로 만들어야** 합니다(§5).

---

## 2. 사전 준비

### 2.1 프로젝트 생성

콘솔 상단의 프로젝트 선택기(스크린샷의 `My First Project` 버튼) → **새 프로젝트**

- 프로젝트 이름: `pretzel-prod`
- **프로젝트 ID**: 전역 고유. 한번 정하면 **못 바꿉니다**. `pretzel-prod-7421` 같이 숫자를 붙이세요
- 위치: 조직 없으면 "조직 없음"

### 2.2 결제 계정 연결

**결제 → 결제 계정 연결.** 연결 안 하면 VM·VPN 생성이 전부 실패합니다.

> 신규 가입이면 $300 크레딧(90일)이 있습니다. 다만 **무료 체험판 계정은 할당량 제한이 있어
> VPN 터널 생성이 막힐 수 있습니다.** 그 경우 유료 계정으로 업그레이드해야 합니다
> (업그레이드해도 크레딧은 유지됩니다).

### 2.3 예산 알림부터 거세요

**결제 → 예산 및 알림 → 예산 만들기.** 월 $300 정도로 잡고 50/90/100% 알림.
처음엔 실수로 비싼 리소스를 켜두기 쉽습니다. 5분 투자할 값어치가 있습니다.

### 2.4 gcloud CLI 설치 (이 머신에 없는 것 확인했습니다)

```bash
curl -sSL https://sdk.cloud.google.com | bash
exec -l $SHELL
gcloud init
```

`gcloud init`이 브라우저 로그인 → 프로젝트 선택 → 기본 리전 설정을 순서대로 물어봅니다.

> 설치가 번거로우면 콘솔 우상단의 **Cloud Shell**(터미널 아이콘)을 쓰셔도 됩니다.
> gcloud가 이미 설치된 브라우저 터미널이고 무료입니다. 처음엔 이쪽이 편합니다.

### 2.5 변수 + API 활성화

```bash
export PROJECT_ID="pretzel-prod-7421"      # 본인 프로젝트 ID로 교체
export REGION="asia-northeast3"
export ZONE="asia-northeast3-a"

gcloud config set project "$PROJECT_ID"
gcloud config set compute/region "$REGION"
gcloud config set compute/zone "$ZONE"
```

```bash
gcloud services enable \
  compute.googleapis.com \
  iap.googleapis.com \
  secretmanager.googleapis.com \
  logging.googleapis.com \
  monitoring.googleapis.com
```

---

## 3. ★ IP 주소 설계 — 가장 중요하고, 나중에 못 바꿉니다

서브넷은 생성 후 **확장만 되고 축소·변경이 안 됩니다.** VPN 연동에서 IP가 겹치면
터널은 올라가는데 트래픽만 조용히 안 가는, 가장 디버깅하기 싫은 상황이 됩니다.
**여기서 30분 쓰는 게 나중에 재구축 하루를 아낍니다.**

### 3.1 겹치면 안 되는 목록

Prisma Access 연동에서는 최소 다섯 가지가 서로 겹치면 안 됩니다:

1. GCP VPC 서브넷
2. **Prisma Access 인프라 서브넷** (PA 내부 전용 대역)
3. **Mobile User IP 풀** (GlobalProtect 사용자에게 주는 IP)
4. 온프렘/기존 사내망
5. 다른 Service Connection / Remote Network 대역

### 3.2 권장 설계

```
┌─ GCP ────────────────────────────────────────────┐
│ VPC: pretzel-vpc           10.100.0.0/16         │
│   ├ pz-mgmt   (서울)       10.100.10.0/24  ← Pretzel VM
│   ├ (예약) pz-workload     10.100.20.0/24        │
│   └ (예약) pz-data         10.100.30.0/24        │
│ 2차 리전 확장 예약          10.101.0.0/16        │
└──────────────────────────────────────────────────┘
┌─ Prisma Access ──────────────────────────────────┐
│ 인프라 서브넷              10.200.0.0/16         │
│ Mobile User 풀             10.210.0.0/16         │
└──────────────────────────────────────────────────┘
┌─ 기타 ───────────────────────────────────────────┐
│ BGP link-local             169.254.x.y/30        │
│ 온프렘 (있다면)            실사 후 기입           │
└──────────────────────────────────────────────────┘
```

### 3.3 피해야 할 대역

| 대역 | 이유 |
|---|---|
| `10.128.0.0/9` | GCP auto-mode VPC가 쓰는 범위. 나중에 충돌 |
| `172.17.0.0/16` | Docker 기본 브리지 |
| `192.168.0.0/16` | 가정용 공유기. 재택 사용자와 충돌 |
| `169.254.0.0/16` | link-local. BGP 세션 전용 |
| `10.0.0.0/24`, `10.1.1.0/24` | 너무 흔해서 어딘가와 반드시 겹침 |

### 3.4 서브넷은 4개 IP를 뺍니다

`10.100.10.0/24` (256개) → 실제 할당 가능 **252개**:

| IP | 용도 |
|---|---|
| `10.100.10.0` | 네트워크 주소 |
| `10.100.10.1` | 기본 게이트웨이 |
| `10.100.10.254` | GCP 예약 |
| `10.100.10.255` | 브로드캐스트 |

### 3.5 Prisma Access 쪽에서 확인해야 할 값

GCP 작업 전에 **Prisma Access 테넌트에서 아래를 먼저 확인/결정**하세요.
(Strata Cloud Manager 또는 Panorama 플러그인)

| 항목 | 어디서 | 메모란 |
|---|---|---|
| 인프라 서브넷 | Setup ▸ Service Setup | `________` |
| Mobile User IP 풀 | Mobile Users 설정 | `________` |
| Service Connection 배포 리전 | Service Connections | `________` (서울 권장) |
| SC의 공인 피어 IP | SC 생성 후 표시 | `________` |
| BGP 사용 여부 / PA측 ASN | SC 설정 | `________` |
| IKE/IPsec 제안값 | SC 설정 | `________` |

> 이 값들은 테넌트·버전마다 달라서 제가 단정할 수 없습니다. **반드시 실제 화면에서 확인**하세요.
> 특히 인프라 서브넷은 테넌트 전역 설정이라 나중에 바꾸면 모든 연결이 끊깁니다.

---

## 4. VPC 생성

### 4.1 auto 모드를 쓰지 마세요

| | auto 모드 | **custom 모드** |
|---|---|---|
| 서브넷 | 전 리전에 자동 생성 (`10.128.0.0/9`) | 내가 정의한 것만 |
| 하이브리드 | ❌ 대역 충돌 위험 | ✅ |

VPN을 붙일 거면 **custom 모드가 유일한 선택**입니다.

### 4.2 생성

```bash
gcloud compute networks create pretzel-vpc \
  --subnet-mode=custom \
  --bgp-routing-mode=regional \
  --description="Pretzel appliance VPC (Prisma Access service connection)"
```

> `--bgp-routing-mode`: `regional`은 VPN이 **같은 리전** 서브넷만 광고, `global`은 전 리전 광고.
> 지금은 서울 하나뿐이니 `regional`로 시작하고, 리전을 늘릴 때 `global`로 바꾸면 됩니다
> (이건 나중에 변경 가능합니다).

```bash
gcloud compute networks subnets create pz-mgmt \
  --network=pretzel-vpc \
  --region="$REGION" \
  --range=10.100.10.0/24 \
  --enable-private-ip-google-access \
  --enable-flow-logs \
  --logging-aggregation-interval=interval-5-sec \
  --logging-flow-sampling=0.5
```

세 플래그의 의미:

- `--enable-private-ip-google-access` — **외부 IP 없는 VM이 Google API(Secret Manager, Logging 등)에 접근**. 없으면 §2.5에서 켠 API를 VM이 못 씁니다. 필수입니다.
- `--enable-flow-logs` — VPC 플로우 로그. 나중에 "터널은 올라왔는데 트래픽이 안 가요"를 디버깅할 유일한 수단입니다. **켜두세요.**
- `--logging-flow-sampling=0.5` — 50% 샘플링. 비용 절반.

### 4.3 기본 네트워크 정리

프로젝트 생성 시 `default` VPC가 자동으로 생기는데, **`default-allow-ssh`(0.0.0.0/0 → 22),
`default-allow-rdp`(0.0.0.0/0 → 3389) 같은 전면 허용 규칙을 달고 있습니다.**

안 쓸 거면 지우세요:

```bash
gcloud compute networks delete default --quiet
```

> 삭제가 거부되면 그 안에 리소스가 있는 겁니다. 최소한 위 두 방화벽 규칙만이라도 지우세요:
> ```bash
> gcloud compute firewall-rules delete default-allow-ssh default-allow-rdp --quiet
> ```

---

## 5. 방화벽 기본

**순서가 중요합니다. 거부를 먼저 깔고 필요한 것만 엽니다.**

```bash
# 1) 이그레스 전면 거부 (암묵적 "전부 허용"을 덮어씀)
gcloud compute firewall-rules create pz-deny-egress-all \
  --network=pretzel-vpc --direction=EGRESS --action=DENY --rules=all \
  --destination-ranges=0.0.0.0/0 --priority=65000 --enable-logging
```

```bash
# 2) Prisma Access 터널 너머에서 오는 콘솔 접근 (사용자 트래픽)
gcloud compute firewall-rules create pz-allow-from-prisma \
  --network=pretzel-vpc --direction=INGRESS --action=ALLOW \
  --rules=tcp:8443 --target-tags=pretzel \
  --source-ranges=10.210.0.0/16 \
  --priority=1000 --enable-logging \
  --description="GlobalProtect mobile users via service connection"
```

> `10.210.0.0/16`을 §3.5에서 확인한 **실제 Mobile User 풀**로 바꾸세요.

```bash
# 3) IAP TCP forwarding — SSH + pgAdmin (35.235.240.0/20은 IAP 전용 고정 대역)
gcloud compute firewall-rules create pz-allow-iap \
  --network=pretzel-vpc --direction=INGRESS --action=ALLOW \
  --rules=tcp:22,tcp:5050 --target-tags=pretzel \
  --source-ranges=35.235.240.0/20 --priority=1000
```

```bash
# 4) 이그레스 허용 — Palo Alto API + DNS. 터널 쪽은 별도 룰로
gcloud compute firewall-rules create pz-allow-egress-https \
  --network=pretzel-vpc --direction=EGRESS --action=ALLOW \
  --rules=tcp:443,tcp:53,udp:53 --destination-ranges=0.0.0.0/0 \
  --target-tags=pretzel --priority=1000
```

**방화벽 핵심 3가지**

1. **우선순위는 숫자가 작을수록 먼저** (0~65535)
2. **타겟 태그**로 적용 대상을 지정 — VM에 `--tags=pretzel`을 붙여야 룰이 걸립니다
3. 허용과 거부가 겹치면 **우선순위가 낮은 숫자가 이깁니다**

---

## 6. Prisma Access Service Connection 연동 런북

GCP 쪽 준비물은 모두 생성 완료 상태입니다. 아래는 **실제 값이 들어간 런북**입니다.

### 6.0 확보된 값

| 항목 | 값 |
|---|---|
| VPC | `pretzel-vpc` (custom) |
| 서브넷 | `pz-mgmt` · `10.100.10.0/24` · asia-northeast3 |
| Cloud Router | `pretzel-router` · **ASN 65001** |
| HA VPN 게이트웨이 | `pretzel-vpn-gw` |
| └ 인터페이스 0 | **34.64.66.182** |
| └ 인터페이스 1 | **34.64.128.190** |
| Cloud NAT | `pretzel-nat` → 34.64.98.211 |

### 6.1 ★ 시작 전 — IP 중복 검사

**이것부터 하세요.** 중복이 있으면 터널은 `ESTABLISHED`로 올라오고 BGP도 맺히는데
트래픽만 조용히 사라집니다. 가장 추적하기 어려운 장애입니다.

```
GCP VPC               10.100.0.0/16     (실사용 10.100.10.0/24)
PA 인프라 서브넷       ____________      ← 겹치면 안 됨
PA Mobile User 풀     ____________      ← 겹치면 안 됨
기존 온프렘           ____________      ← 겹치면 안 됨
다른 SC / RN 대역     ____________      ← 겹치면 안 됨
```

하나라도 `10.100.x`와 겹치면 **GCP 서브넷을 다시 만드는 쪽이 빠릅니다**(PA 인프라 서브넷은
테넌트 전역 설정이라 바꾸면 모든 연결이 끊깁니다). 서브넷 재생성은 §4.2 참고.

### 6.2 PA에 입력할 값 (GCP → PA)

Prisma Access에서 Service Connection을 만들 때 그대로 넣습니다.

```
[IPSec Tunnel #1]
  Peer Address          34.64.66.182
  IKE Version           IKEv2
  Pre-Shared Key        $PSK1   (아래에서 생성)

[IPSec Tunnel #2]       ← PA가 IP를 2개 주는 경우만
  Peer Address          34.64.128.190
  IKE Version           IKEv2
  Pre-Shared Key        $PSK2

[BGP]
  Peer AS               65001
  광고할 대역 (GCP→PA)   10.100.10.0/24
```

PSK 생성 — **양쪽에 같은 값**을 넣어야 합니다:

```bash
PSK1=$(openssl rand -base64 32); echo "PSK1: $PSK1"
PSK2=$(openssl rand -base64 32); echo "PSK2: $PSK2"

echo -n "$PSK1" | gcloud secrets create VPN_PSK_1 --data-file=- --replication-policy=automatic
echo -n "$PSK2" | gcloud secrets create VPN_PSK_2 --data-file=- --replication-policy=automatic
```


### 6.2a 암호화 제안값 — 양쪽을 똑같이 맞추세요

GCP는 제안된 것 중에서 고르지만, **양쪽에 하나씩만 두는 것**이 공식 권장입니다.
여러 개를 열어두면 재협상 때마다 다른 조합이 뽑혀 간헐적 끊김의 원인이 됩니다.

| | 값 |
|---|---|
| **IKE (Phase 1)** | AES-256-CBC · SHA-256 · **DH Group 14** |
| IKE lifetime | **28800초** (8시간) |
| **IPsec (Phase 2)** | ESP · AES-256-CBC · SHA-256 |
| **PFS** | **DH Group 14 — 필수** |
| IPsec lifetime | **3600초** (1시간) |

> ⚠️ **PFS는 GCP에서 필수입니다.** PAN-OS의 IPsec Crypto Profile에서 DH Group을
> `no-pfs`로 두면 Phase 2가 올라오지 않습니다. 가장 흔한 실패 원인입니다.
>
> ⚠️ **lifetime은 양쪽이 정확히 같아야** 합니다. 다르면 터널이 붙었다 끊겼다 반복합니다.

GCP가 AES-GCM을 더 선호하긴 하지만, CBC+SHA-256 조합이 PAN-OS와의 상호운용에서
가장 안전합니다. 양쪽 모두 GCM을 지원하면 `AES-GCM-16-256`으로 바꿔도 됩니다.

### 6.3 PA에서 받아올 값 (PA → GCP)

SC를 만들면 PA가 자기 값을 할당합니다. **이게 없으면 아래 6.4를 진행할 수 없습니다.**

```
□ PA 공인 IP #1              ____________
□ PA 공인 IP #2              ____________  (1개만 주면 터널도 1개)
□ PA BGP ASN                 ____________  ← 65001과 달라야 함
□ BGP link-local /30         ____________  ← 6.6의 주의사항 확인
□ Mobile User 풀             ____________  ← 방화벽 규칙용
```

### 6.4 GCP 터널 생성

```bash
export REGION=asia-northeast3
export PA_IP1=<PA 공인 IP #1>
export PA_IP2=<PA 공인 IP #2>
export PA_ASN=<PA BGP ASN>
```

**피어 게이트웨이 등록** — PA가 IP를 2개 준 경우:

```bash
gcloud compute external-vpn-gateways create prisma-access-gw \
  --interfaces=0="$PA_IP1",1="$PA_IP2"
```

PA가 **1개만** 준 경우:

```bash
gcloud compute external-vpn-gateways create prisma-access-gw \
  --interfaces=0="$PA_IP1"
```

**터널 생성** (2 IP 기준):

```bash
gcloud compute vpn-tunnels create pretzel-tunnel-1 \
  --region="$REGION" \
  --vpn-gateway=pretzel-vpn-gw --interface=0 \
  --peer-external-gateway=prisma-access-gw \
  --peer-external-gateway-interface=0 \
  --router=pretzel-router \
  --ike-version=2 --shared-secret="$PSK1"

gcloud compute vpn-tunnels create pretzel-tunnel-2 \
  --region="$REGION" \
  --vpn-gateway=pretzel-vpn-gw --interface=1 \
  --peer-external-gateway=prisma-access-gw \
  --peer-external-gateway-interface=1 \
  --router=pretzel-router \
  --ike-version=2 --shared-secret="$PSK2"
```

> PA가 IP 1개면 두 터널 모두 `--peer-external-gateway-interface=0`을 쓰고, GCP 쪽
> `--interface`만 0/1로 나눕니다. 이 구성은 GCP 측만 이중화되므로 SLA는 99.9%입니다.

### 6.5 BGP 세션

```bash
gcloud compute routers add-interface pretzel-router --region="$REGION" \
  --interface-name=if-tunnel-1 --vpn-tunnel=pretzel-tunnel-1 \
  --ip-address=169.254.1.1 --mask-length=30

gcloud compute routers add-bgp-peer pretzel-router --region="$REGION" \
  --peer-name=prisma-peer-1 --interface=if-tunnel-1 \
  --peer-ip-address=169.254.1.2 --peer-asn="$PA_ASN"
```

```bash
gcloud compute routers add-interface pretzel-router --region="$REGION" \
  --interface-name=if-tunnel-2 --vpn-tunnel=pretzel-tunnel-2 \
  --ip-address=169.254.2.1 --mask-length=30

gcloud compute routers add-bgp-peer pretzel-router --region="$REGION" \
  --peer-name=prisma-peer-2 --interface=if-tunnel-2 \
  --peer-ip-address=169.254.2.2 --peer-asn="$PA_ASN"
```

Cloud Router는 regional 모드라 **같은 리전 서브넷(`10.100.10.0/24`)을 자동 광고**합니다.
명시하고 싶으면:

```bash
gcloud compute routers update pretzel-router --region="$REGION" \
  --advertisement-mode=CUSTOM --set-advertisement-ranges=10.100.10.0/24
```

### 6.6 ⚠️ 실제 연동에서 드러난 두 가지

#### (1) PA는 Service Endpoint를 FQDN으로 줍니다 — GCP는 IP만 받습니다

이 테넌트가 발급한 값:

```
gcp-pretzel-subnet.south-korea.sc.ocy2cs25j2.gpcloudservice.com
  → 134.231.237.246   (TTL 59s)
```

**GCP의 external VPN gateway는 FQDN을 받지 않습니다.** 해석한 IP를 넣는 것 외에
방법이 없습니다.

```bash
dig +short A gcp-pretzel-subnet.south-korea.sc.ocy2cs25j2.gpcloudservice.com
```

TTL이 59초라 불안해 보이지만, Palo Alto 문서 기준으로 이 주소가 **실제로 바뀌는 경우는
관리 행위뿐**입니다:

| 바뀜 | 안 바뀜 |
|---|---|
| compute location 변경 | 대역폭 모델 마이그레이션 |
| SC 삭제 후 재생성 | 평상시 운영 |

즉 **운영 중 저절로 바뀌지는 않습니다.** 다만 위 두 작업을 하게 되면 GCP의
`external-vpn-gateways`를 **반드시 같이 갱신**해야 하므로, 변경 절차서에 넣어두세요.

```bash
# 엔드포인트가 바뀌었을 때
gcloud compute vpn-tunnels delete pretzel-tunnel-1 --region="$REGION" --quiet
gcloud compute external-vpn-gateways delete prisma-access-gw --quiet
# 새 IP로 재생성 (6.4)
```

#### (2) BGP 피어 주소 — GCP는 link-local만 받습니다

GCP Cloud Router는 BGP 피어링 주소로 **`169.254.0.0/16`만** 지원합니다(IPv4 기준).
그런데 PA 콘솔의 `BGP Router` 열에는 인프라 대역 주소(예: `192.168.255.9`)가 표시됩니다.

**둘 중 무엇인지 먼저 구분하세요:**

| PA의 `192.168.255.x`가 | 대응 |
|---|---|
| 단순 **router ID / 표시값**이고, Routing 화면의 `Local IP Address`에 link-local을 넣을 수 있음 | 6.5 그대로 진행 |
| 실제 **BGP 소스 주소**로 고정됨 | **정적 경로로 전환** (아래) |

판단 방법 — SC 목록의 `BGP IPv4` 열에 있는 **`Primary (Status)`** 링크를 열어
실제 피어링 주소를 확인하고, Routing 설정의 `Local IP Address` 입력값과 비교합니다.

**정적 경로 폴백** — BGP를 포기하는 구성입니다. 동작은 하고, 자동 장애 전환만 없습니다.

```bash
# GCP → PA 방향
gcloud compute routes create pz-route-to-prisma \
  --network=pretzel-vpc \
  --destination-range=<PA Mobile User 풀> \
  --next-hop-vpn-tunnel=pretzel-tunnel-1 \
  --next-hop-vpn-tunnel-region="$REGION" \
  --priority=1000
```

PA 쪽은 Routing 화면의 **Static Routing** 항목에 `10.100.10.0/24`를 추가합니다.

> **권장 순서**: IPsec 터널을 **먼저** 올리고(§6.4), 그 다음 BGP를 붙이세요.
> 둘을 동시에 세우면 터널 문제인지 BGP 문제인지 구분이 안 됩니다.

### 6.7 방화벽 규칙 추가

Mobile User 풀을 알게 되면:

```bash
export MU_POOL=<PA Mobile User 풀>

# 관리자 SSH + pgAdmin
gcloud compute firewall-rules create pz-allow-prisma-mgmt \
  --network=pretzel-vpc --direction=INGRESS --action=ALLOW \
  --rules=tcp:22,tcp:5050 --target-tags=pretzel \
  --source-ranges="$MU_POOL" --priority=900 --enable-logging
```

> 서비스 포트(`8443`)는 **고객이 공개 LB로** 들어오므로 여기 넣지 않습니다.
> 관리 평면과 서비스 평면의 분리가 이 규칙에서 드러납니다.

---

## 7. 검증 — 반드시 이 순서로

중간을 건너뛰면 원인 추적이 어렵습니다.

```bash
# 1) 터널 — ESTABLISHED
gcloud compute vpn-tunnels list --format="table(name,status,detailedStatus)"
```

```bash
# 2) BGP 세션 — state: Established
gcloud compute routers get-status pretzel-router --region="$REGION" \
  --format="yaml(result.bgpPeerStatus)"
```

```bash
# 3) 학습 경로 — PA 대역이 보여야 함
gcloud compute routers get-status pretzel-router --region="$REGION" \
  --format="table(result.bestRoutes.destRange, result.bestRoutes.nextHopIp)"
```

```bash
# 4) 종단 테스트 VM
gcloud compute instances create pz-test \
  --zone=asia-northeast3-a --machine-type=e2-micro \
  --subnet=pz-mgmt --no-address \
  --image-family=debian-12 --image-project=debian-cloud --tags=pretzel
```

GlobalProtect로 접속한 단말에서 `10.100.10.x`로 핑이 가면 연동 완료입니다.

### 증상별 원인

| 증상 | 먼저 볼 곳 |
|---|---|
| 터널이 안 올라옴 | IKE 제안 불일치, PSK 오타, peer IP 오기입 |
| 터널 OK / BGP `Idle` | **link-local /30 불일치** (§6.6), ASN 중복 |
| BGP OK / 통신 안 됨 | **IP 대역 중복** (§6.1), 방화벽 누락, 광고 범위 |
| 한 방향만 됨 | 이그레스 거부 룰(§5), PA 보안 정책 |
| 핑은 되는데 큰 전송이 멈춤 | **MTU/MSS** — 아래 참조 |

> **MTU 함정**: GCP HA VPN의 터널 MTU는 1460입니다. IPsec 오버헤드로 실효 MTU가 더
> 내려가므로, PA 쪽에서 **TCP MSS clamping**(보통 1350~1400)을 걸지 않으면
> SSH 접속은 되는데 `scp`나 웹 페이지 로딩이 멈추는 증상이 납니다. 터널이
> 올라온 뒤 큰 파일 전송으로 반드시 확인하세요.

## 8. 다음 단계

1. ✅ 이 문서 — VPC + VPN 기반
2. ⏭ **Pretzel P0/P1 코드 수정** (`cloud-deployment-security.md` §9)
3. ⏭ Pretzel VM 배포 — `--no-address`, `--tags=pretzel`, Secret Manager 주입
4. ⏭ Prisma Access 보안 정책 — 어느 사용자 그룹이 `10.100.10.0/24`에 닿을지
5. ⏭ (선택) 키 분리 아키텍처 — 수집 플레인을 별도 VM으로

---

## 부록. 비용 (asia-northeast3, 월 개략)

| 항목 | 대략 |
|---|---|
| HA VPN 터널 2개 | ~$75 |
| Cloud Router | 무료 (터널 비용에 포함) |
| VPN 이그레스 트래픽 | GB당 과금, 콘솔 트래픽이면 미미 |
| e2-standard-4 (Pretzel) | ~$120 |
| pd-balanced 100GB | ~$12 |
| VPC 플로우 로그 (50% 샘플링) | ~$5 |
| **합계** | **~$215/월** |

앞 가이드의 LB($25) + Cloud Armor($15) + Cloud NAT($35)가 빠지고 VPN($75)이 들어와
**총액은 비슷한데 공격 표면은 훨씬 작습니다.**

> Cloud NAT는 Pretzel이 Palo Alto API로 나가야 하므로 여전히 필요할 수 있습니다.
> 다만 그 egress를 **Service Connection 너머 온프렘 방화벽으로 돌리는** 선택지도 있습니다.
> 그러면 아웃바운드도 고객 방화벽 정책의 통제를 받게 되어 더 깔끔합니다 — 구성 시 결정하세요.
