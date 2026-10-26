#!/usr/bin/env node
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync, spawn } from 'node:child_process';
import { createReadStream } from 'node:fs';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { canonical, fingerprint, label, permutations, render, rewardSpec, schema, sha, splits, trainingRow } from './d3-dataset.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const get = (name, fallback) => {
    const i = args.indexOf(`--${name}`);
    return i < 0 ? fallback : args[i + 1];
};
const output = path.resolve(get('output', path.join(root, 'bin', `d3-dataset-${new Date().toISOString().replace(/[:.]/g, '-')}`)));
const configPath = path.resolve(get('config', path.join(root, 'scripts', 'd3-dataset-config.json')));
const read = async file => JSON.parse(await fs.readFile(file, 'utf8'));
const write = async (file, data) => fs.writeFile(file, JSON.stringify(data, null, 2) + '\n');
const fileHash = async file => {
    const hash = createHash('sha256');
    for await (const chunk of createReadStream(file)) hash.update(chunk);
    return hash.digest('hex');
};

async function command(exe, argv, log, env = {}) {
    const handle = await fs.open(log, 'w');
    try {
        await new Promise((resolve, reject) => {
            const child = spawn(exe, argv, { cwd: path.join(root, 'bin'), env: { ...process.env, ...env },
                stdio: ['ignore', handle.fd, handle.fd], windowsHide: true });
            child.once('error', reject);
            child.once('exit', code => code === 0 ? resolve() : reject(new Error(`${path.basename(exe)} exited ${code}; see ${log}`)));
        });
    } finally {
        await handle.close();
    }
}

async function diskUsage(directory) {
    let bytes = 0;
    for (const entry of await fs.readdir(directory, { withFileTypes: true })) {
        const file = path.join(directory, entry.name);
        bytes += entry.isDirectory() ? await diskUsage(file) : (await fs.stat(file)).size;
    }
    return bytes;
}

async function main() {
    const config = await read(configPath);
    assert(config.maximum_cases > 0 && config.maximum_cases <= 5000);
    assert(config.maximum_branches <= 100000 && config.maximum_branches > 0);
    assert(config.replicas >= 2 && config.replicas <= 8);
    assert(Number.isInteger(config.workers) && config.workers >= 1 && config.workers <= 4);
    assert(config.maximum_seconds > 0 && config.maximum_seconds <= 86400);
    assert(config.maximum_bytes > config.checkpoint_reserve_bytes && config.checkpoint_reserve_bytes > 0);
    assert(config.episodes.length * config.capture_windows <= config.maximum_checkpoints);
    assert(config.horizons.every((t, i, a) => Number.isInteger(t) && t > 0 && t <= 48000 && (i === 0 || t > a[i - 1])));
    await fs.mkdir(output, { recursive: true });
    await fs.mkdir(path.join(output, 'audit'), { recursive: true });
    await fs.mkdir(path.join(output, 'review'), { recursive: true });
    const frozen = path.join(output, 'configuration.json');
    try {
        await fs.writeFile(frozen, JSON.stringify(config, null, 2) + '\n', { flag: 'wx' });
    } catch (error) {
        if (error.code !== 'EEXIST') throw error;
        assert.equal(canonical(await read(frozen)), canonical(config), 'Resuming with changed configuration is forbidden.');
    }
    const episodes = config.episodes;
    assert.deepEqual([...new Set(episodes.map(e => e.split))].sort(), [...splits].sort());
    assert.equal(new Set(episodes.map(e => e.id)).size, episodes.length);
    const park = path.join(root, config.park);
    const hashes = {
        park: await fileHash(park),
        tests_executable: await fileHash(path.join(root, 'bin', 'tests.exe')),
        tokenizer: await fileHash(path.join(root, 'bin', 'models', 'guest-decisions', 'd3-lite', 'tokenizer.json')),
        decision_config: await fileHash(path.join(root, 'bin', 'models', 'guest-decisions', 'd3-lite', 'decision_config.json')),
        readout: await fileHash(path.join(root, 'bin', 'models', 'guest-decisions', 'd3-lite', 'readout.f32')),
        llama_dll: await fileHash(path.join(root, 'bin', 'guest-thoughts', 'llama.dll')),
        tokenizer_gguf: await fileHash(path.join(root, 'bin', 'models', 'guest-decisions', 'd3-lite', 'd3-lite.gguf')),
        engine_source: await fileHash(path.join(root, 'src', 'openrct2', 'peep', 'GuestDecision.cpp')),
        replay_harness: await fileHash(path.join(root, 'test', 'tests', 'GuestDecisionDataset.cpp')),
        utility_rng_source: await fileHash(path.join(root, 'src', 'openrct2', 'util', 'Util.cpp')),
        utility_rng_header: await fileHash(path.join(root, 'src', 'openrct2', 'util', 'Util.h')),
        map_animation_source: await fileHash(path.join(root, 'src', 'openrct2', 'world', 'MapAnimation.cpp')),
        map_animation_header: await fileHash(path.join(root, 'src', 'openrct2', 'world', 'MapAnimation.h')),
        exporter: await fileHash(path.join(root, 'scripts', 'produce-d3-dataset.mjs')),
        labeler: await fileHash(path.join(root, 'scripts', 'd3-dataset.mjs')),
        tokenizer_helper: await fileHash(path.join(root, 'scripts', 'tokenize-d3-dataset.py')),
        reward_specification: sha(canonical(rewardSpec)),
    };
    const pinnedRuntime = await read(path.join(root, 'guest-thoughts.json'));
    assert.equal(pinnedRuntime.llamaRevision, 'd775b8967a46d8beb110d444aa3b8938179e0dd8');
    assert.equal(hashes.llama_dll, pinnedRuntime.runtimeHashes['llama.dll']);
    const prepared = await read(path.join(root, 'bin', 'models', 'guest-decisions', 'd3-lite', 'prepared.json'));
    assert.equal(prepared.revision, '03560c26f5199c3c04129a2ccb38960c913635e0');
    assert.equal(hashes.tokenizer_gguf, prepared.sha256);
    assert.equal(hashes.readout, prepared.readout);
    await write(path.join(output, 'engine-provenance.json'), {
        base_commit: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(),
        source_branch: execFileSync('git', ['branch', '--show-current'], { cwd: root, encoding: 'utf8' }).trim(),
        compiled_source_hashes: { engine: hashes.engine_source, harness: hashes.replay_harness,
            utility_rng: hashes.utility_rng_source, utility_rng_header: hashes.utility_rng_header,
            map_animation: hashes.map_animation_source, map_animation_header: hashes.map_animation_header },
        executable_sha256: hashes.tests_executable,
        note: 'The executable/hash pair and exact source files are retained. The final package replay manifest records the published source commit.',
    });
    const hashFile = path.join(output, 'source-hashes.json');
    try { assert.deepEqual(await read(hashFile), hashes, 'Source/model/runtime changed since capture.'); }
    catch (error) { if (error.code === 'ENOENT') await write(hashFile, hashes); else throw error; }
    await write(path.join(output, 'reward-specification.json'), rewardSpec);
    const modelConfig = await read(path.join(root, 'bin', 'models', 'guest-decisions', 'd3-lite', 'decision_config.json'));
    assert.equal(modelConfig.attention_mode, 'noncausal_full_attention');
    assert.equal(modelConfig.pooling, 'last');
    assert.deepEqual(modelConfig.codes.slice(0, 8), ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H']);
    await write(path.join(output, 'training-contract.json'), {
        input_schema: schema, classifier: 'd3-code-readout-v1', checkpoint: 'vllm-sr/d3-lite',
        checkpoint_revision: '03560c26f5199c3c04129a2ccb38960c913635e0',
        base_model: modelConfig.base_model, base_revision: modelConfig.revision,
        attention: modelConfig.attention_mode, pooling: modelConfig.pooling,
        readout_shape: [255, 1024], readout_sha256: hashes.readout,
        max_prompt_tokens: 1536, tokenizer_sha256: hashes.tokenizer,
        tokenization: { add_special: false, parse_special: true },
        inference: 'Score offered codes only; softmax at temperature 1. Align original/reversed option probabilities and average.',
        training: 'Classifier loss against offered-code targets. No chat completion, rationale or causal-LM target.',
        adapter: 'No training performed. Future trainer must validate noncausal attention and LoRA compatibility. Readout freezing versus training/saving is a future explicit choice.',
        gameplay: 'No caller enables the new format by default. Existing original and incumbent-neutral prompts are unchanged.',
        metadata: 'Only prompt is input text. Never concatenate IDs, code mappings, labels, split metadata, rewards or audit records.',
    });
    const start = Date.now();
    const deadline = start + config.maximum_seconds * 1000;
    let cases = [];
    for (const episode of episodes) {
        const directory = path.join(output, 'capture', String(episode.id));
        await fs.mkdir(directory, { recursive: true });
        const capture = {
            operation: 'capture', output: directory, park, episode: episode.id, split: episode.split,
            warmup_ticks: episode.warmup_ticks,
            maximum_cases: Math.min(config.cases_per_episode, config.maximum_cases - cases.length),
            capture_interval_ticks: config.capture_interval_ticks, capture_windows: config.capture_windows,
            cases_per_checkpoint: config.cases_per_checkpoint,
        };
        const result = path.join(directory, 'cases.json');
        try { await fs.access(result); }
        catch {
            if (Date.now() >= deadline) break;
            if (await diskUsage(output) + config.checkpoint_reserve_bytes > config.maximum_bytes)
                throw new Error('Checkpoint/disk cap reached before capture.');
            const job = path.join(directory, 'job.json');
            await write(job, capture);
            await command(path.join(root, 'bin', 'tests.exe'),
                ['--gtest_filter=GuestDecisionDataset.DISABLED_Offline', '--gtest_also_run_disabled_tests'],
                path.join(directory, 'capture.log'), { OPENRCT2_D3_DATASET_CONFIG: job });
        }
        cases.push(...await read(result));
        console.log(`Captured ${cases.length}/${config.maximum_cases} decision groups; episode ${episode.id}.`);
        if (cases.length >= config.maximum_cases) break;
    }
    assert(cases.length, 'No capture cases.');
    const usedGuests = new Map();
    for (const entry of cases) {
        assert.equal(render(entry), entry.prompt, 'Engine/export prompt mismatch.');
        const split = usedGuests.get(entry.guest_id);
        assert(!split || split === entry.split, 'Focal guest lineage crossed partitions.');
        usedGuests.set(entry.guest_id, entry.split);
    }
    // Round-robin partitions before applying rollout caps, rather than filling training first.
    const queues = splits.map(split => cases.filter(entry => entry.split === split)
        .sort((a, b) => sha(`${a.episode}:${a.guest_id}`).localeCompare(sha(`${b.episode}:${b.guest_id}`))));
    cases = [];
    while (queues.some(queue => queue.length)) for (const queue of queues) if (queue.length) cases.push(queue.shift());
    const hashSplits = new Map();
    for (const entry of cases) {
        for (const coarse of [false, true]) {
            const key = `${coarse}:${fingerprint(entry.features, coarse)}`;
            if (!hashSplits.has(key)) hashSplits.set(key, new Set());
            hashSplits.get(key).add(entry.split);
        }
    }
    const exclusions = [];
    const processed = [];
    let branches = 0;
    let executedBranches = 0;
    let budgetReached = false;
    let next = 0;
    async function processCase(index, entry) {
        const id = sha(`${entry.episode}:${entry.guest_id}:${entry.tick}:${fingerprint(entry.features)}`).slice(0, 24);
        entry.case_id = id;
        const cost = config.replicas * (entry.features.options.length + 1) + 1;
        if ([false, true].some(coarse => hashSplits.get(`${coarse}:${fingerprint(entry.features, coarse)}`).size > 1)) {
            exclusions.push({ id, split: entry.split, reason: 'cross_split_duplicate_or_coarse_near_duplicate' });
            return;
        }
        const directory = path.join(output, 'audit', id);
        const result = path.join(directory, 'rollouts.json');
        let exists = true;
        try { await fs.access(result); } catch { exists = false; }
        if (!exists && (Date.now() >= deadline || branches + cost > config.maximum_branches
            || await diskUsage(output) + config.checkpoint_reserve_bytes > config.maximum_bytes)) {
            budgetReached = true;
            exclusions.push({ id, split: entry.split, reason: 'bounded_run_budget_not_evaluated' });
            return;
        }
        // Reserve before spawning; workers must never overbook the branch cap.
        if (branches + cost > config.maximum_branches) {
            budgetReached = true;
            exclusions.push({ id, split: entry.split, reason: 'bounded_run_budget_not_evaluated' });
            return;
        }
        branches += cost;
        await fs.mkdir(directory, { recursive: true });
        if (!exists) {
            const results = [];
            async function branch(replica, option, name) {
                const branchDirectory = path.join(directory, name);
                await fs.mkdir(branchDirectory, { recursive: true });
                const job = path.join(branchDirectory, 'job.json');
                await write(job, { operation: 'branch', output: branchDirectory, case: entry,
                    horizons: config.horizons, replica, option });
                // Fresh process + fresh profile prevent runtime/external-state carry-over between interventions.
                await command(path.join(root, 'bin', 'tests.exe'),
                    ['--gtest_filter=GuestDecisionDataset.DISABLED_Offline', '--gtest_also_run_disabled_tests'],
                    path.join(branchDirectory, 'branch.log'), { OPENRCT2_D3_DATASET_CONFIG: job });
                executedBranches++;
                try { return { invalid: await read(path.join(branchDirectory, 'branch-invalid.json')) }; }
                catch (error) { if (error.code !== 'ENOENT') throw error; }
                return read(path.join(branchDirectory, 'branch.json'));
            }
            for (let replica = 0; replica < config.replicas; replica++) {
                for (let option = -1; option < entry.features.options.length; option++) {
                    const measured = await branch(replica, option, `replica-${replica}-option-${option}`);
                    if (measured.invalid) {
                        await write(path.join(directory, 'measurement-invalid.json'), measured.invalid);
                        exclusions.push({ id, split: entry.split, reason: 'native_validation_failed', details: measured.invalid });
                        console.log(`Excluded ${index + 1}/${cases.length}: initial native validation failed; no label.`);
                        return;
                    }
                    results.push(measured);
                }
            }
            const duplicate = await branch(0, -1, 'native-repeat');
            if (canonical(duplicate) !== canonical(results[0])) {
                await write(path.join(directory, 'replay-mismatch.json'), { expected: results[0], actual: duplicate });
                throw new Error(`Native cold-process replay diverged for ${id}; no labels accepted.`);
            }
            await write(result, { case: entry, horizons: config.horizons, restore_verified: true,
                native_replay_verified: true, fresh_process_per_branch: true,
                native_replay_sha256: sha(canonical(duplicate)), results });
        }
        const raw = await read(result);
        assert.equal(raw.case.prompt, entry.prompt);
        const outcome = label(raw);
        await write(path.join(directory, 'label.json'), outcome);
        processed.push({ entry, outcome, id });
        if (outcome.status === 'censored') exclusions.push({ id, split: entry.split, reason: 'censored', details: outcome.reasons });
        if (outcome.status === 'no_offered_improvement') exclusions.push({ id, split: entry.split, reason: 'no_offered_improvement' });
        console.log(`Measured ${index + 1}/${cases.length}: ${entry.split}, ${outcome.status}; ${branches} branch runs.`);
    }
    let failure;
    await Promise.all(Array.from({ length: config.workers }, async () => {
        while (!failure && next < cases.length) {
            const index = next++;
            try { await processCase(index, cases[index]); }
            catch (error) { failure = error; }
        }
    }));
    if (failure) throw failure;
    processed.sort((a, b) => a.id.localeCompare(b.id));
    exclusions.sort((a, b) => a.id.localeCompare(b.id));
    const observableGroups = new Map();
    for (const group of processed.filter(group => ['clear', 'ambiguous'].includes(group.outcome.status))) {
        const key = fingerprint(group.entry.features);
        if (!observableGroups.has(key)) observableGroups.set(key, []);
        observableGroups.get(key).push(group);
    }
    const observability = [];
    for (const [hash, groups] of observableGroups) {
        if (groups.length < 2) continue;
        const winningAttributes = groups.map(group => canonical(group.entry.features.options[
            group.outcome.utilities.indexOf(Math.max(...group.outcome.utilities))]));
        const conflict = new Set(winningAttributes).size > 1;
        observability.push({ feature_hash: hash, group_ids: groups.map(group => group.id), conflicting_winners: conflict });
        if (!conflict) continue;
        const votes = new Map();
        for (const group of groups) group.entry.features.options.forEach((option, i) => {
            const key = canonical(option);
            votes.set(key, (votes.get(key) || 0) + group.outcome.target_distribution[i] / groups.length);
        });
        for (const group of groups) {
            const keys = group.entry.features.options.map(canonical);
            group.outcome.target_distribution = keys.map(key => votes.get(key) / keys.filter(value => value === key).length);
            group.outcome.status = 'ambiguous';
            group.outcome.hard_target = null;
            group.outcome.weight = 0.1;
            group.outcome.observability_conflict = true;
            group.outcome.acceptable_options = group.outcome.target_distribution.flatMap((value, i) => value > 0 ? [i] : []);
            await write(path.join(output, 'audit', group.id, 'label.json'), group.outcome);
        }
    }
    await write(path.join(output, 'observability-report.json'), {
        exact_observable_duplicates: observability,
        note: 'Conflicting winners for indistinguishable inputs are pooled into soft targets; audit IDs never resolve ambiguity.',
    });
    const rows = [];
    const review = [];
    for (const { entry, outcome, id } of processed) {
        const renamed = structuredClone(entry);
        renamed.guest_id = 987654;
        renamed.entity_id = 456;
        renamed.incumbent_id = 999;
        renamed.park_label = 'A different anonymous park';
        renamed.audit_options = entry.audit_options.map((_, i) => ({ ride_id: 400 + i, name: `Renamed ${i}` }));
        assert.equal(render(renamed), entry.prompt);
        if (['clear', 'ambiguous'].includes(outcome.status)) {
            for (const [variant, order] of permutations(entry.features.options.length, parseInt(id.slice(0, 8), 16)).entries())
                rows.push(trainingRow(entry, outcome, order, id, variant));
        }
        // Test/calibration answers are locked, not exposed to the development review queue.
        if (['train', 'development'].includes(entry.split)) {
            const visibleOutcomes = outcome.status === 'censored'
                ? { status: outcome.status, reasons: outcome.reasons, components: outcome.components.filter(row => row.option >= 0) }
                : { status: outcome.status, utilities: outcome.utilities, per_replica_utilities: outcome.per_replica_utilities,
                    acceptable_options: outcome.acceptable_options, components: outcome.components.filter(row => row.option >= 0) };
            review.push({ id, split: entry.split, context: entry.features.state,
                options: entry.features.options, outcomes: visibleOutcomes, status: 'unreviewed',
                strata: { needs: entry.features.state.current_needs, candidate_count: entry.features.options.length,
                    nausea: entry.features.state.nausea.value >= 140, label_status: outcome.status,
                    unaffordable_option: entry.features.options.some(option => option.price_raw_tenths > entry.features.state.cash_raw_tenths),
                    native_relative_regret_case: outcome.advantage_over_native > 0.04 },
                annotation: { action: null, options: [], reason: '' },
                allowed_actions: ['accept', 'change', 'tie', 'unobservable', 'measurement issue'] });
        }
    }
    const promptFile = path.join(output, 'audit', 'tokenizer-input.jsonl');
    await fs.writeFile(promptFile, rows.map(row => JSON.stringify({ id: row.id, prompt: row.prompt })).join('\n') + (rows.length ? '\n' : ''));
    const tokenFile = path.join(output, 'audit', 'tokenization.jsonl');
    await command(get('python', 'python'), [path.join(root, 'scripts', 'tokenize-d3-dataset.py'),
        '--runtime', path.join(root, 'bin', 'guest-thoughts'),
        '--model', path.join(root, 'bin', 'models', 'guest-decisions', 'd3-lite', 'd3-lite.gguf'),
        '--input', promptFile, '--output', tokenFile], path.join(output, 'audit', 'tokenizer.log'));
    const tokens = new Map((await fs.readFile(tokenFile, 'utf8')).trim().split('\n').filter(Boolean)
        .map(line => { const value = JSON.parse(line); return [value.id, value]; }));
    const final = [];
    for (const row of rows) {
        assert(tokens.has(row.id));
        if (!tokens.get(row.id).within_budget) {
            exclusions.push({ id: row.group_id, reason: 'prompt_over_1536_tokens', variant: row.augmentation_index });
        } else final.push({ ...row, token_count: tokens.get(row.id).tokens,
            token_ids_sha256: tokens.get(row.id).token_ids_sha256 });
    }
    for (const split of splits) {
        const selected = final.filter(row => row.split === split);
        await fs.writeFile(path.join(output, `${split}.jsonl`), selected.map(row => JSON.stringify(row)).join('\n') + (selected.length ? '\n' : ''));
        const evaluation = processed.filter(group => group.entry.split === split).map(({ entry, outcome, id }) => ({
            schema, group_id: id, split, prompt: entry.prompt, prompt_sha256: sha(entry.prompt),
            valid_codes: entry.features.options.map((_, i) => String.fromCharCode(65 + i)),
            measurement_status: outcome.status, utilities: outcome.utilities || null,
            native_utilities: outcome.native_utilities || null,
            native_relative_advantage: outcome.advantage_over_native ?? null,
            note: 'Evaluation pool includes non-improving/censored cases. No destination target is fabricated for those cases.',
        }));
        await fs.writeFile(path.join(output, `evaluation-${split}.jsonl`),
            evaluation.map(row => JSON.stringify(row)).join('\n') + (evaluation.length ? '\n' : ''));
    }
    await write(path.join(output, 'audit', 'exclusions.json'), exclusions);
    await write(path.join(output, 'audit', 'case-index.json'), cases);
    await write(path.join(output, 'review', 'queue.json'), review);
    const bySplit = Object.fromEntries(splits.map(split => [split, {
        captured_groups: cases.filter(entry => entry.split === split).length,
        measured_groups: processed.filter(group => group.entry.split === split).length,
        retained_groups: new Set(final.filter(row => row.split === split).map(row => row.group_id)).size,
        augmented_rows: final.filter(row => row.split === split).length,
    }]));
    const coverage = { goals: {}, candidate_counts: {}, needs: {}, outcome_status: {}, exclusion_reasons: {} };
    for (const entry of cases) {
        const count = entry.features.options.length;
        coverage.candidate_counts[count] = (coverage.candidate_counts[count] || 0) + 1;
        for (const need of entry.features.state.current_needs) coverage.needs[need] = (coverage.needs[need] || 0) + 1;
        for (const goal of new Set(entry.features.options.map(option => option.goal)))
            coverage.goals[goal] = (coverage.goals[goal] || 0) + 1;
    }
    for (const group of processed) coverage.outcome_status[group.outcome.status] = (coverage.outcome_status[group.outcome.status] || 0) + 1;
    for (const exclusion of exclusions) coverage.exclusion_reasons[exclusion.reason] = (coverage.exclusion_reasons[exclusion.reason] || 0) + 1;
    await write(path.join(output, 'coverage.json'), coverage);
    const attributeSignature = option => canonical(Object.fromEntries(Object.entries(option)
        .filter(([key]) => key !== 'distance_tiles' && key !== 'queue_guests')));
    const trainingIds = new Set(final.filter(row => row.split === 'train').map(row => row.group_id));
    const trainingCombinations = new Set(cases.filter(entry => trainingIds.has(entry.case_id))
        .flatMap(entry => entry.features.options.map(attributeSignature)));
    await write(path.join(output, 'unseen-attribute-stress.json'), Object.fromEntries(splits.map(split => [split, {
        groups: cases.filter(entry => entry.split === split).length,
        groups_with_unseen_attribute_combination: cases.filter(entry => entry.split === split
            && entry.features.options.some(option => !trainingCombinations.has(attributeSignature(option)))).length,
        interpretation: 'Within-park attribute-combination stress check, not an unseen-park test.',
    }])));
    const manifest = {
        schema, status: 'provisional-automatic-unreviewed', produced_at: new Date().toISOString(),
        target_production_groups: 5000, configured_maximum_cases: config.maximum_cases,
        captured_groups: cases.length, measured_groups: processed.length,
        retained_groups: new Set(final.map(row => row.group_id)).size, augmented_rows: final.length,
        retained_focal_guest_lineages: new Set(processed.filter(group => final.some(row => row.group_id === group.id))
            .map(group => group.entry.guest_id)).size,
        retained_episode_families: new Set(processed.filter(group => final.some(row => row.group_id === group.id))
            .map(group => group.entry.episode)).size,
        branch_budget_reserved: branches, branches_executed_this_invocation: executedBranches,
        bounded_run_budget_reached: budgetReached, elapsed_seconds: (Date.now() - start) / 1000,
        budget_semantics: 'Disk/branch limits checked before dispatch. maximum_seconds stops new dispatch; each in-progress atomic group may finish after that deadline.',
        by_split: bySplit, hashes, horizons: config.horizons, replicas: config.replicas,
        test_locked: true, human_reviews_completed: 0, training_performed: false,
        gates_changed: false, identity_invariance_passed: true, source_prompt_roundtrip_passed: true,
        partition_guest_lineage_check_passed: true,
        checkpoint_policy: 'All nonauthoring park chunks, utility RNG and state-changing map update queues fingerprinted. Authoring chunk 1 alone excluded for wall-clock timestamps. Uninterrupted 256-tick source probe matches restored and cold-native replay. Each branch runs in a fresh process/profile. Full native continuation repeated and compared in full.',
        limitations: [
            'Blackpool-only. No cross-park generalization claim; attributes can still fingerprint rides statistically.',
            'Seeded descendants of one public starting park. Focal guest partitions are disjoint, background guests and physical destinations overlap.',
            'All labels are automatic and unreviewed. Matched initial RNG seeds do not ensure matched random draws after different actions.',
            'Two or more RNG replicas estimate limited sensitivity, not population uncertainty or calibrated probabilities.',
            'Decision groups are independently measured interventions, not IID samples: guest lineages, checkpoints and one park induce correlation.',
            'Fixed-horizon native continuation after one intervention, not a closed-loop trained-policy evaluation.',
            'Supervised JSONL contains only choices with measured native advantage above 0.01. Use evaluation-*.jsonl, not that selected subset, for later selective-policy evaluation.',
            'Conservative censoring can select for shorter journeys; excluded evidence is retained.',
            'Coarse near-duplicate check is a specified bucketing diagnostic, not proof against every approximate match.',
            'No model inference used for capture; original-versus-neutral disagreement is not an available review stratum.',
            'The 5000-group goal is not claimed achieved unless actual independent retained totals reach it.',
        ],
    };
    await write(path.join(output, 'manifest.json'), manifest);
    await write(path.join(output, 'split-seed-manifest.json'), { episodes,
        guest_assignment: 'Partition(Mix(peepId)): uint32 avalanche 7feb352d/846ca68b; modulo20 cutpoints12/15/17.',
        rng: 's0=Mix(episode+74101), s1=Mix(episode+97613). Utility mt19937 seeded Mix(episode+38471); its complete checkpoint state is restored for each branch. Replica 0 restored scenario RNG; replica r>0: Mix(tick+r*173+241), Mix(tick+r*619+991).',
        old_evaluations: 'Not imported; original historical episode lineage remains development evidence only.',
        sampling: 'Native-only walking guests, 2-8 native choices, once per guest/episode. Deterministic hashed priority within tick windows. Inclusion probabilities unknown; sampling_weight=1 is not population correction.',
    });
    await write(path.join(output, 'input-contract.json'), {
        schema, state_features: stateKeysForContract(), option_features: ['goal', 'distance_tiles', 'price_raw_tenths', 'queue_guests', 'excitement', 'intensity', 'nausea'],
        excluded: ['ride/shop/guest names and IDs', 'park identity', 'coordinates', 'incumbent hints', 'model scores', 'outcomes', 'seeds'],
        choice_identity: 'Question-local A-H only. code_to_local_option joins audit mapping but is never input text.',
        units: 'Distance is Manhattan tiles, not route length. Price is raw tenths; ratings use engine decimal ratings; queue count is guests.',
    });
    const retained = processed.filter(group => final.some(row => row.group_id === group.id));
    const diagnostics = [];
    for (const split of splits) {
        const groups = retained.filter(group => group.entry.split === split);
        const trained = retained.filter(group => group.entry.split === 'train');
        const lookup = new Map();
        for (const group of trained) group.entry.audit_options.forEach((option, i) => {
            if (!lookup.has(option.ride_id)) lookup.set(option.ride_id, []);
            lookup.get(option.ride_id).push(group.outcome.utilities[i]);
        });
        const scores = groups.map(group => {
            const attrs = group.entry.features.options;
            const heuristic = attrs.map(option => -(option.distance_tiles || 0) / 100 - (option.queue_guests || 0) / 20
                + (option.excitement || 0) / 10);
            const idOnly = group.entry.audit_options.map(option => {
                const values = lookup.get(option.ride_id) || [];
                return values.length ? values.reduce((a, b) => a + b) / values.length : 0.5;
            });
            const regret = values => Math.max(...group.outcome.utilities)
                - group.outcome.utilities[values.indexOf(Math.max(...values))];
            return { attribute_regret: regret(heuristic), audit_id_lookup_regret: regret(idOnly) };
        });
        diagnostics.push({ split, groups: groups.length,
            attribute_mean_regret: scores.length ? scores.reduce((sum, row) => sum + row.attribute_regret, 0) / scores.length : null,
            audit_id_lookup_mean_regret: scores.length ? scores.reduce((sum, row) => sum + row.audit_id_lookup_regret, 0) / scores.length : null });
    }
    await write(path.join(output, 'shortcut-diagnostics.json'), {
        results: diagnostics,
        attribute_heuristic: '-distance/100 - queue/20 + excitement/10; diagnostic only, not tuned.',
        audit_id_lookup: 'Mean training utility by audit ride ID; missing IDs=0.5; forbidden training feature. Training scores are in-sample.',
        interpretation: 'This is a leakage/shortcut diagnostic, not a policy evaluation or cross-park evidence.',
    });
    const card = `# D3-Lite neutral-anonymous dataset\n\n**Provisional, automatic, unreviewed. No fine-tuning performed.**\n\n`
        + `Captured ${cases.length}; measured ${processed.length}; retained ${manifest.retained_groups} unaugmented decision groups `
        + `(${final.length} augmented rows). Production goal: 5,000, not an achieved claim.\n\n`
        + `Fixed tick horizons: ${config.horizons.join(', ')}. ${config.replicas} matched RNG replicas per action plus exact native replay. `
        + `Rewards were frozen before capture; test/calibration labels must not be used to tune weights or select models.\n\n`
        + manifest.limitations.map(line => `- ${line}`).join('\n') + '\n\n'
        + `Only the prompt field is model input. JSONL uses the pinned D3 classifier's offered-code targets, not chat/SFT labels. `
        + `See training-contract.json and reward-specification.json.\n\n`
        + `No offered improvement over native means no destination training target. evaluation-*.jsonl preserves those cases `
        + `and censored cases, separately from the selected supervised subset. Never evaluate fallback coverage only on the supervised subset.\n\n`
        + `Review queue exposes train/development only. Native targets and model scores are not answer hints. `
        + `Annotations are empty. Detailed audit files contain identities and must never be concatenated into training text.\n\n`
        + `Replay checkpoints remain in local ignored capture directories, not this distributable bundle. No game assets, models or binaries are included. `
        + `The source hash, seed/config manifests and capture recipe reproduce them with the required original RCT2 objects installed. `
        + `Failed restores/replays abort collection rather than generating labels.\n`;
    await fs.writeFile(path.join(output, 'DATASET-CARD.md'), card);
    await fs.writeFile(path.join(output, 'review', 'README.txt'),
        'Blinded model/identity-free review queue: queue.json. No annotations are completed.\n'
        + 'Use accept/change/tie/unobservable/measurement issue and explain the reason. Do not edit locked calibration/test labels.\n');
    await write(path.join(output, 'validation.json'), {
        schema_valid: true, exact_engine_prompt_roundtrip: true, identity_renaming_invariant: true,
        partitions_isolate_focal_guests: true, cross_partition_exact_and_bucketed_duplicates_quarantined: true,
        permutation_labels_remapped: true, tokenization: 'Pinned native vocab; actual token IDs hashed; no weights loaded.',
        token_budget: 1536, locked_partitions: ['calibration', 'test'], unreviewed: true,
    });
    console.log(JSON.stringify(manifest, null, 2));
}

function stateKeysForContract() {
    return ['activity', 'carrying_food_or_drink', 'cash_raw_tenths', 'current_needs', 'energy', 'happiness', 'hunger',
        'nausea', 'nausea_tolerance_0_to_3', 'preferred_intensity_max', 'preferred_intensity_min', 'thirst', 'toilet'];
}

await main();
