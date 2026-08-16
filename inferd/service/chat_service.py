"""One turn, end to end: optionally retrieve, then answer.

This is the reason the two daemons became one. Retrieval and inference are the same
concern — grounding an answer — and splitting them across a process boundary bought a
serialisation hop and a second failure mode without buying any isolation.

A turn answers TWICE on the same ticket. When retrieval runs, the passages go back the
moment they are found, before the gateway is called at all; the answer follows when it
lands. The operator is meant to see what was retrieved and judge it while the model is
still working — an answer is only as good as the passages behind it, and a retrieval
that missed is worth catching before the answer arrives to disguise it.

With rag off, the turn is one call and one response, exactly as it was before.
"""

import logging

log = logging.getLogger("inferd.chat")

# A retrieval that finds nothing relevant is worse than no retrieval: it fills the
# prompt with confident-looking noise the model then cites. Passages below this cosine
# score are dropped, and if none survive the turn proceeds ungrounded and says so.
MIN_SCORE = 0.70

RAG_SYSTEM_PROMPT = """\
You answer questions about Palo Alto Networks Prisma Access using only the documentation \
excerpts provided.

Rules:
- Ground every claim in the excerpts. If they do not contain the answer, say so plainly \
rather than filling the gap from memory — a wrong Prisma Access answer costs the reader \
a broken deployment.
- Cite the excerpt number inline as [1], [2] for each claim.
- When excerpts carry a version, say which version the answer applies to.
- Answer in the language the user writes in.
"""


def build_context(hits):
    """Number the excerpts so the model has something concrete to cite."""
    blocks = []
    for i, h in enumerate(hits, 1):
        head = h["title"]
        if h.get("heading_path"):
            head += f" > {h['heading_path']}"
        if h.get("version"):
            head += f" (version {h['version']})"
        blocks.append(f"[{i}] {head}\nSource: {h['url']}\n\n{h['text']}")
    return "\n\n---\n\n".join(blocks)


class ChatService:
    def __init__(self, retrieval, gateway):
        self._retrieval = retrieval
        self._gateway = gateway

    def handle_turn(self, request, on_retrieval, on_answer):
        """`on_retrieval` fires only when retrieval ran, and always before the gateway
        call. `on_answer` fires exactly once, whatever happened — a caller holding a
        ticket must never be left waiting on a reply that is not coming."""
        message = (request.get("message") or "").strip()
        if not message:
            return on_answer({"ok": False, "code": "BAD_REQUEST",
                              "error": "message is required"})

        model, model_err = self._gateway.resolve_model(request.get("model"))
        if model_err:
            return on_answer({"ok": False, "code": "BAD_REQUEST", "error": model_err})

        hits, system_prompt = [], None

        if request.get("rag"):
            result = self._retrieval.retrieve({
                "query": message,
                "k": request.get("k") or 5,
                "docset": request.get("docset"),
                "version": request.get("version"),
            })

            # A broken corpus does not fail the turn. The model can still answer from
            # what it knows; the console is told the grounding is missing so the answer
            # is read for what it is.
            if "error" in result:
                log.warning("retrieval failed, answering ungrounded: %s", result["error"])
                on_retrieval({"ok": False, "code": result.get("code", "RETRIEVAL_FAILED"),
                              "error": result["error"], "hits": []})
            else:
                hits = [h for h in result["hits"] if h["score"] >= MIN_SCORE]
                on_retrieval({"ok": True, "k": result["k"], "model": result["model"],
                              "took_ms": result["took_ms"], "hits": hits,
                              "dropped": len(result["hits"]) - len(hits)})

            if hits:
                system_prompt = RAG_SYSTEM_PROMPT
                message = (f"Documentation excerpts:\n\n{build_context(hits)}\n\n"
                           f"Question: {message}")

        answer = self._gateway.complete(model, self._gateway.build_messages(message, system_prompt))

        # The console needs to know whether the answer it is showing was grounded, and
        # a turn that asked for grounding and got none is the case worth naming.
        answer["grounded"] = bool(hits)
        if request.get("rag") and not hits:
            answer["grounding"] = "requested but no passage cleared the relevance floor"

        on_answer(answer)
