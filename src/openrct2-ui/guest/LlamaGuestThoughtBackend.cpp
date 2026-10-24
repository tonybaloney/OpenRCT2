/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "LlamaGuestThoughtBackend.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <openrct2/Diagnostic.h>
#include <openrct2/core/File.h>
#include <openrct2/core/Json.hpp>
#include <openrct2/core/Path.hpp>
#include <openrct2/core/String.hpp>
#include <openrct2/peep/GuestDecision.h>
#include <openrct2/peep/GuestThoughtService.h>
#include <stdexcept>

namespace OpenRCT2::Ui
{
    std::vector<GuestDecisionModel> LoadGuestDecisionModels(const std::string& directory)
    {
        const auto catalog = Json::ReadFromFile(Path::Combine(directory, "guest-decision-models.json"));
        if (catalog.at("models").size() != 2)
            throw std::runtime_error("Guest decision catalogue must contain D3-Edge and D3-Lite.");
        std::vector<GuestDecisionModel> result;
        for (const auto& entry : catalog.at("models"))
        {
            GuestDecisionModel model{ entry.at("id").get<std::string>(), entry.at("label").get<std::string>() };
            if ((model.id != "d3-edge" && model.id != "d3-lite") || (!result.empty() && result.front().id == model.id))
                throw std::runtime_error("Unknown or duplicate guest decision model.");
            const auto modelDirectory = Path::Combine(directory, model.id);
            const auto file = Path::Combine(modelDirectory, model.id + ".gguf");
            const auto readout = Path::Combine(modelDirectory, "readout.f32");
            const auto stamp = Path::Combine(modelDirectory, "prepared.json");
            if (File::Exists(file) && File::Exists(readout) && File::Exists(stamp)
                && File::Exists(Path::Combine(modelDirectory, "decision_config.json")))
            {
                const auto prepared = Json::ReadFromFile(stamp);
                model.available = prepared.at("revision") == entry.at("revision")
                    && prepared.at("repository") == entry.at("repository")
                    && File::GetSize(file) == prepared.at("bytes").get<uint64_t>()
                    && File::GetSize(readout) == 255 * 1024 * sizeof(float);
            }
            result.push_back(std::move(model));
        }
        return result;
    }

    std::vector<GuestThoughtModel> LoadGuestThoughtModels(const std::string& modelDirectory)
    {
        const auto catalog = Json::ReadFromFile(Path::Combine(modelDirectory, "guest-thought-models.json"));
        if (catalog.at("models").empty() || catalog.at("models").size() > 32)
            throw std::runtime_error("Guest thought model catalog must contain between 1 and 32 models.");
        std::vector<GuestThoughtModel> models;
        for (const auto& entry : catalog.at("models"))
        {
            GuestThoughtModel model;
            model.label = entry.at("label").get<std::string>();
            model.instruct = entry.at("instruct").get<bool>();
            model.allowCpuFallback = entry.value("allowCpuFallback", false);
            model.firstPersonPrefill = entry.value("firstPersonPrefill", false);
            model.available = true;
            for (const auto& file : entry.at("files"))
            {
                const auto name = file.at("name").get<std::string>();
                if (name.empty() || Path::GetFileName(name) != name || name == "." || name == "..")
                    throw std::runtime_error("Invalid filename in the guest thought model catalog.");
                const auto path = Path::Combine(modelDirectory, name);
                if (model.path.empty())
                    model.path = path;
                const auto bytes = file.at("size").get<uint64_t>();
                model.bytes += bytes;
                model.available &= File::Exists(path) && File::GetSize(path) == bytes;
            }
            if (model.path.empty())
                throw std::runtime_error("Guest thought model catalog contains a model without files.");
            models.push_back(std::move(model));
        }
        return models;
    }
} // namespace OpenRCT2::Ui

#if defined(ENABLE_GUEST_THOUGHTS) && defined(_WIN32)
    #include <llama.h>
    #include <windows.h>

namespace OpenRCT2::Ui
{
    static std::mutex _guestInferenceMutex;

    class LlamaApi
    {
        struct Module
        {
            HMODULE handle;
            explicit Module(const std::string& directory)
            {
                const auto path = String::toWideChar(Path::Combine(directory, "llama.dll"));
                handle = LoadLibraryExW(
                    path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if (handle == nullptr)
                    throw std::runtime_error(
                        "Cannot load pinned llama.dll (Windows error " + std::to_string(GetLastError()) + ").");
                // This runtime installs process-wide handlers and retains backend threads.
                HMODULE pinned{};
                if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, path.c_str(), &pinned))
                    throw std::runtime_error("Cannot retain the guest thought runtime.");
            }
            ~Module()
            {
                FreeLibrary(handle);
            }
        } _module;

        template<typename T>
        T load(const char* name)
        {
            auto address = GetProcAddress(_module.handle, name);
            if (!address)
                address = GetProcAddress(GetModuleHandleW(L"ggml.dll"), name);
            if (!address)
                address = GetProcAddress(GetModuleHandleW(L"ggml-base.dll"), name);
            if (!address)
                throw std::runtime_error(std::string("Pinned llama runtime is missing API: ") + name);
            return std::bit_cast<T>(address);
        }

    public:
        explicit LlamaApi(const std::string& directory)
            : _module(directory)
        {
            static std::once_flag initialised;
            std::call_once(initialised, [this] {
                llama_log_set(
                    [](ggml_log_level level, const char* text, void*) {
                        if (level >= GGML_LOG_LEVEL_ERROR)
                            LOG_ERROR("llama: %s", text);
                        else if (std::strstr(text, "offloaded"))
                            LOG_INFO("llama: %s", text);
                    },
                    nullptr);
                ggml_backend_load_all();
                llama_backend_init();
            });
        }

        // Bind the supplied DLL's C ABI without linking its CRT into the game.
    #define GUEST_LLAMA_API(name) decltype(&::name) name = load<decltype(&::name)>(#name)
        GUEST_LLAMA_API(llama_backend_init);
        GUEST_LLAMA_API(llama_backend_free);
        GUEST_LLAMA_API(llama_log_set);
        GUEST_LLAMA_API(llama_model_default_params);
        GUEST_LLAMA_API(llama_context_default_params);
        GUEST_LLAMA_API(llama_model_load_from_file);
        GUEST_LLAMA_API(llama_model_free);
        GUEST_LLAMA_API(llama_init_from_model);
        GUEST_LLAMA_API(llama_free);
        GUEST_LLAMA_API(llama_model_get_vocab);
        GUEST_LLAMA_API(llama_model_chat_template);
        GUEST_LLAMA_API(llama_chat_apply_template);
        GUEST_LLAMA_API(llama_tokenize);
        GUEST_LLAMA_API(llama_token_to_piece);
        GUEST_LLAMA_API(llama_vocab_is_eog);
        GUEST_LLAMA_API(llama_batch_get_one);
        GUEST_LLAMA_API(llama_decode);
        GUEST_LLAMA_API(llama_get_memory);
        GUEST_LLAMA_API(llama_get_embeddings_ith);
        GUEST_LLAMA_API(llama_model_n_embd);
        GUEST_LLAMA_API(llama_memory_clear);
        GUEST_LLAMA_API(llama_set_abort_callback);
        GUEST_LLAMA_API(llama_sampler_init_greedy);
        GUEST_LLAMA_API(llama_sampler_sample);
        GUEST_LLAMA_API(llama_sampler_reset);
        GUEST_LLAMA_API(llama_sampler_free);
        GUEST_LLAMA_API(ggml_backend_load_all);
        GUEST_LLAMA_API(ggml_backend_dev_by_type);
        GUEST_LLAMA_API(ggml_backend_dev_name);
        GUEST_LLAMA_API(ggml_backend_dev_memory);
    #undef GUEST_LLAMA_API
    };

    class LlamaGuestThoughtBackend final : public IGuestThoughtBackend
    {
        static constexpr int32_t kContextTokens = 2048;
        static constexpr int32_t kOutputTokens = 192;

        LlamaApi _api;
        std::string _modelPath;
        llama_model* _model{};
        llama_context* _context{};
        llama_sampler* _sampler{};
        const std::atomic_bool* _cancelled{};
        GuestThoughtBackendOptions _options;

        void clear()
        {
            if (_sampler)
                _api.llama_sampler_free(_sampler);
            if (_context)
                _api.llama_free(_context);
            if (_model)
                _api.llama_model_free(_model);
            _sampler = nullptr;
            _context = nullptr;
            _model = nullptr;
        }

        static bool Cancelled(void* data)
        {
            auto* self = static_cast<LlamaGuestThoughtBackend*>(data);
            return self->_cancelled && self->_cancelled->load();
        }

        void initialise()
        {
            clear();
            auto params = _api.llama_model_default_params();
            ggml_backend_dev_t cpuDevices[] = { nullptr };
            auto gpu = _api.ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
            if (!gpu)
                gpu = _api.ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_IGPU);
            if (!gpu && !_options.forceCpu)
            {
                if (!_options.allowCpuFallback)
                    throw std::runtime_error("Guest thought model requires an available CUDA device.");
                LOG_WARNING("Guest thoughts: no CUDA device found; using CPU fallback.");
                _options.forceCpu = true;
            }
            if (!_options.forceCpu && _options.modelBytes != 0)
            {
                size_t free{}, total{};
                _api.ggml_backend_dev_memory(gpu, &free, &total);
                if (_options.modelBytes > free || free - _options.modelBytes < 1024ULL * 1024 * 1024)
                    throw std::runtime_error("Not enough free GPU memory for this model and its inference context.");
            }
            params.n_gpu_layers = _options.forceCpu ? 0 : 99;
            ggml_backend_dev_t gpuDevices[] = { gpu, nullptr };
            if (_options.forceCpu)
                params.devices = cpuDevices;
            else
                params.devices = gpuDevices;
            params.progress_callback = [](float, void* data) { return !Cancelled(data); };
            params.progress_callback_user_data = this;
            _model = _api.llama_model_load_from_file(_modelPath.c_str(), params);
            if (!_model)
                throw std::runtime_error("Cannot load guest thought GGUF model.");
            auto context = _api.llama_context_default_params();
            context.n_ctx = kContextTokens;
            context.n_batch = kContextTokens;
            context.n_ubatch = 256;
            context.n_threads = 4;
            context.n_threads_batch = 4;
            context.no_perf = true;
            context.offload_kqv = !_options.forceCpu;
            context.op_offload = !_options.forceCpu;
            context.abort_callback = Cancelled;
            context.abort_callback_data = this;
            _context = _api.llama_init_from_model(_model, context);
            if (!_context)
                throw std::runtime_error("Cannot create guest thought inference context.");
            _sampler = _api.llama_sampler_init_greedy();
            if (!_sampler)
                throw std::runtime_error("Cannot create guest thought sampler.");
            LOG_INFO(
                "Guest thoughts: embedded %s inference: %s.",
                _options.forceCpu ? "CPU fallback" : _api.ggml_backend_dev_name(gpu), _modelPath.c_str());
        }

        std::string generateOnce(const GuestNarrativeSnapshot& snapshot)
        {
            if (!_context)
                initialise();
            if (Cancelled(this))
                return {};
            _api.llama_memory_clear(_api.llama_get_memory(_context), true);
            _api.llama_sampler_reset(_sampler);
            const auto system = std::string(GuestNarrativeSystemPrompt());
            const auto feeling = snapshot.feeling();
            const auto needs = snapshot.needs();
            auto user = snapshot.prompt() + "\nMy current feeling is "
                + (feeling == "toilet" ? "I need a toilet" : feeling == "nauseous" ? "queasy in my stomach" : feeling)
                + (needs.empty() ? ". Write two short sentences about this feeling in one paragraph. "
                                 : ". Write three sentences about this feeling, 30 to 50 words in one paragraph. ")
                + "Make this feeling clear in affirmative sentences. Describe only this feeling, not the other stats or "
                  "needs I do not have.";
            if (needs.empty())
                user += " My physical needs are all satisfied. Keep bodily sensations out of this paragraph; focus solely "
                        "on my mood or recorded opinion, without padding.";
            const llama_chat_message messages[] = {
                { "system", system.c_str() },
                { "user", R"({"dominant_feeling":"hungry","current_needs":["hungry"],"activity":"walking"})" },
                { "assistant",
                  "My stomach's growling, and it keeps stealing my attention. I could really go for a snack before thinking "
                  "about anything else. A bit of food would make this walk much more pleasant." },
                { "user", R"({"dominant_feeling":"nauseous","current_needs":["sick"],"activity":"walking"})" },
                { "assistant",
                  "Ugh, my stomach's turning. I'm trying to keep myself together while I walk, but this queasy feeling is "
                  "taking all the fun out of it. I'd really like my stomach to settle down." },
                { "user", R"({"dominant_feeling":"toilet","current_needs":["toilet"],"activity":"walking"})" },
                { "assistant",
                  "I need a toilet, and that's pretty much all I can think about right now. Everything else can wait. "
                  "I want to get this sorted before trying to enjoy the park again." },
                { "user", R"({"dominant_feeling":"happy","current_needs":[],"activity":"walking"})" },
                { "assistant",
                  "I'm having a great time, and just walking around feels good right now. I'm happy to keep exploring "
                  "without rushing anything. It's nice being able to enjoy the park at my own pace." },
                { "user", R"({"dominant_feeling":"unhappy","current_needs":[],"activity":"walking"})" },
                { "assistant",
                  "I'm in a lousy mood, and even walking around feels like a chore. I wanted to enjoy myself, but that spark "
                  "just isn't there right now. Come on, this is supposed to be fun." },
                { "user",
                  R"({"dominant_feeling":"cannot find a ride","current_needs":[],"activity":"walking","earlier_native_thoughts":["cannot find a ride","too crowded","the park is clean"]})" },
                { "assistant",
                  "I wanted to find that ride, but this is getting frustrating. All this wandering feels like a lot of "
                  "effort for very little fun. At least the park looks clean while I try to figure things out." },
                { "user", user.c_str() },
            };
            std::string prompt;
            if (_options.instruct)
            {
                const auto* chatTemplate = _api.llama_model_chat_template(_model, nullptr);
                if (!chatTemplate)
                    throw std::runtime_error("Guest thought model has no chat template.");
                const auto length = _api.llama_chat_apply_template(
                    chatTemplate, messages, std::size(messages), true, nullptr, 0);
                if (length <= 0 || length > 16384)
                    throw std::runtime_error("Guest thought model chat template is unsupported.");
                std::vector<char> formatted(static_cast<size_t>(length) + 1);
                _api.llama_chat_apply_template(chatTemplate, messages, std::size(messages), true, formatted.data(), length);
                prompt.assign(formatted.data(), length);
                if (_options.firstPersonPrefill)
                    prompt += "I";
            }
            else
            {
                prompt = system + "\n\n";
                for (size_t i = 1; i + 1 < std::size(messages); i += 2)
                {
                    prompt += std::string("Guest state: ") + messages[i].content
                        + "\nPrivate thought: " + messages[i + 1].content + "\n\n";
                }
                prompt += "Guest state: " + user + "\nPrivate thought:";
            }
            const auto* vocab = _api.llama_model_get_vocab(_model);
            const auto count = -_api.llama_tokenize(
                vocab, prompt.data(), static_cast<int32_t>(prompt.size()), nullptr, 0, true, true);
            if (count <= 0 || count > kContextTokens - kOutputTokens)
                throw std::runtime_error("Guest thought prompt exceeds the context budget.");
            std::vector<llama_token> tokens(count);
            if (_api.llama_tokenize(vocab, prompt.data(), static_cast<int32_t>(prompt.size()), tokens.data(), count, true, true)
                != count)
                throw std::runtime_error("Cannot tokenize guest thought prompt.");
            auto batch = _api.llama_batch_get_one(tokens.data(), count);
            std::string result = _options.firstPersonPrefill ? "I" : "";
            llama_token token{};
            for (int32_t i = 0; i < kOutputTokens && !Cancelled(this); ++i)
            {
                if (_api.llama_decode(_context, batch) != 0)
                {
                    if (Cancelled(this))
                        return {};
                    throw std::runtime_error("Guest thought decoding failed.");
                }
                token = _api.llama_sampler_sample(_sampler, _context, -1);
                if (_api.llama_vocab_is_eog(vocab, token))
                    break;
                char piece[256];
                const auto size = _api.llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, false);
                if (size < 0)
                    throw std::runtime_error("Cannot decode guest thought token.");
                result.append(piece, size);
                if (!_options.instruct)
                {
                    const auto end = result.find_first_of("\r\n");
                    if (end != std::string::npos)
                    {
                        result.resize(end);
                        break;
                    }
                }
                batch = _api.llama_batch_get_one(&token, 1);
            }
            return result;
        }

    public:
        LlamaGuestThoughtBackend(std::string directory, std::string modelPath, GuestThoughtBackendOptions options)
            : _api(directory)
            , _modelPath(std::move(modelPath))
            , _options(options)
        {
        }

        ~LlamaGuestThoughtBackend() override
        {
            clear();
            // The pinned backend is shared with the decision classifier and stays
            // alive until process exit, just like its process-wide handlers.
        }

        std::string generate(const GuestNarrativeSnapshot& snapshot, const std::atomic_bool& cancelled) override
        {
            std::lock_guard inferenceLock(_guestInferenceMutex);
            if (cancelled)
                return {};
            _cancelled = &cancelled;
            struct CancellationScope
            {
                const std::atomic_bool*& current;
                ~CancellationScope()
                {
                    current = nullptr;
                }
            } cancellationScope{ _cancelled };
            try
            {
                return generateOnce(snapshot);
            }
            catch (const std::runtime_error& e)
            {
                clear();
                if (_options.forceCpu || !_options.allowCpuFallback || cancelled)
                    throw;
                LOG_WARNING("Guest thoughts: CUDA failed (%s); retrying on CPU.", e.what());
                _options.forceCpu = true;
                try
                {
                    return generateOnce(snapshot);
                }
                catch (const std::runtime_error&)
                {
                    clear();
                    throw;
                }
            }
        }
    };

    std::unique_ptr<IGuestThoughtBackend> CreateLlamaGuestThoughtBackend(
        std::string runtimeDirectory, std::string modelPath, GuestThoughtBackendOptions options)
    {
        return std::make_unique<LlamaGuestThoughtBackend>(std::move(runtimeDirectory), std::move(modelPath), options);
    }

    class LlamaGuestDecisionBackend final : public IGuestDecisionBackend
    {
        LlamaApi _api;
        std::string _directory;
        bool _omitIncumbentHints{};
        llama_model* _model{};
        llama_context* _context{};
        std::vector<float> _readout;
        const std::atomic_bool* _cancelled{};

        static bool Cancelled(void* data)
        {
            const auto& self = *static_cast<LlamaGuestDecisionBackend*>(data);
            return self._cancelled && self._cancelled->load();
        }

        void clear()
        {
            if (_context)
                _api.llama_free(_context);
            if (_model)
                _api.llama_model_free(_model);
            _context = nullptr;
            _model = nullptr;
        }

        void initialise()
        {
            const auto id = Path::GetFileName(_directory);
            if (id != "d3-edge" && id != "d3-lite")
                throw std::runtime_error("Unknown guest decision model.");
            const auto config = Json::ReadFromFile(Path::Combine(_directory, "decision_config.json"));
            if (config.at("format_id") != "d3-code-readout-v1" || config.at("temperature") != 1.0
                || config.at("attention_mode") != "noncausal_full_attention")
                throw std::runtime_error("Unsupported guest decision model contract.");
            if (config.at("codes").size() != 255)
                throw std::runtime_error("Invalid guest decision answer-code table.");
            for (size_t i = 0; i < kGuestDecisionMaxCandidates; ++i)
                if (config.at("codes").at(i) != std::string(1, static_cast<char>('A' + i)))
                    throw std::runtime_error("Unexpected guest decision answer-code order.");
            auto gpu = _api.ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
            if (!gpu)
                gpu = _api.ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_IGPU);
            if (!gpu)
                throw std::runtime_error("Guest decisions require CUDA; no GPU device is available.");
            const auto file = Path::Combine(_directory, id + ".gguf");
            size_t free{}, total{};
            _api.ggml_backend_dev_memory(gpu, &free, &total);
            const auto size = File::GetSize(file);
            if (size > free || free - size < 3ULL * 1024 * 1024 * 1024)
                throw std::runtime_error("Insufficient free GPU memory for guest decisions; choose a smaller narration model.");
            ggml_backend_dev_t devices[] = { gpu, nullptr };
            auto params = _api.llama_model_default_params();
            params.devices = devices;
            params.n_gpu_layers = 99;
            params.progress_callback = [](float, void* data) { return !Cancelled(data); };
            params.progress_callback_user_data = this;
            _model = _api.llama_model_load_from_file(file.c_str(), params);
            if (!_model || _api.llama_model_n_embd(_model) != 1024)
                throw std::runtime_error("Cannot load the guest decision text backbone.");
            auto context = _api.llama_context_default_params();
            context.n_ctx = 1536;
            context.n_batch = 1536;
            // D3 full-attention layers must see the entire prompt in one pass.
            context.n_ubatch = 1536;
            context.n_threads = 4;
            context.n_threads_batch = 4;
            context.embeddings = true;
            context.pooling_type = LLAMA_POOLING_TYPE_NONE;
            context.attention_type = LLAMA_ATTENTION_TYPE_NON_CAUSAL;
            context.no_perf = true;
            context.abort_callback = Cancelled;
            context.abort_callback_data = this;
            _context = _api.llama_init_from_model(_model, context);
            if (!_context)
                throw std::runtime_error("Cannot create the non-causal guest decision context.");
            const auto readout = Path::Combine(_directory, "readout.f32");
            _readout.resize(255 * 1024);
            if (File::GetSize(readout) != _readout.size() * sizeof(float))
                throw std::runtime_error("Invalid guest decision readout size.");
            std::ifstream stream(std::filesystem::u8path(readout), std::ios::binary);
            if (!stream.read(reinterpret_cast<char*>(_readout.data()), _readout.size() * sizeof(float)))
                throw std::runtime_error("Cannot read the guest decision readout.");
            LOG_INFO("Guest decisions: embedded %s classifier: %s.", _api.ggml_backend_dev_name(gpu), id.c_str());
        }

    public:
        LlamaGuestDecisionBackend(std::string runtime, std::string directory, bool omitIncumbentHints)
            : _api(runtime)
            , _directory(std::move(directory))
            , _omitIncumbentHints(omitIncumbentHints)
        {
        }

        ~LlamaGuestDecisionBackend() override
        {
            clear();
        }

        std::vector<float> score(const GuestDecisionSnapshot& snapshot, const std::atomic_bool& cancelled)
        {
            _cancelled = &cancelled;
            struct Scope
            {
                const std::atomic_bool*& value;
                ~Scope()
                {
                    value = nullptr;
                }
            } scope{ _cancelled };
            try
            {
                if (cancelled)
                    return {};
                if (!_context)
                    initialise();
                const auto prompt = snapshot.prompt(_omitIncumbentHints);
                const auto* vocab = _api.llama_model_get_vocab(_model);
                const auto count = -_api.llama_tokenize(
                    vocab, prompt.data(), static_cast<int32_t>(prompt.size()), nullptr, 0, false, true);
                if (count <= 0 || count > 1536)
                    throw std::runtime_error("Guest decision prompt exceeds the context budget.");
                std::vector<llama_token> tokens(count);
                if (_api.llama_tokenize(
                        vocab, prompt.data(), static_cast<int32_t>(prompt.size()), tokens.data(), count, false, true)
                    != count)
                    throw std::runtime_error("Cannot tokenize the guest decision prompt.");
                _api.llama_memory_clear(_api.llama_get_memory(_context), true);
                auto batch = _api.llama_batch_get_one(tokens.data(), count);
                if (_api.llama_decode(_context, batch) != 0)
                {
                    if (cancelled)
                        return {};
                    throw std::runtime_error("Guest decision forward pass failed.");
                }
                const auto* hidden = _api.llama_get_embeddings_ith(_context, -1);
                if (!hidden)
                    throw std::runtime_error("Guest decision hidden state is unavailable.");
                std::vector<float> scores(snapshot.candidates.size());
                if (scores.size() < 2 || scores.size() > kGuestDecisionMaxCandidates)
                    throw std::runtime_error("Invalid number of guest decision options.");
                for (size_t option = 0; option < scores.size(); ++option)
                {
                    for (size_t i = 0; i < 1024; ++i)
                        scores[option] += hidden[i] * _readout[option * 1024 + i];
                    if (!std::isfinite(scores[option]))
                        throw std::runtime_error("Non-finite guest decision score.");
                }
                const auto maximum = *std::max_element(scores.begin(), scores.end());
                float sum{};
                for (auto& score : scores)
                    sum += score = std::exp(score - maximum);
                for (auto& score : scores)
                    score /= sum;
                return scores;
            }
            catch (...)
            {
                clear();
                throw;
            }
        }

        std::vector<float> decide(const GuestDecisionSnapshot& snapshot, const std::atomic_bool& cancelled) override
        {
            std::lock_guard inferenceLock(_guestInferenceMutex);
            auto scores = score(snapshot, cancelled);
            if (cancelled)
                return {};
            // Match upstream permutation_average: score both orders, align by
            // option identity, then average probabilities to reduce code bias.
            auto reversed = snapshot;
            std::reverse(reversed.candidates.begin(), reversed.candidates.end());
            const auto reverseScores = score(reversed, cancelled);
            if (cancelled)
                return {};
            for (size_t i = 0; i < scores.size(); ++i)
                scores[i] = (scores[i] + reverseScores[scores.size() - 1 - i]) / 2;
            return scores;
        }
    };

    std::unique_ptr<IGuestDecisionBackend> CreateLlamaGuestDecisionBackend(
        std::string runtime, std::string directory, bool omitIncumbentHints)
    {
        return std::make_unique<LlamaGuestDecisionBackend>(std::move(runtime), std::move(directory), omitIncumbentHints);
    }
} // namespace OpenRCT2::Ui
#else
namespace OpenRCT2::Ui
{
    std::unique_ptr<IGuestDecisionBackend> CreateLlamaGuestDecisionBackend(std::string, std::string, bool)
    {
        throw std::runtime_error("Embedded guest decisions are not enabled in this build.");
    }
    std::unique_ptr<IGuestThoughtBackend> CreateLlamaGuestThoughtBackend(std::string, std::string, GuestThoughtBackendOptions)
    {
        throw std::runtime_error("Embedded guest thoughts are not enabled in this build.");
    }
} // namespace OpenRCT2::Ui
#endif
