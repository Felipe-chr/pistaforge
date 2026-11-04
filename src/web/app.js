"use strict";

const state = {
    info: null,
    doc: null,
    cursor: { x: 300, y: 300 },
    bridge: "pending",
    p50: null,
    p95: null,
    history: [],
    view: null,
    measuring: false,
};

const el = (id) => document.getElementById(id);

let requestId = 0;
let refreshToken = 0;

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

function askPath(message, fallback) {
    const dialog = el("path-dialog");
    if (!dialog || typeof dialog.showModal !== "function") {
        try {
            return Promise.resolve(window.prompt(message, fallback));
        } catch {
            setStatus("Este navegador não permite escolher o caminho do arquivo.");
            return Promise.resolve(null);
        }
    }

    el("path-dialog-title").textContent = message;
    const input = el("path-input");
    input.value = fallback;
    dialog.returnValue = "";

    return new Promise((resolve) => {
        const onClose = () => {
            dialog.removeEventListener("close", onClose);
            const value = input.value.trim();
            resolve(dialog.returnValue === "ok" && value ? value : null);
        };
        dialog.addEventListener("close", onClose);
        dialog.showModal();
        input.focus();
        input.select();
    });
}

function docArena() {
    return state.doc?.arena ?? state.info?.arena ?? { width: 4000, height: 3000, margin: 100 };
}

function layerState() {
    return state.doc?.layers ?? { track: true, markers: true, annotations: true, guides: false };
}

function updateFacts() {
    const doc = state.doc;
    const arena = docArena();

    el("doc-elements").textContent = String(doc?.elements?.length ?? 0);
    el("doc-connections").textContent = String(doc?.connections?.length ?? 0);
    el("doc-revision").textContent = String(doc?.revision ?? 0);
    el("doc-cursor").textContent =
        `${Math.round(state.cursor.x)}, ${Math.round(state.cursor.y)}`;

    el("arena-dims").textContent = `${formatMm(arena.width)} × ${formatMm(arena.height)}`;
    el("arena-margin").textContent = formatMm(arena.margin ?? 0);
    el("arena-status").textContent =
        `Arena ${formatMm(arena.width)} × ${formatMm(arena.height)}`;
}

function renderInfo(info) {
    state.info = info;
    el("core-version").textContent = info.version ?? "—";
    el("core-compiler").textContent = info.compiler ?? "—";
    el("core-build").textContent = info.build ?? "—";

    updateFacts();
    draw();
}

async function refreshDoc() {
    const token = ++refreshToken;
    const doc = await rpc("doc.state");
    if (token !== refreshToken) return state.doc;

    state.doc = doc;
    updateFacts();
    syncLayerToggles();
    draw();
    return doc;
}

function syncLayerToggles() {
    const layers = layerState();
    document.querySelectorAll(".layer-toggle").forEach((input) => {
        const key = input.dataset.layer;
        if (key && key in layers) input.checked = Boolean(layers[key]);
    });
}

/* ----------------------------------------------------------------- canvas */

function sampleArc(center, radius, startDeg, sweepDeg) {
    const start = (startDeg * Math.PI) / 180;
    const sweep = (sweepDeg * Math.PI) / 180;
    const steps = Math.max(8, Math.ceil(Math.abs(sweep) / (Math.PI / 36)));
    const points = [];
    for (let i = 0; i <= steps; ++i) {
        const angle = start + (sweep * i) / steps;
        points.push([
            center[0] + radius * Math.cos(angle),
            center[1] + radius * Math.sin(angle),
        ]);
    }
    return points;
}

function sampleCubic(bezier, steps = 40) {
    const { p0, p1, p2, p3 } = bezier;
    const points = [];
    for (let i = 0; i <= steps; ++i) {
        const t = i / steps;
        const mt = 1 - t;
        const a = mt * mt * mt;
        const b = 3 * mt * mt * t;
        const c = 3 * mt * t * t;
        const d = t * t * t;
        points.push([
            a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
            a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1],
        ]);
    }
    return points;
}

function modulePolylines(element) {
    const transform = element.transform ?? {};
    const tx = transform.tx ?? 0;
    const ty = transform.ty ?? 0;
    const rotation = ((transform.rot ?? 0) * Math.PI) / 180;
    const mirrored = Boolean(transform.mirrored);
    const cos = Math.cos(rotation);
    const sin = Math.sin(rotation);

    const apply = ([x, y]) => {
        let localX = x;
        if (mirrored) localX = -localX;
        return [localX * cos - y * sin + tx, localX * sin + y * cos + ty];
    };

    if (element.template === "cross.90") {
        const arm = element.params?.arm ?? 80;
        return [
            [apply([-arm, 0]), apply([arm, 0])],
            [apply([0, -arm]), apply([0, arm])],
        ];
    }

    if (element.template === "curve.90") {
        const radius = element.params?.radius ?? 100;
        return [sampleArc([0, 0], radius, 0, 90).map(apply)];
    }

    return [];
}

function elementPolylines(element) {
    switch (element.type) {
        case "line":
            return [[element.a, element.b]];
        case "cubic":
            return [sampleCubic(element)];
        case "arc":
            return [sampleArc(element.center, element.radius, element.start, element.sweep)];
        case "module":
            return modulePolylines(element);
        default:
            return [];
    }
}

function elementVisible(element, layers) {
    if (element.hidden) return false;
    switch (element.layer) {
        case "track":
            return layers.track !== false;
        case "markers":
            return layers.markers !== false;
        case "annotations":
            return layers.annotations !== false;
        case "guides":
            return layers.guides !== false;
        default:
            return true;
    }
}

function worldToScreen(point) {
    const view = state.view;
    if (!view) return { x: 0, y: 0 };
    return {
        x: view.originX + point.x * view.scale,
        y: view.originY + point.y * view.scale,
    };
}

function tracePolyline(ctx, points) {
    if (!points || points.length < 2) return false;
    ctx.beginPath();
    const first = worldToScreen({ x: points[0][0], y: points[0][1] });
    ctx.moveTo(first.x, first.y);
    for (let i = 1; i < points.length; ++i) {
        const p = worldToScreen({ x: points[i][0], y: points[i][1] });
        ctx.lineTo(p.x, p.y);
    }
    return true;
}

function drawElements(ctx, doc, layers, scale) {
    for (const element of doc.elements ?? []) {
        if (!elementVisible(element, layers)) continue;

        const polylines = elementPolylines(element);
        if (polylines.length === 0) continue;

        const widthPx = Math.max(2, (element.width ?? 19) * scale);

        ctx.save();
        ctx.strokeStyle = "#0f172a";
        ctx.lineWidth = widthPx;
        ctx.lineCap = "round";
        ctx.lineJoin = "round";
        for (const points of polylines) {
            if (tracePolyline(ctx, points)) ctx.stroke();
        }
        ctx.restore();

        ctx.save();
        ctx.fillStyle = "#2563eb";
        ctx.strokeStyle = "#ffffff";
        ctx.lineWidth = 1;
        for (const points of polylines) {
            if (!points || points.length < 2) continue;
            for (const point of [points[0], points[points.length - 1]]) {
                const p = worldToScreen({ x: point[0], y: point[1] });
                ctx.beginPath();
                ctx.arc(p.x, p.y, 2.4, 0, Math.PI * 2);
                ctx.fill();
                ctx.stroke();
            }
        }
        ctx.restore();
    }
}

function drawMarkers(ctx, doc, layers) {
    if (layers.markers === false) return;

    for (const marker of doc.markers ?? []) {
        const p = worldToScreen({ x: marker.x ?? 0, y: marker.y ?? 0 });
        ctx.save();
        ctx.translate(p.x, p.y);
        ctx.rotate(((marker.angle ?? 0) * Math.PI) / 180);
        ctx.fillStyle = "#dc2626";
        ctx.beginPath();
        ctx.moveTo(0, -6);
        ctx.lineTo(6, 0);
        ctx.lineTo(0, 6);
        ctx.lineTo(-6, 0);
        ctx.closePath();
        ctx.fill();
        ctx.restore();

        if (marker.text) {
            ctx.fillStyle = "#7f1d1d";
            ctx.font = '600 10px "Segoe UI", sans-serif';
            ctx.textAlign = "left";
            ctx.textBaseline = "middle";
            ctx.fillText(marker.text, p.x + 9, p.y);
        }
    }
}

function drawGuides(ctx, doc, layers) {
    if (layers.guides !== true) return;

    ctx.save();
    ctx.setLineDash([5, 4]);
    ctx.strokeStyle = "#7c3aed";
    ctx.lineWidth = 1;
    for (const guide of doc.guides ?? []) {
        if (tracePolyline(ctx, [guide.a, guide.b])) ctx.stroke();
    }
    ctx.restore();
}

function drawCursor(ctx) {
    const p = worldToScreen(state.cursor);
    ctx.save();
    ctx.strokeStyle = "#2563eb";
    ctx.fillStyle = "#2563eb";
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.arc(p.x, p.y, 6, 0, Math.PI * 2);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(p.x - 12, p.y);
    ctx.lineTo(p.x - 7, p.y);
    ctx.moveTo(p.x + 7, p.y);
    ctx.lineTo(p.x + 12, p.y);
    ctx.moveTo(p.x, p.y - 12);
    ctx.lineTo(p.x, p.y - 7);
    ctx.moveTo(p.x, p.y + 7);
    ctx.lineTo(p.x, p.y + 12);
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(p.x, p.y, 1.8, 0, Math.PI * 2);
    ctx.fill();

    ctx.font = '600 10px "Cascadia Mono", Consolas, monospace';
    ctx.textAlign = "left";
    ctx.textBaseline = "bottom";
    ctx.fillText(
        `cursor ${Math.round(state.cursor.x)}, ${Math.round(state.cursor.y)}`,
        p.x + 10,
        p.y - 8
    );
    ctx.restore();
}

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

    const arena = docArena();
    const padding = 46;
    const scale = Math.min(
        (width - padding * 2) / arena.width,
        (height - padding * 2) / arena.height
    );

    if (!(scale > 0)) return;

    const arenaWidth = arena.width * scale;
    const arenaHeight = arena.height * scale;
    const originX = (width - arenaWidth) / 2;
    const originY = (height - arenaHeight) / 2;

    state.view = { scale, originX, originY, width, height, arenaWidth, arenaHeight };

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
    if (arena.show_dimensions !== false) {
        ctx.fillStyle = "#475569";
        ctx.font = '600 12px "Segoe UI", sans-serif';
        ctx.textAlign = "center";
        ctx.textBaseline = "bottom";
        ctx.fillText(`${formatMm(arena.width)}`, originX + arenaWidth / 2, originY - 10);

        ctx.save();
        ctx.translate(originX - 14, originY + arenaHeight / 2);
        ctx.rotate(-Math.PI / 2);
        ctx.textAlign = "center";
        ctx.textBaseline = "top";
        ctx.fillText(`${formatMm(arena.height)}`, 0, 0);
        ctx.restore();
    }

    // Origem.
    ctx.fillStyle = "#64748b";
    ctx.font = '500 10px "Cascadia Mono", Consolas, monospace';
    ctx.textAlign = "left";
    ctx.textBaseline = "top";
    ctx.fillText("0,0", originX + 6, originY + 6);

    const doc = state.doc;
    const layers = layerState();

    if (doc) {
        drawGuides(ctx, doc, layers);
        drawElements(ctx, doc, layers, scale);
        drawMarkers(ctx, doc, layers);
    }

    drawCursor(ctx);

    // Aviso de área vazia.
    if (!doc || (doc.elements?.length ?? 0) === 0) {
        ctx.fillStyle = "#94a3b8";
        ctx.font = '600 13px "Segoe UI", sans-serif';
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(
            "clique em uma curva na faixa Inserir",
            originX + arenaWidth / 2,
            originY + arenaHeight / 2 - 8
        );
        ctx.font = '400 11px "Segoe UI", sans-serif';
        ctx.fillText(
            "ou clique na arena para posicionar o cursor",
            originX + arenaWidth / 2,
            originY + arenaHeight / 2 + 10
        );
    }
}

/* --------------------------------------------------------------- insertion */

function fitCursor(dx, dy, up = 0) {
    const arena = docArena();
    const fits =
        state.cursor.x + dx <= arena.width - 40 &&
        state.cursor.y + dy <= arena.height - 40 &&
        state.cursor.y - up >= 40;
    if (fits) return;

    let y = state.cursor.y + 320;
    if (y + dy > arena.height - 40) y = 300;
    state.cursor = { x: 300, y };
}

function advanceCursor(end) {
    const arena = docArena();
    let x = end.x;
    let y = end.y;

    if (x > arena.width - 40 || y > arena.height - 40) {
        x = 300;
        y += 320;
        if (y > arena.height - 40) y = 300;
    }

    state.cursor = { x, y };
}

async function afterInsert(id, end, label) {
    state.history.push({ id, cursor: { ...state.cursor } });
    advanceCursor(end);
    await refreshDoc();
    setStatus(
        `Elemento ${id} (${label}) inserido · cursor em ` +
            `${Math.round(state.cursor.x)}, ${Math.round(state.cursor.y)} mm`
    );
}

async function insertLine() {
    fitCursor(400, 0);
    const a = { ...state.cursor };
    const b = { x: a.x + 400, y: a.y };
    const result = await rpc("doc.addLine", {
        a: [a.x, a.y],
        b: [b.x, b.y],
        width: 19,
    });
    await afterInsert(result.id, b, "Linha");
}

async function insertBezier() {
    fitCursor(400, 120, 120);
    const p0 = { ...state.cursor };
    const p1 = { x: p0.x + 130, y: p0.y - 120 };
    const p2 = { x: p0.x + 270, y: p0.y + 120 };
    const p3 = { x: p0.x + 400, y: p0.y };
    const result = await rpc("doc.addCubic", {
        p0: [p0.x, p0.y],
        p1: [p1.x, p1.y],
        p2: [p2.x, p2.y],
        p3: [p3.x, p3.y],
        width: 19,
    });
    await afterInsert(result.id, p3, "Bézier");
}

async function insertArc90() {
    fitCursor(200, 200);
    const start = { ...state.cursor };
    const radius = 200;
    const center = { x: start.x, y: start.y + radius };
    const end = { x: start.x + radius, y: start.y + radius };
    const result = await rpc("doc.addArc", {
        center: [center.x, center.y],
        radius,
        start_deg: -90,
        sweep_deg: 90,
        width: 19,
    });
    await afterInsert(result.id, end, "Arco 90°");
}

async function insertCurve90() {
    fitCursor(100, 100);
    const start = { ...state.cursor };
    const radius = 100;
    const end = { x: start.x + radius, y: start.y + radius };
    const result = await rpc("doc.addModule", {
        template: "curve.90",
        params: { radius, width: 19 },
        transform: {
            tx: start.x,
            ty: start.y + radius,
            rot_deg: -90,
            mirrored: false,
        },
        width: 19,
    });
    await afterInsert(result.id, end, "Curva 90°");
}

async function insertArc180() {
    fitCursor(400, 0, 200);
    const start = { ...state.cursor };
    const radius = 200;
    const center = { x: start.x + radius, y: start.y };
    const end = { x: start.x + radius * 2, y: start.y };
    const result = await rpc("doc.addArc", {
        center: [center.x, center.y],
        radius,
        start_deg: 180,
        sweep_deg: 180,
        width: 19,
    });
    await afterInsert(result.id, end, "Curva 180°");
}

async function insertS() {
    fitCursor(400, 120, 120);
    const p0 = { ...state.cursor };
    const p1 = { x: p0.x + 100, y: p0.y + 120 };
    const p2 = { x: p0.x + 300, y: p0.y - 120 };
    const p3 = { x: p0.x + 400, y: p0.y };
    const result = await rpc("doc.addCubic", {
        p0: [p0.x, p0.y],
        p1: [p1.x, p1.y],
        p2: [p2.x, p2.y],
        p3: [p3.x, p3.y],
        width: 19,
    });
    await afterInsert(result.id, p3, "Curva S");
}

async function insertCross() {
    fitCursor(160, 80, 80);
    const center = { ...state.cursor };
    const arm = 80;
    const end = { x: center.x + arm * 2, y: center.y };
    const result = await rpc("doc.addModule", {
        template: "cross.90",
        params: { arm, width: 19 },
        transform: {
            tx: center.x,
            ty: center.y,
            rot_deg: 0,
            mirrored: false,
        },
        width: 19,
    });
    await afterInsert(result.id, end, "Cruzamento");
}

async function undo() {
    const entry = state.history.pop();
    if (!entry) {
        setStatus("Nada para desfazer.");
        return;
    }

    await rpc("doc.removeElement", { id: entry.id });
    state.cursor = entry.cursor;
    await refreshDoc();
    setStatus(`Elemento ${entry.id} removido.`);
}

async function newDocument() {
    await rpc("doc.new");
    state.history = [];
    state.cursor = { x: 300, y: 300 };
    await refreshDoc();
    setStatus("Documento novo · arena 4 × 3 m.");
}

async function saveDocument() {
    let path = state.doc?.path ?? null;
    if (!path) {
        path = await askPath("Salvar documento como:", "C:/Temp/pistaforge.pftrack");
        if (!path) return;
    }

    const result = await rpc("doc.save", { path });
    await refreshDoc();
    setStatus(`Salvo em ${result.path}`);
}

async function openDocument() {
    const path = await askPath("Abrir documento:", "C:/Temp/pistaforge.pftrack");
    if (!path) return;

    await rpc("doc.load", { path });
    state.history = [];
    state.cursor = { x: 300, y: 300 };
    await refreshDoc();
    setStatus(`Aberto: ${path}`);
}

async function setArena(width, height) {
    await rpc("doc.setArena", { width, height, margin: 100 });
    state.history = [];
    state.cursor = { x: 300, y: 300 };
    await refreshDoc();
    setStatus(`Arena ${formatMm(width)} × ${formatMm(height)}.`);
}

async function setLayer(key, visible) {
    if (!state.doc) return;
    const result = await rpc("doc.setLayers", { [key]: visible });
    state.doc.layers = result.layers;
    syncLayerToggles();
    updateFacts();
    draw();

    const labels = {
        track: "Traçado",
        markers: "Marcadores",
        annotations: "Anotações",
        guides: "Guias",
    };
    setStatus(`Camada ${labels[key] ?? key} ${visible ? "visível" : "oculta"}.`);
}

/* ------------------------------------------------------------- interactions */

async function measurePing() {
    const start = performance.now();
    await rpc("app.ping", { at: new Date().toISOString() });
    const elapsed = performance.now() - start;
    setStatus(`pong em ${elapsed.toFixed(2)} ms`);
}

async function measureLatency() {
    if (state.measuring) return;
    state.measuring = true;

    const samples = [];
    const rounds = 100;

    el("ping").disabled = true;
    el("latency").disabled = true;

    try {
        for (let i = 0; i < rounds; ++i) {
            const start = performance.now();
            await rpc("app.ping", { i });
            samples.push(performance.now() - start);
            if (i % 20 === 0) {
                setStatus(`medindo latência… ${i}/${rounds}`);
            }
        }
    } finally {
        el("ping").disabled = false;
        el("latency").disabled = false;
        state.measuring = false;
    }

    samples.sort((a, b) => a - b);
    const percentile = (p) =>
        samples[Math.min(samples.length - 1, Math.floor(p * samples.length))];

    state.p50 = percentile(0.5);
    state.p95 = percentile(0.95);

    el("latency-result").textContent =
        `${state.p50.toFixed(2)} / ${state.p95.toFixed(2)} ms`;
    setStatus(
        `latência: p50 ${state.p50.toFixed(2)} ms · p95 ${state.p95.toFixed(2)} ms`
    );
}

function activateTab(name) {
    document.querySelectorAll(".ribbon-tab").forEach((tab) => {
        const active = tab.dataset.tab === name;
        tab.classList.toggle("active", active);
        tab.setAttribute("aria-selected", String(active));
    });
    document.querySelectorAll(".ribbon-panel").forEach((panel) => {
        panel.classList.toggle("active", panel.dataset.panel === name);
    });
}

function canvasToWorld(event) {
    const view = state.view;
    if (!view) return null;

    const rect = el("canvas").getBoundingClientRect();
    return {
        x: (event.clientX - rect.left - view.originX) / view.scale,
        y: (event.clientY - rect.top - view.originY) / view.scale,
    };
}

function handleCanvasClick(event) {
    const point = canvasToWorld(event);
    if (!point) return;

    const arena = docArena();
    if (point.x < 0 || point.x > arena.width || point.y < 0 || point.y > arena.height) {
        return;
    }

    state.cursor = {
        x: Math.round(point.x),
        y: Math.round(point.y),
    };
    updateFacts();
    draw();
    setStatus(`Cursor em ${state.cursor.x}, ${state.cursor.y} mm.`);
}

async function runAction(action) {
    switch (action) {
        case "insert-line":
            return insertLine();
        case "insert-bezier":
            return insertBezier();
        case "insert-arc90":
            return insertArc90();
        case "insert-curve90":
            return insertCurve90();
        case "insert-arc180":
            return insertArc180();
        case "insert-s":
            return insertS();
        case "insert-cross":
            return insertCross();
        case "undo":
            return undo();
        case "new":
            return newDocument();
        case "open":
            return openDocument();
        case "save":
            return saveDocument();
        case "arena-default":
            return setArena(4000, 3000);
        case "arena-3x4":
            return setArena(3000, 4000);
        case "ping":
            return measurePing();
        case "latency":
            return measureLatency();
        default:
            setStatus(`Ação desconhecida: ${action}`);
            return undefined;
    }
}

/* -------------------------------------------------------------------- init */

async function init() {
    setStatus("Conectando ao núcleo C++…");

    document.addEventListener("click", (event) => {
        const tab = event.target.closest(".ribbon-tab");
        if (tab) {
            activateTab(tab.dataset.tab);
            return;
        }

        const actionElement = event.target.closest("[data-action]");
        if (!actionElement) return;

        event.preventDefault();
        runAction(actionElement.dataset.action).catch((error) => {
            setStatus(String(error.message ?? error));
        });
    });

    document.addEventListener("change", (event) => {
        const input = event.target.closest(".layer-toggle");
        if (!input) return;
        setLayer(input.dataset.layer, input.checked).catch((error) => {
            setStatus(String(error.message ?? error));
        });
    });

    el("canvas").addEventListener("click", handleCanvasClick);

    try {
        await waitForBridge();
        setBridgeState("online", "conectada");

        const info = await rpc("app.info");
        renderInfo(info);

        el("ping").disabled = false;
        el("latency").disabled = false;

        await refreshDoc();
        syncLayerToggles();
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
