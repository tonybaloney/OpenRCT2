#!/usr/bin/env python3
"""Count tokens with the pinned game's tokenizer, without loading model weights."""

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import sys


class ModelParams(ctypes.Structure):
    # Pinned llama.h: d775b8967a46d8beb110d444aa3b8938179e0dd8.
    _fields_ = [
        ("devices", ctypes.c_void_p),
        ("tensor_buft_overrides", ctypes.c_void_p),
        ("n_gpu_layers", ctypes.c_int32),
        ("split_mode", ctypes.c_int32),
        ("load_mode", ctypes.c_int32),
        ("main_gpu", ctypes.c_int32),
        ("tensor_split", ctypes.c_void_p),
        ("progress_callback", ctypes.c_void_p),
        ("progress_callback_user_data", ctypes.c_void_p),
        ("kv_overrides", ctypes.c_void_p),
        ("vocab_only", ctypes.c_bool),
        ("check_tensors", ctypes.c_bool),
        ("use_extra_bufts", ctypes.c_bool),
        ("no_host", ctypes.c_bool),
        ("no_alloc", ctypes.c_bool),
        ("load_mtp", ctypes.c_bool),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This helper validates the pinned Windows runtime only.")
    pinned = json.loads((Path(__file__).resolve().parents[1] / "guest-thoughts.json").read_text(encoding="utf-8"))
    if pinned["llamaRevision"] != "d775b8967a46d8beb110d444aa3b8938179e0dd8":
        raise ValueError("Unexpected native ABI revision.")
    if hashlib.sha256((args.runtime / "llama.dll").read_bytes()).hexdigest() != pinned["runtimeHashes"]["llama.dll"]:
        raise ValueError("Native tokenizer DLL checksum mismatch.")
    if ctypes.sizeof(ModelParams) != 72:
        raise ValueError("Unsupported native ABI layout.")
    directory = os.add_dll_directory(str(args.runtime.resolve()))
    api = ctypes.CDLL(str((args.runtime / "llama.dll").resolve()))
    api.llama_model_default_params.restype = ModelParams
    api.llama_model_load_from_file.argtypes = [ctypes.c_char_p, ModelParams]
    api.llama_model_load_from_file.restype = ctypes.c_void_p
    api.llama_model_get_vocab.argtypes = [ctypes.c_void_p]
    api.llama_model_get_vocab.restype = ctypes.c_void_p
    api.llama_tokenize.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32,
        ctypes.POINTER(ctypes.c_int32), ctypes.c_int32, ctypes.c_bool, ctypes.c_bool,
    ]
    api.llama_tokenize.restype = ctypes.c_int32
    api.llama_model_free.argtypes = [ctypes.c_void_p]
    params = api.llama_model_default_params()
    params.vocab_only = True
    params.n_gpu_layers = 0
    model = api.llama_model_load_from_file(str(args.model.resolve()).encode("utf-8"), params)
    if not model:
        raise RuntimeError("Cannot load pinned vocabulary.")
    try:
        vocab = api.llama_model_get_vocab(model)
        with args.input.open(encoding="utf-8") as source, args.output.open("w", encoding="utf-8", newline="\n") as target:
            for line in source:
                row = json.loads(line)
                prompt = row["prompt"].encode("utf-8")
                count = -api.llama_tokenize(vocab, prompt, len(prompt), None, 0, False, True)
                if count <= 0:
                    raise ValueError("Tokenizer returned no tokens.")
                tokens = (ctypes.c_int32 * count)()
                actual = api.llama_tokenize(vocab, prompt, len(prompt), tokens, count, False, True)
                if actual != count:
                    raise ValueError("Tokenizer count/read mismatch.")
                target.write(json.dumps({
                    "id": row["id"], "tokens": count,
                    "token_ids_sha256": hashlib.sha256(bytes(tokens)).hexdigest(),
                    "within_budget": count <= 1536,
                }) + "\n")
    finally:
        api.llama_model_free(model)
        # Native backend DLLs stay pinned until process exit.
        _ = directory


if __name__ == "__main__":
    main()
