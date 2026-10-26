import assert from 'node:assert/strict';
import { test } from 'node:test';
import { fingerprint, horizonSample, label, permutations, render, rewardSpec, trainingRow, utility, validateFeatures } from '../../scripts/d3-dataset.mjs';

function entry() {
    const state = {
        activity: 'walking', carrying_food_or_drink: false, cash_raw_tenths: 100,
        current_needs: ['hungry'], preferred_intensity_max: 8, preferred_intensity_min: 3, nausea_tolerance_0_to_3: 2,
        happiness: { value: 100, range: [0, 255], feeling: 'content' },
        energy: { value: 128, normal_range: [32, 128], feeling: 'rested' },
        hunger: { value: 8, range: [0, 255], direction: 'lower means hungrier', feeling: 'hungry' },
        thirst: { value: 255, range: [0, 255], direction: 'lower means thirstier', feeling: 'hydrated' },
        nausea: { value: 0, range: [0, 255], feeling: 'well' },
        toilet: { value: 0, range: [0, 255], feeling: 'comfortable' },
    };
    const options = [{ goal: 'food', distance_tiles: 10, price_raw_tenths: 12, queue_guests: 0 },
        { goal: 'ride', distance_tiles: 40, price_raw_tenths: 10, queue_guests: 2, excitement: 5, intensity: 4, nausea: 2 }];
    return { split: 'train', features: { state, options }, state_json: JSON.stringify(state),
        option_json: options.map(JSON.stringify), audit_options: [{ ride_id: 1 }, { ride_id: 2 }] };
}

function sample(happiness = 128, need = 0, walking = 0, queue = 0, tick = 100) {
    return { tick, walking_ticks: walking, queue_ticks: queue,
        integrals: { hunger_burden: need * tick, thirst_burden: need * tick, toilet_burden: need * tick,
            nausea_burden: need * tick, energy_burden: need * tick * 96 / 255, happiness: happiness * tick } };
}

function raw() {
    return { case: entry(), restore_verified: true, native_replay_verified: true, horizons: [100],
        results: [0, 1].flatMap(replica => [-1, 0, 1].map(option => ({
            replica, option, initial_rng: [1 + replica, 2], terminal: '', observed_ticks: 100,
            initial_target_resolution: 'target_ride_or_facility_exit',
            end: { state: 5 }, samples: [sample(option === 0 ? 255 : 0, option === 0 ? 0 : 128)],
        }))) };
}

test('weighted trajectory reward has bounded direction and exact weights', () => {
    assert.equal(utility(sample(255)).value, 1);
    assert.equal(utility(sample(0, 255, 100, 100)).value, 0);
    assert(utility(sample(255, 0, 0)).value > utility(sample(255, 0, 100)).value);
    assert(utility(sample(255, 0, 0, 0)).value > utility(sample(255, 0, 0, 100)).value);
    assert.equal(Object.values(rewardSpec.weights).reduce((a, b) => a + b), 1);
    assert.throws(() => utility(sample(Number.NaN)));
});

test('anonymous rendering excludes identities and rejects input leakage', () => {
    const source = entry();
    const prompt = render(source);
    const changed = structuredClone(source);
    changed.guest_id = 1234;
    changed.park = 'Different park';
    changed.incumbent_id = 3;
    changed.audit_options = [{ ride_id: 887, name: 'Other name' }, { ride_id: 999, name: 'Renamed' }];
    assert.equal(render(changed), prompt);
    assert.equal(fingerprint(changed.features), fingerprint(source.features));
    changed.features.options[0].ride_id = 22;
    assert.throws(() => render(changed));
    const invalid = entry();
    invalid.features.state.happiness.feeling = 'A secret ride name';
    assert.throws(() => validateFeatures(invalid.features));
    assert(!prompt.includes('ride_'));
    for (const value of [-1, 'not a number', Infinity]) {
        const invalid = entry();
        invalid.features.options[0].distance_tiles = value;
        assert.throws(() => validateFeatures(invalid.features));
    }
    const invalidGoal = entry();
    invalidGoal.features.options[0].goal = 'ride named Blackpool';
    assert.throws(() => validateFeatures(invalidGoal.features));
});

test('option permutations remap classifier codes, distributions and utilities together', () => {
    const source = raw();
    const outcome = label(source);
    assert.equal(outcome.status, 'clear');
    const reverse = trainingRow(source.case, outcome, [1, 0], 'opaque', 1);
    assert.equal(reverse.hard_target_code, 'B');
    assert.deepEqual(reverse.target_distribution, [0, 1]);
    assert.deepEqual(reverse.utilities, [...outcome.utilities].reverse());
    assert.equal(reverse.valid_code_mask_255.length, 255);
    assert.equal(reverse.valid_code_mask_255.reduce((a, b) => a + b), 2);
    assert.deepEqual(permutations(4, 123), permutations(4, 123));
    const reversed = structuredClone(source.case.features);
    reversed.options.reverse();
    assert.equal(fingerprint(reversed), fingerprint(source.case.features));
});

test('ties remain soft, RNG disagreements cannot produce a hard label', () => {
    const source = raw();
    source.results.forEach(result => { result.samples = [sample(result.option === -1 ? 0 : 128)]; });
    const tie = label(source);
    assert.equal(tie.status, 'ambiguous');
    assert.deepEqual(tie.target_distribution, [0.5, 0.5]);
    source.results.find(result => result.option === 1 && result.replica === 1).samples = [sample(255)];
    assert.equal(label(source).hard_target, null);
});

test('unfinished queues, terminals and invalid replay do not become bad-action labels', () => {
    const source = raw();
    source.results[1].initial_target_resolution = '';
    assert.equal(label(source).status, 'censored');
    assert.equal(label(source).target_distribution, undefined);
    source.results[1].initial_target_resolution = 'target_ride_or_facility_exit';
    source.results[0].terminal = 'removed_or_slot_reused';
    assert.equal(label(source).status, 'censored');
    source.restore_verified = false;
    assert.throws(() => label(source));
});

test('near-duplicate fingerprints are identity-free and order-free', () => {
    const original = entry();
    const near = structuredClone(original.features);
    near.options.reverse();
    near.state.happiness.value++;
    assert.equal(fingerprint(original.features, true), fingerprint(near, true));
    assert.notEqual(fingerprint(original.features), fingerprint(near));
    near.options[0].distance_tiles += 100;
    assert.notEqual(fingerprint(original.features, true), fingerprint(near, true));
});

test('observed departures have an explicit zero-utility tail, not missing-success bias', () => {
    const branch = { terminal: 'departed', terminal_tick: 25, terminal_measurement: sample(255, 0, 0, 0, 25), samples: [] };
    assert.equal(utility(horizonSample(branch, 100)).value, 0.25);
    branch.terminal = 'removed_or_slot_reused';
    assert.equal(horizonSample(branch, 100), null);
});

test('native can outperform all offered actions without a fabricated fallback code or target', () => {
    const source = raw();
    source.results.forEach(result => { result.samples = [sample(result.option === -1 ? 255 : 0)]; });
    const outcome = label(source);
    assert.equal(outcome.status, 'no_offered_improvement');
    assert.equal(outcome.target_distribution, undefined);
    assert.equal(outcome.hard_target, undefined);
    assert.throws(() => trainingRow(source.case, outcome, [0, 1], 'opaque', 0));
});
