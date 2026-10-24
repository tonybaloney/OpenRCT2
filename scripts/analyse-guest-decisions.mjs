import fs from "node:fs";
import path from "node:path";

const directory = process.argv[2];
if (!directory) throw new Error("Usage: node analyse-guest-decisions.mjs <comparison directory>");
const readJson = file => JSON.parse(fs.readFileSync(path.join(directory, file), "utf8").replace(/^\uFEFF/, ""));
const readLines = file => fs.readFileSync(path.join(directory, file), "utf8")
    .split(/\r?\n/).filter(line => line.trim()).map(line => JSON.parse(line));
const mean = values => values.length ? values.reduce((a, b) => a + b, 0) / values.length : null;
const rate = (values, predicate) => values.length ? values.filter(predicate).length / values.length : null;
const counts = (values, key) => {
    const result = {};
    for (const value of values) {
        const category = key(value);
        result[category] = (result[category] ?? 0) + 1;
    }
    return result;
};
const comparison = readJson("comparison.json");
if (!comparison.native_off || !comparison.model_on) throw new Error("Both A/B runs must finish before analysis.");
const events = readLines("model-decisions.jsonl");
const decisions = events.filter(event => event.outcome !== "pending");
if (events.filter(event => event.outcome === "pending").length !== comparison.model_on.submitted)
    throw new Error("Submitted trace count does not match the controller.");
if (decisions.length !== comparison.model_on.accepted + comparison.model_on.low_confidence + comparison.model_on.stale)
    throw new Error("Completed trace count does not match the controller.");
for (const event of decisions) {
    const probabilities = event.options.map(option => option.probability);
    if (event.schema !== 1 || probabilities.length < 2 || probabilities.length > 8
        || probabilities.some(p => !Number.isFinite(p) || p < 0 || p > 1)
        || Math.abs(probabilities.reduce((a, b) => a + b, 0) - 1) > 0.001) {
        throw new Error("Malformed decision trace.");
    }
}
const summarize = rows => ({
    decisions: rows.length,
    mean_max_probability: mean(rows.map(row => row.confidence)),
    mean_margin: mean(rows.map(row => row.margin)),
    confidence_gate_pass_rate: rate(rows, row => row.outcome !== "low_confidence"),
    mean_options: mean(rows.map(row => row.options.length)),
    outcomes: counts(rows, row => row.outcome),
});
const ablations = readLines("incumbent-ablation.jsonl");
const analysis = {
    completed_decisions: decisions.length,
    outcomes: counts(decisions, row => row.outcome),
    rejection_reasons: counts(decisions.filter(row => row.rejection), row => row.rejection),
    groups: {
        top_choice_is_existing_target: summarize(decisions.filter(row => row.winner_is_incumbent)),
        top_choice_is_different_target: summarize(decisions.filter(row => !row.winner_is_incumbent)),
        existing_target_offered: summarize(decisions.filter(row => row.incumbent_in_options)),
        existing_target_not_offered: summarize(decisions.filter(row => !row.incumbent_in_options)),
    },
    by_option_count: {},
    incumbent_ablation: {
        samples: ablations.length,
        original_mean_confidence: mean(ablations.map(row => row.original.confidence)),
        hidden_mean_confidence: mean(ablations.map(row => row.hidden.confidence)),
        swapped_mean_confidence: mean(ablations.map(row => row.swapped.confidence)),
        original_gate_pass_rate: rate(ablations, row => row.original.passes_confidence),
        hidden_gate_pass_rate: rate(ablations, row => row.hidden.passes_confidence),
        swapped_gate_pass_rate: rate(ablations, row => row.swapped.passes_confidence),
        original_target_wins_rate: rate(ablations, row => row.original.winner_id === row.original_incumbent),
        original_target_wins_when_hidden_rate: rate(ablations, row => row.hidden.winner_id === row.original_incumbent),
        swapped_target_wins_rate: rate(ablations, row => row.swapped.winner_id === row.swapped_incumbent),
        hidden_winner_changed_rate: rate(ablations, row => row.hidden.winner_id !== row.original.winner_id),
    },
    limitations: [
        "One paired short run, not evidence of long-term park improvement or statistical significance.",
        "Native and model runs start from identical imported entity checksums, RNG state, and aggregate metrics.",
        "Asynchronous inference timing can vary between repeated model-enabled runs.",
        "Incumbent ablations change both the current_destination context and already_heading_here attributes.",
        "Ablations rescore fixed captured inputs; they do not simulate counterfactual park outcomes.",
        "Park metrics are population aggregates; treatment can change which guests remain in the park.",
    ],
};
for (let count = 2; count <= 8; count++) {
    const rows = decisions.filter(row => row.options.length === count);
    if (rows.length) analysis.by_option_count[count] = summarize(rows);
}
for (const variant of ["native", "model"]) {
    const rows = readLines(`${variant}-metrics.jsonl`);
    const expected = comparison.seconds_per_variant + 1;
    if (rows.length !== expected || rows.at(-1).tick - rows[0].tick
        !== comparison.seconds_per_variant * comparison.ticks_per_second) {
        throw new Error(`${variant} metrics do not cover the requested equal tick count.`);
    }
    const keys = ["elapsed_seconds", ...Object.keys(rows[0])];
    const csv = [keys.join(","), ...rows.map(row => keys.map(key =>
        key === "elapsed_seconds" ? row[key] ?? 0 : row[key]).join(","))].join("\r\n") + "\r\n";
    fs.writeFileSync(path.join(directory, `${variant}-metrics.csv`), csv);
}
fs.writeFileSync(path.join(directory, "analysis.json"), JSON.stringify(analysis, null, 2) + "\n");
console.log(JSON.stringify(analysis, null, 2));
