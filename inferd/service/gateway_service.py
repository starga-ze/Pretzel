"""The AI gateway call, and the AIRS verdict that rides back with it.

Port of the C++ ChatService. The response document this builds is a contract: the
assistant page parses these exact keys, so a field renamed here is a field the console
silently stops showing.

The scan is attached to every outcome, including outcomes that failed for reasons with
nothing to do with security. Whether a turn was inspected is a separate question from
whether it succeeded, and collapsing the two is the exact mistake this path exists to
avoid.
"""

import json
import logging
import os
import time
import urllib.error
import urllib.request

log = logging.getLogger("inferd.gateway")

# before_request_hooks is the prompt direction, after_request_hooks the response one.
# Both are read: a turn can be clean on the way out and dirty on the way back, and that
# asymmetry is exactly what a request-only view would miss.
_PHASES = (("before_request_hooks", "prompt"), ("after_request_hooks", "response"))

_DETECTION_FIELDS = ("prompt_detected", "response_detected", "tool_detected")

USER_AGENT = "pz-inferd/1.0"


def extract_scan(doc):
    """Fold the gateway's hook_results into the shape the console renders."""
    scan = {"present": False}
    hooks = doc.get("hook_results") if isinstance(doc, dict) else None
    if not isinstance(hooks, dict):
        return scan

    categories, masked = [], {}
    present = any_fail = denied = any_async = transformed = timed_out = errored = False
    latency = 0
    profile = profile_id = scan_id = report_id = action = ""

    for key, direction in _PHASES:
        for hook in hooks.get(key) or []:
            if not isinstance(hook, dict):
                continue
            present = True

            # verdict defaults True: a hook that reported no verdict has not failed.
            if hook.get("verdict", True) is False:
                any_fail = True
            denied = denied or bool(hook.get("deny", False))
            any_async = any_async or bool(hook.get("async", False))
            transformed = transformed or bool(hook.get("transformed", False))
            latency = max(latency, hook.get("execution_time") or 0)

            for check in hook.get("checks") or []:
                data = check.get("data") if isinstance(check, dict) else None
                if not isinstance(data, dict):
                    continue

                profile = profile or data.get("profile_name", "")
                profile_id = profile_id or data.get("profile_id", "")
                scan_id = scan_id or data.get("scan_id", "")
                report_id = report_id or data.get("report_id", "")
                action = action or data.get("action", "")
                timed_out = timed_out or bool(data.get("timeout", False))
                errored = errored or bool(data.get("error", False))

                # Every category AIRS reported is carried through, hit or not, with the
                # key names taken from the payload rather than a list compiled here. A
                # category AIRS adds tomorrow then appears on its own; a hard-coded list
                # would drop it, and "not shown" reads identically to "not detected".
                for field in _DETECTION_FIELDS:
                    det = data.get(field)
                    if not isinstance(det, dict):
                        continue
                    for cat_id, hit in det.items():
                        if isinstance(hit, bool):
                            categories.append({"id": cat_id, "direction": direction, "hit": hit})

                # The masked string is computed whenever DLP matches; `transformed` says
                # whether it was the one actually forwarded. Both facts are reported: a
                # mask computed and NOT applied means the original text went upstream.
                pm = data.get("prompt_masked_data")
                if isinstance(pm, dict) and not masked:
                    masked = {
                        "text": pm.get("data", ""),
                        "patterns": [p["pattern"] for p in (pm.get("pattern_detections") or [])
                                     if isinstance(p, dict) and isinstance(p.get("pattern"), str)],
                    }

    if not present:
        return scan  # the guardrail was not on this call's path

    # Three states, not two. "flagged" is the one a boolean would hide: AIRS found
    # something and the gateway forwarded it anyway (deny off, or an async guardrail,
    # which cannot deny whatever it found).
    scan.update({
        "present": True,
        "verdict": "allow" if not any_fail else ("block" if denied else "flagged"),
        "enforced": denied,
        "async": any_async,
        "action": action,
        "profile": profile,
        "profile_id": profile_id,
        "scan_id": scan_id,
        "report_id": report_id,
        "latency_ms": latency,
        "timeout": timed_out,
        "error": errored,
        "categories": categories,
    })
    if masked:
        masked["applied"] = transformed
        scan["masked"] = masked
    return scan


class GatewayService:
    def __init__(self, config):
        self._gw = config
        self._models = {m["id"]: m for m in config.get("models", [])}

    @property
    def default_model(self):
        return self._gw.get("default_model", "")

    def resolve_model(self, requested):
        """An unknown model is not silently substituted — the console shows which model
        answered, and quietly serving a different one makes that display a lie."""
        if not requested:
            return self.default_model, ""
        if requested in self._models:
            return requested, ""
        return "", f"unknown model '{requested}'"

    def build_messages(self, message, system_prompt=None):
        messages = []
        prompt = system_prompt if system_prompt is not None else self._gw.get("system_prompt", "")
        if prompt:
            messages.append({"role": "system", "content": prompt})
        messages.append({"role": "user", "content": message})
        return messages

    def complete(self, model, messages):
        """Returns the response document the console consumes, whatever happened."""
        started = time.monotonic()
        out = {"model": model}

        key = os.environ.get(self._gw.get("api_key_env", ""), "")
        if not key:
            out.update({"ok": False, "code": "NO_CREDENTIAL",
                        "error": "no gateway credential is configured on this appliance",
                        "latency_ms": 0})
            return out

        url = f"https://{self._gw['host']}:{self._gw.get('port', 443)}{self._gw['path']}"
        body = json.dumps({
            "model": model,
            "max_tokens": self._gw.get("max_tokens", 512),
            "messages": messages,
            # Said out loud rather than left to the gateway's default: a gateway that
            # streamed by default would return a body this cannot parse.
            "stream": False,
        }).encode()

        # urllib's default User-Agent is "Python-urllib/<ver>", which the gateway's CDN
        # blocks outright (Cloudflare 1010, a 403 with a body that mentions neither the
        # header nor the reason). Naming the daemon is both the fix and the courtesy.
        headers = {"Content-Type": "application/json",
                   "User-Agent": USER_AGENT,
                   self._gw["api_key_header"]: key}

        # The gateway holds the provider credentials; this only names which integration
        # to use. The "@" prefix is Portkey's own syntax for a named provider slug.
        # Sending no provider header is valid — the gateway then falls back to its own
        # config — so a model with no provider declared is not an error here.
        provider = (self._models.get(model) or {}).get("provider", "")
        if provider:
            headers["x-portkey-provider"] = f"@{provider}"

        # Operator-declared extras last, so config can override anything above. Gateways
        # differ in how they want to be told which provider and key to use — Portkey
        # alone accepts a virtual key, an inline config, or a provider header — and that
        # choice belongs to the operator rather than to this file.
        headers.update(self._gw.get("headers") or {})

        status, raw, transport_error = 0, "", ""
        try:
            req = urllib.request.Request(url, data=body, headers=headers, method="POST")
            with urllib.request.urlopen(req, timeout=self._gw.get("timeout_sec", 45)) as r:
                status, raw = r.status, r.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as e:
            # A non-2xx still carries a body, and for a guardrail denial (446) that body
            # is the whole point — it must be parsed, not discarded as a failure.
            status, raw = e.code, e.read().decode("utf-8", "replace")
        except (urllib.error.URLError, OSError) as e:
            transport_error = str(getattr(e, "reason", e))

        out["latency_ms"] = int((time.monotonic() - started) * 1000)

        if transport_error:
            out.update({"ok": False, "code": "UNREACHABLE",
                        "error": transport_error or "could not reach the gateway"})
            log.warning("chat turn failed to leave: %s", transport_error)
            return out

        try:
            doc = json.loads(raw)
        except json.JSONDecodeError as e:
            # Truncated into the message: a gateway that rejects a request usually says
            # why, and burying that behind "not JSON" sent the last diagnosis on a long
            # detour. HTML error pages are the common case, hence the cap.
            detail = " ".join(raw.split())[:200]
            out.update({"ok": False, "code": "BAD_RESPONSE", "status": status,
                        "error": f"gateway response was not JSON ({e}): {detail}"
                                 if detail else f"gateway response was not JSON: {e}"})
            log.warning("chat turn unreadable (status=%s, body=%s)", status, detail[:120])
            return out

        out["scan"] = extract_scan(doc)
        out["status"] = status

        err = doc.get("error") if isinstance(doc, dict) else None
        err_type = err.get("type", "") if isinstance(err, dict) else ""
        err_msg = err.get("message", "") if isinstance(err, dict) else ""

        # The gateway's own rejections (bad headers, unknown route) use a flatter shape
        # than a provider error: {"status":"failure","message":...}. Without this they
        # fell through to "carried no completion", which blames the model for what is
        # actually a request the gateway refused to forward.
        if not err_msg and isinstance(doc, dict) and doc.get("status") == "failure":
            err_msg = doc.get("message", "") or "the gateway rejected the request"
            err_type = err_type or "gateway_rejected"

        # 446 is the gateway's documented guardrail-denial status and `hooks_failed` the
        # error type that rides with it. NOT a failure of the appliance: it is the control
        # working, and it gets its own code so the console can say so rather than showing
        # an outage.
        if status == 446 or err_type == "hooks_failed":
            out.update({"ok": False, "code": "BLOCKED",
                        "error": err_msg or "the guardrail denied this turn"})
            log.info("chat turn blocked by guardrail (status=%s, scan_id=%s)",
                     status, out["scan"].get("scan_id", ""))
            return out

        # An error the provider itself raised (quota, credit, rate limit). The turn was
        # inspected and allowed — it just never got an answer — so the scan still stands.
        if err_type or err_msg:
            out.update({"ok": False, "code": "UPSTREAM_ERROR", "upstream_type": err_type,
                        "error": err_msg or "the provider returned an error"})
            log.warning("chat turn upstream error (status=%s, type=%s)", status, err_type)
            return out

        text = None
        choices = doc.get("choices") if isinstance(doc, dict) else None
        if isinstance(choices, list) and choices and isinstance(choices[0], dict):
            msg = choices[0].get("message")
            if isinstance(msg, dict) and isinstance(msg.get("content"), str):
                text = msg["content"]

        if text is None:
            out.update({"ok": False, "code": "BAD_RESPONSE",
                        "error": "gateway response carried no completion"})
            log.warning("chat turn had no completion (status=%s)", status)
            return out

        out["ok"] = True
        out["reply"] = text
        if isinstance(doc.get("usage"), dict):
            out["usage"] = doc["usage"]
        return out
