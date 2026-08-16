"""Corpus retrieval — the reason this daemon is Python.

The daemon is Python because this step is: turning a question into a vector needs the
embedding model. Everything downstream (pgvector, ranking, filtering) a C++ daemon
could do perfectly well, but the query has to be embedded first, and that decides
where the whole service lives.

The corpus itself is built elsewhere — crawl, chunk and embed are batch jobs in
prisma-rag, not daemon work. ragd only reads what they produced.
"""

import json
import logging
import os
import threading
import time
from pathlib import Path

import psycopg

log = logging.getLogger("inferd.retrieval")

DEFAULT_MODEL = "intfloat/multilingual-e5-small"
MAX_K = 20

# Prefixes are per-model and not optional: e5 was trained with "query:"/"passage:",
# bge-en wants a query-side instruction, bge-m3 wants nothing. Getting this wrong is
# silent — retrieval just quietly gets worse — so it lives in one table.
_QUERY_PREFIX = [
    ("bge-m3", ""),
    ("e5", "query: "),
    ("bge", "Represent this sentence for searching relevant passages: "),
]


def query_prefix(model_name):
    n = (model_name or "").lower()
    for key, prefix in _QUERY_PREFIX:
        if key in n:
            return prefix
    return ""


class RetrievalService:
    def __init__(self, config):
        self._model_name = config.get("model", DEFAULT_MODEL)
        self._table = config.get("table", "rag_chunk")
        self._db = config.get("database", {})
        self._encoder = None
        self._ready = threading.Event()

    @property
    def ready(self):
        return self._ready.is_set()

    def start(self):
        """Warm up off the main thread, so the daemon can register with ipcd at once.

        Loading the encoder takes ~10s. Blocking on it before registering left a window
        where ipcd had no route to this daemon, and ipcd drops an unroutable frame
        without telling the sender — so a turn sent during startup vanished and the
        browser waited on a ticket that would never resolve.

        Registering first closes that window. A turn arriving mid-warmup is still
        answerable: only retrieval needs the encoder, so an ungrounded turn is served
        normally and a grounded one degrades to ungrounded with the reason attached.
        """
        threading.Thread(target=self._warm_up, name="encoder-warmup", daemon=True).start()

    def _warm_up(self):
        from sentence_transformers import SentenceTransformer

        log.info("loading encoder %s (CPU)", self._model_name)
        t0 = time.perf_counter()
        try:
            self._encoder = SentenceTransformer(self._model_name, device="cpu")
        except Exception:
            # Retrieval stays unavailable and every turn goes ungrounded, which is a
            # far better outcome than a daemon that refuses to answer at all.
            log.exception("encoder failed to load — retrieval will be unavailable")
            return
        log.info("encoder ready in %.1fs", time.perf_counter() - t0)

        rows = self.corpus_rows()
        log.info("corpus: %s rows in %s", rows if rows is not None else "unreachable", self._table)
        self._ready.set()

    def _dsn(self):
        pw = os.environ.get("PZ_PG_PASSWORD", "")
        return (f"host={self._db.get('host', '127.0.0.1')} port={self._db.get('port', 5432)} "
                f"dbname={self._db.get('name', 'pretzel_rag')} "
                f"user={self._db.get('user', 'pretzel')} password={pw}")

    def corpus_rows(self):
        try:
            with psycopg.connect(self._dsn(), connect_timeout=3) as c:
                return c.execute(f"SELECT count(*) FROM {self._table}").fetchone()[0]
        except Exception as e:
            log.warning("corpus unreachable: %s", e)
            return None

    def retrieve(self, request):
        """request: {query, k?, docset?, version?} → the response payload, verbatim.

        Failures are returned as a payload rather than raised: mgmtd is waiting on a
        ticket, and a daemon that answers "the corpus is down" is far more useful to
        the operator than one that answers nothing.
        """
        if not self.ready:
            return {"error": "the corpus is still warming up", "code": "NOT_READY"}

        query = (request.get("query") or "").strip()
        if not query:
            return {"error": "query is required", "code": "BAD_REQUEST"}

        # Clamped, not rejected: k is a UI control, and a slider that errors at its own
        # maximum is worse than one that saturates.
        try:
            k = max(1, min(MAX_K, int(request.get("k") or 5)))
        except (TypeError, ValueError):
            k = 5

        t0 = time.perf_counter()
        try:
            vec = self._encoder.encode([query_prefix(self._model_name) + query],
                                       normalize_embeddings=True)[0]
            literal = "[" + ",".join(f"{x:.7g}" for x in vec) + "]"

            where, params = ["TRUE"], []
            if request.get("docset"):
                where.append("docset = %s")
                params.append(request["docset"])
            if request.get("version"):
                where.append("version = %s")
                params.append(request["version"])

            sql = (f"SELECT title, url, docset, version, heading_path, text, n_chars, "
                   f"       1 - (embedding <=> %s::vector) AS score "
                   f"FROM {self._table} WHERE {' AND '.join(where)} "
                   f"ORDER BY embedding <=> %s::vector LIMIT %s")

            with psycopg.connect(self._dsn(), connect_timeout=3) as conn:
                rows = conn.execute(sql, [literal, *params, literal, k]).fetchall()
        except Exception as e:
            log.error("retrieval failed: %s: %s", type(e).__name__, e)
            return {"error": f"{type(e).__name__}: {e}", "code": "RETRIEVAL_FAILED"}

        hits = [{
            "title": t, "url": u, "docset": d, "version": v or "",
            "heading_path": hp or "", "text": txt, "n_chars": n, "score": s,
        } for t, u, d, v, hp, txt, n, s in rows]

        # The query is not logged. It is whatever someone typed, and this log is read by
        # people with no business reading it; the shape of the result is enough to debug.
        took = round((time.perf_counter() - t0) * 1000, 1)
        log.debug("retrieval served (k=%d, hits=%d, took_ms=%s)", k, len(hits), took)

        return {"k": k, "model": self._model_name, "took_ms": took, "hits": hits}
