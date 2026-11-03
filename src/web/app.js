"use strict";

const state = {
    info: null,
    bridge: "pending",
    p50: null,
    p95: null,
};

const el = (id) => document.getElementById(id);

let requestId = 0;

function bridgeAvailable() {
    return typeof window.app_call === "function";
}

async function waitForBridge(timeoutMs = 6000) {
    const start = performance.now();
    while (!bridgeAvailable()) {
        if (performance.now() - start > timeoutMs) {
            throw new Error("A ponte FastWebUI não inicializou.");
        }
        await new Promise((resolve) => setTimeout(resolve, 25));
    }
}

async function rpc(method, params = {}) {
    const id = ++requestId;
    const payload = JSON.stringify({ id, method, params });
    const maybePromise = window.app_call(payload);
    const raw =
        maybePromise && typeof maybePromise.then === "function"
            ? await maybePromise
            : maybePromise;
    const message = JSON.parse(raw);

    if (!message.ok) {
        const code = message.error?.code ?? "error";
        const text = message.error?.message ?? "Erro desconhecido";
        throw new Error(`${code}: ${text}`);
    }
    return message.result;
}

function setBridgeState(kind, text) {
    const node = el("bridge-state");
    node.className = `state ${kind}`;
    node.textContent = text;
    state.bridge = kind;
}

function setStatus(text) {
    el("status").textContent = text;
}

function formatMm(value) {
    return `${Number(value).toLocaleString("pt-BR", { maximumFractionDigits: 0 })} mm`;
}

function renderInfo(info) {
    state.info = info;
    el("core-version").textContent = info.version ?? "—";
    el("core-compiler").textContent = info.compiler ?? "—";
    el("core-build").textContent = info.build ?? "—";

    const arena = info.arena ?? { width: 4000, height: 3000, margin: 100 };
    el("arena-dims").textContent = `${formatMm(arena.width)} × ${formatMm(arena.height)}`;
    el("arena-margin").textContent = formatMm(arena.margin);
    el("arena-status").textContent = `Arena ${formatMm(arena.width)} × ${formatMm(arena.height)}`;

    draw();
}

/* ----------------------------------------------------------------- canvas */

function draw() {
    const canvas = el("canvas");
    if (!canvas) return;

    const ctx = canvas.getContext("2d");
    const dpr = window.devicePixelRatio || 1;
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    if (width <= 0 || height <= 0) return;

    const pixelWidth = Math.round(width * dpr);
    const pixelHeight = Math.round(height * dpr);
    if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
        canvas.width = pixelWidth;
        canvas.height = pixelHeight;
    }

    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, width, height);

    const arena = state.info?.arena ?? { width: 4000, height: 3000, margin: 100 };
    const padding = 46;
    const scale = Math.min(
        (width - padding * 2) / arena.width,
        (height - padding * 2) / arena.height
    );

    if (scale <= 0) return;

    const arenaWidth = arena.width * scale;
    const arenaHeight = arena.height * scale;
    const originX = (width - arenaWidth) / 2;
    const originY = (height - arenaHeight) / 2;

    // Fundo da prancheta.
    ctx.fillStyle = "#f1f4f9";
    ctx.fillRect(0, 0, width, height);

    // Sombra suave da arena.
    ctx.save();
    ctx.shadowColor = "rgba(15, 23, 42, 0.12)";
    ctx.shadowBlur = 18;
    ctx.shadowOffsetY = 6;
    ctx.fillStyle = "#ffffff";
    ctx.fillRect(originX, originY, arenaWidth, arenaHeight);
    ctx.restore();

    // Grade.
    ctx.save();
    ctx.beginPath();
    ctx.rect(originX, originY, arenaWidth, arenaHeight);
    ctx.clip();

    const candidates = [10, 20, 50, 100, 200, 500, 1000];
    const minorStep = candidates.find((step) => step * scale >= 10) ?? 1000;
    const majorStep = minorStep * 10;

    const drawGrid = (step, color, lineWidth) => {
        ctx.beginPath();
        for (let x = 0; x <= arena.width + 1e-6; x += step) {
            const px = originX + x * scale;
            ctx.moveTo(px, originY);
            ctx.lineTo(px, originY + arenaHeight);
        }
        for (let y = 0; y <= arena.height + 1e-6; y += step) {
            const py = originY + y * scale;
            ctx.moveTo(originX, py);
            ctx.lineTo(originX + arenaWidth, py);
        }
        ctx.strokeStyle = color;
        ctx.lineWidth = lineWidth;
        ctx.stroke();
    };

    drawGrid(minorStep, "#eef2f7", 1);
    drawGrid(majorStep, "#dde5ee", 1);
    ctx.restore();

    // Contorno da arena.
    ctx.strokeStyle = "#94a3b8";
    ctx.lineWidth = 1.5;
    ctx.strokeRect(originX + 0.5, originY + 0.5, arenaWidth - 1, arenaHeight - 1);

    // Margem de segurança.
    if (arena.margin > 0) {
        const margin = arena.margin * scale;
        ctx.save();
        ctx.setLineDash([7, 5]);
        ctx.strokeStyle = "#f59e0b";
        ctx.lineWidth = 1.2;
        ctx.strokeRect(
            originX + margin,
            originY + margin,
            arenaWidth - margin * 2,
            arenaHeight - margin * 2
        );
        ctx.restore();
    }

    // Cotas.
    ctx.fillStyle = "#475569";
    ctx.font = '600 12px "Segoe UI", sans-serif';
    ctx.textAlign = "center";
    ctx.textBaseline = "bottom";
    ctx.fillText(
        `${formatMm(arena.width)}`,
        originX + arenaWidth / 2,
        originY - 10
    );

    ctx.save();
    ctx.translate(originX - 14, originY + arenaHeight / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.textAlign = "center";
    ctx.textBaseline = "top";
    ctx.fillText(`${formatMm(arena.height)}`, 0, 0);
    ctx.restore();

    // Origem.
    ctx.fillStyle = "#64748b";
    ctx.font = '500 10px "Cascadia Mono", Consolas, monospace';
    ctx.textAlign = "left";
    ctx.textBaseline = "top";
    ctx.fillText("0,0", originX + 6, originY + 6);

    // Aviso de área vazia.
    ctx.fillStyle = "#cbd5e1";
    ctx.font = '600 13px "Segoe UI", sans-serif';
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText("área de desenho", originX + arenaWidth / 2, originY + arenaHeight / 2 - 8);
    ctx.font = '400 11px "Segoe UI", sans-serif';
    ctx.fillText(
        "as ferramentas de traçado chegam no M2",
        originX + arenaWidth / 2,
        originY + arenaHeight / 2 + 10
    );
}

/* ------------------------------------------------------------- interactions */

async function measurePing() {
    const start = performance.now();
    await rpc("app.ping", { at: new Date().toISOString() });
    const elapsed = performance.now() - start;
    setStatus(`pong em ${elapsed.toFixed(2)} ms`);
}

async function measureLatency() {
    const samples = [];
    const rounds = 100;

    el("ping").disabled = true;
    el("latency").disabled = true;

    for (let i = 0; i < rounds; ++i) {
        const start = performance.now();
        await rpc("app.ping", { i });
        samples.push(performance.now() - start);
        if (i % 20 === 0) {
            setStatus(`medindo latência… ${i}/${rounds}`);
        }
    }

    samples.sort((a, b) => a - b);
    const percentile = (p) =>
        samples[Math.min(samples.length - 1, Math.floor(p * samples.length))];

    state.p50 = percentile(0.5);
    state.p95 = percentile(0.95);

    el("latency-result").textContent = `${state.p50.toFixed(2)} / ${state.p95.toFixed(2)} ms`;
    el("ping").disabled = false;
    el("latency").disabled = false;
    setStatus(`latência: p50 ${state.p50.toFixed(2)} ms · p95 ${state.p95.toFixed(2)} ms`);
}

/* -------------------------------------------------------------------- init */

async function init() {
    setStatus("Conectando ao núcleo C++…");

    try {
        await waitForBridge();
        setBridgeState("online", "conectada");

        const info = await rpc("app.info");
        renderInfo(info);

        el("ping").disabled = false;
        el("latency").disabled = false;
        el("ping").addEventListener("click", () => {
            measurePing().catch((error) => setStatus(String(error.message ?? error)));
        });
        el("latency").addEventListener("click", () => {
            measureLatency().catch((error) => {
                setStatus(String(error.message ?? error));
                el("ping").disabled = false;
                el("latency").disabled = false;
            });
        });

        setStatus(`Pronto · ${info.name} ${info.version} · ${info.compiler}`);
    } catch (error) {
        setBridgeState("offline", "indisponível");
        setStatus(`Ponte indisponível: ${error.message ?? error}`);
        draw();
    }
}

const observer = new ResizeObserver(() => draw());
observer.observe(el("canvas").parentElement);
window.addEventListener("resize", draw);

init();
