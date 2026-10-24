import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const fixture = fs.mkdtempSync(path.join(root, "bin", "d3-dashboard-fixture-"));
const suiteDirectory = path.join(fixture, "suite");
const output = path.join(fixture, "report");
fs.mkdirSync(suiteDirectory);
const write = (file, value) => fs.writeFileSync(file, JSON.stringify(value));
const lines = (file, rows) => fs.writeFileSync(file, rows.map(row => JSON.stringify(row)).join("\n") + "\n");
const ids = ["d3-lite-original", "d3-lite-incumbent-neutral", "d3-edge-original", "d3-edge-incumbent-neutral"];
const metrics = Array.from({ length: 3 }, (_, i) => ({
    tick: i * 40, elapsed_seconds: i, guests: 200, mean_happiness: 150+i, park_rating: 700+i,
    hungry: 10-i, thirsty: 3, sick: 20, queuing: 40, cash_raw_tenths: 10000+i*20,
    ride_customers: 100+i, shop_customers: 20+i,
}));
const suite = {
    schema: 1, source_revision: "fixture-only-not-a-real-evaluation", seconds_per_variant: 2,
    minimum_probability: 0.45, minimum_margin: 0.08, runs: ids,
    park_sha256: "FIXTURE", completed_utc: "2026-10-25T00:00:00Z",
};
write(path.join(suiteDirectory, "suite.json"), suite);
for (const id of ids) {
    const directory = path.join(suiteDirectory, id);
    fs.mkdirSync(directory);
    const neutral = id.endsWith("incumbent-neutral");
    const variant = neutral ? "incumbent-neutral" : "original";
    const model = id.startsWith("d3-lite") ? "d3-lite" : "d3-edge";
    const options = [{ code:"A", ride_id:1, probability:0.7, display_name:"Fixture ride", attributes:{} },
        { code:"B", ride_id:2, probability:0.3, display_name:"Fixture shop", attributes:{} }];
    const events = ["pending", "applied", "pending", "low_confidence"].map((outcome, i) => ({
        schema:1, prompt_variant:variant, minimum_probability:0.45, minimum_margin:0.08,
        outcome, prompt:neutral ? "State and options" : "State: current_destination already_heading_here",
        state:{current_needs:[]}, options:i<2 ? options : options.map(option=>({...option,probability:0.5})),
        confidence:i<2 ? 0.7 : 0.5, margin:i<2 ? 0.4 : 0, elapsed_ms:120+i,
        incumbent_in_options:i<2, guest_id:i, incumbent_id:1, winner_id:1,
    }));
    const part = {
        submitted:2, accepted:1, low_confidence:1, stale:0, pending:0,
        start:metrics[0], end:metrics.at(-1), time_mean:{mean_happiness:151.5},
    };
    write(path.join(directory,"comparison.json"), {
        model, prompt_variant:variant, seconds_per_variant:2, ticks_per_second:40,
        minimum_probability:0.45, minimum_margin:0.08, ablation_samples:0,
        initial_state:{entity_checksum:"fixture",rng:[1,2],metrics:metrics[0]}, model_on:part, native_off:part,
    });
    write(path.join(directory,"analysis.json"), {
        completed_decisions:2, incumbent_ablation:{samples:0},
    });
    write(path.join(directory,"metadata.json"),{fixture_only:true});
    lines(path.join(directory,"model-decisions.jsonl"),events);
    lines(path.join(directory,"model-metrics.jsonl"),metrics);
    lines(path.join(directory,"native-metrics.jsonl"),metrics);
    for(const file of ["model-metrics.csv","native-metrics.csv","incumbent-ablation.jsonl","run.log","run.stderr","test-results.xml"])
        fs.writeFileSync(path.join(directory,file),"");
}
const builder = path.join(root,"scripts","build-d3-eval-dashboard.mjs");
const build = target => execFileSync(process.execPath,[builder,suiteDirectory,target],{encoding:"utf8",stdio:"pipe"});
build(output);
const report = JSON.parse(fs.readFileSync(path.join(output,"eval-data.json"),"utf8"));
assert.equal(report.runs.length,4);
assert.equal(report.runs[0].redirect_rate,0.5);
assert.equal(report.runs[0].fallback_rate,0.5);
assert.equal(report.runs[0].latency_p95_ms,123);
assert.equal(report.runs[0].confidence_present,0.7);
assert.equal(report.runs[0].confidence_absent,0.5);
assert.equal(report.evidence.files.length,48);
assert.ok(fs.readFileSync(path.join(output,"dashboard.html"),"utf8").includes('data-view="dashboard"'));
assert.ok(fs.readFileSync(path.join(output,"slides.html"),"utf8").includes('data-view="slides"'));
assert.throws(()=>build(output),/Command failed/);
const neutralTrace = path.join(suiteDirectory,ids[1],"model-decisions.jsonl");
const saved = fs.readFileSync(neutralTrace,"utf8");
const tampered = JSON.parse(saved.split("\n")[0]);
tampered.prompt = "already_heading_here";
fs.writeFileSync(neutralTrace,JSON.stringify(tampered)+"\n"+saved.split("\n").slice(1).join("\n"));
assert.throws(()=>build(path.join(fixture,"must-reject-hints")),/Command failed/);
assert.ok(!fs.existsSync(path.join(fixture,"must-reject-hints")));
fs.writeFileSync(neutralTrace,saved);
const comparisonPath=path.join(suiteDirectory,ids[1],"comparison.json");
const comparison=JSON.parse(fs.readFileSync(comparisonPath,"utf8"));
comparison.initial_state.rng=[3,4];write(comparisonPath,comparison);
assert.throws(()=>build(path.join(fixture,"must-reject-rng")),/Command failed/);
assert.ok(!fs.existsSync(path.join(fixture,"must-reject-rng")));
comparison.initial_state.rng=[1,2];write(comparisonPath,comparison);
const badGate=JSON.parse(saved.split("\n")[1]);
badGate.outcome="low_confidence";
fs.writeFileSync(neutralTrace,saved.split("\n")[0]+"\n"+JSON.stringify(badGate)+"\n"+saved.split("\n").slice(2).join("\n"));
assert.throws(()=>build(path.join(fixture,"must-reject-gate")),/Command failed/);
assert.ok(!fs.existsSync(path.join(fixture,"must-reject-gate")));
console.log("PASS: four-model summaries, exact rates, queue-inclusive latency, output isolation, hint rejection, gate validation and initial-RNG validation.");
if(process.env.KEEP_D3_DASHBOARD_FIXTURE==="1") console.log("FIXTURE ONLY (not deliverable evaluation data): "+output);
else fs.rmSync(fixture,{recursive:true});
