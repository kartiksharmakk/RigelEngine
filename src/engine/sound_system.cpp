/* Copyright (C) 2016, Nikolai Wuttke. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sound_system.hpp"

#include "base/math_tools.hpp"
#include "data/game_options.hpp"
#include "engine/imf_player.hpp"
#include "loader/resource_loader.hpp"
#include "sdl_utils/error.hpp"

#include <speex/speex_resampler.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <utility>


namespace rigel::sdl_mixer {

namespace {

void check(int result) {
  using namespace std::string_literals;

  if (result != 0) {
    throw std::runtime_error{"SDL_mixer error: "s + Mix_GetError()};
  }
}

}

}


namespace rigel::engine {

namespace {

const auto SAMPLE_RATE = 44100;
const auto BUFFER_SIZE = 2048;

data::AudioBuffer resampleAudio(
  const data::AudioBuffer& buffer,
  const int newSampleRate
) {
  using ResamplerPtr =
    std::unique_ptr<SpeexResamplerState, decltype(&speex_resampler_destroy)>;
  ResamplerPtr pResampler(
    speex_resampler_init(
      1,
      buffer.mSampleRate,
      newSampleRate,
      5,
      nullptr),
    &speex_resampler_destroy);
  speex_resampler_skip_zeros(pResampler.get());

  auto inputLength = static_cast<spx_uint32_t>(buffer.mSamples.size());
  auto outputLength = static_cast<spx_uint32_t>(
    base::integerDivCeil<spx_uint32_t>(
      inputLength, buffer.mSampleRate) * newSampleRate);

  std::vector<data::Sample> resampled(outputLength);
  speex_resampler_process_int(
    pResampler.get(),
    0,
    buffer.mSamples.data(),
    &inputLength,
    resampled.data(),
    &outputLength);
  resampled.resize(outputLength);
  return {newSampleRate, resampled};
}


void appendRampToZero(data::AudioBuffer& buffer) {
  // Roughly 10 ms of linear ramp
  const auto rampLength = (SAMPLE_RATE / 100);

  buffer.mSamples.reserve(buffer.mSamples.size() + rampLength - 1);
  const auto lastSample = buffer.mSamples.back();

  for (int i=1; i<rampLength; ++i) {
    const auto interpolation = i / static_cast<double>(rampLength);
    const auto rampedValue = lastSample * (1.0 - interpolation);
    buffer.mSamples.push_back(base::roundTo<data::Sample>(rampedValue));
  }
}


data::AudioBuffer convertBuffer(const data::AudioBuffer& original) {
  auto buffer = resampleAudio(original, SAMPLE_RATE);
  if (buffer.mSamples.back() != 0) {
    // Prevent clicks/pops with samples that don't return to 0 at the end
    // by adding a small linear ramp leading back to zero.
    appendRampToZero(buffer);
  }

#if MIX_DEFAULT_FORMAT != AUDIO_S16LSB
  SDL_AudioSpec originalSoundSpec{
    buffer.mSampleRate,
    AUDIO_S16LSB,
    1,
    0, 0, 0, 0, nullptr};
  SDL_AudioCVT conversionSpecs;
  SDL_BuildAudioCVT(
    &conversionSpecs,
    AUDIO_S16LSB, 1, buffer.mSampleRate,
    MIX_DEFAULT_FORMAT, 1, SAMPLE_RATE);

  conversionSpecs.len = static_cast<int>(
    buffer.mSamples.size() * 2);
  std::vector<data::Sample> tempBuffer(
    conversionSpecs.len * conversionSpecs.len_mult);
  conversionSpecs.buf = reinterpret_cast<Uint8*>(tempBuffer.data());
  std::copy(
    buffer.mSamples.begin(), buffer.mSamples.end(), tempBuffer.begin());

  SDL_ConvertAudio(&conversionSpecs);

  data::AudioBuffer convertedBuffer{SAMPLE_RATE};
  convertedBuffer.mSamples.insert(
    convertedBuffer.mSamples.end(),
    tempBuffer.begin(),
    tempBuffer.begin() + conversionSpecs.len_cvt);
  return convertedBuffer;
#else
  return buffer;
#endif
}


auto idToIndex(const data::SoundId id) {
  return static_cast<int>(id);
}


RawBuffer asRawBuffer(const data::AudioBuffer& buffer) {
  const auto sizeInBytes = buffer.mSamples.size() * sizeof(data::Sample);
  auto rawBuffer = RawBuffer(sizeInBytes);
  std::memcpy(rawBuffer.data(), buffer.mSamples.data(), sizeInBytes);
  return rawBuffer;
}

}


SoundSystem::LoadedSound::LoadedSound(const data::AudioBuffer& buffer)
  : LoadedSound(asRawBuffer(buffer))
{
}


SoundSystem::LoadedSound::LoadedSound(RawBuffer buffer)
  : mData(std::move(buffer))
  , mpMixChunk(sdl_utils::Ptr<Mix_Chunk>{
      Mix_QuickLoad_RAW(mData.data(), static_cast<Uint32>(mData.size()))})
{
}


SoundSystem::SoundSystem(const loader::ResourceLoader& resources)
  : mpMusicPlayer(std::make_unique<ImfPlayer>(SAMPLE_RATE))
{
  sdl_mixer::check(Mix_OpenAudio(
    SAMPLE_RATE,
    MIX_DEFAULT_FORMAT,
    1, // mono
    BUFFER_SIZE));

  Mix_HookMusic(
    [](void* pUserData, Uint8* pOutBuffer, int bytesRequired) {
      auto pPlayer = static_cast<ImfPlayer*>(pUserData);
      auto pDestination = reinterpret_cast<std::int16_t*>(pOutBuffer);
      const auto samplesRequired = bytesRequired / sizeof(std::int16_t);

      pPlayer->render(pDestination, samplesRequired);
    },
    mpMusicPlayer.get());

  Mix_AllocateChannels(data::NUM_SOUND_IDS);

  data::forEachSoundId([&](const auto id) {
    mSounds[idToIndex(id)] = LoadedSound{convertBuffer(resources.loadSound(id))};
  });

  setMusicVolume(data::MUSIC_VOLUME_DEFAULT);
  setSoundVolume(data::SOUND_VOLUME_DEFAULT);
}


SoundSystem::~SoundSystem() {
  Mix_HookMusic(nullptr, nullptr);

  // We have to destroy all the MixChunks before we can call Mix_Quit().
  for (auto& sound : mSounds) {
    sound.mpMixChunk.reset(nullptr);
  }

  Mix_Quit();
}


void SoundSystem::playSong(data::Song&& song) {
  mpMusicPlayer->playSong(std::move(song));
}


void SoundSystem::stopMusic() const {
  mpMusicPlayer->playSong({});
}


void SoundSystem::playSound(const data::SoundId id) const {
  const auto index = idToIndex(id);
  Mix_PlayChannel(index, mSounds[index].mpMixChunk.get(), 0);
}


void SoundSystem::stopSound(const data::SoundId id) const {
  const auto index = idToIndex(id);
  Mix_HaltChannel(index);
}


void SoundSystem::setMusicVolume(const float volume) {
  mpMusicPlayer->setVolume(volume);
}


void SoundSystem::setSoundVolume(const float volume) {
  const auto sdlVolume = static_cast<int>(
    std::clamp(volume, 0.0f, 1.0f) * MIX_MAX_VOLUME);

  for (auto& sound : mSounds) {
    if (sound.mpMixChunk) {
      Mix_VolumeChunk(sound.mpMixChunk.get(), sdlVolume);
    }
  }
}

}
