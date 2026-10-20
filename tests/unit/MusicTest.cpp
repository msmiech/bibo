#include <algorithm>
#include <cmath>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Audio/Music.hpp"

using namespace bibo::audio;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("Melodies are read note by note with their bars", "[audio]")
{
	std::string error;
	std::vector<Note> notes = parseMelody("A4:.5 C5:.5 F#5:1 r:1 Bb3:1 | E5:4", 8.0f, error);
	CAPTURE(error);
	REQUIRE(notes.size() == 5);
	CHECK(notes[0].pitch == 69);
	CHECK(notes[0].start == 8.0f);
	CHECK(notes[1].pitch == 72);
	CHECK(notes[1].start == 8.5f);
	CHECK(notes[2].pitch == 78);
	CHECK(notes[3].pitch == 58); //after the rest
	CHECK(notes[3].start == 11.0f);
	CHECK(notes[4].start == 12.0f);
	CHECK(notes[4].length == 4.0f);

	SECTION("A bar must have 4 beats")
	{
		CHECK(parseMelody("C5:1 D5:1 E5:1 | F5:4", 0.0f, error).empty());
		CHECK(error.find("bar 1") != std::string::npos);
		CHECK(parseMelody("C5:4 | F5:2", 0.0f, error).empty()); //the last bar too
	}
	SECTION("Unknown notes")
	{
		CHECK(parseMelody("H5:4", 0.0f, error).empty());
		CHECK(parseMelody("C5 D5:3", 0.0f, error).empty());
		CHECK(parseMelody("C:4", 0.0f, error).empty());
	}
}

TEST_CASE("Notes have the frequencies of equal temperament", "[audio]")
{
	CHECK_THAT(noteFrequency(69.0f), WithinRel(440.0f, 1e-5f));
	CHECK_THAT(noteFrequency(81.0f), WithinRel(880.0f, 1e-5f));
	CHECK_THAT(noteFrequency(60.0f), WithinRel(261.626f, 1e-4f));
}

TEST_CASE("The melodies of the music are 8 bars each", "[audio]")
{
	for (const std::string& text : islandTuneMelodies())
	{
		std::string error;
		std::vector<Note> notes = parseMelody(text, 0.0f, error);
		CAPTURE(text, error);
		REQUIRE_FALSE(notes.empty());
		const Note& last = notes.back();
		CHECK(last.start + last.length <= 32.0f);
		CHECK(std::count(text.begin(), text.end(), '|') == 7);
	}
}

TEST_CASE("The music is a loop of 32 bars that is loud, but not too loud, and has no click where it starts again", "[audio]")
{
	const int sampleRate = 8000; //enough for the test, the game uses 44100
	Song song = composeIslandTune(sampleRate);
	REQUIRE(song.bars == 32);
	REQUIRE(song.samples.size() == 2 * song.frames());
	CHECK_THAT(song.seconds(), WithinAbs(32.0f * 4.0f * 60.0f / song.beatsPerMinute, 1e-3f));

	float peak = 0.0f, sum = 0.0f, largestStep = 0.0f;
	for (std::size_t i = 0; i < song.samples.size(); ++i)
	{
		float sample = song.samples[i];
		REQUIRE(std::isfinite(sample));
		peak = std::max(peak, std::abs(sample));
		sum += sample * sample;
		if (i >= 2)
		{
			largestStep = std::max(largestStep, std::abs(sample - song.samples[i - 2]));
		}
	}
	float rms = std::sqrt(sum / static_cast<float>(song.samples.size()));
	CAPTURE(peak, rms);
	CHECK(peak <= 0.9f);
	CHECK(peak > 0.8f);
	CHECK(rms > 0.08f); //music, not silence
	//From the end to the beginning it goes on like from one sample to the next
	for (std::size_t channel = 0; channel < 2; ++channel)
	{
		float wrap = std::abs(song.samples[channel] - song.samples[song.samples.size() - 2 + channel]);
		CAPTURE(channel, wrap, largestStep);
		CHECK(wrap <= largestStep);
	}

	SECTION("It is the same every time")
	{
		Song again = composeIslandTune(sampleRate);
		CHECK(again.samples == song.samples);
	}
}
