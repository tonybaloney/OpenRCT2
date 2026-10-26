#!/usr/bin/env node
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { canonical, fingerprint, label, render, sha, splits, trainingRow } from './d3-dataset.mjs';

const directory = path.resolve(process.argv[2] || '');
const read = async file => JSON.parse(await fs.readFile(path.join(directory, file), 'utf8'));
const manifest = await read('manifest.json');
const rowSchema = JSON.parse(await fs.readFile(new URL('./d3-dataset-row.schema.json', import.meta.url), 'utf8'));
const schemaKeywords = new Set(['$schema', '$defs', '$comment', 'title', '$ref', 'anyOf', 'const', 'enum', 'type',
    'minimum', 'maximum', 'exclusiveMinimum', 'minLength', 'pattern', 'minItems', 'maxItems', 'uniqueItems',
    'items', 'required', 'additionalProperties', 'properties']);
function validateShape(value, rule) {
    for (const key of Object.keys(rule)) assert(schemaKeywords.has(key), `Unsupported row-schema keyword ${key}`);
    if (rule.$ref) {
        assert.equal(Object.keys(rule).length, 1, 'Unsupported schema reference siblings');
        assert(rule.$ref.startsWith('#/$defs/'));
        return validateShape(value, rowSchema.$defs[rule.$ref.split('/').at(-1)]);
    }
    if (rule.anyOf) {
        assert(rule.anyOf.some(candidate => {
            try { validateShape(value, candidate); return true; } catch { return false; }
        }), 'Formal schema alternative mismatch');
    }
    if ('const' in rule) assert.deepEqual(value, rule.const);
    if (rule.enum) assert(rule.enum.includes(value), 'Formal schema enum mismatch');
    if (rule.type) {
        const type = value === null ? 'null' : Array.isArray(value) ? 'array' : typeof value;
        assert(rule.type === 'integer' ? Number.isInteger(value) : type === rule.type, 'Formal schema type mismatch');
    }
    if (typeof value === 'number') {
        assert(Number.isFinite(value), 'Nonfinite schema number');
        if ('minimum' in rule) assert(value >= rule.minimum, 'Formal schema minimum');
        if ('maximum' in rule) assert(value <= rule.maximum, 'Formal schema maximum');
        if ('exclusiveMinimum' in rule) assert(value > rule.exclusiveMinimum, 'Formal schema exclusive minimum');
    }
    if (typeof value === 'string') {
        if (rule.minLength) assert([...value].length >= rule.minLength);
        if (rule.pattern) assert(new RegExp(rule.pattern).test(value), 'Formal schema pattern mismatch');
    }
    if (Array.isArray(value)) {
        if (rule.minItems) assert(value.length >= rule.minItems);
        if (rule.maxItems) assert(value.length <= rule.maxItems);
        if (rule.uniqueItems) assert.equal(new Set(value.map(canonical)).size, value.length);
        if (rule.items) for (const item of value) validateShape(item, rule.items);
    } else if (value !== null && typeof value === 'object') {
        for (const key of rule.required || []) assert(Object.hasOwn(value, key), `Missing schema field ${key}`);
        for (const [key, item] of Object.entries(value)) {
            if (rule.additionalProperties === false) assert(Object.hasOwn(rule.properties, key), `Extra schema field ${key}`);
            if (rule.properties?.[key]) validateShape(item, rule.properties[key]);
        }
    }
}
const cases = await read(path.join('audit', 'case-index.json'));
const index = new Map(cases.map(entry => [entry.case_id, entry]));
assert.equal(index.size, cases.length);
const guests = new Map();
for (const entry of cases) {
    assert.equal(render(entry), entry.prompt);
    assert.equal(sha(entry.utility_rng_state), entry.world.utility_rng, 'Missing/mismatched utility RNG checkpoint');
    assert.equal(sha(JSON.stringify(entry.map_update_queues)), entry.world.map_update_queues,
        'Missing/mismatched map update queues');
    assert(!guests.has(entry.guest_id) || guests.get(entry.guest_id) === entry.split);
    guests.set(entry.guest_id, entry.split);
}
const tokenizations = new Map((await fs.readFile(path.join(directory, 'audit', 'tokenization.jsonl'), 'utf8'))
    .trim().split('\n').filter(Boolean).map(line => { const row = JSON.parse(line); return [row.id, row]; }));
const seenIds = new Set();
const groupSplits = new Map();
const fingerprints = new Map();
const evidence = new Map();
const measuredEvidence = new Map();
const evaluationBySplit = {};
const exclusions = await read(path.join('audit', 'exclusions.json'));
for (const split of splits) {
    const rows = (await fs.readFile(path.join(directory, `evaluation-${split}.jsonl`), 'utf8'))
        .trim().split('\n').filter(Boolean).map(line => JSON.parse(line));
    evaluationBySplit[split] = rows.length;
    assert.equal(rows.length, manifest.by_split[split].measured_groups);
    for (const row of rows) {
        assert(!measuredEvidence.has(row.group_id), 'Duplicate evaluation group');
        const entry = index.get(row.group_id);
        assert(entry && entry.split === split && row.split === split);
        assert.equal(row.prompt, render(entry));
        assert.equal(row.prompt_sha256, sha(row.prompt));
        assert.deepEqual(row.valid_codes, entry.features.options.map((_, i) => String.fromCharCode(65 + i)));
        for (const coarse of [false, true]) {
            const key = `${coarse}:${fingerprint(entry.features, coarse)}`;
            assert(!fingerprints.has(key) || fingerprints.get(key) === split, 'Cross-split evaluation duplicate');
            fingerprints.set(key, split);
        }
        const raw = await read(path.join('audit', row.group_id, 'rollouts.json'));
        assert.deepEqual(raw.case, entry);
        assert.equal(raw.restore_verified, true);
        assert.equal(raw.native_replay_verified, true);
        assert.equal(raw.fresh_process_per_branch, true);
        assert.deepEqual(raw.horizons, manifest.horizons);
        assert.equal(raw.results.length, manifest.replicas * (entry.features.options.length + 1));
        const actions = new Set();
        for (const result of raw.results) {
            assert(Number.isInteger(result.replica) && result.replica >= 0 && result.replica < manifest.replicas);
            assert(Number.isInteger(result.option) && result.option >= -1 && result.option < entry.features.options.length);
            const key = `replica-${result.replica}-option-${result.option}`;
            assert(!actions.has(key), 'Duplicate counterfactual branch');
            actions.add(key);
            assert.deepEqual(result, await read(path.join('audit', row.group_id, key, 'branch.json')));
        }
        assert.equal(entry.uninterrupted_probe.matched, true);
        assert.deepEqual(raw.results[0].probe_world_256, entry.uninterrupted_probe.world);
        const repeat = await read(path.join('audit', row.group_id, 'native-repeat', 'branch.json'));
        assert.deepEqual(repeat, raw.results[0], 'Native repeat differs from baseline');
        assert.equal(raw.native_replay_sha256, sha(canonical(repeat)));
        const measured = label(raw);
        assert.deepEqual(row.utilities, measured.utilities || null);
        assert.deepEqual(row.native_utilities, measured.native_utilities || null);
        assert.equal(row.native_relative_advantage, measured.advantage_over_native ?? null);
        measuredEvidence.set(row.group_id, { entry, measured, row });
    }
}
const observableGroups = new Map();
for (const group of measuredEvidence.values()) {
    if (!['clear', 'ambiguous'].includes(group.measured.status)) continue;
    const key = fingerprint(group.entry.features);
    if (!observableGroups.has(key)) observableGroups.set(key, []);
    observableGroups.get(key).push(group);
}
for (const groups of observableGroups.values()) {
    const winners = groups.map(({ entry, measured }) =>
        canonical(entry.features.options[measured.utilities.indexOf(Math.max(...measured.utilities))]));
    if (new Set(winners).size < 2) continue;
    const votes = new Map();
    for (const { entry, measured } of groups) entry.features.options.forEach((option, i) => {
        const key = canonical(option);
        votes.set(key, (votes.get(key) || 0) + measured.target_distribution[i] / groups.length);
    });
    for (const { entry, measured } of groups) {
        const keys = entry.features.options.map(canonical);
        measured.target_distribution = keys.map(key => votes.get(key) / keys.filter(value => value === key).length);
        Object.assign(measured, { status: 'ambiguous', hard_target: null, weight: 0.1, observability_conflict: true });
        measured.acceptable_options = measured.target_distribution.flatMap((value, i) => value > 0 ? [i] : []);
    }
}
for (const [id, { measured, row }] of measuredEvidence) {
    const stored = await read(path.join('audit', id, 'label.json'));
    assert.deepEqual(stored, measured, 'Stored targets do not match measured outcome evidence');
    assert.equal(row.measurement_status, measured.status);
    evidence.set(id, measured);
}
for (const exclusion of exclusions.filter(row => row.reason === 'native_validation_failed')) {
    assert(!measuredEvidence.has(exclusion.id), 'Invalid measurement entered evaluation');
    assert.equal((await read(path.join('audit', exclusion.id, 'measurement-invalid.json'))).status, 'invalid');
}
const rowsBySplit = {};
for (const split of splits) {
    const rows = (await fs.readFile(path.join(directory, `${split}.jsonl`), 'utf8')).trim().split('\n')
        .filter(Boolean).map(line => JSON.parse(line));
    rowsBySplit[split] = rows.length;
    for (const row of rows) {
        validateShape(row, rowSchema);
        assert(!seenIds.has(row.id), 'Duplicate row ID');
        seenIds.add(row.id);
        assert.equal(row.split, split);
        assert(!groupSplits.has(row.group_id) || groupSplits.get(row.group_id) === split);
        groupSplits.set(row.group_id, split);
        const entry = index.get(row.group_id);
        assert(entry);
        assert.equal(entry.split, split);
        for (const coarse of [false, true]) {
            const key = `${coarse}:${fingerprint(entry.features, coarse)}`;
            assert(!fingerprints.has(key) || fingerprints.get(key) === split, 'Cross-split duplicate');
            fingerprints.set(key, split);
        }
        assert(evidence.has(row.group_id), 'Training group absent from evaluation pool');
        assert(['clear', 'ambiguous'].includes(evidence.get(row.group_id).status));
        const expected = trainingRow(entry, evidence.get(row.group_id), row.code_to_local_option, row.group_id, row.augmentation_index);
        const { token_count, token_ids_sha256, ...actual } = row;
        assert.deepEqual(actual, expected, 'Label/code/prompt alignment mismatch');
        assert.equal(row.prompt_sha256, sha(row.prompt));
        const tokenization = tokenizations.get(row.id);
        assert(tokenization && tokenization.within_budget);
        assert.equal(token_count, tokenization.tokens);
        assert.equal(token_ids_sha256, tokenization.token_ids_sha256);
        const renamed = structuredClone(entry);
        renamed.park = 'Not Blackpool';
        renamed.guest_id = 987;
        renamed.entity_id = 654;
        renamed.audit_options = entry.audit_options.map((_, i) => ({ ride_id: 400 + i, name: `Renamed ${i}` }));
        assert.equal(render(renamed, row.code_to_local_option), row.prompt);
    }
    assert.equal(rows.length, manifest.by_split[split].augmented_rows);
}
const reviews = await read(path.join('review', 'queue.json'));
for (const review of reviews) {
    assert(['train', 'development'].includes(review.split));
    assert.deepEqual(review.annotation, { action: null, options: [], reason: '' });
    assert.equal(review.status, 'unreviewed');
    assert(!canonical(review).includes('incumbent_id'));
}
assert.equal(manifest.retained_groups, groupSplits.size);
assert.equal(manifest.augmented_rows, seenIds.size);
assert.equal(manifest.measured_groups, measuredEvidence.size);
assert.equal(manifest.training_performed, false);
assert.equal(manifest.human_reviews_completed, 0);
const result = { passed: true, rows: seenIds.size, groups: groupSplits.size, by_split: rowsBySplit,
    evaluation_groups: measuredEvidence.size, evaluation_by_split: evaluationBySplit,
    formal_row_schema: true,
    raw_targets_recomputed: true, native_replay_evidence: true, invalid_measurements_excluded: true,
    exact_prompts: true, remapped_targets: true, identities_excluded: true,
    split_isolation: true, native_token_counts: true, reviews_unmodified: true };
await fs.writeFile(path.join(directory, 'independent-audit.json'), JSON.stringify(result, null, 2) + '\n');
console.log(JSON.stringify(result, null, 2));
