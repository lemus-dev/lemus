// lemus-render: plays scenes through plugins offline and writes a WAV per source. Headless (no
// audio device, no window), faster than real time, and each run is its own process, so any
// number can run at once. See tools/host/README.md.
//
//   lemus-render SCENE.json [SCENE.json ...] [--out DIR] [--only LABEL,...] [--sources FILE ...]
// Source templates come from every machines/<id>/host/sources.json; --sources adds files.
//   lemus-render --params PLUGIN|use:TEMPLATE [--machine ID]   list a plugin's parameters
#include "../Player.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

using namespace lemus::host;

namespace {

int listParams(const juce::String &plugin, const juce::String &machine, const Templates &templates,
               juce::AudioPluginFormatManager &formats)
{
	Scene scene;
	Source src;
	src.plugin = plugin;
	src.machine = machine;
	if (plugin.startsWith("use:")) {
		// A template from sources.json, state and all: load a one-source scene that uses it.
		const auto tmp = juce::File::createTempFile(".json");
		tmp.replaceWithText("{\"sources\": [{\"use\": \"" + plugin.fromFirstOccurrenceOf("use:", false, false) + "\"}]}");
		juce::String error;
		scene = Scene::load(tmp, templates, error);
		tmp.deleteFile();
		if (error.isNotEmpty()) {
			std::fprintf(stderr, "%s\n", error.toRawUTF8());
			return 1;
		}
		src = scene.sources[0];
	}
	Player player(formats, scene, src, false);
	if (player.error().isNotEmpty()) {
		std::fprintf(stderr, "%s\n", player.error().toRawUTF8());
		return 1;
	}
	const auto &params = player.plugin()->getParameters();
	std::printf("%s: %d parameters, %d in, %d out, latency %d\n", player.plugin()->getName().toRawUTF8(), params.size(),
	            player.plugin()->getTotalNumInputChannels(), player.numOutputs(), player.latency());
	for (int i = 0; i < params.size(); ++i) {
		auto *p = params[i];
		std::printf("%4d  %-32s %-24s (%.4f, %d steps)\n", i, p->getName(64).toRawUTF8(), p->getCurrentValueAsText().toRawUTF8(),
		            double(p->getValue()), p->getNumSteps());
	}
	return 0;
}

bool render(const Scene &scene, const Source &src, const juce::File &outDir, juce::AudioPluginFormatManager &formats)
{
	Player player(formats, scene, src, false);
	for (const auto &w : player.warnings())
		std::fprintf(stderr, "%s/%s: %s\n", scene.name.toRawUTF8(), src.label.toRawUTF8(), w.toRawUTF8());
	if (player.error().isNotEmpty()) {
		std::fprintf(stderr, "%s/%s: %s\n", scene.name.toRawUTF8(), src.label.toRawUTF8(), player.error().toRawUTF8());
		return false;
	}
	// Render past the end by the plugin's latency and drop that much from the start.
	const int lat = player.latency();
	const int64_t total = player.length();
	juce::AudioBuffer<float> out(player.numOutputs(), int(total));
	juce::AudioBuffer<float> block(player.numOutputs(), scene.block);
	for (int64_t pos = 0; pos < total + lat; pos += scene.block) {
		const int n = int(std::min<int64_t>(scene.block, total + lat - pos));
		juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), block.getNumChannels(), 0, n);
		player.process(view, pos);
		for (int i = 0; i < n; ++i) {
			const int64_t at = pos + i - lat;
			if (at >= 0 && at < total)
				for (int c = 0; c < out.getNumChannels(); ++c)
					out.setSample(c, int(at), view.getSample(c, i));
		}
	}
	const auto file = outDir.getChildFile(scene.name + "-" + src.label + ".wav");
	file.deleteFile();
	std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::AudioFormatWriter> writer(stream ? wav.createWriterFor(stream.get(), scene.rate, juce::uint32(out.getNumChannels()), 32, {}, 0) : nullptr);
	if (writer == nullptr) {
		std::fprintf(stderr, "can't write %s\n", file.getFullPathName().toRawUTF8());
		return false;
	}
	stream.release();
	writer->writeFromAudioSampleBuffer(out, 0, out.getNumSamples());
	std::printf("%s\n", file.getFullPathName().toRawUTF8());
	return true;
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI gui; // plugins load on the message thread
	juce::AudioPluginFormatManager formats;
	formats.addDefaultFormats();

	const auto cwd = juce::File::getCurrentWorkingDirectory();
	juce::File outDir = cwd;
	auto templateFiles = Templates::standardFiles();
	juce::StringArray only, scenes;
	juce::String paramsOf, machine;
	for (int i = 1; i < argc; ++i) {
		const juce::String a(argv[i]);
		if (a == "--out" && i + 1 < argc)
			outDir = cwd.getChildFile(argv[++i]);
		else if (a == "--only" && i + 1 < argc)
			only.addTokens(argv[++i], ",", "");
		else if (a == "--sources" && i + 1 < argc)
			templateFiles.add(cwd.getChildFile(argv[++i]));
		else if (a == "--params" && i + 1 < argc)
			paramsOf = argv[++i];
		else if (a == "--machine" && i + 1 < argc)
			machine = argv[++i];
		else
			scenes.add(a);
	}
	juce::String templateError;
	const auto templates = Templates::load(templateFiles, templateError);
	if (templateError.isNotEmpty()) {
		std::fprintf(stderr, "%s\n", templateError.toRawUTF8());
		return 2;
	}
	if (paramsOf.isNotEmpty())
		return listParams(paramsOf, machine, templates, formats);
	if (scenes.isEmpty()) {
		std::fprintf(stderr, "usage: lemus-render SCENE.json [...] [--out DIR] [--only LABEL,...] [--sources FILE]\n"
		                     "       lemus-render --params PLUGIN|use:TEMPLATE [--machine ID]\n");
		return 2;
	}
	outDir.createDirectory();
	int failures = 0;
	for (const auto &path : scenes) {
		juce::String error;
		const auto scene = Scene::load(cwd.getChildFile(path), templates, error);
		if (error.isNotEmpty()) {
			std::fprintf(stderr, "%s: %s\n", path.toRawUTF8(), error.toRawUTF8());
			++failures;
			continue;
		}
		for (const auto &src : scene.sources)
			if (only.isEmpty() || only.contains(src.label))
				failures += render(scene, src, outDir, formats) ? 0 : 1;
	}
	return failures == 0 ? 0 : 1;
}
