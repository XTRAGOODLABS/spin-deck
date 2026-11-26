#include <JuceHeader.h>

#include "MainComponent.h"

namespace spindeck {

class SpinDeckApplication final : public juce::JUCEApplication {
  public:
    const juce::String getApplicationName() override {
        return "Spin Deck";
    }

    const juce::String getApplicationVersion() override {
        return "3.0.0";
    }

    void initialise(const juce::String&) override {
        window = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override {
        window.reset();
    }

    void systemRequestedQuit() override {
        if (window) {
            static_cast<MainComponent*>(window->getContentComponent())->pauseStemQueue([] { quit(); });
        } else {
            quit();
        }
    }

  private:
    class MainWindow final : public juce::DocumentWindow {
      public:
        explicit MainWindow(const juce::String& name)
                : juce::DocumentWindow(name,
                          juce::Colour::fromRGB(11, 11, 11),
                          juce::DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(1000, 730, 4096, 2160);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> window;
};

}

START_JUCE_APPLICATION(spindeck::SpinDeckApplication)
