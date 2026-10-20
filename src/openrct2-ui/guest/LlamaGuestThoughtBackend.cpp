/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "LlamaGuestThoughtBackend.h"

#include <bit>
#include <openrct2/Diagnostic.h>
#include <openrct2/core/Path.hpp>
#include <openrct2/core/String.hpp>
#include <openrct2/peep/GuestThoughtService.h>
#include <stdexcept>

#if defined(ENABLE_GUEST_THOUGHTS) && defined(_WIN32)
    #include <llama.h>
    #include <windows.h>

namespace OpenRCT2::Ui
{
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
        GUEST_LLAMA_API(llama_memory_clear);
        GUEST_LLAMA_API(llama_set_abort_callback);
        GUEST_LLAMA_API(llama_sampler_init_greedy);
        GUEST_LLAMA_API(llama_sampler_sample);
        GUEST_LLAMA_API(llama_sampler_reset);
        GUEST_LLAMA_API(llama_sampler_free);
        GUEST_LLAMA_API(ggml_backend_load_all);
        GUEST_LLAMA_API(ggml_backend_dev_by_type);
        GUEST_LLAMA_API(ggml_backend_dev_name);
    #undef GUEST_LLAMA_API
    };

    class LlamaGuestThoughtBackend final : public IGuestThoughtBackend
    {
        LlamaApi _api;
        std::string _modelPath;
        llama_model* _model{};
        llama_context* _context{};
        llama_sampler* _sampler{};
        const std::atomic_bool* _cancelled{};
        bool _forceCpu{};

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
            if (!gpu && !_forceCpu)
            {
                LOG_WARNING("Guest thoughts: no CUDA device found; using CPU fallback.");
                _forceCpu = true;
            }
            params.n_gpu_layers = _forceCpu ? 0 : 99;
            ggml_backend_dev_t gpuDevices[] = { gpu, nullptr };
            if (_forceCpu)
                params.devices = cpuDevices;
            else
                params.devices = gpuDevices;
            params.progress_callback = [](float, void* data) { return !Cancelled(data); };
            params.progress_callback_user_data = this;
            _model = _api.llama_model_load_from_file(_modelPath.c_str(), params);
            if (!_model)
                throw std::runtime_error("Cannot load guest thought GGUF model.");
            auto context = _api.llama_context_default_params();
            context.n_ctx = 1536;
            context.n_batch = 1536;
            context.n_ubatch = 256;
            context.n_threads = 4;
            context.n_threads_batch = 4;
            context.no_perf = true;
            context.offload_kqv = !_forceCpu;
            context.op_offload = !_forceCpu;
            context.abort_callback = Cancelled;
            context.abort_callback_data = this;
            _context = _api.llama_init_from_model(_model, context);
            if (!_context)
                throw std::runtime_error("Cannot create guest thought inference context.");
            _sampler = _api.llama_sampler_init_greedy();
            if (!_sampler)
                throw std::runtime_error("Cannot create guest thought sampler.");
            LOG_INFO("Guest thoughts: embedded %s inference.", _forceCpu ? "CPU fallback" : _api.ggml_backend_dev_name(gpu));
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
            const auto user = snapshot.prompt() + "\nMy current feeling is: " + snapshot.feeling()
                + ". Make this feeling clear in my thought. Do not deny it or suggest unrelated needs.";
            const llama_chat_message messages[] = {
                { "system", system.c_str() },
                { "user", R"({"dominant_feeling":"hungry","current_needs":["hungry"],"activity":"walking"})" },
                { "assistant", "My stomach's growling. I could really go for a snack." },
                { "user", R"({"dominant_feeling":"nauseous","current_needs":["sick"],"activity":"walking"})" },
                { "assistant", "Ugh, my stomach's turning. I need a moment to settle down." },
                { "user", R"({"dominant_feeling":"toilet","current_needs":["toilet"],"activity":"walking"})" },
                { "assistant", "I need a toilet before I do anything else!" },
                { "user", R"({"dominant_feeling":"happy","current_needs":[],"activity":"walking"})" },
                { "assistant", "I'm having a great time. What's next?" },
                { "user", user.c_str() },
            };
            const auto* chatTemplate = _api.llama_model_chat_template(_model, nullptr);
            if (!chatTemplate)
                throw std::runtime_error("Guest thought model has no chat template.");
            const auto length = _api.llama_chat_apply_template(chatTemplate, messages, std::size(messages), true, nullptr, 0);
            if (length <= 0 || length > 16384)
                throw std::runtime_error("Guest thought model chat template is unsupported.");
            std::vector<char> formatted(static_cast<size_t>(length) + 1);
            _api.llama_chat_apply_template(chatTemplate, messages, std::size(messages), true, formatted.data(), length);
            const auto prompt = std::string(formatted.data(), length);
            const auto* vocab = _api.llama_model_get_vocab(_model);
            const auto count = -_api.llama_tokenize(
                vocab, prompt.data(), static_cast<int32_t>(prompt.size()), nullptr, 0, true, true);
            if (count <= 0 || count > 1472)
                throw std::runtime_error("Guest thought prompt exceeds the context budget.");
            std::vector<llama_token> tokens(count);
            if (_api.llama_tokenize(vocab, prompt.data(), static_cast<int32_t>(prompt.size()), tokens.data(), count, true, true)
                != count)
                throw std::runtime_error("Cannot tokenize guest thought prompt.");
            auto batch = _api.llama_batch_get_one(tokens.data(), count);
            std::string result;
            llama_token token{};
            for (size_t i = 0; i < 64 && !Cancelled(this); ++i)
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
                batch = _api.llama_batch_get_one(&token, 1);
            }
            return result;
        }

    public:
        LlamaGuestThoughtBackend(std::string directory, std::string modelPath, bool forceCpu)
            : _api(directory)
            , _modelPath(std::move(modelPath))
            , _forceCpu(forceCpu)
        {
            _api.llama_log_set(
                [](ggml_log_level level, const char* text, void*) {
                    if (level >= GGML_LOG_LEVEL_ERROR)
                        LOG_ERROR("llama: %s", text);
                },
                nullptr);
            _api.ggml_backend_load_all();
            _api.llama_backend_init();
        }

        ~LlamaGuestThoughtBackend() override
        {
            clear();
            _api.llama_backend_free();
        }

        std::string generate(const GuestNarrativeSnapshot& snapshot, const std::atomic_bool& cancelled) override
        {
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
                if (_forceCpu || cancelled)
                    throw;
                LOG_WARNING("Guest thoughts: CUDA failed (%s); retrying on CPU.", e.what());
                _forceCpu = true;
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
        std::string runtimeDirectory, std::string modelPath, bool forceCpu)
    {
        return std::make_unique<LlamaGuestThoughtBackend>(std::move(runtimeDirectory), std::move(modelPath), forceCpu);
    }
} // namespace OpenRCT2::Ui
#else
namespace OpenRCT2::Ui
{
    std::unique_ptr<IGuestThoughtBackend> CreateLlamaGuestThoughtBackend(std::string, std::string, bool)
    {
        throw std::runtime_error("Embedded guest thoughts are not enabled in this build.");
    }
} // namespace OpenRCT2::Ui
#endif
