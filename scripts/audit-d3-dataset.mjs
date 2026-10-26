#!/usr/bin/env node
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { canonical, fingerprint, label, render, sha, splits, trainingRow } from './d3-dataset.mjs';

const directory = path.resolve(process.argv[2] || '');
const read = async file => JSON.parse(await fs.readFile(path.join(directory, file), 'utf8'));
const manifest = await read('manifest.json');
const cases = await read(path.join('audit', 'case-index.json'));
const index = new Map(cases.map(entry => [entry.case_id, entry]));
assert.equal(index.size, cases.length);
const guests = new Map();
for (const entry of cases) {
    assert.equal(render(entry), entry.prompt);
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
    assert.equal(review.annotation.action, null);
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
    raw_targets_recomputed: true, native_replay_evidence: true, invalid_measurements_excluded: true,
    exact_prompts: true, remapped_targets: true, identities_excluded: true,
    split_isolation: true, native_token_counts: true, reviews_unmodified: true };
await fs.writeFile(path.join(directory, 'independent-audit.json'), JSON.stringify(result, null, 2) + '\n');
console.log(JSON.stringify(result, null, 2));
