// Converts the pinned text-only D3 classifiers for the supplied llama.cpp runtime.
// No remote Python code is executed and no Python dependencies are required.
import fs from "node:fs";
import path from "node:path";
import crypto from "node:crypto";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const modelRoot = process.argv[2] ? path.resolve(process.argv[2]) : path.join(root, "bin", "models", "guest-decisions");
const catalog = JSON.parse(fs.readFileSync(path.join(root, "guest-decision-models.json")));
fs.mkdirSync(modelRoot, { recursive: true });

function hash(file) {
    const digest = crypto.createHash("sha256");
    const fd = fs.openSync(file, "r");
    const buffer = Buffer.alloc(4 * 1024 * 1024);
    try {
        let size;
        while ((size = fs.readSync(fd, buffer)) !== 0) digest.update(buffer.subarray(0, size));
        return digest.digest("hex");
    } finally { fs.closeSync(fd); }
}

function download(url, file, expected) {
    if (fs.existsSync(file) && (!expected || hash(file) === expected)) return;
    const temp = `${file}.download`;
    const result = spawnSync("curl.exe", ["--fail", "--location", "--retry", "3", "--silent",
        "--show-error", "--output", temp, url], { stdio: "inherit" });
    if (result.status !== 0) throw new Error(`Download failed: ${url}`);
    if (expected && hash(temp) !== expected) throw new Error(`Checksum mismatch: ${file}`);
    fs.renameSync(temp, file);
}

const u32 = value => { const b = Buffer.alloc(4); b.writeUInt32LE(value); return b; };
const u64 = value => { const b = Buffer.alloc(8); b.writeBigUInt64LE(BigInt(value)); return b; };
const str = value => { const b = Buffer.from(value); return Buffer.concat([u64(b.length), b]); };
const typed = (type, value) => {
    if (type === 8) return str(value);
    if (type === 7) return Buffer.from([value ? 1 : 0]);
    if (type === 6) { const b = Buffer.alloc(4); b.writeFloatLE(value); return b; }
    if (type === 4) return u32(value);
    if (type === 5) { const b = Buffer.alloc(4); b.writeInt32LE(value); return b; }
    throw new Error(`Unsupported metadata type ${type}`);
};
const alignment = n => Math.ceil(n / 32) * 32;

function safetensors(file) {
    const fd = fs.openSync(file, "r");
    const size = Buffer.alloc(8);
    fs.readSync(fd, size, 0, 8, 0);
    const length = Number(size.readBigUInt64LE());
    if (length > 10 * 1024 * 1024) throw new Error("Invalid safetensors header");
    const header = Buffer.alloc(length);
    fs.readSync(fd, header, 0, length, 8);
    return { fd, header: JSON.parse(header), offset: 8 + length };
}

function readTensor(source, info) {
    const data = Buffer.alloc(info.data_offsets[1] - info.data_offsets[0]);
    if (fs.readSync(source.fd, data, 0, data.length, source.offset + info.data_offsets[0]) !== data.length)
        throw new Error("Truncated tensor");
    if (info.dtype === "F32") return new Float32Array(data.buffer, data.byteOffset, data.length / 4).slice();
    if (info.dtype !== "BF16") throw new Error(`Unexpected tensor dtype: ${info.dtype}`);
    const input = new Uint16Array(data.buffer, data.byteOffset, data.length / 2);
    const floats = new Float32Array(input.length);
    const bits = new Uint32Array(floats.buffer);
    for (let i = 0; i < input.length; i++) bits[i] = input[i] << 16;
    return floats;
}

function half(value) {
    // BF16 source values have fewer mantissa bits than F16; handle the exponent
    // change and round the subnormal range to nearest, ties to even.
    const sign = value < 0 ? 0x8000 : 0;
    const absolute = Math.abs(value);
    if (!Number.isFinite(absolute) || absolute > 65504) throw new Error("Non-finite or overflowing F16 weight");
    if (absolute < 2 ** -14) {
        const scaled = absolute * 2 ** 24;
        const floor = Math.floor(scaled);
        const fraction = scaled - floor;
        return sign | (floor + (fraction > 0.5 || (fraction === 0.5 && floor % 2) ? 1 : 0));
    }
    const exponent = Math.floor(Math.log2(absolute));
    return sign | (((exponent + 15) << 10) + Math.round((absolute / 2 ** exponent - 1) * 1024));
}

const names = {
    "input_layernorm.weight": "attn_norm.weight",
    "post_attention_layernorm.weight": "post_attention_norm.weight",
    "mlp.gate_proj.weight": "ffn_gate.weight",
    "mlp.up_proj.weight": "ffn_up.weight",
    "mlp.down_proj.weight": "ffn_down.weight",
    "self_attn.q_proj.weight": "attn_q.weight",
    "self_attn.k_proj.weight": "attn_k.weight",
    "self_attn.v_proj.weight": "attn_v.weight",
    "self_attn.o_proj.weight": "attn_output.weight",
    "self_attn.q_norm.weight": "attn_q_norm.weight",
    "self_attn.k_norm.weight": "attn_k_norm.weight",
    "linear_attn.in_proj_qkv.weight": "attn_qkv.weight",
    "linear_attn.in_proj_z.weight": "attn_gate.weight",
    "linear_attn.in_proj_a.weight": "ssm_alpha.weight",
    "linear_attn.in_proj_b.weight": "ssm_beta.weight",
    "linear_attn.out_proj.weight": "ssm_out.weight",
    "linear_attn.conv1d.weight": "ssm_conv1d.weight",
    "linear_attn.dt_bias": "ssm_dt.bias",
    "linear_attn.A_log": "ssm_a",
    "linear_attn.norm.weight": "ssm_norm.weight",
};

function convert(directory, output, model) {
    const config = JSON.parse(fs.readFileSync(path.join(directory, "config.json"))).text_config;
    const decision = JSON.parse(fs.readFileSync(path.join(directory, "decision_config.json")));
    if (decision.attention_mode !== "noncausal_full_attention" || decision.temperature !== 1 ||
        decision.format_id !== "d3-code-readout-v1" || config.linear_num_key_heads !== config.linear_num_value_heads)
        throw new Error("Unsupported D3 architecture or decision contract");
    const source = safetensors(path.join(directory, "model-00001-of-00001.safetensors"));
    const tensors = [];
    let offset = 0;
    for (const [name, info] of Object.entries(source.header)) {
        if (name === "__metadata__" || name.startsWith("visual.")) continue;
        let mapped;
        if (name === "language_model.embed_tokens.weight") mapped = "token_embd.weight";
        else if (name === "language_model.norm.weight") mapped = "output_norm.weight";
        else {
            const match = /^language_model\.layers\.(\d+)\.(.*)$/.exec(name);
            if (!match || !names[match[2]]) throw new Error(`Unrecognised text tensor ${name}`);
            mapped = `blk.${match[1]}.${names[match[2]]}`;
        }
        const shape = info.shape.filter(n => n !== 1);
        const type = shape.length === 1 || name.includes("conv1d") ? 0 : 1;
        tensors.push({ name, info, mapped, shape, type, offset });
        offset = alignment(offset + shape.reduce((a, b) => a * b, 1) * (type === 0 ? 4 : 2));
    }
    const metadata = [];
    const add = (key, type, value) => metadata.push(Buffer.concat([str(key), u32(type), typed(type, value)]));
    const array = (key, type, values) => metadata.push(Buffer.concat([
        str(key), u32(9), u32(type), u64(values.length), ...values.map(v => typed(type, v)),
    ]));
    add("general.architecture", 8, "qwen35");
    add("general.name", 8, `${model.label} text classifier`);
    add("general.file_type", 4, 1);
    add("general.source.url", 8, `https://huggingface.co/${model.repository}/tree/${model.revision}`);
    for (const [key, value] of Object.entries({
        context_length: 1536, embedding_length: config.hidden_size, block_count: config.num_hidden_layers,
        feed_forward_length: config.intermediate_size, "attention.head_count": config.num_attention_heads,
        "attention.head_count_kv": config.num_key_value_heads, "attention.key_length": config.head_dim,
        "attention.value_length": config.head_dim, "rope.dimension_count": config.head_dim / 4,
        "ssm.conv_kernel": config.linear_conv_kernel_dim, "ssm.inner_size": config.linear_value_head_dim * config.linear_num_value_heads,
        "ssm.state_size": config.linear_key_head_dim, "ssm.time_step_rank": config.linear_num_value_heads,
        "ssm.group_count": config.linear_num_key_heads,
    })) add(`qwen35.${key}`, 4, value);
    add("qwen35.attention.layer_norm_rms_epsilon", 6, config.rms_norm_eps);
    add("qwen35.rope.freq_base", 6, config.rope_parameters.rope_theta);
    add("qwen35.attention.causal", 7, false);
    array("qwen35.rope.dimension_sections", 4, [...config.rope_parameters.mrope_section, 0]);
    array("qwen35.attention.recurrent_layers", 7, config.layer_types.map(t => t === "linear_attention"));
    const tokenizer = JSON.parse(fs.readFileSync(path.join(directory, "tokenizer.json")));
    const tokens = Array.from({ length: config.vocab_size }, (_, i) => `[PAD${i}]`);
    const types = tokens.map(() => 4);
    for (const [token, id] of Object.entries(tokenizer.model.vocab)) { tokens[id] = token; types[id] = 1; }
    for (const token of tokenizer.added_tokens) { tokens[token.id] = token.content; types[token.id] = token.special ? 3 : 1; }
    add("tokenizer.ggml.model", 8, "gpt2");
    add("tokenizer.ggml.pre", 8, "qwen35");
    add("tokenizer.ggml.eos_token_id", 4, config.eos_token_id);
    add("tokenizer.ggml.add_bos_token", 7, false);
    array("tokenizer.ggml.tokens", 8, tokens);
    array("tokenizer.ggml.token_type", 5, types);
    array("tokenizer.ggml.merges", 8, tokenizer.model.merges.map(m => Array.isArray(m) ? m.join(" ") : m));
    const header = Buffer.concat([
        Buffer.from("GGUF"), u32(3), u64(tensors.length), u64(metadata.length), ...metadata,
        ...tensors.map(t => Buffer.concat([
            str(t.mapped), u32(t.shape.length), ...t.shape.toReversed().map(u64), u32(t.type), u64(t.offset),
        ])),
    ]);
    const temp = `${output}.download`;
    const fd = fs.openSync(temp, "w");
    try {
        fs.writeSync(fd, header);
        fs.writeSync(fd, Buffer.alloc(alignment(header.length) - header.length));
        for (const t of tensors) {
            const values = readTensor(source, t.info);
            if (t.name.endsWith(".A_log")) for (let i = 0; i < values.length; i++) values[i] = -Math.exp(values[i]);
            else if (t.name.endsWith("norm.weight") && !t.name.endsWith("linear_attn.norm.weight"))
                for (let i = 0; i < values.length; i++) values[i] += 1;
            let data = Buffer.from(values.buffer);
            if (t.type === 1) {
                const converted = new Uint16Array(values.length);
                for (let i = 0; i < values.length; i++) converted[i] = half(values[i]);
                data = Buffer.from(converted.buffer);
            }
            fs.writeSync(fd, data);
            fs.writeSync(fd, Buffer.alloc(alignment(data.length) - data.length));
        }
    } finally { fs.closeSync(fd); fs.closeSync(source.fd); }
    fs.renameSync(temp, output);
    const readout = safetensors(path.join(directory, "readout.safetensors"));
    try {
        const info = readout.header.weight;
        if (info.dtype !== "F32" || info.shape[0] !== 255 || info.shape[1] !== config.hidden_size)
            throw new Error("Unsupported readout shape");
        fs.writeFileSync(path.join(directory, "readout.f32"), Buffer.from(readTensor(readout, info).buffer));
    } finally { fs.closeSync(readout.fd); }
}

if (catalog.models.length !== 2 || new Set(catalog.models.map(m => m.id)).size !== 2)
    throw new Error("Expected exactly the D3-Edge and D3-Lite catalogue entries");
for (const model of catalog.models) {
    if (!["d3-edge", "d3-lite"].includes(model.id) || model.repository !== `vllm-sr/${model.id}` ||
        !/^[a-f0-9]{40}$/.test(model.revision)) throw new Error("Invalid pinned decision model entry");
    const directory = path.join(modelRoot, model.id);
    fs.mkdirSync(directory, { recursive: true });
    const base = `https://huggingface.co/${model.repository}/resolve/${model.revision}/`;
    const manifestFile = path.join(directory, "MODEL_MANIFEST.json");
    download(`${base}MODEL_MANIFEST.json`, manifestFile);
    const manifest = JSON.parse(fs.readFileSync(manifestFile));
    for (const file of ["config.json", "decision_config.json", "tokenizer.json", "readout.safetensors",
        "model-00001-of-00001.safetensors", "LICENSE", "NOTICE"]) {
        const expected = manifest.files_sha256[file];
        if (!/^[a-f0-9]{64}$/.test(expected)) throw new Error(`Missing upstream digest for ${file}`);
        console.log(`${model.id}: verify ${file}`);
        download(`${base}${file}`, path.join(directory, file), expected);
    }
    const output = path.join(directory, `${model.id}.gguf`);
    const stampFile = path.join(directory, "prepared.json");
    if (fs.existsSync(stampFile) && fs.existsSync(output) && fs.existsSync(path.join(directory, "readout.f32"))) {
        const stamp = JSON.parse(fs.readFileSync(stampFile));
        if (stamp.revision === model.revision && stamp.repository === model.repository &&
            stamp.converter === hash(fileURLToPath(import.meta.url)) && stamp.sha256 === hash(output) &&
            stamp.readout === hash(path.join(directory, "readout.f32"))) continue;
    }
    console.log(`${model.id}: converting F16 text backbone`);
    if (fs.existsSync(stampFile)) fs.unlinkSync(stampFile);
    convert(directory, output, model);
    fs.writeFileSync(`${stampFile}.download`, JSON.stringify({
        converter: hash(fileURLToPath(import.meta.url)), sha256: hash(output),
        readout: hash(path.join(directory, "readout.f32")), bytes: fs.statSync(output).size,
        repository: model.repository, revision: model.revision,
    }, null, 2));
    fs.renameSync(`${stampFile}.download`, stampFile);
    console.log(`${model.id}: ready (${fs.statSync(output).size} bytes)`);
}
fs.copyFileSync(path.join(root, "guest-decision-models.json"), path.join(modelRoot, "guest-decision-models.json"));
