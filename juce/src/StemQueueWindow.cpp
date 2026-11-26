#include "StemQueueWindow.h"
#include "audio/StemFile.h"
#include "audio/StemMaker.h"

#include <algorithm>
#include <vector>

namespace spindeck {

class StemQueueWindow::Queue final : public juce::Component, private juce::ListBoxModel {
  public:
    Queue() {
        list.setModel(this);
        list.setRowHeight(43);
        list.setTitle("Stem jobs");
        add.onClick = [this] { chooseSources(); };
        cancel.onClick = [this] { cancelSelected(); };
        addAndMakeVisible(list);
        addAndMakeVisible(add);
        addAndMakeVisible(cancel);
        setup.onClick = [this] { offerSetup(); };
        addChildComponent(setup);
        notice.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(notice);
        cancel.setEnabled(false);
        setSize(620, 320);
        storageUsable = restore();
        if (!storageUsable) {
            notice.setText("Could not read the saved stem queue.", juce::dontSendNotification);
        }
        refresh();
        startNext();
    }

    ~Queue() override {
        if (cancellation) {
            cancellation->request();
        }
        worker.removeAllJobs(false, -1);
        list.setModel(nullptr);
    }

    void pauseForQuit(std::function<void()> callback) {
        if (onPaused) {
            return;
        }
        if (!cancellation) {
            callback();
            return;
        }
        onPaused = std::move(callback);
        cancellation->request();
        refresh();
    }

    juce::String progressNote() const {
        const Entry* running = nullptr;
        int waiting = 0;
        for (const auto& entry : entries) {
            if (entry.state == State::running || entry.state == State::cancelling) {
                running = &entry;
            } else if (entry.state == State::queued) {
                ++waiting;
            }
        }
        if (running == nullptr && waiting == 0) {
            return {};
        }
        juce::String note = "Stems: ";
        note << (running != nullptr ? running->source.getFileNameWithoutExtension() + " " + running->status
                                  : juce::String("starting"));
        if (waiting > 0) {
            note << " +" << waiting;
        }
        return note;
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);
        auto toolbar = area.removeFromTop(34);
        add.setBounds(toolbar.removeFromLeft(106).reduced(2));
        cancel.setBounds(toolbar.removeFromLeft(130).reduced(2));
        setup.setBounds(toolbar.removeFromLeft(140).reduced(2));
        notice.setBounds(area.removeFromBottom(25));
        area.removeFromTop(8);
        list.setBounds(area);
    }

  private:
    enum class State { queued = 0, running = 1, cancelling = 2, finished = 3, failed = 4, cancelled = 5 };
    struct Entry {
        juce::File source;
        juce::File output;
        State state = State::queued;
        juce::String status = "Queued";
    };

    bool restore() {
        if (!queueFile.exists()) {
            return true;
        }
        const auto json = juce::JSON::parse(queueFile);
        if (int(json.getProperty("version", 0)) != 1) {
            return false;
        }
        const auto rows = json.getProperty("tracks", {});
        const auto* array = rows.getArray();
        if (array == nullptr) {
            return false;
        }
        std::vector<Entry> restored;
        for (const auto& row : *array) {
            const auto source = row.getProperty("source", {}).toString();
            const auto output = row.getProperty("output", {}).toString();
            const int rawState = int(row.getProperty("state", -1));
            if (!juce::File::isAbsolutePath(source) || rawState < int(State::queued) ||
                    rawState > int(State::cancelled)) {
                return false;
            }
            Entry entry{juce::File(source), juce::File(output), static_cast<State>(rawState),
                    row.getProperty("status", {}).toString()};
            if (entry.state == State::running) {
                if (entry.output.existsAsFile()) {
                    entry.output = output;
                    entry.state = State::finished;
                    entry.status = "Created " + entry.output.getFileName();
                } else {
                    entry.state = State::queued;
                    entry.status = "Queued";
                }
            } else if (entry.state == State::cancelling) {
                entry.state = State::cancelled;
                entry.status = "Cancelled";
            }
            restored.push_back(std::move(entry));
        }
        entries = std::move(restored);
        return true;
    }

    bool save() {
        if (!storageUsable) {
            return false;
        }
        juce::Array<juce::var> rows;
        for (const auto& entry : entries) {
            juce::var row(new juce::DynamicObject());
            auto* object = row.getDynamicObject();
            object->setProperty("source", entry.source.getFullPathName());
            object->setProperty("output", entry.output.getFullPathName());
            object->setProperty("state", static_cast<int>(entry.state));
            object->setProperty("status", entry.status);
            rows.add(std::move(row));
        }
        juce::var json(new juce::DynamicObject());
        json.getDynamicObject()->setProperty("version", 1);
        json.getDynamicObject()->setProperty("tracks", std::move(rows));
        const auto directory = queueFile.getParentDirectory().createDirectory();
        juce::TemporaryFile staged(queueFile);
        const bool written = directory.wasOk() &&
                staged.getFile().replaceWithText(juce::JSON::toString(json)) &&
                staged.overwriteTargetFileWithTemporary();
        notice.setText(written ? "" : "Could not save the stem queue.", juce::dontSendNotification);
        return written;
    }

    int getNumRows() override { return static_cast<int>(entries.size()); }

    juce::String getNameForRow(int row) override {
        if (!juce::isPositiveAndBelow(row, getNumRows())) {
            return {};
        }
        const auto& entry = entries[static_cast<std::size_t>(row)];
        return entry.source.getFileName() + ", " + entry.status;
    }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override {
        if (!juce::isPositiveAndBelow(row, getNumRows())) {
            return;
        }
        const auto& entry = entries[static_cast<std::size_t>(row)];
        if (selected) {
            g.fillAll(juce::Colour::fromRGB(38, 58, 45));
        }
        auto bounds = juce::Rectangle<int>(0, 0, width, height).reduced(9, 2);
        g.setColour(juce::Colours::white);
        g.drawText(entry.source.getFileName(), bounds.removeFromTop(20), juce::Justification::centredLeft);
        g.setColour(entry.state == State::failed ? juce::Colours::salmon : juce::Colours::lightgrey);
        g.drawText(entry.status, bounds, juce::Justification::centredLeft);
    }

    void selectedRowsChanged(int) override { updateCancelButton(); }

    void updateCancelButton() {
        const int row = list.getSelectedRow();
        const auto state = juce::isPositiveAndBelow(row, getNumRows())
                ? entries[static_cast<std::size_t>(row)].state : State::finished;
        cancel.setEnabled(storageUsable && !onPaused && (state == State::queued || state == State::running));
    }

    void refresh() {
        add.setEnabled(storageUsable && !onPaused);
        const bool pending = std::any_of(entries.begin(), entries.end(),
                [](const auto& entry) { return entry.state == State::queued; });
        setup.setVisible(pending && !allowSetup && !StemMaker::isReady() && !cancellation && !onPaused);
        list.updateContent();
        list.repaint();
        updateCancelButton();
    }

    void chooseSources() {
        if (chooser || onPaused) {
            return;
        }
        chooser = std::make_unique<juce::FileChooser>("Choose tracks to split");
        const auto flags = juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems;
        chooser->launchAsync(flags, [safe = SafePointer<Queue>(this)](const juce::FileChooser& picker) {
            if (!safe) {
                return;
            }
            const auto sources = picker.getResults();
            safe->chooser.reset();
            if (sources.isEmpty()) {
                return;
            }
            if (StemMaker::isReady() || safe->allowSetup) {
                safe->enqueue(sources);
            } else {
                safe->offerSetup(sources);
            }
        });
    }

    void offerSetup(juce::Array<juce::File> sources = {}) {
        const auto options = juce::MessageBoxOptions{}
                .withIconType(juce::MessageBoxIconType::QuestionIcon)
                .withTitle("Set up Make Stems")
                .withMessage("Install Demucs and its model locally? The one-time download is about 2 to 3 GB.")
                .withButton("Install", 1)
                .withButton("Cancel", 0);
        juce::AlertWindow::showAsync(options, [safe = SafePointer<Queue>(this), sources](int accepted) {
            if (safe && accepted == 1) {
                safe->allowSetup = true;
                safe->enqueue(sources);
            }
        });
    }

    void enqueue(const juce::Array<juce::File>& files) {
        if (onPaused) {
            return;
        }
        for (const auto& file : files) {
            if (std::none_of(entries.begin(), entries.end(), [&](const auto& entry) {
                    return entry.source == file && (entry.state == State::queued || entry.state == State::running ||
                            entry.state == State::cancelling);
                })) {
                entries.push_back({file, {}});
            }
        }
        save();
        refresh();
        startNext();
    }

  public:
    void request(const juce::Array<juce::File>& sources) {
        if (sources.isEmpty() || onPaused) {
            return;
        }
        if (StemMaker::isReady() || allowSetup) {
            enqueue(sources);
        } else {
            offerSetup(sources);
        }
    }

    void cancelCurrent() {
        for (auto& entry : entries) {
            if (entry.state == State::running && cancellation && cancellation->request()) {
                entry.state = State::cancelling;
                entry.status = "Cancelling...";
            }
        }
        save();
        refresh();
    }

    void cancelWaiting() {
        for (auto& entry : entries) {
            if (entry.state == State::queued) {
                entry.state = State::cancelled;
                entry.status = "Cancelled";
            }
        }
        save();
        refresh();
    }

    int waitingCount() const {
        return static_cast<int>(std::count_if(entries.begin(), entries.end(),
                [](const auto& entry) { return entry.state == State::queued; }));
    }

  private:
    void cancelSelected() {
        const auto row = list.getSelectedRow();
        if (!juce::isPositiveAndBelow(row, getNumRows())) {
            return;
        }
        auto& entry = entries[static_cast<std::size_t>(row)];
        if (entry.state == State::queued) {
            entry.state = State::cancelled;
            entry.status = "Cancelled";
        } else if (entry.state == State::running && cancellation && cancellation->request()) {
            entry.state = State::cancelling;
            entry.status = "Cancelling...";
        }
        save();
        refresh();
    }

    void startNext() {
        if (cancellation || onPaused || !storageUsable || (!allowSetup && !StemMaker::isReady())) {
            return;
        }
        const auto next = std::find_if(entries.begin(), entries.end(),
                [](const auto& entry) { return entry.state == State::queued; });
        if (next == entries.end()) {
            return;
        }
        const auto row = static_cast<std::size_t>(next - entries.begin());
        next->state = State::running;
        next->status = "Preparing...";
        const auto source = next->source;
        const auto setupAllowed = allowSetup;
        if (next->output == juce::File{} || next->output.exists()) {
            next->output = StemMaker::nextOutputFor(source);
        }
        const auto destination = next->output;
        if (!save()) {
            next->state = State::queued;
            next->status = "Queued";
            refresh();
            return;
        }
        auto job = std::make_shared<StemMaker::Cancellation>();
        cancellation = job;
        refresh();

        const auto resources = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                .getParentDirectory().getSiblingFile("Resources");
        worker.addJob(std::function<void()>{[safe = SafePointer<Queue>(this), job, row, source, resources, setupAllowed, destination] {
            juce::File output;
            const auto result = StemMaker::make(source, resources, setupAllowed, output,
                    [safe, row](juce::String status) {
                        juce::MessageManager::callAsync([safe, row, status] {
                            if (safe && safe->entries[row].state == State::running) {
                                safe->entries[row].status = status;
                                safe->refresh();
                            }
                        });
                    }, job.get(), destination);
            juce::MessageManager::callAsync([safe, row, job, output, result, setupAllowed] {
                if (!safe) {
                    return;
                }
                auto& entry = safe->entries[row];
                if (result.wasOk()) {
                    entry.output = output;
                    entry.state = State::finished;
                    entry.status = "Created " + output.getFileName();
                } else if (job->requested()) {
                    const bool suspended = safe->onPaused && entry.state == State::running;
                    entry.state = suspended ? State::queued : State::cancelled;
                    entry.status = suspended ? "Queued" : "Cancelled";
                } else {
                    entry.state = State::failed;
                    entry.status = result.getErrorMessage();
                }
                if (setupAllowed) {
                    safe->allowSetup = false;
                }
                safe->cancellation.reset();
                const bool saved = safe->save();
                safe->refresh();
                if (safe->onPaused) {
                    auto done = std::move(safe->onPaused);
                    done();
                } else if (saved) {
                    safe->startNext();
                }
            });
        }});
    }

    const juce::File queueFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support/Spin Deck JUCE/Stem Queue.json");
    std::vector<Entry> entries;
    juce::Label notice;
    juce::TextButton setup{"SET UP DEMUCS"};
    bool storageUsable = true;
    std::function<void()> onPaused;
    juce::TextButton add{"ADD TRACKS"};
    juce::TextButton cancel{"CANCEL SELECTED"};
    juce::ListBox list;
    std::unique_ptr<juce::FileChooser> chooser;
    bool allowSetup = false;
    std::shared_ptr<StemMaker::Cancellation> cancellation;
    juce::ThreadPool worker{1};
};

StemQueueWindow::StemQueueWindow(juce::Component* owner)
        : juce::DocumentWindow("Make Stems", juce::Colour::fromRGB(22, 22, 22),
                juce::DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    setContentOwned(new Queue(), true);
    centreAroundComponent(owner, getWidth(), getHeight());
}

void StemQueueWindow::pauseForQuit(std::function<void()> onPaused) {
    static_cast<Queue*>(getContentComponent())->pauseForQuit(std::move(onPaused));
}

void StemQueueWindow::addTracks(const juce::Array<juce::File>& files) {
    static_cast<Queue*>(getContentComponent())->request(files);
}

void StemQueueWindow::cancelCurrent() {
    static_cast<Queue*>(getContentComponent())->cancelCurrent();
}

void StemQueueWindow::cancelWaiting() {
    static_cast<Queue*>(getContentComponent())->cancelWaiting();
}

int StemQueueWindow::waitingCount() const {
    const auto* queue = dynamic_cast<const Queue*>(getContentComponent());
    return queue ? queue->waitingCount() : 0;
}

juce::String StemQueueWindow::progressNote() const {
    const auto* queue = dynamic_cast<const Queue*>(getContentComponent());
    return queue ? queue->progressNote() : juce::String{};
}

void StemQueueWindow::closeButtonPressed() {
    setVisible(false);
}

}
