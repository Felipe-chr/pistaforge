"use strict";

const state = {
    info: null,
    doc: null,
    cursor: { x: 300, y: 300 },
    bridge: "pending",
    p50: null,
    p95: null,
    view: null,
    measuring: false,
    selection: null,
    tool: "select",
    zoom: 1,
    pan: { x: 0, y: 0 },
    drag: null,
    snap: null,
    placement: null,
    analysis: null,
    robotVision: false,
    ribbon: { elementId: null, kind: null, info: null, token: 0 },
    propertiesClipboard: null,
    validation: null,
    savedSignature: null,
    dirty: false,
    exportOptions: {},
};

const el = (id) => document.getElementById(id);

let requestId = 0;
let refreshToken = 0;
let spaceDown = false;

const LAYER_LABELS = {
    track: "Traçado",
    markers: "Marcadores",
    annotations: "Anotações",
    guides: "Guias",
};

const TOOL_LABELS = {
    select: "Selecionar",
    line: "Reta",
    bezier: "Bézier",
};

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

function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

function isTypingTarget(target) {
    return Boolean(
        target &&
            (target.closest?.("input, textarea, select") ||
                target.isContentEditable)
    );
}

async function askPath(message, fallback) {
    const dialog = el("path-dialog");
    if (!dialog || typeof dialog.showModal !== "function") {
        try {
            return window.prompt(message, fallback);
        } catch {
            setStatus("Este navegador não permite escolher o caminho do arquivo.");
            return null;
        }
    }

    el("path-dialog-title").textContent = message;
    const input = el("path-input");
    input.value = fallback;

    const result = await askDialog("path-dialog", () => {
        input.focus();
        input.select();
    });
    const value = input.value.trim();
    return result === "ok" && value ? value : null;
}

function docArena() {
    return state.doc?.arena ?? state.info?.arena ?? { width: 4000, height: 3000, margin: 100 };
}

/* ------------------------------------------- estado de salvamento e diálogos */

/// Assinatura estável do conteúdo editável (ignora revisão, caminho e ids livres).
function docSignature(doc) {
    if (!doc) return null;
    return JSON.stringify({
        arena: doc.arena,
        elements: doc.elements,
        connections: doc.connections,
        markers: doc.markers,
        guides: doc.guides,
        layers: doc.layers,
    });
}

function updateDirtyState() {
    const signature = docSignature(state.doc);
    state.dirty = Boolean(signature) && signature !== state.savedSignature;
}

function markSaved() {
    state.savedSignature = docSignature(state.doc);
    state.dirty = false;
    updateFacts();
}

/// Abre um `<dialog>` de formulário e resolve com o `returnValue` ("ok", "cancel", …).
///
/// O evento `close` não é confiável em todos os WebViews, então a resolução
/// acontece no clique de um botão com `value`; `cancel` (Esc) e `close` ficam
/// como fallback. Cliques cancelados por validação (`preventDefault`) mantêm o
/// diálogo aberto.
function askDialog(id, onOpen) {
    const dialog = el(id);
    if (!dialog || typeof dialog.showModal !== "function") return Promise.resolve(null);

    dialog.returnValue = "";
    return new Promise((resolve) => {
        let settled = false;
        const finish = (value) => {
            if (settled) return;
            settled = true;
            dialog.removeEventListener("click", onClick);
            dialog.removeEventListener("cancel", onCancel);
            dialog.removeEventListener("close", onClose);
            if (dialog.open) dialog.close();
            resolve(value || "cancel");
        };
        const onClick = (event) => {
            if (event.defaultPrevented) return;
            const button = event.target.closest("button[value]");
            if (!button || !dialog.contains(button)) return;
            event.preventDefault();
            finish(button.value);
        };
        const onCancel = () => finish("cancel");
        const onClose = () => finish(dialog.returnValue);

        dialog.addEventListener("click", onClick);
        dialog.addEventListener("cancel", onCancel);
        dialog.addEventListener("close", onClose);
        dialog.showModal();
        if (typeof onOpen === "function") onOpen(dialog);
    });
}

async function askUnsaved() {
    const dialog = el("confirm-dialog");
    if (!dialog || typeof dialog.showModal !== "function") {
        return window.confirm("Há alterações não salvas. Descartar?")
            ? "discard"
            : "cancel";
    }
    return (await askDialog("confirm-dialog")) ?? "cancel";
}

/// Devolve `true` quando é seguro descartar o documento atual.
async function confirmDiscard() {
    if (!state.dirty) return true;

    const choice = await askUnsaved();
    if (choice === "cancel") {
        setStatus("Operação cancelada · alterações mantidas.");
        return false;
    }
    if (choice === "save") {
        await saveDocument();
        if (state.dirty) {
            setStatus("Salvamento cancelado · alterações mantidas.");
            return false;
        }
    }
    return true;
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

    const path = doc?.path ?? null;
    const fileNode = el("doc-file");
    fileNode.textContent = path ? path.split(/[\\/]/).pop() : "sem título";
    fileNode.title = path ?? "Documento ainda não salvo";
    const dirtyNode = el("doc-dirty");
    dirtyNode.textContent = state.dirty ? "não salvo" : "salvo";
    dirtyNode.className = state.dirty ? "dirty" : "";
    el("zoom-status").textContent = `${Math.round(state.zoom * 100)}%`;

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
    updateDirtyState();
    if (
        state.selection != null &&
        !(doc.elements ?? []).some((element) => element.id === state.selection)
    ) {
        state.selection = null;
    }
    updateFacts();
    updateSelectionPanel();
    syncLayerToggles();
    draw();
    await refreshAnalysis();
    try {
        await updateRibbon();
    } catch {
        hideRibbon();
    }
    if (state.robotVision) draw();
    return doc;
}

function syncLayerToggles() {
    const layers = layerState();
    document.querySelectorAll(".layer-toggle").forEach((input) => {
        const key = input.dataset.layer;
        if (key && key in layers) input.checked = Boolean(layers[key]);
    });
}

function updateSelectionPanel() {
    const element = state.doc?.elements?.find(
        (candidate) => candidate.id === state.selection
    );

    if (!element) {
        el("selection-id").textContent = "—";
        el("selection-type").textContent = "—";
        el("selection-layer").textContent = "—";
        el("selection-width").textContent = "—";
        el("selection-hint").textContent = "Clique em um elemento para selecioná-lo.";
        return;
    }

    const typeLabels = {
        line: "Reta",
        cubic: "Bézier",
        arc: "Arco",
        module: element.template ? `Módulo ${element.template}` : "Módulo",
    };

    el("selection-id").textContent = String(element.id);
    el("selection-type").textContent = typeLabels[element.type] ?? element.type;
    el("selection-layer").textContent = LAYER_LABELS[element.layer] ?? element.layer;
    el("selection-width").textContent = formatMm(element.width ?? 19);
    el("selection-hint").textContent =
        "Arraste as alças para editar a geometria · arraste o traçado para mover · Del exclui.";
}

/* ------------------------------------------- análise, validação e mini-ribbon */

function difficultyKey(label) {
    switch (label) {
        case "Fácil":
            return "facil";
        case "Médio":
            return "medio";
        case "Difícil":
            return "dificil";
        case "Extremo":
            return "extremo";
        default:
            return "";
    }
}

async function refreshAnalysis() {
    try {
        state.analysis = await rpc("scene.analysis");
    } catch {
        state.analysis = null;
    }
    renderAnalysis();
}

function renderAnalysis() {
    const metrics = state.analysis;
    if (!metrics) return;

    el("analysis-length").textContent =
        `${(metrics.total_length_mm / 1000).toFixed(2)} m`;
    el("analysis-straights").textContent = String(metrics.straight_count ?? 0);
    el("analysis-curves").textContent = String(metrics.curve_count ?? 0);
    el("analysis-crossings").textContent = String(metrics.crossing_count ?? 0);
    el("analysis-specials").textContent = String(metrics.special_count ?? 0);
    el("analysis-time").textContent = `${(metrics.estimated_time_s ?? 0).toFixed(1)} s`;
    el("analysis-speed").textContent =
        `${(metrics.recommended_speed_mps ?? 0).toFixed(2)} m/s`;

    const path = metrics.path ?? {};
    el("analysis-points").textContent = String(path.point_count ?? 0);
    el("analysis-step").textContent =
        path.step_mm === undefined ? "—" : `${Number(path.step_mm).toFixed(0)} mm`;

    const score = Math.round(metrics.difficulty ?? 0);
    el("difficulty-score").textContent = String(score);
    el("difficulty-label").textContent = metrics.difficulty_label ?? "Muito Fácil";
    el("difficulty-badge").dataset.level = difficultyKey(metrics.difficulty_label);
}

function elementWorldBBox(element) {
    const polylines = elementPolylines(element);
    let minX = Infinity;
    let minY = Infinity;
    let maxX = -Infinity;
    let maxY = -Infinity;
    for (const line of polylines) {
        for (const [x, y] of line) {
            if (x < minX) minX = x;
            if (y < minY) minY = y;
            if (x > maxX) maxX = x;
            if (y > maxY) maxY = y;
        }
    }
    if (!Number.isFinite(minX) || !Number.isFinite(minY)) return null;
    return { minX, minY, maxX, maxY };
}

function ribbonKind(element) {
    if (!element) return null;
    if (element.type === "line") return "straight";
    if (element.type === "arc" || element.type === "cubic") return "curve";
    if (element.type === "module") {
        return element.template === "curve.90" ? "curve" : "other";
    }
    return "other";
}

function ribbonMarkup(kind, element, info) {
    const width = Math.round(element.width ?? 19);
    const manual = Number(info.difficulty ?? element.difficulty ?? 0);
    const difficulty = [0, 1, 2, 3, 4, 5]
        .map(
            (level) =>
                `<option value="${level}"${level === manual ? " selected" : ""}>` +
                `${level === 0 ? "Auto" : level}</option>`
        )
        .join("");

    const actionRow =
        kind === "curve"
            ? `
            <div class="mini-row">
                <button class="mini-btn" data-ribbon="flip" title="Inverter direção da curva">Inverter</button>
                <button class="mini-btn" data-ribbon="smooth" title="Converter para curva suave">Suave</button>
                <button class="mini-btn" data-ribbon="close" title="Converter para curva fechada">Fechar</button>
                <button class="mini-btn" data-ribbon="mirror" title="Espelhar curva">Espelhar</button>
            </div>
            <div class="mini-row">
                <button class="mini-btn" data-ribbon="radius-down" title="Diminuir raio">Raio −</button>
                <button class="mini-btn" data-ribbon="radius-up" title="Aumentar raio">Raio +</button>
                <button class="mini-btn" data-ribbon="angle-down" title="Diminuir ângulo">Ângulo −</button>
                <button class="mini-btn" data-ribbon="angle-up" title="Aumentar ângulo">Ângulo +</button>
            </div>`
            : kind === "straight"
              ? `
            <div class="mini-row">
                <button class="mini-btn" data-ribbon="split" title="Dividir o segmento ao meio">Dividir</button>
                <button class="mini-btn" data-ribbon="insert-curve" title="Inserir uma curva no trecho">Inserir curva</button>
            </div>`
              : "";

    const quickProps =
        kind === "curve"
            ? `
            <label class="mini-field">Raio (mm)<input data-prop="radius" type="number" min="1" step="5" value="${Math.round(info.radius_mm ?? 0)}"></label>` +
              (element.type === "module"
                  ? ""
                  : `
            <label class="mini-field">Ângulo (°)<input data-prop="angle" type="number" min="1" max="180" step="5" value="${Math.round(Math.abs(info.angle_deg ?? 0))}"></label>`)
            : kind === "straight"
              ? `
            <label class="mini-field">Comprimento (mm)<input data-prop="length" type="number" min="1" step="10" value="${Math.round(elementLineLength(element))}"></label>`
              : "";

    return `
        <div class="mini-group">
            <div class="mini-stack">
                ${actionRow}
                <div class="mini-row">
                    <button class="mini-btn" data-ribbon="duplicate" title="Duplicar elemento">Duplicar</button>
                    <button class="mini-btn" data-ribbon="copy-props" title="Copiar propriedades">Copiar</button>
                    <button class="mini-btn" data-ribbon="paste-props" title="Colar propriedades">Colar</button>
                    <button class="mini-btn danger" data-ribbon="delete" title="Excluir elemento">Excluir</button>
                </div>
                <span class="mini-group-title">${kind === "curve" ? "Curva" : kind === "straight" ? "Reta" : "Elemento"}</span>
            </div>
        </div>
        <div class="mini-group mini-props">
            ${quickProps}
            <label class="mini-field">Largura (mm)<input data-prop="width" type="number" min="1" step="1" value="${width}"></label>
            <label class="mini-field">Dificuldade<select data-prop="difficulty">${difficulty}</select></label>
        </div>`;
}

function elementLineLength(element) {
    if (!element || element.type !== "line") return 0;
    return Math.hypot(element.b[0] - element.a[0], element.b[1] - element.a[1]);
}

function hideRibbon() {
    const ribbon = el("element-ribbon");
    if (!ribbon) return;
    state.ribbon.elementId = null;
    state.ribbon.kind = null;
    state.ribbon.info = null;
    state.ribbon.token += 1;
    ribbon.classList.remove("open");
    ribbon.setAttribute("aria-hidden", "true");
}

function positionRibbon(element) {
    const ribbon = el("element-ribbon");
    const view = state.view;
    const box = elementWorldBBox(element);
    if (!ribbon || !view || !box) return;

    const canvasRect = el("canvas").getBoundingClientRect();
    const topLeft = worldToScreen({ x: box.minX, y: box.minY });
    const bottomRight = worldToScreen({ x: box.maxX, y: box.maxY });
    const centerX = canvasRect.left + (topLeft.x + bottomRight.x) / 2;
    const topY = canvasRect.top + topLeft.y;
    const bottomY = canvasRect.top + bottomRight.y;
    const height = ribbon.offsetHeight || 140;
    const width = ribbon.offsetWidth || 300;

    const viewportWidth = document.documentElement.clientWidth;
    const minX = width / 2 + 8;
    const maxX = viewportWidth - width / 2 - 8;
    const clampedX =
        maxX < minX ? viewportWidth / 2 : Math.min(Math.max(centerX, minX), maxX);

    const below = topY - height - 18 < canvasRect.top + 6;
    ribbon.classList.toggle("below", below);
    ribbon.style.left = `${clampedX}px`;
    ribbon.style.top = `${below ? bottomY : topY}px`;
}

function syncRibbonValues(element, info) {
    const setValue = (name, value) => {
        const input = document.querySelector(`#element-ribbon [data-prop="${name}"]`);
        if (input && document.activeElement !== input) input.value = String(value);
    };

    if (info.radius_mm != null) setValue("radius", Math.round(info.radius_mm));
    if (info.angle_deg != null) setValue("angle", Math.round(Math.abs(info.angle_deg)));
    setValue("width", Math.round(element.width ?? 19));
    setValue("difficulty", Number(info.difficulty ?? element.difficulty ?? 0));
    if (element.type === "line") setValue("length", Math.round(elementLineLength(element)));
}

async function updateRibbon() {
    const ribbon = el("element-ribbon");
    if (!ribbon) return;

    const id = state.selection;
    const element = state.doc?.elements?.find((candidate) => candidate.id === id);
    if (!element) {
        hideRibbon();
        return;
    }

    const token = ++state.ribbon.token;
    let info = null;
    try {
        info = await rpc("scene.elementInfo", { id });
    } catch {
        info = null;
    }
    if (token !== state.ribbon.token || state.selection !== id) return;

    const kind = ribbonKind(element);
    const rebuild = state.ribbon.elementId !== id || state.ribbon.kind !== kind;
    state.ribbon.elementId = id;
    state.ribbon.kind = kind;
    state.ribbon.info = info;

    if (rebuild) {
        ribbon.innerHTML = ribbonMarkup(kind, element, info ?? {});
        ribbon.classList.remove("open");
        ribbon.setAttribute("aria-hidden", "false");
        positionRibbon(element);
        requestAnimationFrame(() => ribbon.classList.add("open"));
    } else {
        syncRibbonValues(element, info ?? {});
        positionRibbon(element);
    }
}

async function applyRibbonProperty(prop, raw) {
    const id = state.selection;
    if (id == null) return;
    const value = Number(raw);
    if (!Number.isFinite(value)) return;

    if (prop === "radius") {
        await rpc("doc.setRadius", { id, radius: value });
    } else if (prop === "angle") {
        await rpc("doc.setAngle", { id, angle: value });
    } else if (prop === "width") {
        await rpc("doc.setElement", { id, width: value });
    } else if (prop === "difficulty") {
        await rpc("doc.setElement", { id, difficulty: value });
    } else if (prop === "length") {
        await rpc("doc.adjustLength", { id, length: value });
    }
    await refreshDoc();
}

async function ribbonAction(action) {
    const id = state.selection;
    if (id == null) return;
    const info = state.ribbon.info ?? {};

    switch (action) {
        case "flip":
            await rpc("doc.flipElement", { id });
            break;
        case "mirror":
            await rpc("doc.mirrorElement", { id });
            break;
        case "smooth":
            await rpc("doc.smoothElement", { id });
            break;
        case "close":
            await rpc("doc.closeElement", { id });
            break;
        case "radius-up":
            await rpc("doc.adjustRadius", { id, factor: 1.25 });
            break;
        case "radius-down":
            await rpc("doc.adjustRadius", { id, factor: 0.8 });
            break;
        case "angle-up":
            await rpc("doc.adjustAngle", { id, delta: 15 });
            break;
        case "angle-down":
            await rpc("doc.adjustAngle", { id, delta: -15 });
            break;
        case "split":
            await rpc("doc.splitElement", { id, t: 0.5 });
            break;
        case "insert-curve":
            await rpc("doc.convertElement", { id, shape: "arc90" });
            break;
        case "duplicate": {
            const copy = await rpc("doc.duplicateElement", { id });
            state.selection = copy.id;
            break;
        }
        case "copy-props":
            state.propertiesClipboard = {
                width: state.ribbon.info?.width ?? 19,
                difficulty: state.ribbon.info?.difficulty ?? 0,
                radius: info.radius_mm ?? null,
                angle:
                    state.ribbon.kind === "curve" && info.angle_deg
                        ? Math.abs(info.angle_deg)
                        : null,
            };
            setStatus("Propriedades copiadas.");
            return;
        case "paste-props": {
            const clip = state.propertiesClipboard;
            if (!clip) {
                setStatus("Nenhuma propriedade copiada.");
                return;
            }
            await rpc("doc.setElement", {
                id,
                width: clip.width,
                difficulty: clip.difficulty,
            });
            if (state.ribbon.kind === "curve") {
                if (clip.radius != null && info.radius_mm != null) {
                    await rpc("doc.setRadius", { id, radius: clip.radius });
                }
                if (clip.angle != null && info.angle_deg != null) {
                    await rpc("doc.setAngle", { id, angle: clip.angle });
                }
            }
            break;
        }
        case "delete":
            await deleteSelection();
            return;
        default:
            return;
    }

    await refreshDoc();
    setStatus(`Ação aplicada ao elemento ${id}.`);
}

function toggleRobotVision() {
    state.robotVision = !state.robotVision;
    el("robot-legend").hidden = !state.robotVision;
    document
        .querySelectorAll("#robot-vision-ribbon, #robot-vision-side")
        .forEach((button) => button.classList.toggle("active", state.robotVision));
    setStatus(
        state.robotVision
            ? "Visão do Robô ativada · pontos críticos destacados."
            : "Visão do Robô desativada."
    );
    draw();
}

function renderValidation(diagnostics) {
    const list = el("validation-list");
    list.innerHTML = "";
    for (const diagnostic of diagnostics) {
        const item = document.createElement("li");
        const button = document.createElement("button");
        button.className = "validation-item";
        button.dataset.severity = diagnostic.severity ?? "info";
        if (diagnostic.element != null) button.dataset.focus = diagnostic.element;

        const dot = document.createElement("span");
        dot.className = "validation-dot";

        const body = document.createElement("span");
        const message = document.createElement("span");
        message.textContent = diagnostic.message ?? diagnostic.code ?? "Problema";
        const code = document.createElement("span");
        code.className = "validation-code";
        code.textContent = diagnostic.code ?? "";
        body.append(message, code);

        button.append(dot, body);
        item.appendChild(button);
        list.appendChild(item);
    }
}

async function validateTrack() {
    const diagnostics = await rpc("scene.validate");
    state.validation = diagnostics;
    renderValidation(diagnostics);

    const errors = diagnostics.filter((entry) => entry.severity === "error").length;
    const warnings = diagnostics.filter((entry) => entry.severity === "warning").length;
    el("validation-summary").textContent =
        diagnostics.length === 0
            ? "Nenhum problema encontrado."
            : `${errors} erro(s) · ${warnings} aviso(s) · clique para navegar.`;
    setStatus(
        diagnostics.length === 0
            ? "Validação concluída: nenhum problema encontrado."
            : `Validação concluída: ${errors} erro(s) e ${warnings} aviso(s).`
    );
}

async function focusElement(id) {
    const numeric = Number(id);
    if (!Number.isFinite(numeric)) return;

    state.selection = numeric;
    const element = state.doc?.elements?.find((candidate) => candidate.id === numeric);
    const view = state.view;
    const arena = docArena();
    const box = element ? elementWorldBBox(element) : null;
    if (view && box) {
        const centerX = (box.minX + box.maxX) / 2;
        const centerY = (box.minY + box.maxY) / 2;
        state.pan.x = (arena.width * view.scale) / 2 - centerX * view.scale;
        state.pan.y = (arena.height * view.scale) / 2 - centerY * view.scale;
    }

    await refreshDoc();
    setStatus(`Navegando para o elemento ${numeric}.`);
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

function canvasToWorld(event) {
    const view = state.view;
    if (!view) return null;

    const rect = el("canvas").getBoundingClientRect();
    return {
        x: (event.clientX - rect.left - view.originX) / view.scale,
        y: (event.clientY - rect.top - view.originY) / view.scale,
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
        const selected = element.id === state.selection;

        if (selected) {
            ctx.save();
            ctx.strokeStyle = "rgba(37, 99, 235, 0.22)";
            ctx.lineWidth = widthPx + 12;
            ctx.lineCap = "round";
            ctx.lineJoin = "round";
            for (const points of polylines) {
                if (tracePolyline(ctx, points)) ctx.stroke();
            }
            ctx.restore();
        }

        ctx.save();
        ctx.strokeStyle = selected ? "#1d4ed8" : "#0f172a";
        ctx.lineWidth = widthPx;
        ctx.lineCap = "round";
        ctx.lineJoin = "round";
        for (const points of polylines) {
            if (tracePolyline(ctx, points)) ctx.stroke();
        }
        ctx.restore();

        ctx.save();
        ctx.fillStyle = selected ? "#f59e0b" : "#2563eb";
        ctx.strokeStyle = "#ffffff";
        ctx.lineWidth = 1;
        for (const points of polylines) {
            if (!points || points.length < 2) continue;
            for (const point of [points[0], points[points.length - 1]]) {
                const p = worldToScreen({ x: point[0], y: point[1] });
                ctx.beginPath();
                ctx.arc(p.x, p.y, selected ? 4 : 2.4, 0, Math.PI * 2);
                ctx.fill();
                ctx.stroke();
            }
        }
        ctx.restore();
    }
}

function markerBandSize(scale) {
    return {
        length: Math.max(24, 19 * scale + 10),
        thickness: Math.max(5, 3 * scale),
    };
}

function drawCheckeredBand(ctx, x, y, angleRad, lengthPx, thicknessPx) {
    const columns = Math.max(4, Math.round(lengthPx / 8));
    const rows = 2;
    const cellW = lengthPx / columns;
    const cellH = thicknessPx / rows;

    ctx.save();
    ctx.translate(x, y);
    ctx.rotate(angleRad);
    for (let row = 0; row < rows; ++row) {
        for (let column = 0; column < columns; ++column) {
            ctx.fillStyle = (row + column) % 2 === 0 ? "#0f172a" : "#ffffff";
            ctx.fillRect(
                -lengthPx / 2 + column * cellW,
                -thicknessPx / 2 + row * cellH,
                cellW + 0.5,
                cellH + 0.5
            );
        }
    }
    ctx.strokeStyle = "rgba(15, 23, 42, 0.85)";
    ctx.lineWidth = 1;
    ctx.strokeRect(-lengthPx / 2, -thicknessPx / 2, lengthPx, thicknessPx);
    ctx.restore();
}

function drawMarkers(ctx, doc, layers) {
    if (layers.markers === false) return;

    const scale = state.view?.scale ?? 1;
    const band = markerBandSize(scale);

    for (const marker of doc.markers ?? []) {
        const type = marker.type ?? "start_finish";
        const p = worldToScreen({ x: marker.x ?? 0, y: marker.y ?? 0 });
        const angle = ((marker.angle ?? 0) * Math.PI) / 180;

        if (type === "start" || type === "finish" || type === "start_finish") {
            drawCheckeredBand(ctx, p.x, p.y, angle, band.length, band.thickness);

            const accent =
                type === "start" ? "#15803d" : type === "finish" ? "#b91c1c" : "#1d4ed8";
            const label =
                type === "start"
                    ? "LARGADA"
                    : type === "finish"
                      ? "CHEGADA"
                      : "LARGADA / CHEGADA";

            ctx.save();
            ctx.fillStyle = accent;
            ctx.beginPath();
            ctx.arc(p.x, p.y, 2.6, 0, Math.PI * 2);
            ctx.fill();
            ctx.font = '700 10px "Segoe UI", sans-serif';
            ctx.textAlign = "left";
            ctx.textBaseline = "middle";
            ctx.fillText(label, p.x + 10, p.y - 12);
            ctx.restore();
            continue;
        }

        ctx.save();
        ctx.translate(p.x, p.y);
        ctx.rotate(angle);
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
    if (state.drag || state.placement) return;

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

function snapColor(kind) {
    if (kind === "port") return "#16a34a";
    if (kind === "element") return "#0ea5e9";
    return "#94a3b8";
}

function snapLabel(kind) {
    if (kind === "port") return "conectar";
    if (kind === "element") return "traçado";
    return "grade";
}

function drawSnapIndicator(ctx) {
    const snap = state.snap;
    if (!snap || snap.kind === "none" || !snap.point) return;

    const p = worldToScreen({ x: snap.point[0], y: snap.point[1] });
    const color = snapColor(snap.kind);

    ctx.save();
    ctx.strokeStyle = color;
    ctx.fillStyle = color;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.arc(p.x, p.y, 7, 0, Math.PI * 2);
    ctx.stroke();

    if (snap.kind === "port") {
        ctx.beginPath();
        ctx.arc(p.x, p.y, 3.2, 0, Math.PI * 2);
        ctx.fill();
    }

    ctx.beginPath();
    ctx.moveTo(p.x - 12, p.y);
    ctx.lineTo(p.x - 8, p.y);
    ctx.moveTo(p.x + 8, p.y);
    ctx.lineTo(p.x + 12, p.y);
    ctx.moveTo(p.x, p.y - 12);
    ctx.lineTo(p.x, p.y - 8);
    ctx.moveTo(p.x, p.y + 8);
    ctx.lineTo(p.x, p.y + 12);
    ctx.stroke();

    ctx.font = '600 10px "Segoe UI", sans-serif';
    ctx.textAlign = "left";
    ctx.textBaseline = "bottom";
    ctx.fillText(snapLabel(snap.kind), p.x + 11, p.y - 8);
    ctx.restore();
}

function drawPreview(ctx) {
    const drag = state.drag;
    if (!drag || drag.mode !== "draw") return;

    ctx.save();
    ctx.strokeStyle = "#2563eb";
    ctx.fillStyle = "#2563eb";
    ctx.lineWidth = 1.6;
    ctx.setLineDash([6, 4]);

    if (drag.tool === "line") {
        tracePolyline(ctx, [[drag.start.x, drag.start.y], [drag.end.x, drag.end.y]]);
        ctx.stroke();
    } else {
        const curve = bezierFromDrag(drag.start, drag.end, drag.shift);
        tracePolyline(ctx, sampleCubic(curve));
        ctx.stroke();
    }

    ctx.setLineDash([]);
    for (const point of [drag.start, drag.end]) {
        const p = worldToScreen(point);
        ctx.beginPath();
        ctx.arc(p.x, p.y, 4, 0, Math.PI * 2);
        ctx.fill();
    }
    ctx.restore();
}

function drawPlacement(ctx) {
    const placement = state.placement;
    if (!placement) return;

    if (placement.kind === "start" || placement.kind === "finish") {
        const anchor = placement.start ?? placement.end;
        if (!anchor) return;

        const scale = state.view?.scale ?? 1;
        const p = worldToScreen(anchor);
        const angle = ((placement.angle ?? 90) * Math.PI) / 180;
        const band = markerBandSize(scale);
        drawCheckeredBand(ctx, p.x, p.y, angle, band.length, band.thickness);

        ctx.save();
        ctx.fillStyle = placement.kind === "start" ? "#15803d" : "#b91c1c";
        ctx.font = '700 10px "Segoe UI", sans-serif';
        ctx.textAlign = "left";
        ctx.textBaseline = "middle";
        ctx.fillText(placement.label, p.x + 10, p.y - 14);
        ctx.restore();
        return;
    }

    if (!placement.start) return;

    const end = placement.end ?? placement.start;
    const geometry = placementGeometry(
        placement.kind,
        placement.start,
        end,
        placement.shift
    );

    ctx.save();
    if (geometry.polylines.length > 0) {
        ctx.strokeStyle = "rgba(37, 99, 235, 0.28)";
        ctx.lineWidth = Math.max(4, 19 * (state.view?.scale ?? 1));
        ctx.lineCap = "round";
        ctx.lineJoin = "round";
        for (const points of geometry.polylines) {
            if (tracePolyline(ctx, points)) ctx.stroke();
        }

        ctx.strokeStyle = "#2563eb";
        ctx.lineWidth = 1.6;
        ctx.setLineDash([6, 4]);
        for (const points of geometry.polylines) {
            if (tracePolyline(ctx, points)) ctx.stroke();
        }
        ctx.setLineDash([]);
    }

    const anchor = worldToScreen(placement.start);
    ctx.fillStyle = "#1d4ed8";
    ctx.beginPath();
    ctx.arc(anchor.x, anchor.y, 4.5, 0, Math.PI * 2);
    ctx.fill();

    if (placement.end) {
        const tip = worldToScreen(placement.end);
        ctx.beginPath();
        ctx.arc(tip.x, tip.y, 4.5, 0, Math.PI * 2);
        ctx.fill();

        ctx.fillStyle = "#1e293b";
        ctx.font = '600 11px "Cascadia Mono", Consolas, monospace';
        ctx.textAlign = "left";
        ctx.textBaseline = "bottom";
        ctx.fillText(`${Math.round(geometry.distance)} mm`, tip.x + 10, tip.y - 8);
    }

    ctx.restore();
}

function elementHandles(element) {
    if (!element) return [];
    if (element.type === "line") {
        return [
            { key: "a", point: element.a, kind: "endpoint" },
            { key: "b", point: element.b, kind: "endpoint" },
        ];
    }
    if (element.type === "cubic") {
        return [
            { key: "p0", point: element.p0, kind: "endpoint" },
            { key: "p1", point: element.p1, kind: "control" },
            { key: "p2", point: element.p2, kind: "control" },
            { key: "p3", point: element.p3, kind: "endpoint" },
        ];
    }
    return [];
}

function handleAtPoint(point) {
    if (state.selection == null) return null;
    const element = state.doc?.elements?.find(
        (candidate) => candidate.id === state.selection
    );
    const handles = elementHandles(element);
    if (handles.length === 0) return null;

    const tolerance = Math.max(8 / (state.view?.scale ?? 1), 1.5);
    let best = null;
    for (const handle of handles) {
        const distance = Math.hypot(
            point.x - handle.point[0],
            point.y - handle.point[1]
        );
        if (distance <= tolerance && (!best || distance < best.distance)) {
            best = { ...handle, distance };
        }
    }
    return best;
}

function drawHandles(ctx) {
    if (state.placement) return;

    const element = state.doc?.elements?.find(
        (candidate) => candidate.id === state.selection
    );
    const handles = elementHandles(element);
    if (handles.length === 0) return;

    ctx.save();

    if (element.type === "cubic") {
        ctx.strokeStyle = "rgba(37, 99, 235, 0.5)";
        ctx.lineWidth = 1;
        ctx.setLineDash([4, 3]);
        for (const [from, to] of [
            [element.p0, element.p1],
            [element.p2, element.p3],
        ]) {
            if (tracePolyline(ctx, [from, to])) ctx.stroke();
        }
        ctx.setLineDash([]);
    }

    for (const handle of handles) {
        const p = worldToScreen({ x: handle.point[0], y: handle.point[1] });
        ctx.beginPath();
        if (handle.kind === "control") {
            ctx.arc(p.x, p.y, 4.5, 0, Math.PI * 2);
            ctx.fillStyle = "#ffffff";
            ctx.fill();
            ctx.strokeStyle = "#2563eb";
            ctx.lineWidth = 1.6;
            ctx.stroke();
        } else {
            ctx.rect(p.x - 4.5, p.y - 4.5, 9, 9);
            ctx.fillStyle = "#ffffff";
            ctx.fill();
            ctx.strokeStyle = "#1d4ed8";
            ctx.lineWidth = 1.8;
            ctx.stroke();
        }
    }

    ctx.restore();
}

function drawRobotVision(ctx) {
    const points = state.analysis?.critical_points ?? [];
    if (points.length === 0) return;

    const colors = {
        tight_curve: "#f97316",
        abrupt_turn: "#ef4444",
        line_loss: "#dc2626",
        sensor_critical: "#eab308",
        high_difficulty: "#a855f7",
    };

    ctx.save();
    for (const point of points) {
        const p = worldToScreen({ x: point.x, y: point.y });
        const color = colors[point.kind] ?? "#2563eb";
        const severity = Math.min(Math.max(point.severity ?? 0.5, 0.2), 1);
        const radius = 10 + severity * 20;

        ctx.globalAlpha = 0.14 + severity * 0.18;
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.arc(p.x, p.y, radius, 0, Math.PI * 2);
        ctx.fill();

        ctx.globalAlpha = 0.9;
        ctx.strokeStyle = color;
        ctx.lineWidth = 1.8;
        ctx.beginPath();
        ctx.arc(p.x, p.y, radius, 0, Math.PI * 2);
        ctx.stroke();

        if (point.kind === "abrupt_turn") {
            ctx.beginPath();
            ctx.moveTo(p.x - 5, p.y + 4);
            ctx.lineTo(p.x, p.y - 4);
            ctx.lineTo(p.x + 5, p.y + 4);
            ctx.stroke();
        }
    }
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
    const fitScale = Math.min(
        (width - padding * 2) / arena.width,
        (height - padding * 2) / arena.height
    );

    if (!(fitScale > 0)) return;

    const scale = fitScale * state.zoom;
    const arenaWidth = arena.width * scale;
    const arenaHeight = arena.height * scale;
    const originX = width / 2 + state.pan.x - arenaWidth / 2;
    const originY = height / 2 + state.pan.y - arenaHeight / 2;

    state.view = {
        scale,
        fitScale,
        originX,
        originY,
        width,
        height,
        arenaWidth,
        arenaHeight,
    };

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
        if (state.robotVision) drawRobotVision(ctx);
        drawMarkers(ctx, doc, layers);
    }

    drawCursor(ctx);
    drawHandles(ctx);
    drawPreview(ctx);
    drawPlacement(ctx);
    drawSnapIndicator(ctx);

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
            "ou escolha Reta/Bézier e arraste na arena",
            originX + arenaWidth / 2,
            originY + arenaHeight / 2 + 10
        );
    }
}

/* ------------------------------------------------------------- view control */

function clampZoom(value) {
    return Math.min(16, Math.max(0.15, value));
}

function updateZoomStatus() {
    el("zoom-status").textContent = `${Math.round(state.zoom * 100)}%`;
}

function zoomBy(factor, clientX, clientY) {
    const view = state.view;
    if (!view) return;

    const canvas = el("canvas");
    const rect = canvas.getBoundingClientRect();
    const sx = clientX == null ? rect.width / 2 : clientX - rect.left;
    const sy = clientY == null ? rect.height / 2 : clientY - rect.top;

    const worldX = (sx - view.originX) / view.scale;
    const worldY = (sy - view.originY) / view.scale;

    state.zoom = clampZoom(state.zoom * factor);

    const arena = docArena();
    const scale = view.fitScale * state.zoom;
    state.pan.x =
        sx - view.width / 2 + (arena.width * scale) / 2 - worldX * scale;
    state.pan.y =
        sy - view.height / 2 + (arena.height * scale) / 2 - worldY * scale;

    updateZoomStatus();
    draw();
}

function resetView() {
    state.zoom = 1;
    state.pan = { x: 0, y: 0 };
    updateZoomStatus();
    draw();
}

/* --------------------------------------------------------------- insertion */

/// Arco de 90° entre dois pontos: o centro fica ao lado da corda e o
/// deslocamento do mouse define o raio (corda = raio × √2).
function quarterArc(start, end, flip) {
    const dx = end.x - start.x;
    const dy = end.y - start.y;
    const chord = Math.hypot(dx, dy);
    if (chord < 1e-6) return null;

    const side = flip ? -1 : 1;
    const center = {
        x: (start.x + end.x) / 2 - (dy / 2) * side,
        y: (start.y + end.y) / 2 + (dx / 2) * side,
    };
    const radius = chord / Math.SQRT2;
    const a0 = Math.atan2(start.y - center.y, start.x - center.x);
    const a1 = Math.atan2(end.y - center.y, end.x - center.x);
    let sweep = ((a1 - a0) * 180) / Math.PI;
    while (sweep > 180) sweep -= 360;
    while (sweep <= -180) sweep += 360;
    if (Math.abs(sweep) < 1) return null;

    return {
        center,
        radius,
        start_deg: (a0 * 180) / Math.PI,
        sweep_deg: sweep > 0 ? 90 : -90,
    };
}

/// Semicírculo entre dois pontos (diâmetro = corda).
function halfArc(start, end, flip) {
    const dx = end.x - start.x;
    const dy = end.y - start.y;
    const chord = Math.hypot(dx, dy);
    if (chord < 1e-6) return null;

    const center = { x: (start.x + end.x) / 2, y: (start.y + end.y) / 2 };
    const a0 = Math.atan2(start.y - center.y, start.x - center.x);
    return {
        center,
        radius: chord / 2,
        start_deg: (a0 * 180) / Math.PI,
        sweep_deg: flip ? -180 : 180,
    };
}

/// Curva em S: os dois pontos de controle ficam em lados opostos da corda.
function sCurveFromDrag(start, end, shift) {
    const dx = end.x - start.x;
    const dy = end.y - start.y;
    const length = Math.hypot(dx, dy) || 1;
    const nx = -dy / length;
    const ny = dx / length;
    const bow = Math.min(length * 0.25, 160) * (shift ? -1 : 1);

    return {
        p0: [start.x, start.y],
        p1: [start.x + dx * 0.33 + nx * bow, start.y + dy * 0.33 + ny * bow],
        p2: [start.x + dx * 0.66 - nx * bow, start.y + dy * 0.66 - ny * bow],
        p3: [end.x, end.y],
    };
}

/// Prévia geométrica da forma em posicionamento (sem tocar no documento).
function placementGeometry(kind, start, end, shift) {
    if (kind === "start" || kind === "finish") {
        return { polylines: [], valid: true, distance: 0 };
    }

    const distance = Math.hypot(end.x - start.x, end.y - start.y);
    if (distance < 1e-6) {
        return { polylines: [], valid: false, distance };
    }

    switch (kind) {
        case "line":
            return {
                polylines: [[[start.x, start.y], [end.x, end.y]]],
                valid: distance >= 5,
                distance,
            };
        case "bezier": {
            const curve = bezierFromDrag(start, end, shift);
            return { polylines: [sampleCubic(curve)], valid: distance >= 5, distance };
        }
        case "s": {
            const curve = sCurveFromDrag(start, end, shift);
            return { polylines: [sampleCubic(curve)], valid: distance >= 5, distance };
        }
        case "arc90":
        case "curve90": {
            const arc = quarterArc(start, end, shift);
            if (!arc) return { polylines: [], valid: false, distance };
            return {
                polylines: [
                    sampleArc(
                        [arc.center.x, arc.center.y],
                        arc.radius,
                        arc.start_deg,
                        arc.sweep_deg
                    ),
                ],
                valid: distance >= 5,
                distance,
                arc,
            };
        }
        case "arc180": {
            const arc = halfArc(start, end, shift);
            if (!arc) return { polylines: [], valid: false, distance };
            return {
                polylines: [
                    sampleArc(
                        [arc.center.x, arc.center.y],
                        arc.radius,
                        arc.start_deg,
                        arc.sweep_deg
                    ),
                ],
                valid: distance >= 5,
                distance,
                arc,
            };
        }
        case "cross": {
            const arm = Math.max(
                Math.abs(end.x - start.x),
                Math.abs(end.y - start.y),
                20
            );
            return {
                polylines: [
                    [[start.x - arm, start.y], [start.x + arm, start.y]],
                    [[start.x, start.y - arm], [start.x, start.y + arm]],
                ],
                valid: true,
                distance,
                arm,
            };
        }
        default:
            return { polylines: [], valid: false, distance };
    }
}

function startPlacement(kind, label, options = {}) {
    setTool("select");
    state.placement = {
        kind,
        label,
        start: null,
        end: null,
        shift: false,
        seq: 0,
        busy: false,
        single: Boolean(options.single),
        angle: 90,
        startPort: null,
        endPort: null,
    };
    state.selection = null;
    state.snap = null;
    state.drag = null;
    el("canvas").classList.add("placing");
    updateSelectionPanel();
    draw();
    setStatus(
        options.single
            ? `${label}: clique sobre a pista para posicionar · Esc cancela.`
            : `Inserir ${label}: clique na arena para posicionar · Esc cancela.`
    );
}

function cancelPlacement(message) {
    if (!state.placement) return false;
    state.placement = null;
    state.snap = null;
    el("canvas").classList.remove("placing");
    draw();
    setStatus(message ?? "Inserção cancelada.");
    return true;
}

async function commitPlacement(placement) {
    const { kind, start, end, shift } = placement;

    if (kind === "start" || kind === "finish") {
        await rpc("doc.setStartFinish", {
            kind,
            x: start.x,
            y: start.y,
            angle: placement.angle ?? 90,
        });

        state.placement = null;
        state.snap = null;
        el("canvas").classList.remove("placing");
        state.selection = null;
        state.cursor = { x: Math.round(start.x), y: Math.round(start.y) };
        await refreshDoc();
        setStatus(
            `${placement.label} definida em ${Math.round(start.x)}, ` +
                `${Math.round(start.y)} mm.`
        );
        return;
    }

    const geometry = placementGeometry(kind, start, end, shift);
    if (!geometry.valid) {
        setStatus("Arraste mais para definir o tamanho do elemento.");
        return;
    }

    let result;
    switch (kind) {
        case "line":
            result = await rpc("doc.addLine", {
                a: [start.x, start.y],
                b: [end.x, end.y],
                width: 19,
            });
            break;
        case "bezier": {
            const curve = bezierFromDrag(start, end, shift);
            result = await rpc("doc.addCubic", { ...curve, width: 19 });
            break;
        }
        case "s": {
            const curve = sCurveFromDrag(start, end, shift);
            result = await rpc("doc.addCubic", { ...curve, width: 19 });
            break;
        }
        case "arc90":
        case "arc180":
        case "curve90": {
            const arc = geometry.arc;
            if (kind === "curve90") {
                const mirrored = arc.sweep_deg < 0;
                result = await rpc("doc.addModule", {
                    template: "curve.90",
                    params: { radius: arc.radius, width: 19 },
                    transform: {
                        tx: arc.center.x,
                        ty: arc.center.y,
                        rot_deg: mirrored ? arc.start_deg + 180 : arc.start_deg,
                        mirrored,
                    },
                    width: 19,
                });
            } else {
                result = await rpc("doc.addArc", {
                    center: [arc.center.x, arc.center.y],
                    radius: arc.radius,
                    start_deg: arc.start_deg,
                    sweep_deg: arc.sweep_deg,
                    width: 19,
                });
            }
            break;
        }
        case "cross": {
            result = await rpc("doc.addModule", {
                template: "cross.90",
                params: { arm: geometry.arm, width: 19 },
                transform: { tx: start.x, ty: start.y, rot_deg: 0, mirrored: false },
                width: 19,
            });
            break;
        }
        default:
            return;
    }

    const connected = await connectEndpoints(
        result.id,
        kind,
        placement.startPort,
        placement.endPort
    );

    state.placement = null;
    state.snap = null;
    el("canvas").classList.remove("placing");
    state.selection = result.id;
    state.cursor = { x: Math.round(end.x), y: Math.round(end.y) };
    await refreshDoc();
    setStatus(
        `Elemento ${result.id} (${placement.label}) inserido` +
            (connected > 0
                ? ` · ${connected} conexão(ões) automática(s).`
                : " · arraste as alças para ajustar.")
    );
}

function insertLine() {
    startPlacement("line", "Reta");
}

function insertBezier() {
    startPlacement("bezier", "Bézier");
}

function insertArc90() {
    startPlacement("arc90", "Arco 90°");
}

function insertCurve90() {
    startPlacement("curve90", "Curva 90°");
}

function insertArc180() {
    startPlacement("arc180", "Curva 180°");
}

function insertS() {
    startPlacement("s", "Curva S");
}

function insertCross() {
    startPlacement("cross", "Cruzamento 90°");
}

function insertStart() {
    startPlacement("start", "Largada", { single: true });
}

function insertFinish() {
    startPlacement("finish", "Chegada", { single: true });
}

async function clearStartFinish() {
    const result = await rpc("doc.clearStartFinish", {});
    await refreshDoc();
    setStatus(
        result.removed > 0
            ? `Marcas de circuito removidas (${result.removed}).`
            : "Nenhuma marca de circuito para remover."
    );
}
/* ------------------------------------------------------------------- tools */

function setTool(tool) {
    if (state.placement) {
        state.placement = null;
        state.snap = null;
        el("canvas").classList.remove("placing");
    }

    state.tool = tool;

    document.querySelectorAll(".tool-button").forEach((button) => {
        button.classList.toggle("active", button.dataset.tool === tool);
    });

    const canvas = el("canvas");
    canvas.classList.remove("tool-select", "tool-line", "tool-bezier");
    canvas.classList.add(`tool-${tool}`);

    setStatus(`Ferramenta: ${TOOL_LABELS[tool] ?? tool}.`);
}

function bezierFromDrag(start, end, shift) {
    const dx = end.x - start.x;
    const dy = end.y - start.y;
    const length = Math.hypot(dx, dy) || 1;
    const nx = -dy / length;
    const ny = dx / length;
    const bow = Math.min(length * 0.25, 160) * (shift ? -1 : 1);

    return {
        p0: [start.x, start.y],
        p1: [start.x + dx * 0.33 + nx * bow, start.y + dy * 0.33 + ny * bow],
        p2: [start.x + dx * 0.66 + nx * bow, start.y + dy * 0.66 + ny * bow],
        p3: [end.x, end.y],
    };
}

function portRef(result) {
    if (!result || result.kind !== "port") return null;
    if (result.element == null || !result.port) return null;
    return { element: result.element, port: result.port };
}

/// Ângulo (graus) da faixa de largada/chegada: perpendicular à tangente da pista.
function markerAngleDeg(result) {
    const direction = result?.direction;
    if (
        Array.isArray(direction) &&
        Math.hypot(direction[0] ?? 0, direction[1] ?? 0) > 1e-9
    ) {
        return (Math.atan2(direction[1], direction[0]) * 180) / Math.PI + 90;
    }
    return 90;
}

function connectionPortNames(kind) {
    if (kind === "cross") return null;
    if (kind === "curve90") return { start: "in", end: "out" };
    return { start: "start", end: "end" };
}

/// Conecta as pontas do elemento recém-criado às portas magnéticas usadas.
async function connectEndpoints(createdId, kind, startPort, endPort) {
    const names = connectionPortNames(kind);
    if (!names || createdId == null) return 0;

    const pairs = [
        [startPort, names.start],
        [endPort, names.end],
    ];

    let connected = 0;
    for (const [existing, port] of pairs) {
        if (!existing || existing.element === createdId) continue;
        try {
            await rpc("doc.connect", {
                a: [existing.element, existing.port],
                b: [createdId, port],
            });
            connected += 1;
        } catch (error) {
            const text = String(error.message ?? error);
            if (!text.startsWith("port_in_use")) {
                setStatus(`Conexão automática falhou: ${text}`);
            }
        }
    }
    return connected;
}

async function requestSnap(point, ignore) {
    const scale = state.view?.scale ?? 1;
    const params = {
        point: [point.x, point.y],
        tolerance: Math.max(10 / scale, 6),
        grid: 10,
    };
    if (ignore != null) params.ignore = ignore;
    return rpc("scene.snap", params);
}

function snappedPoint(result, fallback) {
    if (!result || result.kind === "none" || !result.point) return fallback;
    return { x: result.point[0], y: result.point[1] };
}

function translateLocal(id, delta) {
    const element = state.doc?.elements?.find((candidate) => candidate.id === id);
    if (!element) return;

    const move = (point) => [point[0] + delta.x, point[1] + delta.y];

    switch (element.type) {
        case "line":
            element.a = move(element.a);
            element.b = move(element.b);
            break;
        case "cubic":
            element.p0 = move(element.p0);
            element.p1 = move(element.p1);
            element.p2 = move(element.p2);
            element.p3 = move(element.p3);
            break;
        case "arc":
            element.center = move(element.center);
            break;
        case "module":
            element.transform = element.transform ?? {};
            element.transform.tx = (element.transform.tx ?? 0) + delta.x;
            element.transform.ty = (element.transform.ty ?? 0) + delta.y;
            break;
        default:
            break;
    }
}

function queueMove(drag) {
    const target = { ...drag.targetDelta };

    drag.chain = drag.chain
        .then(async () => {
            const step = {
                x: target.x - drag.applied.x,
                y: target.y - drag.applied.y,
            };
            if (Math.abs(step.x) < 1e-9 && Math.abs(step.y) < 1e-9) return;

            await rpc("doc.moveElement", {
                id: drag.id,
                delta: [step.x, step.y],
                merge: drag.mergeKey,
            });
            drag.applied = target;
            translateLocal(drag.id, step);
            draw();
        })
        .catch((error) => {
            setStatus(String(error.message ?? error));
        });
}

function setElementPointLocal(id, key, point) {
    const element = state.doc?.elements?.find((candidate) => candidate.id === id);
    if (!element) return;
    element[key] = [point.x, point.y];
}

function queueHandle(drag) {
    drag.chain = drag.chain
        .then(async () => {
            if (drag.ended || !drag.targetPoint) return;
            const target = drag.targetPoint;
            drag.targetPoint = null;

            await rpc("doc.setGeometry", {
                id: drag.id,
                [drag.key]: [target.x, target.y],
                merge: drag.mergeKey,
            });
            setElementPointLocal(drag.id, drag.key, target);
            draw();
        })
        .catch((error) => {
            setStatus(String(error.message ?? error));
        });
}

/* -------------------------------------------------------------- interactions */

async function undo() {
    try {
        const result = await rpc("doc.undo");
        state.selection = null;
        await refreshDoc();
        setStatus(`Desfeito: ${result.label}.`);
    } catch (error) {
        if (String(error.message ?? error).startsWith("nothing_to_undo")) {
            setStatus("Nada para desfazer.");
        } else {
            setStatus(String(error.message ?? error));
        }
    }
}

async function redo() {
    try {
        const result = await rpc("doc.redo");
        state.selection = null;
        await refreshDoc();
        setStatus(`Refeito: ${result.label}.`);
    } catch (error) {
        if (String(error.message ?? error).startsWith("nothing_to_redo")) {
            setStatus("Nada para refazer.");
        } else {
            setStatus(String(error.message ?? error));
        }
    }
}

async function deleteSelection() {
    const id = state.selection;
    if (id == null) {
        setStatus("Selecione um elemento para excluir.");
        return;
    }

    await rpc("doc.removeElement", { id });
    state.selection = null;
    await refreshDoc();
    setStatus(`Elemento ${id} excluído.`);
}

async function newDocument() {
    if (!(await confirmDiscard())) return;

    await rpc("doc.new");
    state.selection = null;
    state.cursor = { x: 300, y: 300 };
    state.snap = null;
    state.drag = null;
    state.placement = null;
    resetView();
    setTool("select");
    await refreshDoc();
    markSaved();
    setStatus("Documento novo · arena 4 × 3 m.");
}

/// Abre o diálogo nativo e devolve o caminho, ou `null` se cancelado.
/// Se o diálogo nativo não estiver disponível, usa o diálogo da página.
async function chooseFilePath(save) {
    try {
        const result = await rpc(save ? "doc.saveDialog" : "doc.openDialog");
        if (result.cancelled) return null;
        return result.path;
    } catch (error) {
        const text = String(error.message ?? error);
        if (!text.startsWith("dialog_unavailable")) throw error;
        return askPath(
            save ? "Salvar documento como:" : "Abrir documento:",
            "C:/Temp/pistaforge.pftrack"
        );
    }
}

async function saveDocument() {
    const path = state.doc?.path ?? null;
    if (path) {
        const result = await rpc("doc.save", { path });
        await refreshDoc();
        markSaved();
        setStatus(`Salvo em ${result.path}`);
        return;
    }
    await saveDocumentAs();
}

async function saveDocumentAs() {
    const path = await chooseFilePath(true);
    if (!path) {
        setStatus("Salvamento cancelado.");
        return;
    }

    const result = await rpc("doc.save", { path });
    await refreshDoc();
    markSaved();
    setStatus(`Salvo em ${result.path}`);
}

/* ------------------------------------------------- opções de exportação (UI) */

const EXPORT_FIELDS = {
    pdf: {
        title: "Exportar PDF 1:1",
        fields: [
            { key: "a4", label: "Dividir em páginas A4 (190 × 277 mm)", kind: "bool", default: true },
            { key: "marks", label: "Pontos numerados para desenhar no chão", kind: "bool", default: true },
            { key: "centerline", label: "Somente a linha central", kind: "bool", default: false },
            { key: "grid", label: "Grade de referência", kind: "bool", default: true },
            { key: "calibration", label: "Régua de calibração", kind: "bool", default: true },
            { key: "mark_step", label: "Passo das marcas (mm)", kind: "number", default: 500, min: 10, step: 10 },
            { key: "path_width", label: "Largura da linha central (mm)", kind: "number", default: 1, min: 0.1, step: 0.1 },
            { key: "overlap", label: "Sobreposição das páginas (mm)", kind: "number", default: 10, min: 0, step: 1 },
        ],
    },
    obj: {
        title: "Exportar 3D para Blender",
        fields: [
            { key: "floor", label: "Incluir piso da arena", kind: "bool", default: true },
            { key: "centerline", label: "Incluir linha central", kind: "bool", default: true },
            { key: "scale", label: "Escala (mm → m)", kind: "number", default: 0.001, min: 0.0001, step: 0.001 },
            { key: "height", label: "Espessura da faixa (mm)", kind: "number", default: 2, min: 0.1, step: 0.5 },
            { key: "step", label: "Passo da malha (mm)", kind: "number", default: 20, min: 5, step: 5 },
        ],
    },
    robot: {
        title: "Exportar pontos do robô",
        fields: [
            { key: "step", label: "Passo dos pontos (mm)", kind: "number", default: 100, min: 10, step: 10 },
            { key: "brush", label: "Largura do pincel (mm, 0 = da pista)", kind: "number", default: 0, min: 0, step: 1 },
            { key: "speed", label: "Velocidade base (mm/s)", kind: "number", default: 250, min: 10, step: 10 },
        ],
    },
    table: {
        title: "Exportar matriz de coordenadas",
        fields: [
            { key: "step", label: "Passo da matriz (mm)", kind: "number", default: 50, min: 5, step: 5 },
        ],
    },
    sim: {
        title: "Exportar documento do simulador",
        fields: [
            { key: "step", label: "Passo da linha central (mm)", kind: "number", default: 50, min: 5, step: 5 },
            { key: "tolerance", label: "Tolerância das curvas (mm)", kind: "number", default: 0.25, min: 0.05, step: 0.05 },
        ],
    },
};

function defaultExportOptions(kind) {
    const spec = EXPORT_FIELDS[kind];
    const stored = state.exportOptions[kind] ?? {};
    const options = {};
    for (const field of spec.fields) {
        options[field.key] = stored[field.key] ?? field.default;
    }
    return options;
}

/// Monta o formulário do diálogo de exportação e devolve as opções escolhidas.
async function askExportOptions(kind) {
    const spec = EXPORT_FIELDS[kind];
    if (!spec) return null;

    const dialog = el("export-dialog");
    if (!dialog || typeof dialog.showModal !== "function") {
        return defaultExportOptions(kind);
    }

    const stored = state.exportOptions[kind] ?? {};
    const container = el("export-fields");
    const error = el("export-error");
    container.innerHTML = "";
    error.hidden = true;
    el("export-dialog-title").textContent = spec.title;

    for (const field of spec.fields) {
        const id = `export-${kind}-${field.key}`;
        const value = stored[field.key] ?? field.default;

        if (field.kind === "bool") {
            const row = document.createElement("label");
            row.className = "dialog-check";
            row.htmlFor = id;

            const input = document.createElement("input");
            input.type = "checkbox";
            input.id = id;
            input.dataset.key = field.key;
            input.checked = Boolean(value);

            const text = document.createElement("span");
            text.textContent = field.label;

            row.append(input, text);
            container.append(row);
            continue;
        }

        const label = document.createElement("label");
        label.htmlFor = id;
        label.textContent = field.label;

        const input = document.createElement("input");
        input.type = "number";
        input.id = id;
        input.dataset.key = field.key;
        input.min = String(field.min ?? 0);
        input.step = String(field.step ?? 1);
        input.value = String(value);

        container.append(label, input);
    }

    const collect = () => {
        const options = {};
        for (const field of spec.fields) {
            const input = container.querySelector(`[data-key="${field.key}"]`);
            if (!input) continue;
            if (field.kind === "bool") {
                options[field.key] = input.checked;
                continue;
            }
            const number = Number(input.value);
            if (!Number.isFinite(number) || number < (field.min ?? 0)) {
                error.textContent = `Valor inválido em "${field.label}".`;
                error.hidden = false;
                input.focus();
                return null;
            }
            options[field.key] = number;
        }
        error.hidden = true;
        return options;
    };

    const confirm = el("export-confirm");
    const onConfirm = (event) => {
        if (!collect()) event.preventDefault();
    };
    confirm.addEventListener("click", onConfirm);
    try {
        const result = await askDialog("export-dialog", () => {
            container.querySelector("input")?.focus();
        });
        if (result !== "ok") return null;

        const options = collect();
        if (!options) return null;
        state.exportOptions[kind] = options;
        return options;
    } finally {
        confirm.removeEventListener("click", onConfirm);
    }
}

async function exportSvg(tiled) {
    let result;
    try {
        result = await rpc("doc.exportSvgDialog", { tiled });
    } catch (error) {
        const text = String(error.message ?? error);
        if (!text.startsWith("dialog_unavailable")) throw error;
        const path = await askPath(
            tiled ? "Exportar mosaico SVG (arquivo base):" : "Exportar SVG:",
            "C:/Temp/pista.svg"
        );
        if (!path) {
            setStatus("Exportação cancelada.");
            return;
        }
        result = await rpc("doc.exportSvg", { path, tiled });
    }

    if (result.cancelled) {
        setStatus("Exportação cancelada.");
        return;
    }

    if (result.tiled) {
        const first = result.files[0] ?? "";
        setStatus(`Mosaico SVG: ${result.pages} página(s) · ${first}`);
    } else {
        setStatus(`SVG exportado: ${result.files[0] ?? ""}`);
    }
}

/// Executa uma exportação com diálogo nativo e fallback de caminho digitado.
async function runExport({ dialog, direct, title, fallback, params = {}, describe }) {
    let result;
    try {
        result = await rpc(dialog, params);
    } catch (error) {
        const text = String(error.message ?? error);
        if (!text.startsWith("dialog_unavailable")) throw error;
        const path = await askPath(title, fallback);
        if (!path) {
            setStatus("Exportação cancelada.");
            return;
        }
        result = await rpc(direct, { ...params, path });
    }

    if (result.cancelled) {
        setStatus("Exportação cancelada.");
        return;
    }
    setStatus(describe(result));
}

async function exportPdf() {
    const options = await askExportOptions("pdf");
    if (!options) {
        setStatus("Exportação cancelada.");
        return;
    }

    const arena = docArena();
    return runExport({
        dialog: "doc.exportPdfDialog",
        direct: "doc.exportPdf",
        title: "Exportar PDF 1:1:",
        fallback: "C:/Temp/pista.pdf",
        params: {
            tiled: options.a4,
            page_width: options.a4 ? 190 : arena.width,
            page_height: options.a4 ? 277 : arena.height,
            overlap: options.overlap,
            marks: options.marks,
            centerline: options.centerline,
            grid: options.grid,
            calibration: options.calibration,
            mark_step: options.mark_step,
            path_width: options.path_width,
        },
        describe: (result) =>
            `PDF 1:1: ${result.pages} página(s) · ${result.files[0] ?? ""}`,
    });
}

async function exportObj() {
    const options = await askExportOptions("obj");
    if (!options) {
        setStatus("Exportação cancelada.");
        return;
    }
    return runExport({
        dialog: "doc.exportObjDialog",
        direct: "doc.exportObj",
        title: "Exportar pista 3D (Blender):",
        fallback: "C:/Temp/pista.obj",
        params: options,
        describe: (result) =>
            `3D exportado: ${result.vertices} vértices, ${result.faces} faces · ${result.files[0] ?? ""}`,
    });
}

async function exportRobot() {
    const options = await askExportOptions("robot");
    if (!options) {
        setStatus("Exportação cancelada.");
        return;
    }
    return runExport({
        dialog: "doc.exportRobotDialog",
        direct: "doc.exportRobot",
        title: "Exportar pontos do robô (arquivo base):",
        fallback: "C:/Temp/pista-pontos.csv",
        params: options,
        describe: (result) =>
            `Robô: ${result.points} pontos · ${result.files[0] ?? ""}`,
    });
}

async function exportTable() {
    const options = await askExportOptions("table");
    if (!options) {
        setStatus("Exportação cancelada.");
        return;
    }
    return runExport({
        dialog: "doc.exportTableDialog",
        direct: "doc.exportTable",
        title: "Exportar tabela de coordenadas:",
        fallback: "C:/Temp/pista-tabela.csv",
        params: options,
        describe: (result) =>
            `Matriz: ${result.points} pontos · ${result.files[0] ?? ""}`,
    });
}

async function exportSim() {
    const options = await askExportOptions("sim");
    if (!options) {
        setStatus("Exportação cancelada.");
        return;
    }
    return runExport({
        dialog: "doc.exportSimDialog",
        direct: "doc.exportSim",
        title: "Exportar documento do simulador:",
        fallback: "C:/Temp/pista.pfsim.json",
        params: options,
        describe: (result) =>
            `Simulador: ${result.points} pontos · ${result.files[0] ?? ""}`,
    });
}

async function openDocument() {
    if (!(await confirmDiscard())) return;

    const path = await chooseFilePath(false);
    if (!path) {
        setStatus("Abertura cancelada.");
        return;
    }

    await rpc("doc.load", { path });
    state.selection = null;
    state.cursor = { x: 300, y: 300 };
    state.snap = null;
    state.drag = null;
    state.placement = null;
    resetView();
    setTool("select");
    await refreshDoc();
    markSaved();
    setStatus(`Aberto: ${path}`);
}

async function applyArena(arena) {
    await rpc("doc.setArena", arena);
    state.selection = null;
    state.cursor = { x: 300, y: 300 };
    resetView();
    await refreshDoc();

    const current = docArena();
    setStatus(
        `Arena ${formatMm(current.width)} × ${formatMm(current.height)} · margem ${formatMm(current.margin)}.`
    );
}

function readArenaForm() {
    return {
        name: el("arena-name").value.trim() || "Arena",
        width: Number(el("arena-width").value),
        height: Number(el("arena-height").value),
        margin: Number(el("arena-margin").value),
    };
}

function validateArena(arena) {
    if (!(arena.width > 0) || !(arena.height > 0)) {
        return "Largura e altura devem ser positivas.";
    }
    if (!(arena.margin >= 0)) {
        return "A margem não pode ser negativa.";
    }
    if (arena.margin * 2 >= Math.min(arena.width, arena.height)) {
        return "A margem é grande demais para esta arena.";
    }
    return null;
}

async function openArenaDialog() {
    const arena = docArena();
    el("arena-name").value = arena.name ?? "Arena";
    el("arena-width").value = String(Math.round(arena.width ?? 4000));
    el("arena-height").value = String(Math.round(arena.height ?? 3000));
    el("arena-margin").value = String(Math.round(arena.margin ?? 100));
    el("arena-error").hidden = true;

    const result = await askDialog("arena-dialog", () => {
        const input = el("arena-width");
        input.focus();
        input.select();
    });
    if (result !== "ok") {
        setStatus("Configuração de arena cancelada.");
        return;
    }

    const values = readArenaForm();
    const error = validateArena(values);
    if (error) {
        setStatus(error);
        return;
    }
    await applyArena(values);
}

async function setLayer(key, visible) {
    if (!state.doc) return;
    const result = await rpc("doc.setLayers", { [key]: visible });
    state.doc.layers = result.layers;
    syncLayerToggles();
    updateDirtyState();
    updateFacts();
    draw();

    setStatus(`Camada ${LAYER_LABELS[key] ?? key} ${visible ? "visível" : "oculta"}.`);
}

async function onPointerDown(event) {
    const canvas = el("canvas");

    if (event.button === 1 || (event.button === 0 && spaceDown)) {
        event.preventDefault();
        canvas.setPointerCapture(event.pointerId);
        state.drag = {
            mode: "pan",
            pointerId: event.pointerId,
            startX: event.clientX,
            startY: event.clientY,
            startPan: { ...state.pan },
        };
        canvas.classList.add("panning");
        return;
    }

    if (event.button !== 0) return;

    const point = canvasToWorld(event);
    if (!point) return;

    const placement = state.placement;
    if (placement) {
        if (placement.busy) return;
        placement.busy = true;
        try {
            const seq = ++placement.seq;
            const result = await requestSnap(point, null);
            if (state.placement !== placement || placement.seq !== seq) return;

            const snapped = snappedPoint(result, point);
            state.snap = result?.kind !== "none" ? result : null;

            if (placement.single) {
                placement.start = snapped;
                placement.end = snapped;
                placement.angle = markerAngleDeg(result);
                await commitPlacement(placement);
                return;
            }

            if (!placement.start) {
                placement.start = snapped;
                placement.end = snapped;
                placement.shift = event.shiftKey;
                placement.startPort = portRef(result);
                state.cursor = {
                    x: Math.round(snapped.x),
                    y: Math.round(snapped.y),
                };
                updateFacts();
                draw();
                setStatus(
                    `${placement.label}: mova o mouse para dimensionar` +
                        (placement.startPort ? " · porta magnética travada" : "") +
                        " · clique para confirmar · Esc cancela."
                );
                return;
            }

            placement.end = snapped;
            placement.shift = event.shiftKey;
            placement.endPort = portRef(result);
            await commitPlacement(placement);
        } finally {
            placement.busy = false;
        }
        return;
    }

    canvas.setPointerCapture(event.pointerId);

    if (state.tool === "line" || state.tool === "bezier") {
        const drag = {
            mode: "draw",
            tool: state.tool,
            start: point,
            end: point,
            shift: event.shiftKey,
            pointerId: event.pointerId,
            pending: 1,
            ended: false,
            ready: false,
            startPort: null,
            endPort: null,
        };
        state.drag = drag;

        const result = await requestSnap(point, null);
        drag.pending -= 1;
        if (state.drag !== drag || drag.ended) return;

        drag.start = snappedPoint(result, point);
        drag.end = drag.start;
        drag.startPort = portRef(result);
        drag.ready = true;
        state.snap = result?.kind !== "none" ? result : null;
        draw();
        setStatus("Arraste para desenhar · Esc cancela.");
        return;
    }

    // Alças de edição do elemento selecionado têm prioridade sobre mover.
    const handle = handleAtPoint(point);
    if (handle) {
        const drag = {
            mode: "handle",
            id: state.selection,
            key: handle.key,
            pointerId: event.pointerId,
            ended: false,
            seq: 0,
            targetPoint: null,
            chain: Promise.resolve(),
            mergeKey: `handle-${state.selection}-${handle.key}-${Date.now()}`,
        };
        state.drag = drag;
        canvas.classList.add("dragging");
        draw();
        setStatus(
            `Ajustando ${handle.key.toUpperCase()} do elemento ${state.selection} · ` +
                "Esc cancela."
        );
        return;
    }

    // Ferramenta de seleção: descobre o elemento sob o ponteiro.
    const drag = {
        mode: "pick",
        startPoint: point,
        pointerId: event.pointerId,
        ended: false,
        startX: event.clientX,
        startY: event.clientY,
        startPan: { ...state.pan },
    };
    state.drag = drag;

    const tolerance = Math.max(6 / (state.view?.scale ?? 1), 1.5);
    const pick = await rpc("scene.pick", {
        point: [point.x, point.y],
        tolerance,
    });
    if (state.drag !== drag) return;

    if (pick.element != null) {
        state.selection = pick.element;
        updateSelectionPanel();
        updateRibbon().catch(() => {});

        if (!drag.ended) {
            const grab = pick.point ?? [point.x, point.y];
            drag.mode = "move";
            drag.id = pick.element;
            drag.grabPoint = { x: grab[0], y: grab[1] };
            drag.offset = { x: grab[0] - point.x, y: grab[1] - point.y };
            drag.targetDelta = { x: 0, y: 0 };
            drag.applied = { x: 0, y: 0 };
            drag.mergeKey = `drag-${pick.element}-${Date.now()}`;
            drag.chain = Promise.resolve();
            drag.pending = 0;
            canvas.classList.add("dragging");
            draw();
            setStatus(`Elemento ${pick.element} selecionado · arraste para mover.`);
            return;
        }

        draw();
        setStatus(`Elemento ${pick.element} selecionado.`);
    } else {
        state.selection = null;
        updateSelectionPanel();
        hideRibbon();
        state.cursor = {
            x: Math.round(point.x),
            y: Math.round(point.y),
        };
        updateFacts();

        if (drag.ended) {
            state.drag = null;
            draw();
            setStatus(`Cursor em ${state.cursor.x}, ${state.cursor.y} mm.`);
            return;
        }

        // Arrastar em área vazia desloca a arena (pan).
        drag.mode = "pan";
        canvas.classList.add("panning");
        draw();
        setStatus("Arraste para deslocar a arena · solte para parar.");
        return;
    }

    state.drag = null;
}

async function onPointerMove(event) {
    const placement = state.placement;
    if (placement && !state.drag) {
        const point = canvasToWorld(event);
        if (!point) return;

        placement.shift = event.shiftKey;
        const seq = ++placement.seq;
        const result = await requestSnap(point, null);
        if (state.placement !== placement || placement.seq !== seq) return;

        const snapped = snappedPoint(result, point);
        state.snap = result?.kind !== "none" ? result : null;
        if (placement.single) {
            placement.start = snapped;
            placement.end = snapped;
            placement.angle = markerAngleDeg(result);
        } else if (placement.start) {
            placement.end = snapped;
            placement.endPort = portRef(result);
        }
        state.cursor = { x: Math.round(snapped.x), y: Math.round(snapped.y) };
        updateFacts();
        draw();
        return;
    }

    const drag = state.drag;
    if (!drag || drag.pointerId !== event.pointerId) return;

    if (drag.mode === "pan") {
        state.pan.x = drag.startPan.x + (event.clientX - drag.startX);
        state.pan.y = drag.startPan.y + (event.clientY - drag.startY);
        draw();
        return;
    }

    if (drag.mode === "pick") return;

    const point = canvasToWorld(event);
    if (!point) return;

    if (drag.mode === "move") {
        drag.pending += 1;
        const desired = { x: point.x + drag.offset.x, y: point.y + drag.offset.y };
        const result = await requestSnap(desired, drag.id);
        drag.pending -= 1;
        if (state.drag !== drag || drag.ended) return;

        const snapped = snappedPoint(result, desired);
        state.snap = result?.kind !== "none" ? result : null;
        drag.targetDelta = {
            x: snapped.x - drag.grabPoint.x,
            y: snapped.y - drag.grabPoint.y,
        };
        queueMove(drag);
        draw();
        return;
    }

    if (drag.mode === "handle") {
        const seq = ++drag.seq;
        const result = await requestSnap(point, drag.id);
        if (state.drag !== drag || drag.ended || drag.seq !== seq) return;

        const snapped = snappedPoint(result, point);
        state.snap = result?.kind !== "none" ? result : null;
        drag.targetPoint = snapped;
        queueHandle(drag);
        draw();
        return;
    }

    if (drag.mode === "draw") {
        if (!drag.ready) return;

        drag.pending += 1;
        const result = await requestSnap(point, null);
        drag.pending -= 1;
        if (state.drag !== drag || drag.ended) return;

        drag.end = snappedPoint(result, point);
        drag.endPort = portRef(result);
        drag.shift = event.shiftKey;
        state.snap = result?.kind !== "none" ? result : null;
        draw();
    }
}

async function onPointerUp(event) {
    const drag = state.drag;
    if (!drag || drag.pointerId !== event.pointerId) return;

    const canvas = el("canvas");

    if (drag.mode === "pan") {
        const moved = Math.hypot(
            state.pan.x - drag.startPan.x,
            state.pan.y - drag.startPan.y
        );
        state.drag = null;
        canvas.classList.remove("panning");
        if (moved > 1) {
            setStatus("Arena deslocada · use Ajustar para centralizar.");
        }
        return;
    }

    if (drag.mode === "pick") {
        // A seleção é finalizada quando a consulta de pick terminar.
        drag.ended = true;
        return;
    }

    if (drag.mode === "move") {
        while (drag.pending > 0) await sleep(5);
        drag.ended = true;
        await drag.chain;
        if (state.drag === drag) state.drag = null;
        canvas.classList.remove("dragging");
        state.snap = null;
        await refreshDoc();
        setStatus(`Elemento ${drag.id} movido.`);
        return;
    }

    if (drag.mode === "handle") {
        drag.ended = true;
        await drag.chain;

        if (drag.targetPoint) {
            const target = drag.targetPoint;
            drag.targetPoint = null;
            try {
                await rpc("doc.setGeometry", {
                    id: drag.id,
                    [drag.key]: [target.x, target.y],
                    merge: drag.mergeKey,
                });
                setElementPointLocal(drag.id, drag.key, target);
            } catch (error) {
                setStatus(String(error.message ?? error));
            }
        }

        if (state.drag === drag) state.drag = null;
        canvas.classList.remove("dragging");
        state.snap = null;
        await refreshDoc();
        setStatus(`Geometria do elemento ${drag.id} atualizada.`);
        return;
    }

    if (drag.mode === "draw") {
        while (drag.pending > 0) await sleep(5);
        drag.ended = true;

        const start = drag.start;
        const end = drag.end;
        const shift = event.shiftKey || drag.shift;
        state.drag = null;
        state.snap = null;

        const distance = Math.hypot(end.x - start.x, end.y - start.y);
        if (distance < 5) {
            draw();
            setStatus("Arraste mais para criar o elemento.");
            return;
        }

        let createdId = null;
        if (drag.tool === "line") {
            const result = await rpc("doc.addLine", {
                a: [start.x, start.y],
                b: [end.x, end.y],
                width: 19,
            });
            createdId = result.id;
            state.selection = result.id;
            await refreshDoc();
            setStatus(`Reta ${result.id} criada.`);
        } else {
            const curve = bezierFromDrag(start, end, shift);
            const result = await rpc("doc.addCubic", {
                p0: curve.p0,
                p1: curve.p1,
                p2: curve.p2,
                p3: curve.p3,
                width: 19,
            });
            createdId = result.id;
            state.selection = result.id;
            await refreshDoc();
            setStatus(`Bézier ${result.id} criada.`);
        }

        const connected = await connectEndpoints(
            createdId,
            drag.tool === "line" ? "line" : "bezier",
            drag.startPort,
            drag.endPort
        );
        if (connected > 0) {
            await refreshDoc();
            setStatus(
                `Elemento ${createdId} criado · ${connected} conexão(ões) automática(s).`
            );
        }

        state.cursor = { x: Math.round(end.x), y: Math.round(end.y) };
        updateFacts();
        draw();
    }
}

function onWheel(event) {
    event.preventDefault();
    const factor = Math.exp(-event.deltaY * 0.0015);
    zoomBy(factor, event.clientX, event.clientY);
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
        case "insert-start":
            return insertStart();
        case "insert-finish":
            return insertFinish();
        case "clear-start-finish":
            return clearStartFinish();
        case "undo":
            return undo();
        case "redo":
            return redo();
        case "delete":
            return deleteSelection();
        case "validate-track":
            return validateTrack();
        case "robot-vision":
            return toggleRobotVision();
        case "new":
            return newDocument();
        case "open":
            return openDocument();
        case "save":
            return saveDocument();
        case "save-as":
            return saveDocumentAs();
        case "export-svg":
            return exportSvg(false);
        case "export-tiled":
            return exportSvg(true);
        case "export-pdf":
            return exportPdf();
        case "export-obj":
            return exportObj();
        case "export-robot":
            return exportRobot();
        case "export-table":
            return exportTable();
        case "export-sim":
            return exportSim();
        case "arena-default":
            return applyArena({ width: 4000, height: 3000, margin: 100 });
        case "arena-3x4":
            return applyArena({ width: 3000, height: 4000, margin: 100 });
        case "arena-custom":
            return openArenaDialog();
        case "zoom-in":
            return zoomBy(1.25);
        case "zoom-out":
            return zoomBy(0.8);
        case "zoom-fit":
            return resetView();
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

        const focusTarget = event.target.closest("[data-focus]");
        if (focusTarget) {
            event.preventDefault();
            focusElement(focusTarget.dataset.focus).catch((error) => {
                setStatus(String(error.message ?? error));
            });
            return;
        }

        const ribbonButton = event.target.closest("[data-ribbon]");
        if (ribbonButton) {
            event.preventDefault();
            ribbonAction(ribbonButton.dataset.ribbon).catch((error) => {
                setStatus(String(error.message ?? error));
            });
            return;
        }

        const tool = event.target.closest("[data-tool]");
        if (tool) {
            event.preventDefault();
            setTool(tool.dataset.tool);
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
        const property = event.target.closest("[data-prop]");
        if (property) {
            applyRibbonProperty(property.dataset.prop, property.value).catch((error) => {
                setStatus(String(error.message ?? error));
            });
            return;
        }

        const input = event.target.closest(".layer-toggle");
        if (!input) return;
        setLayer(input.dataset.layer, input.checked).catch((error) => {
            setStatus(String(error.message ?? error));
        });
    });

    el("arena-apply").addEventListener("click", (event) => {
        const error = validateArena(readArenaForm());
        if (!error) return;

        event.preventDefault();
        const node = el("arena-error");
        node.textContent = error;
        node.hidden = false;
    });

    const canvas = el("canvas");
    canvas.addEventListener("pointerdown", (event) => {
        onPointerDown(event).catch((error) => {
            setStatus(String(error.message ?? error));
        });
    });
    canvas.addEventListener("pointermove", (event) => {
        onPointerMove(event).catch((error) => {
            setStatus(String(error.message ?? error));
        });
    });
    canvas.addEventListener("pointerup", (event) => {
        onPointerUp(event).catch((error) => {
            setStatus(String(error.message ?? error));
        });
    });
    canvas.addEventListener("pointercancel", (event) => {
        onPointerUp(event).catch((error) => {
            setStatus(String(error.message ?? error));
        });
    });
    canvas.addEventListener("wheel", onWheel, { passive: false });
    canvas.addEventListener("contextmenu", (event) => event.preventDefault());

    document.addEventListener("keydown", (event) => {
        if (isTypingTarget(event.target)) return;

        if (event.code === "Space") {
            if (!event.target.closest?.("button")) spaceDown = true;
            return;
        }

        if (event.ctrlKey || event.metaKey) {
            const key = event.key.toLowerCase();
            if (key === "z") {
                event.preventDefault();
                (event.shiftKey ? redo() : undo()).catch((error) => {
                    setStatus(String(error.message ?? error));
                });
            } else if (key === "y") {
                event.preventDefault();
                redo().catch((error) => {
                    setStatus(String(error.message ?? error));
                });
            }
            return;
        }

        switch (event.key) {
            case "Delete":
            case "Backspace":
                event.preventDefault();
                deleteSelection().catch((error) => {
                    setStatus(String(error.message ?? error));
                });
                break;
            case "v":
            case "V":
                setTool("select");
                break;
            case "l":
            case "L":
                setTool("line");
                break;
            case "b":
            case "B":
                setTool("bezier");
                break;
            case "Escape":
                state.snap = null;
                if (state.placement) {
                    cancelPlacement();
                } else if (state.drag) {
                    state.drag.ended = true;
                    state.drag = null;
                    el("canvas").classList.remove("dragging", "panning");
                    refreshDoc().catch(() => {});
                } else {
                    state.selection = null;
                    updateSelectionPanel();
                    hideRibbon();
                    setTool("select");
                    draw();
                }
                break;
            default:
                break;
        }
    });

    document.addEventListener("keyup", (event) => {
        if (event.code === "Space") spaceDown = false;
    });

    try {
        await waitForBridge();
        setBridgeState("online", "conectada");

        const info = await rpc("app.info");
        renderInfo(info);

        el("ping").disabled = false;
        el("latency").disabled = false;

        await refreshDoc();
        markSaved();
        setTool("select");
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

window.addEventListener("beforeunload", (event) => {
    if (!state.dirty) return;
    event.preventDefault();
    event.returnValue = "";
});

init();
