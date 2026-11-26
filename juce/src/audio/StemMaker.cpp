#include "audio/StemMaker.h"
#include "audio/StemFile.h"

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace spindeck {
namespace {

juce::File runtimeDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support/Spin Deck/stems");
}

juce::Result run(const juce::StringArray& command, const StemMaker::Progress& progress,
        const StemMaker::Cancellation* cancellation) {
    int pipeEnds[2];
    if (::pipe(pipeEnds) != 0) {
        return juce::Result::fail("Could not open the splitter output pipe.");
    }
    for (const int fd : pipeEnds) {
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], STDERR_FILENO);
    juce::Array<char*> argv;
    for (const auto& argument : command) {
        argv.add(const_cast<char*>(argument.toRawUTF8()));
    }
    argv.add(nullptr);
    pid_t pid = 0;
    const int spawnError = posix_spawn(&pid, argv[0], &actions, &attributes,
            argv.getRawDataPointer(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    ::close(pipeEnds[1]);
    if (spawnError != 0) {
        ::close(pipeEnds[0]);
        return juce::Result::fail("Could not start the local stem splitter.");
    }

    juce::MemoryBlock line;
    juce::String error;
    bool done = false;
    int lastPercent = -1;
    const auto readLine = [&] {
        if (line.isEmpty()) {
            return;
        }
        const auto text = juce::String::fromUTF8(static_cast<const char*>(line.getData()),
                static_cast<int>(line.getSize()));
        line.reset();
        if (text.startsWith("STATUS ")) {
            progress(text.substring(7));
        } else if (text.startsWith("ERROR ")) {
            error = text.substring(6);
        } else if (text == "DONE" || text.startsWith("DONE ")) {
            done = true;
        } else {
            const auto bar = text.indexOf("%|");
            const auto prefix = bar > 0 ? text.substring(0, bar).trim() : juce::String{};
            if (prefix.isNotEmpty() && prefix.containsOnly("0123456789")) {
                const int percent = prefix.getIntValue();
                if (percent <= 100 && percent != lastPercent) {
                    lastPercent = percent;
                    progress("Separating " + juce::String(percent) + "%");
                }
            }
        }
    };
    bool readError = false;
    for (;;) {
        if (cancellation && cancellation->requested()) {
            break;
        }
        pollfd descriptor{pipeEnds[0], POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 100);
        if (ready == 0 || (ready < 0 && errno == EINTR)) {
            continue;
        }
        char bytes[4096];
        const auto count = ready > 0 ? ::read(pipeEnds[0], bytes, sizeof(bytes)) : -1;
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            readError = count < 0;
            break;
        }
        for (ssize_t i = 0; i < count; ++i) {
            if (bytes[i] == '\n' || bytes[i] == '\r') {
                readLine();
            } else if (line.getSize() < 4096) {
                line.append(bytes + i, 1);
            }
        }
    }
    const bool cancelled = cancellation && cancellation->requested();
    if (cancelled || readError) {
        ::kill(-pid, SIGKILL);
    }
    ::close(pipeEnds[0]);
    int status = 0;
    pid_t waited;
    do {
        waited = ::waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (cancelled) {
        return juce::Result::fail("Cancelled");
    }
    readLine();
    if (readError || waited != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
            !done || error.isNotEmpty()) {
        return juce::Result::fail(error.isNotEmpty() ? error : "The local stem splitter stopped unexpectedly.");
    }
    return juce::Result::ok();
}

}

bool StemMaker::Cancellation::request() {
    auto expected = State::running;
    return state.compare_exchange_strong(expected, State::cancelled);
}

bool StemMaker::Cancellation::requested() const {
    return state.load() == State::cancelled;
}

bool StemMaker::isReady() {
    const auto runtime = runtimeDirectory();
    return runtime.getChildFile("ready").existsAsFile() &&
            runtime.getChildFile("venv/bin/python").existsAsFile();
}

juce::File StemMaker::associatedOutputFor(const juce::File& source) {
    const auto queue = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support/Spin Deck JUCE/Stem Queue.json");
    const auto json = juce::JSON::parse(queue);
    const auto tracks = json.getProperty("tracks", {});
    if (const auto* rows = tracks.getArray()) {
        for (int i = rows->size(); --i >= 0;) {
            const auto& job = rows->getReference(i);
            const auto path = job.getProperty("output", {}).toString();
            if (static_cast<int>(job.getProperty("state", -1)) == 3 &&
                    job.getProperty("source", {}).toString() == source.getFullPathName() &&
                    juce::File::isAbsolutePath(path)) {
                const juce::File candidate(path);
                if (candidate.existsAsFile()) {
                    return candidate;
                }
            }
        }
    }
    return source.getSiblingFile(source.getFileNameWithoutExtension() + ".stem.mp4");
}

juce::File StemMaker::nextOutputFor(const juce::File& source) {
    return source.getParentDirectory().getNonexistentChildFile(
            source.getFileNameWithoutExtension(), ".stem.mp4", false);
}

juce::Result StemMaker::make(const juce::File& source, const juce::File& resources,
        bool allowSetup, juce::File& output, const Progress& progress, Cancellation* cancellation,
        const juce::File& destination) {
    output = juce::File{};
    if (cancellation && cancellation->requested()) {
        return juce::Result::fail("Cancelled");
    }
    if (!source.existsAsFile() || isStemFile(source)) {
        return juce::Result::fail("Choose an original audio file to split.");
    }

    const auto script = resources.getChildFile("spindeck_stems.py");
    const auto setupScript = resources.getChildFile("spindeck_stems_setup.sh");
    if (!script.existsAsFile() || !setupScript.existsAsFile()) {
        return juce::Result::fail("The stem splitter resources are missing from the app.");
    }
    const auto runtime = runtimeDirectory();
    if (!isReady()) {
        if (!allowSetup) {
            return juce::Result::fail("The local stem splitter needs setup.");
        }
        progress("Setting up Demucs...");
        const auto setup = run({"/usr/bin/env", "SPINDECK_STEMS_HOME=" + runtime.getFullPathName(),
                "/bin/bash", setupScript.getFullPathName()}, progress, cancellation);
        if (setup.failed()) {
            return setup;
        }
    }
    if (!isReady()) {
        return juce::Result::fail("Demucs setup did not create a working runtime.");
    }

    const auto folder = source.getParentDirectory();
    const juce::TemporaryFile temporary(folder.getChildFile(".spindeck-stems"), 0);
    const auto staging = temporary.getFile();
    const auto created = staging.createDirectory();
    if (created.failed()) {
        return juce::Result::fail("Could not prepare stem output. " + created.getErrorMessage());
    }
    struct StagingDirectory {
        juce::File folder;
        ~StagingDirectory() { folder.deleteRecursively(); }
    } cleanup{staging};

    const auto basename = source.getFileNameWithoutExtension();
    const auto staged = staging.getChildFile(basename + ".stem.mp4");
    progress("Starting Demucs...");
    const auto split = run({"/usr/bin/env",
            "TORCH_HOME=" + runtime.getChildFile("models").getFullPathName(),
            "TMPDIR=" + staging.getFullPathName(), "PYTHONUNBUFFERED=1", runtime.getChildFile("venv/bin/python").getFullPathName(),
            script.getFullPathName(), source.getFullPathName(), "--out", staging.getFullPathName()}, progress, cancellation);
    if (split.failed()) {
        return split;
    }
    if (!staged.existsAsFile() || staged.getSize() == 0) {
        return juce::Result::fail("The splitter did not produce a stem file.");
    }
    if (cancellation) {
        auto expected = Cancellation::State::running;
        if (!cancellation->state.compare_exchange_strong(expected, Cancellation::State::publishing)) {
            return juce::Result::fail("Cancelled");
        }
    }
    const auto target = destination == juce::File{} ? nextOutputFor(source) : destination;
    if (::renamex_np(staged.getFullPathName().toRawUTF8(), target.getFullPathName().toRawUTF8(), RENAME_EXCL) != 0) {
        return juce::Result::fail("Could not save the stem file beside the original.");
    }
    output = target;
    return juce::Result::ok();
}

}
