// miniaudio's implementation and the engine around it. Built without the precompiled header (see AudioEngine.h).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define MA_NO_ENCODING      // Playback only
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "AudioEngine.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace Fox {

	namespace Audio {

		namespace {
			// Files larger than this stream from disk instead of being decoded whole (a 3 minute mp3 decodes to ~60 MB)
			constexpr std::uintmax_t STREAM_ABOVE_BYTES = 1024 * 1024;
		}

		struct AudioEngine::Impl {
			ma_engine engine{};
			ma_sound_group music{};
			ma_sound_group effects{};
			bool ready = false;
			bool paused = false;
			std::string error;

			struct Voice {
				std::unique_ptr<ma_sound> sound;
				bool started = false;
			};
			std::unordered_map<Handle, Voice> voices;
			Handle next = 1;
			Handle musicVoice = 0;

			ma_sound* Find(Handle handle) {
				auto found = voices.find(handle);
				return found != voices.end() ? found->second.sound.get() : nullptr;
			}

			Handle Start(const std::string& file, const PlayOptions& options, ma_sound_group* group, bool stream) {
				if (!ready || file.empty()) {
					return 0;
				}
				std::error_code failed;
				const std::filesystem::path path(file);
				const std::uintmax_t size = std::filesystem::file_size(path, failed);
				if (failed) {
					std::cout << "Audio: no sound file " << file << "\n";
					return 0;
				}
				ma_uint32 flags = (stream || size > STREAM_ABOVE_BYTES) ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
				if (!options.spatial) {
					flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
				}
				auto sound = std::make_unique<ma_sound>();
				const ma_result result = ma_sound_init_from_file_w(&engine, path.wstring().c_str(), flags, group, nullptr, sound.get());
				if (result != MA_SUCCESS) {
					std::cout << "Audio: cannot play " << file << " (" << ma_result_description(result) << ")\n";
					return 0;
				}
				ma_sound_set_volume(sound.get(), std::max(options.volume, 0.0f));
				ma_sound_set_pitch(sound.get(), std::clamp(options.pitch, 0.01f, 8.0f));
				ma_sound_set_looping(sound.get(), options.loop ? MA_TRUE : MA_FALSE);
				if (options.spatial) {
					ma_sound_set_position(sound.get(), options.position.x, options.position.y, options.position.z);
					ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_inverse);
					ma_sound_set_min_distance(sound.get(), std::max(options.minDistance, 0.01f));
					ma_sound_set_max_distance(sound.get(), std::max(options.maxDistance, options.minDistance + 0.01f));
				}
				ma_sound_start(sound.get());
				const Handle handle = next++;
				voices[handle] = Voice{ std::move(sound), true };
				return handle;
			}

			void Release(Handle handle) {
				auto found = voices.find(handle);
				if (found != voices.end()) {
					ma_sound_uninit(found->second.sound.get());
					voices.erase(found);
				}
				if (musicVoice == handle) {
					musicVoice = 0;
				}
			}
		};

		AudioEngine::AudioEngine() : impl(std::make_unique<Impl>()) {
			ma_engine_config config = ma_engine_config_init();
			const ma_result result = ma_engine_init(&config, &impl->engine);
			if (result != MA_SUCCESS) {
				impl->error = std::string("no audio device (") + ma_result_description(result) + ")";
				std::cout << "Audio: off, " << impl->error << "\n";
				return;
			}
			ma_sound_group_init(&impl->engine, 0, nullptr, &impl->music);
			ma_sound_group_init(&impl->engine, 0, nullptr, &impl->effects);
			impl->ready = true;
			std::cout << "Audio: " << ma_engine_get_sample_rate(&impl->engine) << " Hz, " << ma_engine_get_channels(&impl->engine) << " channels\n";
		}

		AudioEngine::~AudioEngine() {
			if (!impl->ready) {
				return;
			}
			for (auto& [handle, voice] : impl->voices) {
				ma_sound_uninit(voice.sound.get());
			}
			impl->voices.clear();
			ma_sound_group_uninit(&impl->music);
			ma_sound_group_uninit(&impl->effects);
			ma_engine_uninit(&impl->engine);
		}

		bool AudioEngine::IsReady() const { return impl->ready; }
		const std::string& AudioEngine::GetError() const { return impl->error; }

		AudioEngine::Handle AudioEngine::Play(const std::string& file, const PlayOptions& options) {
			return impl->Start(file, options, &impl->effects, false);
		}

		void AudioEngine::Stop(Handle sound, float fadeSeconds) {
			ma_sound* s = impl->Find(sound);
			if (!s) {
				return;
			}
			if (fadeSeconds > 0.0f && ma_sound_is_playing(s)) {
				ma_sound_stop_with_fade_in_milliseconds(s, static_cast<ma_uint64>(fadeSeconds * 1000.0f));
				ma_sound_set_looping(s, MA_FALSE);
			}
			else {
				impl->Release(sound);
			}
		}

		bool AudioEngine::IsPlaying(Handle sound) const {
			ma_sound* s = impl->Find(sound);
			return s && (ma_sound_is_playing(s) || impl->paused) && !ma_sound_at_end(s);
		}

		void AudioEngine::SetVolume(Handle sound, float volume) {
			if (ma_sound* s = impl->Find(sound)) ma_sound_set_volume(s, std::max(volume, 0.0f));
		}

		void AudioEngine::SetPitch(Handle sound, float pitch) {
			if (ma_sound* s = impl->Find(sound)) ma_sound_set_pitch(s, std::clamp(pitch, 0.01f, 8.0f));
		}

		void AudioEngine::SetPosition(Handle sound, const Vec3& position) {
			if (ma_sound* s = impl->Find(sound)) ma_sound_set_position(s, position.x, position.y, position.z);
		}

		AudioEngine::Handle AudioEngine::PlayMusic(const std::string& file, bool loop, float volume, float fadeSeconds) {
			StopMusic(fadeSeconds);
			PlayOptions options;
			options.volume = volume;
			options.loop = loop;
			const Handle handle = impl->Start(file, options, &impl->music, true);
			if (handle != 0 && fadeSeconds > 0.0f) {
				ma_sound_set_fade_in_milliseconds(impl->Find(handle), 0.0f, std::max(volume, 0.0f), static_cast<ma_uint64>(fadeSeconds * 1000.0f));
			}
			impl->musicVoice = handle;
			return handle;
		}

		void AudioEngine::StopMusic(float fadeSeconds) {
			const Handle music = impl->musicVoice;
			impl->musicVoice = 0;
			if (music != 0) {
				Stop(music, fadeSeconds);
			}
		}

		AudioEngine::Handle AudioEngine::GetMusic() const {
			return impl->musicVoice != 0 && IsPlaying(impl->musicVoice) ? impl->musicVoice : 0;
		}

		void AudioEngine::SetBusVolume(Bus bus, float volume) {
			if (!impl->ready) return;
			volume = std::max(volume, 0.0f);
			switch (bus) {
			case Bus::Music: ma_sound_group_set_volume(&impl->music, volume); break;
			case Bus::Effects: ma_sound_group_set_volume(&impl->effects, volume); break;
			default: ma_engine_set_volume(&impl->engine, volume); break;
			}
		}

		float AudioEngine::GetBusVolume(Bus bus) const {
			if (!impl->ready) return 0.0f;
			switch (bus) {
			case Bus::Music: return ma_sound_group_get_volume(&impl->music);
			case Bus::Effects: return ma_sound_group_get_volume(&impl->effects);
			default: return ma_engine_get_volume(&impl->engine);
			}
		}

		void AudioEngine::SetListener(const Vec3& position, const Vec3& forward, const Vec3& up) {
			if (!impl->ready) return;
			ma_engine_listener_set_position(&impl->engine, 0, position.x, position.y, position.z);
			ma_engine_listener_set_direction(&impl->engine, 0, forward.x, forward.y, forward.z);
			ma_engine_listener_set_world_up(&impl->engine, 0, up.x, up.y, up.z);
		}

		void AudioEngine::StopAll() {
			std::vector<Handle> all;
			for (const auto& [handle, voice] : impl->voices) all.push_back(handle);
			for (Handle handle : all) impl->Release(handle);
			impl->musicVoice = 0;
			SetPaused(false);
		}

		void AudioEngine::SetPaused(bool paused) {
			if (!impl->ready || paused == impl->paused) return;
			impl->paused = paused;
			if (paused) ma_engine_stop(&impl->engine);
			else ma_engine_start(&impl->engine);
		}

		void AudioEngine::Update() {
			if (!impl->ready || impl->paused) return;
			std::vector<Handle> finished;
			for (const auto& [handle, voice] : impl->voices) {
				if (!ma_sound_is_playing(voice.sound.get())) finished.push_back(handle);
			}
			for (Handle handle : finished) impl->Release(handle);
		}
	}
}
