import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import crypto from "node:crypto";
import { fileURLToPath } from "node:url";

const [suiteDirectory, outputDirectory, historicalDirectory] = process.argv.slice(2);
if (!suiteDirectory || !outputDirectory) {
    throw new Error("Usage: node scripts\\build-d3-eval-dashboard.mjs <suite directory> <new output directory> [historical directory]");
}
if (fs.existsSync(outputDirectory)) throw new Error("Choose a new output directory; reports are not overwritten.");
const readJson = file => JSON.parse(fs.readFileSync(file, "utf8").replace(/^\uFEFF/, ""));
const readLines = file => fs.readFileSync(file, "utf8").split(/\r?\n/).filter(line => line.trim()).map(JSON.parse);
const hash = file => crypto.createHash("sha256").update(fs.readFileSync(file)).digest("hex");
const mean = values => values.length ? values.reduce((a, b) => a + b, 0) / values.length : null;
const percentile = (values, p) => {
    if (!values.length) return null;
    const sorted = [...values].sort((a, b) => a - b);
    return sorted[Math.ceil(sorted.length * p) - 1];
};
const suite = readJson(path.join(suiteDirectory, "suite.json"));
assert.equal(suite.schema, 1);
assert.equal(suite.runs.length, 4);
assert.equal(suite.minimum_probability, 0.45);
assert.equal(suite.minimum_margin, 0.08);
const ids = ["d3-lite-original", "d3-lite-incumbent-neutral", "d3-edge-original", "d3-edge-incumbent-neutral"];
assert.deepEqual([...suite.runs].sort(), [...ids].sort());
const colors = ["#48b8f0", "#f4bd46", "#de83ed", "#7ed175"];
let initial;
let nativeReference;
const manifest = [];
const notesPath = path.join(suiteDirectory, "operator-notes.json");
const environmentNotes = fs.existsSync(notesPath) ? readJson(notesPath).notes : [];
assert.ok(Array.isArray(environmentNotes) && environmentNotes.every(note => typeof note === "string"));
if (fs.existsSync(notesPath)) {
    manifest.push({ source: notesPath, target: path.join("results", "operator-notes.json"), sha256: hash(notesPath) });
}
const runs = suite.runs.map((id, index) => {
    const directory = path.join(suiteDirectory, id);
    const comparison = readJson(path.join(directory, "comparison.json"));
    const analysis = readJson(path.join(directory, "analysis.json"));
    assert.equal(comparison.seconds_per_variant, suite.seconds_per_variant);
    assert.equal(comparison.minimum_probability, 0.45);
    assert.equal(comparison.minimum_margin, 0.08);
    assert.equal(id, `${comparison.model}-${comparison.prompt_variant}`);
    if (!initial) initial = comparison.initial_state;
    assert.deepEqual(comparison.initial_state, initial, `${id}: initial state/RNG mismatch`);
    const events = readLines(path.join(directory, "model-decisions.jsonl"));
    const completed = events.filter(event => event.outcome !== "pending");
    const submitted = events.filter(event => event.outcome === "pending");
    assert.equal(submitted.length, comparison.model_on.submitted);
    assert.equal(completed.length, analysis.completed_decisions);
    assert.ok(completed.length > 0, `${id}: no completed decisions`);
    const outcomes = { applied: 0, unchanged: 0, low_confidence: 0, stale: 0 };
    for (const event of events) {
        assert.equal(event.prompt_variant, comparison.prompt_variant);
        assert.equal(event.minimum_probability, 0.45);
        assert.equal(event.minimum_margin, 0.08);
        if (comparison.prompt_variant === "incumbent-neutral") {
            assert.ok(!event.prompt.includes("current_destination"));
            assert.ok(!event.prompt.includes("already_heading_here"));
            assert.ok(!Object.hasOwn(event.state, "current_destination"));
        }
        if (event.outcome === "pending") continue;
        assert.ok(Object.hasOwn(outcomes, event.outcome));
        outcomes[event.outcome]++;
        assert.ok(event.options.length >= 2 && event.options.length <= 8);
        assert.ok(event.options.every(option => Number.isFinite(option.probability) && option.probability >= 0
            && option.probability <= 1));
        assert.ok(Math.abs(event.options.reduce((sum, option) => sum + option.probability, 0) - 1) < 0.001);
        assert.ok(Math.abs(Math.max(...event.options.map(option => option.probability)) - event.confidence) < 0.00001);
        const sorted = event.options.map(option => option.probability).sort((a, b) => b - a);
        assert.ok(Math.abs(event.margin - (sorted[0] - sorted[1])) < 0.00001);
        const passesGate = event.confidence >= Math.fround(0.45) && event.margin >= Math.fround(0.08);
        assert.equal(passesGate, event.outcome !== "low_confidence", `${id}: outcome disagrees with confidence gate`);
    }
    assert.equal(outcomes.applied + outcomes.unchanged, comparison.model_on.accepted);
    assert.equal(outcomes.low_confidence, comparison.model_on.low_confidence);
    assert.equal(outcomes.stale, comparison.model_on.stale);
    const modelMetrics = readLines(path.join(directory, "model-metrics.jsonl"));
    const nativeMetrics = readLines(path.join(directory, "native-metrics.jsonl"));
    for (const metrics of [modelMetrics, nativeMetrics]) {
        assert.equal(metrics.length, suite.seconds_per_variant + 1);
        assert.equal(metrics.at(-1).tick - metrics[0].tick, suite.seconds_per_variant * 40);
    }
    const withoutElapsed = row => Object.fromEntries(Object.entries(row).filter(([key]) => key !== "elapsed_seconds"));
    for (const [part, metrics] of [[comparison.model_on, modelMetrics], [comparison.native_off, nativeMetrics]]) {
        assert.deepEqual(withoutElapsed(part.start), withoutElapsed(metrics[0]));
        assert.deepEqual(withoutElapsed(part.end), withoutElapsed(metrics.at(-1)));
        for (const [key, expected] of Object.entries(part.time_mean)) {
            assert.ok(Math.abs(mean(metrics.slice(1).map(row => row[key])) - expected) < 0.000001,
                `${id}: ${key} time mean does not match the raw samples`);
        }
    }
    if (!nativeReference) nativeReference = nativeMetrics;
    assert.deepEqual(nativeMetrics, nativeReference, `${id}: native baseline is not identical`);
    const samples = readLines(path.join(directory, "incumbent-ablation.jsonl"));
    assert.equal(samples.length, comparison.ablation_samples);
    const matchedAblationMaxDifference = samples.length ? Math.max(...samples.flatMap(sample =>
        [sample.hidden, sample.swapped].map(result => Math.max(...result.probabilities.map((p, i) =>
            Math.abs(p - sample.original.probabilities[i])))))) : null;
    if (comparison.prompt_variant === "incumbent-neutral" && samples.length) {
        assert.ok(matchedAblationMaxDifference < 0.00001, `${id}: neutral prompt still depends on incumbent hints`);
    }
    const files = ["comparison.json", "analysis.json", "metadata.json", "model-decisions.jsonl",
        "model-metrics.jsonl", "native-metrics.jsonl", "model-metrics.csv", "native-metrics.csv",
        "incumbent-ablation.jsonl", "run.log", "run.stderr", "test-results.xml"];
    for (const file of files) {
        const source = path.join(directory, file);
        manifest.push({ source, target: path.join("results", id, file), sha256: hash(source) });
    }
    const withIncumbent = completed.filter(event => event.incumbent_in_options);
    const withoutIncumbent = completed.filter(event => !event.incumbent_in_options);
    return {
        id, model: comparison.model, variant: comparison.prompt_variant,
        label: `${comparison.model === "d3-lite" ? "D3-Lite" : "D3-Edge"} / ${comparison.prompt_variant === "original" ? "A: original" : "B: neutral"}`,
        color: colors[index], comparison, analysis, outcomes,
        completed: completed.length,
        redirect_rate: outcomes.applied / completed.length,
        fallback_rate: outcomes.low_confidence / completed.length,
        unchanged_rate: outcomes.unchanged / completed.length,
        confidence_present: mean(withIncumbent.map(event => event.confidence)),
        confidence_absent: mean(withoutIncumbent.map(event => event.confidence)),
        incumbent_present_count: withIncumbent.length,
        incumbent_absent_count: withoutIncumbent.length,
        latency_median_ms: percentile(completed.map(event => event.elapsed_ms), 0.5),
        latency_p95_ms: percentile(completed.map(event => event.elapsed_ms), 0.95),
        matched_ablation_max_difference: matchedAblationMaxDifference,
        series: modelMetrics.map((row, second) => ({ ...row, elapsed_seconds: second })),
        examples: ["applied", "unchanged", "low_confidence", "stale"]
            .flatMap(outcome => completed.filter(event => event.outcome === outcome).slice(0, 2))
            .map(event => ({
                guest_id: event.guest_id, outcome: event.outcome, confidence: event.confidence,
                margin: event.margin, incumbent_id: event.incumbent_id, winner_id: event.winner_id,
                elapsed_ms: event.elapsed_ms, prompt: event.prompt,
                options: event.options, state: event.state,
            })),
    };
});
let historical = null;
if (historicalDirectory) {
    historical = { date: "2026-10-20", analysis: readJson(path.join(historicalDirectory, "analysis.json")) };
    for (const file of ["analysis.json", "comparison.json", "incumbent-ablation.jsonl"]) {
        const source = path.join(historicalDirectory, file);
        manifest.push({ source, target: path.join("results", "historical-20261020", file), sha256: hash(source) });
    }
}
const data = {
    schema: 1, suite, runs, historical, environment_notes: environmentNotes,
    native: nativeReference.map((row, second) => ({ ...row, elapsed_seconds: second })),
    evidence: {
        initial_state_identical: true, native_metrics_identical: true,
        neutral_trace_prompts_verified: true, probabilities_and_outcomes_verified: true,
        files: manifest.map(({ target, sha256 }) => ({ path: target.replaceAll("\\", "/"), sha256 })),
    },
};
const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const template = fs.readFileSync(path.join(scriptDirectory, "d3-eval-dashboard.html"), "utf8");
assert.equal(template.split("__D3_EVAL_DATA__").length, 2);
const serialized = JSON.stringify(data).replaceAll("<", "\\u003c").replaceAll("\u2028", "\\u2028").replaceAll("\u2029", "\\u2029");
fs.mkdirSync(outputDirectory, { recursive: true });
for (const entry of manifest) {
    const target = path.join(outputDirectory, entry.target);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.copyFileSync(entry.source, target);
    assert.equal(hash(target), entry.sha256);
}
for (const view of ["dashboard", "slides"]) {
    const html = template.replace("__D3_EVAL_DATA__", serialized).replace("__D3_VIEW__", view);
    fs.writeFileSync(path.join(outputDirectory, `${view}.html`), html);
}
fs.writeFileSync(path.join(outputDirectory, "eval-data.json"), JSON.stringify(data, null, 2) + "\n");
fs.copyFileSync(path.join(suiteDirectory, "suite.json"), path.join(outputDirectory, "suite.json"));
const findings = runs.map(run => `${run.label}: ${run.completed} completed; ${run.outcomes.applied} redirects; `
    + `${run.outcomes.unchanged} unchanged; ${run.outcomes.low_confidence} low-confidence fallbacks; ${run.outcomes.stale} stale.`);
const contrasts = ["d3-lite", "d3-edge"].map(model => {
    const original = runs.find(run => run.model === model && run.variant === "original");
    const neutral = runs.find(run => run.model === model && run.variant === "incumbent-neutral");
    const change = 100 * (neutral.fallback_rate - original.fallback_rate);
    return `${model}: neutral minus original low-confidence fallback = ${change >= 0 ? "+" : ""}${change.toFixed(2)} percentage points.`;
});
fs.writeFileSync(path.join(outputDirectory, "README.txt"), [
    "D3 PARK LAB - OFFLINE EVALUATION DASHBOARD", "",
    "Open dashboard.html in a modern browser. No server, network, fonts or downloads are required.",
    "Open slides.html for the two-slide presentation. Use arrow keys or the slide buttons.",
    "Use the Print / PDF button to print both slides in landscape. A ready-made PDF may also be included.", "",
    "If a PowerPoint is supplied, it contains two full-slide images to preserve the pixel styling.",
    "Use the HTML/source files for interactive data and editable layout, not the image-based PowerPoint.", "",
    "EXPERIMENT",
    `Source branch: ab/d3-incumbent-neutral; evaluation revision: ${suite.source_revision}`,
    `Completed UTC: ${suite.completed_utc}`,
    `Park SHA256: ${suite.park_sha256}`,
    `Duration: ${suite.seconds_per_variant} seconds per native/model variant, 40 ticks/second.`,
    "Each of the four treatments has its own freshly reloaded native baseline.",
    "All initial entity checksums, RNG states and metrics match; native trajectories match exactly.",
    "A: original prompt. B: remove current_destination and already_heading_here from model input only.",
    "Top probability >= 0.45 and top-two margin >= 0.08 remain unchanged.",
    "Candidate filtering, queue budgets, expiry and main-thread validation remain unchanged.",
    "Normal gameplay still defaults to the original prompt. This is an experiment, not an automatic gameplay fix.", "",
    "RESULTS", ...findings, ...contrasts, "",
    "INTERPRETATION",
    "Accepted unchanged targets are not redirects. Low-confidence results deliberately retain native behavior.",
    "Model scores are not calibrated certainty. More redirects or lower confidence are not automatically better.",
    "One park, one run per treatment, one initial state: no statistical-significance or general quality claim.",
    "Asynchronous inference means the four live treatments do not see identical sequences of guest snapshots.",
    "Frozen-input hidden/swapped incumbent checks isolate hint sensitivity within each captured sample.",
    "Per-second park metrics are population aggregates; treatment can change which guests remain.",
    "Queue-inclusive decision latency is not pure model-forward latency.",
    "Historical results (if present) are from 20 October 2026 and are separate from the fresh evaluation.", "",
    ...(environmentNotes.length ? ["ENVIRONMENT NOTES", ...environmentNotes, ""] : []),
    "PROVENANCE",
    "eval-data.json embeds report data and SHA256 checksums for all included raw evidence.",
    "results contains full model traces, native/model metrics, analyses, frozen-input ablations and test outputs.",
    "suite.json identifies the test executable, source revision and park hashes.",
    "The visual style is an original RCT2-inspired pixel/bevel treatment, not extracted game artwork.",
    "No models, game binaries, original game assets or personal save/profile data are included.", "",
].join("\r\n"));
console.log(JSON.stringify({
    output: path.resolve(outputDirectory), treatments: runs.length, seconds: suite.seconds_per_variant,
    decisions: runs.map(run => ({ id: run.id, ...run.outcomes })),
    evidence_files: manifest.length, checks: data.evidence,
}, null, 2));
