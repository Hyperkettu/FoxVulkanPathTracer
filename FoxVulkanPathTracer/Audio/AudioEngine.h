#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Fox {

	namespace Audio {

		// Sound and music playback with miniaudio (ThirdParty/miniaudio, public domain / MIT-0). miniaudio stays in
		// AudioEngine.cpp (it brings windows.h), and this interface takes plain floats, not glm types (that file is
		// built without the renderer's precompiled header and its glm settings).
		//
		// Two buses under the master volume: music (one track at a time, streamed, crossfaded) and effects (any
		// number of sounds, 2D or placed in the world and heard from the listener). Sounds are played by file
		// (mp3, wav, flac); short files are decoded once and kept, long ones streamed. A playing sound is a handle:
		// it stops being valid once the sound has finished.
		class AudioEngine {
		public:
			using Handle = int64_t; // 0: none

			struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

			struct PlayOptions {
				float volume = 1.0f;
				float pitch = 1.0f;
				bool loop = false;
				bool spatial = false;       // Placed at position (else heard the same everywhere)
				Vec3 position;
				float minDistance = 1.0f;   // Full volume within this, metres
				float maxDistance = 50.0f;  // Quieter until this far, then no quieter
			};

			enum class Bus { Master = 0, Music = 1, Effects = 2 };

			AudioEngine();
			~AudioEngine();
			AudioEngine(const AudioEngine&) = delete;
			AudioEngine& operator=(const AudioEngine&) = delete;

			// False (with the reason in GetError) when there is no audio device: everything else then does nothing
			bool IsReady() const;
			const std::string& GetError() const;

			Handle Play(const std::string& file, const PlayOptions& options);
			void Stop(Handle sound, float fadeSeconds = 0.0f);
			bool IsPlaying(Handle sound) const;
			void SetVolume(Handle sound, float volume);
			void SetPitch(Handle sound, float pitch);
			void SetPosition(Handle sound, const Vec3& position);

			// Background music: one track, replacing (crossfading) the one playing
			Handle PlayMusic(const std::string& file, bool loop, float volume, float fadeSeconds);
			void StopMusic(float fadeSeconds);
			Handle GetMusic() const;

			void SetBusVolume(Bus bus, float volume);
			float GetBusVolume(Bus bus) const;

			// Where sounds are heard from (the camera)
			void SetListener(const Vec3& position, const Vec3& forward, const Vec3& up);

			// Everything stops (play mode ended)
			void StopAll();
			// Pauses and resumes all sound (the game paused)
			void SetPaused(bool paused);

			// Once a frame: forgets sounds that have finished
			void Update();

		private:
			struct Impl;
			std::unique_ptr<Impl> impl;
		};
	}
}
