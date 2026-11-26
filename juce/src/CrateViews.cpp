#include "CrateViews.h"
#include "OneLibraryImport.h"
#include "MixxxImport.h"
#include "audio/StemFile.h"

#include <cmath>

namespace spindeck {
namespace {

juce::String displayTitle(const juce::File& file) {
    const auto name = file.getFileNameWithoutExtension();
    return name.endsWithIgnoreCase(".stem") ? name.dropLastCharacters(5) : name;
}

bool sameSessionTrack(const juce::File& a, const juce::File& b) {
    return a == b || (a.getParentDirectory() == b.getParentDirectory() &&
            displayTitle(a).equalsIgnoreCase(displayTitle(b)));
}

juce::String savedTitle(const GoodMixxHistory::Song& song) {
    return song.name.isNotEmpty() ? song.name : displayTitle(song.file);
}

bool matchesSavedSong(const GoodMixxHistory::Song& song, const juce::File& file) {
    return file != juce::File{} && (song.matches(file) ||
            (song.file != juce::File{} && sameSessionTrack(song.file, file)));
}

struct PlaylistImport {
    CratePlaylist playlist;
    int missing = 0;
};

PlaylistImport readPlaylist(const juce::File& source) {
    PlaylistImport result{{source.getFullPathName(), {}, source.getFileNameWithoutExtension()}, 0};
    const juce::File volumes("/Volumes");
    const auto mounted = volumes.findChildFiles(juce::File::findDirectories, false);
    for (const auto& line : juce::StringArray::fromLines(source.loadFileAsString())) {
        const auto path = line.trim();
        if (path.isEmpty() || path.startsWithChar('#')) {
            continue;
        }
        if (path.contains("://")) {
            ++result.missing;
            continue;
        }
        auto file = source.getSiblingFile(path);
        // Crate OS exports absolute paths. Reuse the relative path on a uniquely matching mounted volume.
        if (!file.existsAsFile() && path.startsWith("/Volumes/")) {
            const auto suffixStart = path.indexOf(9, "/");
            if (suffixStart >= 0) {
                juce::File match;
                for (const auto& volume : mounted) {
                    const auto candidate = volume.getChildFile(path.substring(suffixStart + 1));
                    if (candidate.existsAsFile()) {
                        if (match != juce::File{}) {
                            match = juce::File{};
                            break;
                        }
                        match = candidate;
                    }
                }
                file = match;
            }
        }
        if (file.existsAsFile()) {
            result.playlist.files.push_back(file);
        } else {
            ++result.missing;
        }
    }
    return result;
}

class MixLoadControl final : public juce::Component {
  public:
    MixLoadControl() {
        setInterceptsMouseClicks(false, true);
        addAndMakeVisible(button);
    }

    void resized() override {
        button.setBounds(getLocalBounds().withTrimmedBottom(6).reduced(10, 8).removeFromBottom(22).removeFromRight(64));
    }

    juce::TextButton button;
};

}

namespace {
struct Theme {
    const char* name;
    std::array<uint32_t, 28> colours; // Same order as the list in applyTheme.
    std::array<uint32_t, 4> decks;
    std::array<uint32_t, 5> wave;
    bool monospace;
};

const std::array<Theme, 3>& allThemes() {
    static const std::array<Theme, 3> themes{{
        {"CRATE",
                {0xff0c0d0e, 0xff17191b, 0xff0f1112, 0xff2b2f32, 0xffeeeeec, 0xff989ea2, 0xff45c94a, 0xffff8a1d,
                 0xff24282b, 0xff5a2f0c, 0xff0b0c0d, 0xff2f9a33, 0xffe2700f, 0xff070808, 0xff1b1e20, 0xff3a3f43,
                 0xff2a4a33, 0xff121416, 0xff4aa8ff, 0xff1d2022, 0xff1f5a23, 0xff9df59d, 0xff253a4f, 0xff3b4043,
                 0xff151719, 0xff050606, 0xffd8d8d6, 0xffff3b30},
                {0xff76dba0, 0xffedb66c, 0xff8cb4eb, 0xffd89fcf},
                {0xffff2d1c, 0xffff7a14, 0xfff5d31c, 0xff4fe04a, 0xff36c9e8}, false},
        {"Terminal",
                {0xff000000, 0xff030803, 0xff000300, 0xff0d3a14, 0xff3dff6e, 0xff1f9c42, 0xff3dff6e, 0xffb6ff4a,
                 0xff031b09, 0xff0d2a05, 0xff001200, 0xff23c24a, 0xff7cd62a, 0xff000000, 0xff010601, 0xff0b4a1c,
                 0xff0b3d18, 0xff010a03, 0xff9dffb6, 0xff020f05, 0xff0b4a1c, 0xff9dffb6, 0xff0b3d18, 0xff0f3d1a,
                 0xff010a03, 0xff000000, 0xff3dff6e, 0xffe8ff4a},
                {0xff3dff6e, 0xffb6ff4a, 0xff4affd2, 0xff8dff9e},
                {0xff0a5a1e, 0xff12923a, 0xff2ccf58, 0xff6dff92, 0xffcfffdb}, true},
        {"Midnight",
                {0xff090d18, 0xff121a2b, 0xff0b111f, 0xff24314d, 0xffe6ecff, 0xff8a96b8, 0xff4fd1ff, 0xffff5fa2,
                 0xff1b2540, 0xff4a1530, 0xff070a14, 0xff2a8fd6, 0xffe2407f, 0xff05080f, 0xff0e1526, 0xff2c3a5c,
                 0xff1d3355, 0xff0d1424, 0xffffd166, 0xff17203a, 0xff1b3b66, 0xff9fd8ff, 0xff22365e, 0xff34436a,
                 0xff10172a, 0xff04060c, 0xffdfe6ff, 0xffff4d6d},
                {0xff4fd1ff, 0xffff5fa2, 0xffb18cff, 0xffffd166},
                {0xff6a3dff, 0xff3d7bff, 0xff3dc8ff, 0xffff5fa2, 0xffffd166}, false},
    }};
    return themes;
}

std::array<juce::Colour*, 28> themeSlots() {
    using namespace crate;
    return {&background, &panel, &inset, &line, &text, &muted, &green, &amber, &control, &orangeDark, &ink,
            &playFill, &cueFill, &waveBackground, &topBar, &tabOn, &selection, &rowAlt, &playhead, &card,
            &starFill, &starText, &folderSelection, &knobTop, &knobBottom, &track, &thumb, &clip};
}
}

juce::StringArray crate::themeNames() {
    juce::StringArray names;
    for (const auto& theme : allThemes()) {
        names.add(theme.name);
    }
    return names;
}

std::vector<std::pair<juce::Colour, juce::Colour>> crate::applyTheme(int index) {
    const auto& theme = allThemes()[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(allThemes().size()) - 1, index))];
    std::vector<std::pair<juce::Colour, juce::Colour>> pairs;
    const auto change = [&](juce::Colour& slot, juce::Colour next) {
        if (slot != next) {
            pairs.emplace_back(slot, next);
            // Common translucent and darkened variants used by individual controls.
            for (const float alpha : {0.6f, 0.8f}) {
                pairs.emplace_back(slot.withAlpha(alpha), next.withAlpha(alpha));
            }
        }
        slot = next;
    };
    const auto slots = themeSlots();
    for (std::size_t i = 0; i < slots.size(); ++i) {
        change(*slots[i], juce::Colour(theme.colours[i]));
    }
    for (std::size_t i = 0; i < decks.size(); ++i) {
        change(decks[i], juce::Colour(theme.decks[i]));
    }
    for (std::size_t i = 0; i < wave.size(); ++i) {
        wave[i] = juce::Colour(theme.wave[i]);
    }
#if JUCE_MAC
    const juce::String monospace = "Menlo";
#else
    const juce::String monospace = "DejaVu Sans Mono";
#endif
    fontName = theme.monospace ? monospace : juce::String{};
    return pairs;
}

void crate::recolour(juce::Component& component, const std::vector<std::pair<juce::Colour, juce::Colour>>& pairs) {
    auto& properties = component.getProperties();
    for (int i = 0; i < properties.size(); ++i) {
        const auto name = properties.getName(i).toString();
        if (!name.startsWith("jcclr_")) {
            continue;
        }
        const auto argb = static_cast<juce::uint32>(static_cast<int>(properties.getValueAt(i)));
        for (const auto& [from, to] : pairs) {
            if (from.getARGB() == argb) {
                properties.set(properties.getName(i), static_cast<int>(to.getARGB()));
                component.colourChanged();
                break;
            }
        }
    }
    for (auto* child : component.getChildren()) {
        recolour(*child, pairs);
    }
}

juce::String crate::keyName(MusicalKey key) {
    static const juce::StringArray names{"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return names[((key.tonic % 12) + 12) % 12] + (key.major ? "" : "m");
}

juce::String crate::clockText(double seconds, bool hundredths) {
    const bool negative = seconds < 0;
    seconds = std::abs(seconds);
    const auto minutes = static_cast<int>(seconds / 60);
    const double rest = seconds - minutes * 60;
    auto value = juce::String(minutes).paddedLeft('0', 2) + ":" +
            (hundredths ? juce::String(rest, 2).paddedLeft('0', 5) : juce::String(static_cast<int>(rest)).paddedLeft('0', 2));
    return (negative ? juce::String::fromUTF8("\xE2\x80\x93") : juce::String{}) + value;
}

std::pair<juce::String, juce::String> crate::titleAndArtist(const juce::File& file) {
    const auto name = displayTitle(file);
    const auto dash = name.indexOf(" - ");
    if (dash > 0) {
        return {name.substring(dash + 3).trim(), name.substring(0, dash).trim()};
    }
    return {name, {}};
}

crate::Look::Look() {
    applyThemeColours();
}

void crate::Look::applyThemeColours() {
    setDefaultSansSerifTypefaceName(fontName);
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::TextButton::buttonColourId, control);
    setColour(juce::TextButton::buttonOnColourId, green);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, crate::ink);
    setColour(juce::ComboBox::backgroundColourId, control);
    setColour(juce::ComboBox::outlineColourId, line);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::arrowColourId, muted);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::ListBox::backgroundColourId, inset);
    setColour(juce::TableHeaderComponent::backgroundColourId, panel);
    setColour(juce::TableHeaderComponent::textColourId, muted);
    setColour(juce::TableHeaderComponent::outlineColourId, line);
    setColour(juce::TextEditor::backgroundColourId, inset);
    setColour(juce::TextEditor::outlineColourId, line);
    setColour(juce::TextEditor::focusedOutlineColourId, green.withAlpha(0.6f));
    setColour(juce::Slider::thumbColourId, crate::thumb);
    setColour(juce::Slider::trackColourId, green);
    setColour(juce::Slider::backgroundColourId, crate::track);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, text);
    setColour(juce::ScrollBar::thumbColourId, crate::tabOn);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, selection);
    setColour(juce::PopupMenu::highlightedTextColourId, text);
    setColour(juce::AlertWindow::backgroundColourId, panel);
    setColour(juce::AlertWindow::textColourId, text);
    setColour(juce::TooltipWindow::backgroundColourId, panel);
    setColour(juce::TooltipWindow::textColourId, text);
    setColour(juce::TooltipWindow::outlineColourId, line);
}

void crate::Look::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& base,
        bool over, bool down) {
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto fill = button.getToggleState() ? button.findColour(juce::TextButton::buttonOnColourId) : base;
    if (!button.isEnabled()) {
        fill = fill.withMultipliedAlpha(0.55f);
    } else if (down) {
        fill = fill.brighter(0.25f);
    } else if (over) {
        fill = fill.brighter(0.1f);
    }
    if (fill.getAlpha() == 0 && !over && !down) {
        return; // Flat toolbar buttons show only text until hovered.
    }
    if (fill.getAlpha() == 0) {
        fill = juce::Colours::white.withAlpha(down ? 0.12f : 0.07f);
    }
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 3.0f);
    if (!button.getToggleState()) {
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    }
}

juce::Font crate::Look::getTextButtonFont(juce::TextButton& button, int height) {
    const float size = button.getProperties().getWithDefault("fontSize", 0.0f);
    return juce::Font(juce::FontOptions(size > 0 ? size : juce::jmin(13.0f, height * 0.56f), juce::Font::bold));
}

void crate::Look::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
        float startAngle, float endAngle, juce::Slider& slider) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(2.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto knob = juce::Rectangle<float>(radius * 2, radius * 2).withCentre(centre);
    const float angle = startAngle + position * (endAngle - startAngle);
    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius - 1, radius - 1, 0, startAngle, endAngle, true);
    g.setColour(line);
    g.strokePath(arc, juce::PathStrokeType(2.0f));
    const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
    if (accent.getAlpha() > 0 && slider.isEnabled()) {
        const float mid = startAngle + 0.5f * (endAngle - startAngle);
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, radius - 1, radius - 1, 0,
                std::min(mid, angle), std::max(mid, angle), true);
        g.setColour(accent);
        g.strokePath(value, juce::PathStrokeType(2.0f));
    }
    const auto body = knob.reduced(radius * 0.22f);
    g.setGradientFill(juce::ColourGradient(crate::knobTop, body.getX(), body.getY(),
            crate::knobBottom, body.getRight(), body.getBottom(), false));
    g.fillEllipse(body);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawEllipse(body, 1.0f);
    const float inner = body.getWidth() * 0.5f;
    g.setColour(text);
    g.drawLine(centre.x + std::sin(angle) * inner * 0.15f, centre.y - std::cos(angle) * inner * 0.15f,
            centre.x + std::sin(angle) * inner * 0.95f, centre.y - std::cos(angle) * inner * 0.95f, 2.0f);
}

int crate::Look::getSliderThumbRadius(juce::Slider& slider) {
    return slider.isHorizontal() ? 8 : 7;
}

void crate::Look::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float position,
        float, float, juce::Slider::SliderStyle style, juce::Slider& slider) {
    if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, position, 0, 0, style, slider);
        return;
    }
    const auto thumb = slider.findColour(juce::Slider::thumbColourId);
    if (style == juce::Slider::LinearVertical) {
        const float cx = x + width * 0.5f;
        g.setColour(crate::track);
        g.fillRoundedRectangle(cx - 2.0f, static_cast<float>(y), 4.0f, static_cast<float>(height), 2.0f);
        const float capW = juce::jmin(38.0f, width - 2.0f), capH = 14.0f;
        const auto cap = juce::Rectangle<float>(capW, capH).withCentre({cx, position});
        g.setGradientFill(juce::ColourGradient(thumb.brighter(0.2f), cap.getX(), cap.getY(),
                thumb.darker(0.5f), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 2.0f);
        g.setColour(crate::knobBottom);
        g.drawHorizontalLine(juce::roundToInt(cap.getCentreY()), cap.getX() + 2, cap.getRight() - 2);
        return;
    }
    const float cy = y + height * 0.5f;
    g.setColour(crate::track);
    g.fillRoundedRectangle(static_cast<float>(x), cy - 2.0f, static_cast<float>(width), 4.0f, 2.0f);
    if (slider.getProperties().getWithDefault("centreTicks", false)) {
        g.setColour(muted.withAlpha(0.6f));
        for (int i = 0; i <= 4; ++i) {
            const float tx = x + width * i / 4.0f;
            g.drawVerticalLine(juce::roundToInt(tx), cy - (i == 2 ? 9.0f : 5.0f), cy - 4.0f);
            g.drawVerticalLine(juce::roundToInt(tx), cy + 4.0f, cy + (i == 2 ? 9.0f : 5.0f));
        }
    } else if (slider.isEnabled()) {
        const auto fill = slider.findColour(juce::Slider::trackColourId);
        const float zero = slider.getMinimum() < 0 ? x + width * 0.5f : static_cast<float>(x);
        g.setColour(fill.withAlpha(0.8f));
        g.fillRect(juce::Rectangle<float>(std::min(zero, position), cy - 1.5f, std::abs(position - zero), 3.0f));
    }
    const float capW = slider.getProperties().getWithDefault("wideThumb", false) ? 9.0f : 12.0f;
    const float capH = juce::jmin(static_cast<float>(height) - 2.0f, 22.0f);
    const auto cap = juce::Rectangle<float>(capW, capH).withCentre({position, cy});
    g.setColour(thumb);
    g.fillRoundedRectangle(cap, 2.0f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.drawVerticalLine(juce::roundToInt(cap.getCentreX()), cap.getY() + 2, cap.getBottom() - 2);
}

void crate::Look::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
        juce::ComboBox& box) {
    const auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    juce::Path arrow;
    const float ax = width - 13.0f, ay = height * 0.5f;
    arrow.addTriangle(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

juce::Font crate::Look::getComboBoxFont(juce::ComboBox& box) {
    return juce::Font(juce::FontOptions(juce::jmin(14.0f, box.getHeight() * 0.58f)));
}

void crate::Look::drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header) {
    g.fillAll(panel);
    g.setColour(line);
    g.drawHorizontalLine(header.getHeight() - 1, 0, static_cast<float>(header.getWidth()));
}

void crate::Look::drawTableHeaderColumn(juce::Graphics& g, juce::TableHeaderComponent&, const juce::String& name,
        int, int width, int height, bool over, bool, int flags) {
    if (over) {
        g.fillAll(juce::Colours::white.withAlpha(0.04f));
    }
    g.setColour(line);
    g.drawVerticalLine(width - 1, 4.0f, height - 4.0f);
    g.setColour(muted);
    g.setFont(juce::FontOptions(12.5f));
    auto area = juce::Rectangle<int>(width, height).reduced(8, 0);
    if ((flags & (juce::TableHeaderComponent::sortedForwards | juce::TableHeaderComponent::sortedBackwards)) != 0) {
        juce::Path arrow;
        const float ax = area.getRight() - 6.0f, ay = height * 0.5f;
        if (flags & juce::TableHeaderComponent::sortedForwards) {
            arrow.addTriangle(ax - 4, ay + 2, ax + 4, ay + 2, ax, ay - 3);
        } else {
            arrow.addTriangle(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        }
        g.fillPath(arrow);
        area.removeFromRight(14);
    }
    g.drawText(name, area, juce::Justification::centredLeft, true);
}

namespace {
// Waveform colour from how bright (treble-heavy) a slice is: bass red, mids orange to yellow, highs green to cyan.
juce::Colour spectrumColour(float brightness, juce::Colour accent) {
    const auto& stops = crate::wave;
    const float t = juce::jlimit(0.0f, 0.999f, brightness) * (stops.size() - 1);
    const auto index = static_cast<std::size_t>(t);
    const auto colour = stops[index].interpolatedWith(stops[index + 1], t - static_cast<float>(index));
    return colour.interpolatedWith(accent, 0.18f);
}
}

void crate::paintWave(juce::Graphics& g, juce::Rectangle<int> bounds, const DeckEngine::Overview* wave,
        double playhead, const std::optional<TrackBeatGrid>& grid, bool follow, juce::Colour accent,
        const DeckEngine::Markers* markers) {
    if (bounds.isEmpty()) {
        return;
    }
    g.setColour(crate::waveBackground);
    g.fillRect(bounds);
    const auto area = bounds.reduced(0, 2);
    if (!wave || wave->seconds <= 0) {
        g.setColour(line);
        g.drawHorizontalLine(area.getCentreY(), static_cast<float>(area.getX()), static_cast<float>(area.getRight()));
        return;
    }
    const double span = follow ? std::min(16.0, wave->seconds) : wave->seconds;
    const double start = follow ? playhead - span * 0.5 : 0;
    const auto at = [&](double second) {
        return static_cast<float>(area.getX() + (second - start) * area.getWidth() / span);
    };
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(area);
    if (grid && grid->bpm > 0 && follow) {
        const int first = static_cast<int>(std::ceil(grid->beatAt(start)));
        for (int number = first; ; ++number) {
            const double tick = grid->timeAt(number);
            if (tick >= start + span) {
                break;
            }
            if (tick >= 0) {
                g.setColour(juce::Colours::white.withAlpha(number % 4 == 0 ? 0.22f : 0.08f));
                g.drawVerticalLine(juce::roundToInt(at(tick)), static_cast<float>(area.getY()),
                        static_cast<float>(area.getBottom()));
            }
        }
    }
    const auto total = static_cast<int>(wave->bins.size());
    const double secondsPerBin = wave->seconds / total;
    const auto mid = static_cast<float>(area.getCentreY());
    const float scale = static_cast<float>(area.getHeight()) * 0.48f;
    for (int px = area.getX(); px < area.getRight(); ++px) {
        const double from = start + (px - area.getX()) * span / area.getWidth();
        const double to = start + (px + 1 - area.getX()) * span / area.getWidth();
        if (to <= 0 || from >= wave->seconds) {
            continue;
        }
        const int a = juce::jlimit(0, total - 1, static_cast<int>(std::max(0.0, from) / secondsPerBin));
        const int b = juce::jlimit(a, total - 1, static_cast<int>(std::min(wave->seconds, to) / secondsPerBin));
        float peak = 0, bright = 0;
        for (int i = a; i <= b; ++i) {
            const auto& bin = wave->bins[static_cast<std::size_t>(i)];
            if (bin.peak >= peak) {
                peak = bin.peak;
                bright = bin.brightness;
            }
        }
        const float amplitude = std::min(1.0f, std::sqrt(peak)) * scale;
        if (amplitude < 0.5f) {
            continue;
        }
        const bool played = !follow && from < playhead;
        auto colour = spectrumColour(bright, accent);
        if (played) {
            colour = colour.withMultipliedSaturation(0.35f).withMultipliedBrightness(0.55f);
        }
        g.setColour(colour);
        g.drawVerticalLine(px, mid - amplitude, mid + amplitude);
        g.setColour(colour.brighter(0.5f).withAlpha(0.7f));
        g.drawVerticalLine(px, mid - amplitude * 0.35f, mid + amplitude * 0.35f);
    }
    if (markers) {
        const auto flag = [&](double seconds, const juce::String& label, juce::Colour colour) {
            const float x = at(seconds);
            if (x < area.getX() - 30 || x > area.getRight()) {
                return;
            }
            g.setColour(colour);
            g.drawVerticalLine(juce::roundToInt(x), static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            const int w = juce::jmax(11, juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), label) + 6);
            g.fillRect(juce::roundToInt(x), area.getY(), w, 13);
            g.setColour(crate::ink);
            g.drawText(label, juce::roundToInt(x), area.getY(), w, 13, juce::Justification::centred);
        };
        for (std::size_t i = 0; i < markers->hotCues.size(); ++i) {
            if (markers->hotCues[i]) {
                flag(*markers->hotCues[i], juce::String(i + 1), amber);
            }
        }
        if (markers->cue && follow) {
            flag(*markers->cue, "CUE", amber);
        }
    }
    const float x = follow ? static_cast<float>(area.getCentreX()) : at(playhead);
    g.setColour(crate::playhead);
    g.fillRect(juce::Rectangle<float>(x - 1.0f, static_cast<float>(bounds.getY()), 2.0f, static_cast<float>(bounds.getHeight())));
    if (follow) {
        juce::Path tri;
        tri.addTriangle(x - 5, static_cast<float>(bounds.getY()), x + 5, static_cast<float>(bounds.getY()), x, bounds.getY() + 6.0f);
        g.fillPath(tri);
    }
}

CrateMixer::CrateMixer(FourDeckMixer& source) : mixer(source) {
    using Control = FourDeckMixer::ChannelControl;
    const auto knob = [](juce::Slider& slider, const juce::String& title) {
        slider.setTitle(title);
        slider.setTooltip(title + ". Double-click to reset.");
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange(0, 1, 0.001);
        slider.setValue(0.5, juce::dontSendNotification);
        slider.setDoubleClickReturnValue(true, 0.5);
        slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    };
    for (std::size_t i = 0; i < faders.size(); ++i) {
        const auto title = "Deck " + juce::String(i + 1);
        faders[i].setTitle(title + " channel fader");
        faders[i].setSliderStyle(juce::Slider::LinearVertical);
        faders[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        faders[i].setRange(0, 1, 0.001);
        faders[i].onValueChange = [this, i] {
            mixer.setChannelControl(i, Control::fader, static_cast<float>(faders[i].getValue()));
        };
        knob(filters[i], title + " filter");
        filters[i].setColour(juce::Slider::rotarySliderFillColourId, crate::green);
        filters[i].onValueChange = [this, i] {
            mixer.setChannelControl(i, Control::filter, static_cast<float>(filters[i].getValue()));
        };
        constexpr std::array bands{Control::high, Control::mid, Control::low};
        constexpr std::array names{"high", "mid", "low"};
        for (std::size_t b = 0; b < 3; ++b) {
            knob(eq[i][b], title + " " + names[b] + " EQ");
            eq[i][b].setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::transparentBlack);
            eq[i][b].onValueChange = [this, i, b, control = bands[b]] {
                mixer.setChannelControl(i, control, static_cast<float>(eq[i][b].getValue()));
            };
            addAndMakeVisible(eq[i][b]);
        }
        cues[i].setTitle(title + " headphone cue");
        cues[i].setTooltip(title + " headphone cue (PFL)");
        cues[i].setColour(juce::TextButton::buttonOnColourId, crate::amber);
        cues[i].onClick = [this, i] { mixer.toggleCue(i); refresh(); };
        stars[i].setButtonText(juce::String::fromUTF8("\xE2\x98\x85"));
        stars[i].setTitle("Save GOOD MIX into " + title);
        stars[i].setTooltip("Save GOOD MIX: press the star on the deck you just mixed INTO. "
                            "Same as SHIFT + ASSIST on the T7.");
        stars[i].setColour(juce::TextButton::buttonColourId, crate::starFill);
        stars[i].setColour(juce::TextButton::textColourOffId, crate::starText);
        stars[i].onClick = [this, i] {
            if (onSaveMix) {
                onSaveMix(i);
            }
        };
        addAndMakeVisible(faders[i]);
        addAndMakeVisible(filters[i]);
        addAndMakeVisible(cues[i]);
        addChildComponent(stars[i]);
    }
    for (auto* slider : {&crossfade, &blend, &booth, &master}) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(*slider);
    }
    crossfade.setTitle("Crossfader");
    crossfade.setTooltip("Crossfader. Double-click to centre.");
    crossfade.setRange(-1, 1, 0.001);
    crossfade.setDoubleClickReturnValue(true, 0);
    crossfade.getProperties().set("centreTicks", true);
    crossfade.setColour(juce::Slider::thumbColourId, crate::amber);
    crossfade.onValueChange = [this] { mixer.setCrossfader(static_cast<float>(crossfade.getValue())); };
    blend.setTitle("Headphone cue to master blend");
    blend.setTooltip("Headphones: cue on the left, master on the right");
    blend.setRange(0, 1, 0.001);
    blend.getProperties().set("wideThumb", true);
    blend.onValueChange = [this] { mixer.setHeadphoneMix(static_cast<float>(blend.getValue())); };
    booth.setTitle("Booth level");
    booth.setTooltip("Booth level for audio outputs 5 and 6.");
    booth.setRange(0, 1);
    booth.setValue(1, juce::dontSendNotification);
    booth.getProperties().set("wideThumb", true);
    booth.onValueChange = [this] { mixer.setBoothLevel(static_cast<float>(booth.getValue())); };
    master.setTitle("Master volume");
    master.setTooltip("Master volume for the speakers (outputs 1-2). The Ulanzi Dial can turn this.");
    master.setRange(0, 1);
    master.setValue(1, juce::dontSendNotification);
    master.getProperties().set("wideThumb", true);
    master.onValueChange = [this] { mixer.setMasterVolume(static_cast<float>(master.getValue())); };
    split.setTooltip("Split headphones: cue in the left ear, master in the right");
    split.getProperties().set("fontSize", 10.0f);
    split.onClick = [this] { mixer.toggleSplitCue(); refresh(); };
    addAndMakeVisible(split);
    setDeckCount(2);
    refresh();
}

void CrateMixer::setDeckCount(int visible) {
    count = visible;
    for (std::size_t i = 0; i < faders.size(); ++i) {
        const bool shown = i < static_cast<std::size_t>(count);
        faders[i].setVisible(shown);
        filters[i].setVisible(shown);
        cues[i].setVisible(shown);
        stars[i].setVisible(shown && count == 2);
        for (auto& knob : eq[i]) {
            knob.setVisible(shown);
        }
    }
    resized();
    repaint();
}

void CrateMixer::refresh() {
    const auto state = mixer.mixControls();
    for (std::size_t i = 0; i < state.strips.size(); ++i) {
        const auto& strip = state.strips[i];
        if (!faders[i].isMouseButtonDown()) {
            faders[i].setValue(strip.fader, juce::dontSendNotification);
        }
        if (!filters[i].isMouseButtonDown()) {
            filters[i].setValue(strip.filter, juce::dontSendNotification);
        }
        for (std::size_t b = 0; b < 3; ++b) {
            if (!eq[i][b].isMouseButtonDown()) {
                eq[i][b].setValue(strip.eq[2 - b], juce::dontSendNotification);
            }
        }
        cues[i].setToggleState(strip.cue, juce::dontSendNotification);
        peaks[i] = strip.peak;
    }
    if (!crossfade.isMouseButtonDown()) {
        crossfade.setValue(state.crossfade, juce::dontSendNotification);
    }
    if (!blend.isMouseButtonDown()) {
        blend.setValue(state.headphoneBlend, juce::dontSendNotification);
    }
    split.setToggleState(state.split, juce::dontSendNotification);
    if (!master.isMouseButtonDown()) {
        master.setValue(mixer.masterVolume(), juce::dontSendNotification);
    }
    repaint();
}

void CrateMixer::resized() {
    auto area = getLocalBounds().reduced(8, 8);
    auto masterRow = area.removeFromBottom(22);
    masterLabel = masterRow.removeFromLeft(50);
    master.setBounds(masterRow.reduced(2, 3));
    area.removeFromBottom(2);
    auto monitor = area.removeFromBottom(22);
    const int third = monitor.getWidth() / 3;
    auto blendArea = monitor.removeFromLeft(third);
    blendLabel = blendArea.removeFromLeft(30);
    blend.setBounds(blendArea.reduced(2, 3));
    split.setBounds(monitor.removeFromLeft(third).reduced(10, 1));
    boothLabel = monitor.removeFromLeft(38);
    booth.setBounds(monitor.reduced(2, 3));
    area.removeFromBottom(4);
    auto fade = area.removeFromBottom(30);
    if (count == 2) {
        stars[0].setBounds(fade.removeFromLeft(30).withSizeKeepingCentre(24, 22));
        stars[1].setBounds(fade.removeFromRight(30).withSizeKeepingCentre(24, 22));
    }
    crossfade.setBounds(fade.reduced(count == 2 ? 14 : 30, 2));
    area.removeFromBottom(4);
    const int width = area.getWidth() / count;
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        auto strip = area.removeFromLeft(width);
        const bool knobsLeft = static_cast<int>(i) < count / 2;
        const int knobWidth = juce::jmin(52, strip.getWidth() * 11 / 20);
        auto knobs = knobsLeft ? strip.removeFromLeft(knobWidth) : strip.removeFromRight(knobWidth);
        auto faderColumn = strip;
        const int size = juce::jlimit(22, 36, (knobs.getHeight() - 20) / 4);
        const int labelWidth = knobWidth - size > 12 ? 12 : 0;
        for (std::size_t b = 0; b < 3; ++b) {
            auto row = knobs.removeFromTop(size + 4);
            auto label = knobsLeft ? row.removeFromLeft(labelWidth) : row.removeFromRight(labelWidth);
            eqLabels[i][b] = label;
            eq[i][b].setBounds(row.withSizeKeepingCentre(size, size));
        }
        filterLabels[i] = knobs.removeFromTop(16);
        filters[i].setBounds(knobs.withSizeKeepingCentre(size + 6, size + 6).withY(filterLabels[i].getBottom()));
        cues[i].setBounds(faderColumn.removeFromTop(28).withSizeKeepingCentre(juce::jmin(32, faderColumn.getWidth()), 26));
        faderColumn.removeFromTop(6);
        auto column = faderColumn.withSizeKeepingCentre(juce::jmin(faderColumn.getWidth(), 46), faderColumn.getHeight());
        meters[i] = knobsLeft ? column.removeFromLeft(9) : column.removeFromRight(9);
        meters[i] = meters[i].reduced(1, 6);
        faders[i].setBounds(column);
    }
}

void CrateMixer::paint(juce::Graphics& g) {
    g.fillAll(crate::panel);
    g.setColour(crate::line);
    g.drawRect(getLocalBounds());
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    constexpr std::array bandNames{"H", "M", "L"};
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        g.setColour(crate::muted);
        for (std::size_t b = 0; b < 3; ++b) {
            g.drawText(bandNames[b], eqLabels[i][b], juce::Justification::centred);
        }
        g.drawText(juce::String::fromUTF8("\xE2\x96\xBE Filter"), filterLabels[i], juce::Justification::centred);
        auto meter = meters[i];
        const auto db = juce::Decibels::gainToDecibels(peaks[i], -54.0f);
        const auto peak = juce::jlimit(0.0f, 1.0f, (db + 54.0f) / 54.0f);
        const int segments = std::max(1, meter.getHeight() / 6);
        for (int s = 0; s < segments; ++s) {
            const int y = meter.getBottom() - 5 - s * 6;
            const float level = static_cast<float>(s + 1) / static_cast<float>(segments);
            const auto lit = level > 0.9f ? crate::clip : level > 0.72f ? crate::amber : crate::green;
            g.setColour(level <= peak ? lit : lit.withAlpha(0.13f));
            g.fillRect(meter.getX(), y, meter.getWidth(), 4);
        }
    }
    g.setColour(crate::muted);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("CUE", blendLabel, juce::Justification::centredLeft);
    g.drawText("BOOTH", boothLabel, juce::Justification::centredLeft);
    g.drawText("MASTER", masterLabel, juce::Justification::centredLeft);
}

void CrateMixer::paintOverChildren(juce::Graphics& g) {
    // Headphone icon on each PFL button.
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        const auto b = cues[i].getBounds().toFloat().withSizeKeepingCentre(15, 14);
        const bool on = cues[i].getToggleState();
        g.setColour(on ? crate::ink : crate::text);
        juce::Path band;
        band.addCentredArc(b.getCentreX(), b.getCentreY() + 1, 6.5f, 6.5f, 0,
                -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.strokePath(band, juce::PathStrokeType(1.6f));
        g.fillRoundedRectangle(b.getX(), b.getCentreY() + 1, 3.5f, 6, 1);
        g.fillRoundedRectangle(b.getRight() - 3.5f, b.getCentreY() + 1, 3.5f, 6, 1);
    }
}

CrateEcho::CrateEcho(FourDeckMixer& source) : mixer(source) {
    for (std::size_t i = 0; i < armed.size(); ++i) {
        armed[i].setButtonText("ARM");
        armed[i].setTitle("Arm effect on Deck " + juce::String(i + 1));
        armed[i].setTooltip("Arm the selected effect for this deck or its T7 paddle.");
        armed[i].setColour(juce::TextButton::buttonOnColourId, crate::green);
        armed[i].onClick = [this, i] { mixer.toggleEchoArmed(i); refresh(); };
        engaged[i].setButtonText("ON");
        engaged[i].setTitle("Engage effect on Deck " + juce::String(i + 1));
        engaged[i].setColour(juce::TextButton::buttonOnColourId, crate::amber);
        engaged[i].onClick = [this, i] { mixer.toggleEchoEnabled(i); refresh(); };
        addAndMakeVisible(armed[i]);
        addAndMakeVisible(engaged[i]);
        effects[i].setTitle("Effect for Deck " + juce::String(i + 1));
        effects[i].addItemList({"Echo", "Echo Out", "Riser", "Roll", "Brake"}, 1);
        effects[i].onChange = [this, i] {
            const auto selected = effects[i].getSelectedItemIndex();
            if (selected >= 0) {
                mixer.selectEffect(i, static_cast<FourDeckMixer::Effect>(selected));
            }
        };
        addAndMakeVisible(effects[i]);
    }
    division.setTitle("Echo note division");
    division.addItemList({"1/8", "1/4", "1/2"}, 1);
    division.onChange = [this] { mixer.setEchoDivision(division.getSelectedItemIndex()); };
    depth.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    depth.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    depth.setColour(juce::Slider::rotarySliderFillColourId, crate::green);
    depth.setTextBoxStyle(juce::Slider::TextBoxRight, true, 40, 20);
    depth.setRange(0, 100, 1);
    depth.setTextValueSuffix("%");
    depth.setTitle("Effect depth");
    depth.onValueChange = [this] { mixer.setEchoDepth(static_cast<float>(depth.getValue() / 100)); };
    addAndMakeVisible(division);
    addAndMakeVisible(depth);
    setDeckCount(2);
    refresh();
}

void CrateEcho::setDeckCount(int visible) {
    count = visible;
    for (std::size_t i = 0; i < armed.size(); ++i) {
        const bool shown = i < static_cast<std::size_t>(count);
        armed[i].setVisible(shown);
        engaged[i].setVisible(shown);
        effects[i].setVisible(shown);
    }
    resized();
}

void CrateEcho::resized() {
    auto row = getLocalBounds().reduced(8, 5);
    const int centreWidth = 250;
    const int half = (row.getWidth() - centreWidth) / 2;
    auto left = row.removeFromLeft(half);
    centre = row.removeFromLeft(centreWidth);
    auto right = row;
    const auto place = [&](std::size_t i, juce::Rectangle<int> group) {
        groups[i] = group;
        group.removeFromLeft(34);
        effects[i].setBounds(group.removeFromLeft(juce::jmin(130, group.getWidth() / 2)).reduced(1, 0));
        group.removeFromLeft(6);
        armed[i].setBounds(group.removeFromLeft(46).reduced(1, 0));
        engaged[i].setBounds(group.removeFromLeft(40).reduced(1, 0));
    };
    if (count == 2) {
        place(0, left);
        place(1, right);
    } else {
        place(0, left.removeFromLeft(left.getWidth() / 2).withTrimmedRight(6));
        place(1, left);
        place(2, right.removeFromLeft(right.getWidth() / 2).withTrimmedRight(6));
        place(3, right);
    }
    auto c = centre.reduced(10, 0);
    c.removeFromLeft(40);
    division.setBounds(c.removeFromLeft(62));
    c.removeFromLeft(54);
    depth.setBounds(c.removeFromLeft(78));
}

void CrateEcho::refresh() {
    const auto echo = mixer.echoControls();
    for (std::size_t i = 0; i < armed.size(); ++i) {
        effects[i].setSelectedItemIndex(static_cast<int>(echo.strips[i].effect), juce::dontSendNotification);
        armed[i].setToggleState(echo.strips[i].armed, juce::dontSendNotification);
        engaged[i].setEnabled(echo.strips[i].armed);
        engaged[i].setToggleState(echo.strips[i].active, juce::dontSendNotification);
    }
    division.setSelectedItemIndex(echo.division, juce::dontSendNotification);
    if (!depth.isMouseButtonDown()) {
        depth.setValue(static_cast<double>(echo.depth) * 100, juce::dontSendNotification);
    }
}

void CrateEcho::paint(juce::Graphics& g) {
    g.fillAll(crate::panel);
    g.setColour(crate::line);
    g.drawRect(getLocalBounds());
    g.drawVerticalLine(centre.getX(), 0, static_cast<float>(getHeight()));
    g.drawVerticalLine(centre.getRight(), 0, static_cast<float>(getHeight()));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        g.setColour(crate::muted);
        g.drawText("FX" + juce::String(count == 2 ? "" : juce::String(i + 1)) + ":", groups[i].withWidth(34),
                juce::Justification::centredLeft);
    }
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.setColour(crate::muted);
    auto c = centre.reduced(10, 0);
    g.drawText("BEAT", c.removeFromLeft(40), juce::Justification::centredLeft);
    c.removeFromLeft(62);
    g.drawText("DEPTH", c.removeFromLeft(54).withTrimmedLeft(8), juce::Justification::centredLeft);
}

CrateLibrary::CrateLibrary(const juce::File& metadataDirectory)
        : metadata(metadataDirectory),
          crateDatabase(metadataDirectory.getSiblingFile("crate-library.sqlite")),
          libraryStore(crateDatabase),
          history(metadataDirectory.getSiblingFile("GOOD MIXX").getChildFile("proven-mixes.json")) {
    const bool historyReady = history.load();
    if (!historyReady) {
        saveStatus.setText("Could not read saved mixes", juce::dontSendNotification);
    }
    saveMix.setTooltip("Choose two loaded decks in transition order.");
    saveMix.onClick = [this] { chooseMixToSave(); };
    addChildComponent(saveMix);
    relink.setTitle("Relink missing audio");
    relink.setTooltip("Select audio files named Artist - Title for saved mixes.");
    relink.onClick = [this] { chooseAudioToRelink(); };
    addChildComponent(relink);
    mixMenu.setButtonText(juce::String::fromUTF8("\xE2\x98\xB0"));
    mixMenu.setTitle("GOOD MIXX menu");
    mixMenu.setTooltip("Save a mix or relink missing audio");
    mixMenu.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    mixMenu.getProperties().set("fontSize", 18.0f);
    mixMenu.onClick = [this] { showMixMenu(); };
    addAndMakeVisible(mixMenu);
    addMenu.setTooltip("Add audio files or import playlists and libraries");
    addMenu.getProperties().set("fontSize", 11.0f);
    addMenu.onClick = [this] { showAddMenu(); };
    addAndMakeVisible(addMenu);
    sidebarList.setRowHeight(21);
    sidebarList.setColour(juce::ListBox::backgroundColourId, crate::panel);
    sidebarList.setTitle("CRATE folders");
    addAndMakeVisible(sidebarList);
    for (auto* label : {&mixStatus, &saveStatus}) {
        label->setFont(juce::FontOptions(11.5f));
        label->setColour(juce::Label::textColourId, crate::muted);
        addAndMakeVisible(*label);
    }
    mixStatus.setFont(juce::FontOptions(16.0f));
    mixStatus.setColour(juce::Label::textColourId, crate::text);
    mixList.setTitle("GOOD MIXX next tracks");
    mixList.setRowHeight(72);
    mixList.setColour(juce::ListBox::backgroundColourId, crate::panel);
    addAndMakeVisible(mixList);
    search.setTextToShowWhenEmpty("Search Library...", crate::muted);
    search.setFont(juce::FontOptions(14.0f));
    search.setIndents(8, 6);
    search.setTitle("Search library");
    search.onTextChange = [this] { filter(); };
    add.onClick = [this] { browse(); };
    addAndMakeVisible(search);
    addChildComponent(add);
    importPlaylist.onClick = [this] { browsePlaylist(); };
    addChildComponent(importPlaylist);
    importOneLibrary.onClick = [this] { browseOneLibrary(); };
    addChildComponent(importOneLibrary);
    importMixxx.setTooltip("Copy your existing Spin Deck library into CRATE.");
    importMixxx.onClick = [this] { browseMixxx(); };
    addChildComponent(importMixxx);
    playlistPicker.setTitle("CRATE playlist");
    playlistPicker.onChange = [this] { filter(); sidebarList.repaint(); };
    addChildComponent(playlistPicker);
    playlistStatus.setFont(juce::FontOptions(11.5f));
    playlistStatus.setColour(juce::Label::textColourId, crate::muted);
    addAndMakeVisible(playlistStatus);
    playlists = libraryStore.loadPlaylists();
    refreshPlaylistPicker();
    table.setModel(this);
    table.setRowHeight(20);
    table.setHeaderHeight(24);
    table.setColour(juce::ListBox::backgroundColourId, crate::inset);
    const auto fixed = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable;
    table.getHeader().addColumn("#", 5, 42, 30, 60, fixed);
    table.getHeader().addColumn("Title", 1, 330, 120, -1, fixed);
    table.getHeader().addColumn("Artist", 6, 230, 90, -1, fixed);
    table.getHeader().addColumn("Album", 7, 200, 80, -1, fixed);
    table.getHeader().addColumn("Genre", 8, 110, 60, -1, fixed);
    table.getHeader().addColumn("Key", 4, 64, 50, 120, fixed);
    table.getHeader().addColumn("BPM", 2, 64, 50, 100, fixed);
    table.getHeader().addColumn("Year", 9, 56, 44, 90, fixed);
    table.getHeader().addColumn("Duration", 3, 74, 60, 120, fixed);
    table.setTitle("CRATE tracks");
    addAndMakeVisible(table);
    for (std::size_t i = 0; i < route.size(); ++i) {
        route[i].setButtonText(juce::String(i + 1));
        route[i].setTitle("Load selected track on Deck " + juce::String(i + 1));
        route[i].setTooltip("Load the selected track on Deck " + juce::String(i + 1));
        route[i].onClick = [this, i] {
            loadSelected(static_cast<int>(i));
        };
        addAndMakeVisible(route[i]);
    }
    if (auto saved = libraryStore.load()) {
        tracks = std::move(*saved);
        for (auto& track : tracks) {
            track.available = track.file.existsAsFile();
            if (!track.available) {
                continue;
            }
            const auto modifiedMs = track.file.getLastModificationTime().toMilliseconds();
            if (track.modifiedMs != modifiedMs) {
                const auto file = track.file;
                track = {};
                track.file = file;
                track.available = true;
                track.modifiedMs = modifiedMs;
                persistTrack(track);
            }
        }
        applySavedTags();
        filter();
        startTagScan();
    } else {
        saveStatus.setText("Could not open CRATE library", juce::dontSendNotification);
    }
    setDeckCount(2);
    selectedRowsChanged(-1);

    const auto productionDirectory = metadataDirectory.getParentDirectory().getSiblingFile("Spin Deck");
    if (historyReady && !history.hasProductionImport() &&
            productionDirectory.getChildFile("goodmixx/mixes.json").existsAsFile()) {
        analysisWorker.addJob(std::function<void()>{
            [safe = SafePointer<CrateLibrary>(this), store = history, productionDirectory]() mutable {
                const bool imported = store.importProduction(productionDirectory);
                juce::MessageManager::callAsync([safe, imported] {
                    if (safe) {
                        if (!(imported && safe->history.load())) {
                            safe->saveStatus.setText("Could not import production mixes", juce::dontSendNotification);
                        }
                        safe->updateMixes();
                    }
                });
            }});
    }
}

CrateLibrary::~CrateLibrary() {
    tagScanCancel->store(true);
    tagWorker.removeAllJobs(true, 4000);
    analysisWorker.removeAllJobs(false, -1);
    table.setModel(nullptr);
    mixList.setModel(nullptr);
}

void CrateLibrary::setDeckCount(int count) {
    visibleDecks = count;
    for (std::size_t i = 0; i < route.size(); ++i) {
        route[i].setVisible(i < static_cast<std::size_t>(count));
    }
    updateMixes();
}

void CrateLibrary::addTrack(
        const juce::File& file, std::optional<double> bpm, double seconds, std::optional<MusicalKey> key) {
    if (!file.existsAsFile()) {
        return;
    }
    const auto modifiedMs = file.getLastModificationTime().toMilliseconds();
    auto track = std::find_if(tracks.begin(), tracks.end(), [&](const auto& item) { return item.file == file; });
    const bool isNew = track == tracks.end();
    if (isNew) {
        tracks.emplace_back();
        track = std::prev(tracks.end());
    }
    const bool changed = isNew || track->modifiedMs != modifiedMs;
    if (changed) {
        *track = {};
        track->file = file;
        track->modifiedMs = modifiedMs;
    }
    track->available = true;
    if (seconds > 0) {
        track->seconds = seconds;
        if (bpm) {
            track->bpm = bpm;
        }
    }
    if (key) {
        track->key = key;
        track->analyzed = track->bpm.has_value();
    }
    if (changed || seconds > 0 || key) {
        persistTrack(*track);
    }
    if (!track->analyzed) {
        analyzeTrack(*track);
    }
    filter();
    updateMixes();
    if (!track->tagsRead) {
        startTagScan();
    }
}

void CrateLibrary::applySavedTags() {
    const auto cachedTags = libraryStore.loadTags();
    for (auto& track : tracks) {
        const auto found = cachedTags.find(track.file.getFullPathName());
        if (found != cachedTags.end() && found->second.first == track.modifiedMs) {
            track.tags = found->second.second;
            track.tagsRead = true;
        }
    }
}

void CrateLibrary::startTagScan() {
    if (tagScanRunning) {
        return;
    }
    struct Pending {
        std::size_t index;
        juce::File file;
        juce::int64 modified;
    };
    std::vector<Pending> pending;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (!tracks[i].tagsRead && tracks[i].available) {
            pending.push_back({i, tracks[i].file, tracks[i].modifiedMs});
        }
    }
    if (pending.empty()) {
        return;
    }
    tagScanRunning = true;
    // Runs in the background. Results come back in small batches; each one is matched to its library row
    // by position, so filing a batch never searches the whole library.
    tagWorker.addJob(std::function<void()>{[safe = SafePointer<CrateLibrary>(this), pending = std::move(pending),
                                                   cancel = tagScanCancel] {
        struct Result {
            std::size_t index;
            juce::File file;
            juce::int64 modified;
            TrackTags tags;
        };
        std::vector<Result> batch;
        const auto flush = [&] {
            juce::MessageManager::callAsync([safe, rows = std::move(batch)] {
                if (!safe) {
                    return;
                }
                std::vector<std::tuple<juce::String, juce::int64, TrackTags>> saved;
                saved.reserve(rows.size());
                for (const auto& row : rows) {
                    if (row.index < safe->tracks.size()) {
                        auto& track = safe->tracks[row.index];
                        if (track.modifiedMs == row.modified && track.file == row.file) {
                            track.tags = row.tags;
                            track.tagsRead = true;
                        }
                    }
                    saved.emplace_back(row.file.getFullPathName(), row.modified, row.tags);
                }
                safe->libraryStore.saveTags(saved);
                safe->table.repaint();
            });
            batch.clear();
        };
        for (const auto& item : pending) {
            if (cancel->load()) {
                return;
            }
            // A file without readable tags is still recorded, so it isn't read again.
            batch.push_back({item.index, item.file, item.modified, readTrackTags(item.file).value_or(TrackTags{})});
            if (batch.size() >= 100) {
                flush();
                juce::Thread::sleep(5); // Leave the disk and screen room for deck loading during a long first scan.
            }
        }
        if (!batch.empty()) {
            flush();
        }
        juce::MessageManager::callAsync([safe] {
            if (safe) {
                safe->tagScanRunning = false;
                safe->startTagScan(); // Pick up anything added while this pass ran.
            }
        });
    }});
}

bool CrateLibrary::hasStems(const juce::File& file) {
    const auto path = file.getFullPathName();
    if (const auto found = stemMarks.find(path); found != stemMarks.end()) {
        return found->second;
    }
    auto name = file.getFileNameWithoutExtension();
    const bool stems = isStemFile(file) || file.getSiblingFile(name + ".stem.mp4").existsAsFile() ||
            file.getSiblingFile(name + ".stem.m4a").existsAsFile();
    stemMarks[path] = stems;
    return stems;
}

void CrateLibrary::refreshStemMarkers() {
    if (!stemMarks.empty()) {
        stemMarks.clear();
        table.repaint();
    }
}

void CrateLibrary::persistTrack(const CrateTrack& track) {
    if (!libraryStore.save(track)) {
        saveStatus.setText("Could not save CRATE library", juce::dontSendNotification);
    }
}

void CrateLibrary::moveSelection(int delta) {
    if (getNumRows() == 0) {
        return;
    }
    const int selected = table.getSelectedRow();
    const int next = selected < 0 ? 0 : std::clamp(selected + delta, 0, getNumRows() - 1);
    table.selectRow(next);
    table.scrollToEnsureRowIsOnscreen(next);
}

void CrateLibrary::loadSelected(int deck) {
    const int row = table.getSelectedRow();
    if (onLoad && juce::isPositiveAndBelow(row, getNumRows())) {
        const auto& track = tracks[results[static_cast<std::size_t>(row)]];
        if (track.available) {
            onLoad(deck, track.file);
        }
    }
}

void CrateLibrary::setTrackContext(const FourDeckMixer::TrackContext& value) {
    if (context != value) {
        context = value;
        updateMixes();
    }
}

int CrateLibrary::MixList::getNumRows() {
    return static_cast<int>(rows.size());
}

juce::String CrateLibrary::MixList::getNameForRow(int row) {
    if (!juce::isPositiveAndBelow(row, getNumRows())) {
        return {};
    }
    const auto& mix = rows[static_cast<std::size_t>(row)];
    return (mix.outgoing.isEmpty() ? juce::String{} : mix.outgoing + " into ") +
            mix.title + ", " + mix.details;
}

void CrateLibrary::MixList::paintListBoxItem(
        int row, juce::Graphics& g, int width, int height, bool selected) {
    if (!juce::isPositiveAndBelow(row, getNumRows())) {
        return;
    }
    const auto& mix = rows[static_cast<std::size_t>(row)];
    const auto card = juce::Rectangle<int>(width, height).withTrimmedBottom(6).toFloat().reduced(1.0f);
    g.setColour(selected ? crate::selection : crate::card);
    g.fillRoundedRectangle(card, 4.0f);
    g.setColour(row == 0 ? crate::green : crate::line);
    g.drawRoundedRectangle(card, 4.0f, row == 0 ? 2.0f : 1.0f);
    auto bounds = card.toNearestInt().reduced(10, 6);
    auto icon = bounds.removeFromLeft(22);
    juce::Path play;
    const float cy = static_cast<float>(icon.getY() + 12);
    play.addTriangle(static_cast<float>(icon.getX()), cy - 7, static_cast<float>(icon.getX()), cy + 7,
            icon.getX() + 12.0f, cy);
    g.setColour(crate::text);
    g.fillPath(play);
    bounds.removeFromLeft(6);
    const auto [title, artistName] = crate::titleAndArtist(mix.file != juce::File{} ? mix.file
                                                                                    : juce::File("/" + mix.title));
    const auto shownTitle = mix.title.contains(" - ") ? title : mix.title;
    g.setFont(juce::FontOptions(15.5f, juce::Font::bold));
    g.drawText(shownTitle, bounds.removeFromTop(19), juce::Justification::centredLeft, true);
    g.setColour(crate::muted);
    g.setFont(juce::FontOptions(13.0f));
    const auto subtitle = mix.outgoing.isNotEmpty() ? juce::String::fromUTF8("after ") + mix.outgoing : artistName;
    g.drawText(subtitle, bounds.removeFromTop(17), juce::Justification::centredLeft, true);
    if (mix.deck >= 0) {
        bounds.removeFromRight(66);
    }
    g.setColour(mix.proven ? crate::amber : crate::text.withAlpha(0.85f));
    g.drawText(mix.details, bounds, juce::Justification::centredLeft, true);
    if (mix.proven) {
        // A green "proven" tag in the top-right corner.
        auto tag = card.toNearestInt().reduced(8, 7).removeFromTop(18);
        tag = tag.removeFromRight(62);
        g.setColour(crate::green);
        g.fillRoundedRectangle(tag.toFloat(), 3.0f);
        g.setColour(crate::ink);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("Proven", tag, juce::Justification::centred);
    }
}

juce::Component* CrateLibrary::MixList::refreshComponentForRow(
        int row, bool, juce::Component* existing) {
    std::unique_ptr<MixLoadControl> control(static_cast<MixLoadControl*>(existing));
    if (!juce::isPositiveAndBelow(row, getNumRows())) {
        return nullptr;
    }
    const auto& mix = rows[static_cast<std::size_t>(row)];
    if (mix.deck < 0) {
        return nullptr;
    }
    if (!control) {
        control = std::make_unique<MixLoadControl>();
    }
    if (mix.outgoingDeck >= 0) {
        control->button.setButtonText("Recall");
        control->button.setTitle("Recall mix into " + mix.title);
        control->button.setTooltip("Prepare the saved loop, cue and sync settings on paused decks.");
        control->button.onClick = [this, mix] {
            if (owner.onRecallMix) {
                owner.onRecallMix(mix.outgoingDeck, mix.outgoingFile, mix.deck, mix.file, mix.points);
            }
        };
        return control.release();
    }
    control->button.setTooltip({});
    control->button.setButtonText("Load " + juce::String(mix.deck + 1));
    control->button.setTitle("Load " + displayTitle(mix.file) + " on Deck " + juce::String(mix.deck + 1));
    control->button.onClick = [this, file = mix.file, deck = mix.deck] {
        if (owner.onLoadSuggested) {
            owner.onLoadSuggested(deck, file);
        }
    };
    return control.release();
}

void CrateLibrary::chooseMixToSave() {
    juce::PopupMenu menu;
    const auto label = [](int index, const juce::File& file) {
        return "D" + juce::String(index + 1) + " " + displayTitle(file);
    };
    for (int i = 0; i < visibleDecks; ++i) {
        const auto& outgoing = context.decks[static_cast<std::size_t>(i)].file;
        if (outgoing == juce::File{}) {
            continue;
        }
        for (int j = 0; j < visibleDecks; ++j) {
            const auto& incoming = context.decks[static_cast<std::size_t>(j)].file;
            if (incoming == juce::File{} || sameSessionTrack(outgoing, incoming)) {
                continue;
            }
            menu.addItem(label(i, outgoing) + " → " + label(j, incoming),
                    [safe = SafePointer<CrateLibrary>(this), outgoing, incoming,
                            from = static_cast<std::size_t>(i), to = static_cast<std::size_t>(j)] {
                if (!safe) {
                    return;
                }
                const auto state = safe->readTrackContext ? safe->readTrackContext() : safe->context;
                const auto& fromDeck = state.decks[from];
                const auto& toDeck = state.decks[to];
                if (fromDeck.file != outgoing || toDeck.file != incoming) {
                    return;
                }
                using Leader = GoodMixxHistory::TempoSetup::Leader;
                GoodMixxHistory::TempoSetup tempo;
                if (state.leader == from) {
                    tempo.leader = Leader::outgoing;
                } else if (state.leader == to) {
                    tempo.leader = Leader::incoming;
                }
                tempo.outgoing = {fromDeck.sync, fromDeck.keyLock};
                tempo.incoming = {toDeck.sync, toDeck.keyLock};
                safe->chooseMixPoints(outgoing, incoming, tempo);
            });
        }
    }
    if (menu.getNumItems() > 0) {
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(saveMix));
    }
}

bool CrateLibrary::quickSaveMix(std::size_t from, std::size_t to) {
    const auto state = readTrackContext ? readTrackContext() : context;
    if (from >= state.decks.size() || to >= state.decks.size() || from == to) {
        return false;
    }
    const auto& fromDeck = state.decks[from];
    const auto& toDeck = state.decks[to];
    if (fromDeck.file == juce::File{} || toDeck.file == juce::File{} ||
            sameSessionTrack(fromDeck.file, toDeck.file)) {
        saveStatus.setColour(juce::Label::textColourId, crate::amber);
        saveStatus.setText("Load a song on both decks to save a mix", juce::dontSendNotification);
        resized();
        return false;
    }
    using Leader = GoodMixxHistory::TempoSetup::Leader;
    GoodMixxHistory::TempoSetup tempo;
    if (state.leader == from) {
        tempo.leader = Leader::outgoing;
    } else if (state.leader == to) {
        tempo.leader = Leader::incoming;
    }
    tempo.outgoing = {fromDeck.sync, fromDeck.keyLock};
    tempo.incoming = {toDeck.sync, toDeck.keyLock};
    GoodMixxHistory::Points points;
    // Keep any loop or cue picked earlier for this same pair.
    for (const auto& mix : history.mixes()) {
        if (matchesSavedSong(mix.outgoing, fromDeck.file) && matchesSavedSong(mix.incoming, toDeck.file) &&
                mix.points) {
            points = *mix.points;
        }
    }
    points.tempo = tempo;
    const bool saved = history.save(fromDeck.file, toDeck.file, points);
    saveStatus.setColour(juce::Label::textColourId, saved ? crate::green : crate::amber);
    saveStatus.setText(saved ? juce::String::fromUTF8("GOOD MIX saved \xE2\x9C\x93  ") + displayTitle(fromDeck.file) +
                    juce::String::fromUTF8(" \xE2\x86\x92 ") + displayTitle(toDeck.file)
                             : juce::String("Could not save mix"),
            juce::dontSendNotification);
    updateMixes();
    resized();
    return saved;
}

void CrateLibrary::chooseMixPoints(const juce::File& outgoing, const juce::File& incoming,
        GoodMixxHistory::TempoSetup tempo) {
    const auto from = metadata.load(outgoing);
    const auto to = metadata.load(incoming);
    std::vector<TrackLoop> loops;
    std::vector<double> cues;
    juce::StringArray loopNames{"No loop"}, cueNames{"No cue"};
    const auto addLoop = [&](const juce::String& label, TrackLoop range) {
        if (std::none_of(loops.begin(), loops.end(),
                    [&](const auto& existing) { return existing.sameRange(range); })) {
            loops.push_back(range);
            loopNames.add(label + "   " + juce::String(range.start, 3) + " – " +
                    juce::String(range.end, 3) + " s");
        }
    };
    for (std::size_t i = 0; i < from.cues.size(); ++i) {
        if (from.cues[i] && from.hotLoopEnds[i]) {
            addLoop("Hot Loop " + juce::String(i + 1), {*from.cues[i], *from.hotLoopEnds[i]});
        }
    }
    for (std::size_t i = 0; i < from.savedLoops.size(); ++i) {
        addLoop("Saved Loop " + juce::String(i + 1), from.savedLoops[i]);
    }
    for (std::size_t i = 0; i < to.cues.size(); ++i) {
        if (to.cues[i] && !to.hotLoopEnds[i]) {
            cues.push_back(*to.cues[i]);
            cueNames.add("Hot Cue " + juce::String(i + 1) + "   " + juce::String(*to.cues[i], 3) + " s");
        }
    }
    const auto& mixes = history.mixes();
    const auto existing = std::find_if(mixes.begin(), mixes.end(), [&](const auto& mix) {
        return matchesSavedSong(mix.outgoing, outgoing) && matchesSavedSong(mix.incoming, incoming);
    });
    int loopIndex = loops.empty() ? 0 : 1;
    int cueIndex = cues.empty() ? 0 : 1;
    if (existing != mixes.end() && existing->points) {
        const auto& points = *existing->points;
        const auto loop = std::find_if(loops.begin(), loops.end(),
                [&](const auto& range) { return points.loop && range.sameRange(*points.loop); });
        const auto cue = points.cue ? std::find(cues.begin(), cues.end(), *points.cue) : cues.end();
        loopIndex = loop == loops.end() ? 0 : static_cast<int>(loop - loops.begin() + 1);
        cueIndex = cue == cues.end() ? 0 : static_cast<int>(cue - cues.begin() + 1);
        if (points.loop && loopIndex == 0) {
            addLoop("Previously saved loop", *points.loop);
            loopIndex = static_cast<int>(loops.size());
        }
        if (points.cue && cueIndex == 0) {
            cues.push_back(*points.cue);
            cueNames.add("Previously saved cue   " + juce::String(*points.cue, 3) + " s");
            cueIndex = static_cast<int>(cues.size());
        }
    }
    const auto deckSettings = [](const GoodMixxHistory::TempoSetup::Deck& deck) {
        return juce::String(deck.sync ? "Sync on" : "Sync off") +
                (deck.keyLock ? " · Key Lock on" : " · Key Lock off");
    };
    using Leader = GoodMixxHistory::TempoSetup::Leader;
    const auto leader = !tempo.leader ? "Sync leader is outside this mix"
            : *tempo.leader == Leader::outgoing ? "Outgoing track leads" : "Incoming track leads";
    auto* dialog = new juce::AlertWindow("Save GOOD MIXX transition",
            displayTitle(outgoing) + " → " + displayTitle(incoming) + "\n" + leader +
            "\nOutgoing · " + deckSettings(tempo.outgoing) + "   |   Incoming · " + deckSettings(tempo.incoming),
            juce::MessageBoxIconType::NoIcon, this);
    dialog->addComboBox("Outgoing loop", loopNames, "Outgoing loop");
    dialog->addComboBox("Incoming cue", cueNames, "Incoming cue");
    dialog->getComboBoxComponent("Outgoing loop")->setSelectedItemIndex(loopIndex);
    dialog->getComboBoxComponent("Incoming cue")->setSelectedItemIndex(cueIndex);
    dialog->addButton("Save Mix", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [safe = SafePointer<CrateLibrary>(this), window = SafePointer<juce::AlertWindow>(dialog),
                    outgoing, incoming, tempo, loopChoices = std::move(loops), cueChoices = std::move(cues)](int result) {
                if (result != 1 || !safe || !window) {
                    return;
                }
                const int loop = window->getComboBoxComponent("Outgoing loop")->getSelectedItemIndex() - 1;
                const int cue = window->getComboBoxComponent("Incoming cue")->getSelectedItemIndex() - 1;
                GoodMixxHistory::Points points;
                if (juce::isPositiveAndBelow(loop, static_cast<int>(loopChoices.size()))) {
                    points.loop = loopChoices[static_cast<std::size_t>(loop)];
                }
                if (juce::isPositiveAndBelow(cue, static_cast<int>(cueChoices.size()))) {
                    points.cue = cueChoices[static_cast<std::size_t>(cue)];
                }
                points.tempo = tempo;
                const bool saved = safe->history.save(outgoing, incoming, points);
                safe->saveStatus.setColour(juce::Label::textColourId, saved ? crate::green : crate::amber);
                safe->saveStatus.setText(saved ? "Mix saved" : "Could not save mix", juce::dontSendNotification);
                safe->updateMixes();
            }), true);
}

void CrateLibrary::chooseAudioToRelink() {
    if (chooser) {
        return;
    }
    chooser = std::make_unique<juce::FileChooser>("Find audio for saved mixes", juce::File{},
            "*.mp3;*.wav;*.aif;*.aiff;*.flac;*.ogg;*.opus;*.m4a;*.mp4");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems;
    chooser->launchAsync(flags, [safe = SafePointer<CrateLibrary>(this)](const auto& picker) {
        if (!safe) {
            return;
        }
        const auto files = picker.getResults();
        safe->chooser.reset();
        if (files.isEmpty()) {
            return;
        }
        const auto count = safe->history.relinkMissing(files);
        juce::String status = "Could not save new paths";
        if (count) {
            status = *count == 0 ? juce::String("No missing tracks matched")
                    : "Relinked " + juce::String(*count) + (*count == 1 ? " track" : " tracks");
        }
        safe->saveStatus.setText(status, juce::dontSendNotification);
        safe->updateMixes();
    });
}

void CrateLibrary::analyzeTrack(const CrateTrack& track) {
    const auto path = track.file.getFullPathName();
    if (analyzing.contains(path)) {
        return;
    }
    analyzing.add(path);
    analysisWorker.addJob(std::function<void()>{
        [safe = SafePointer<CrateLibrary>(this), file = track.file, modifiedMs = track.modifiedMs,
                needTempo = !track.bpm, store = &metadata] {
            const auto saved = needTempo ? store->load(file).grid : std::nullopt;
            auto analysis = probeTrackAnalysis(file, needTempo && !saved);
            if (saved) {
                analysis.bpm = saved->bpm;
            }
            juce::MessageManager::callAsync([safe, file, modifiedMs, analysis] {
                if (!safe) {
                    return;
                }
                safe->analyzing.removeString(file.getFullPathName());
                const auto current = std::find_if(safe->tracks.begin(), safe->tracks.end(),
                        [&](const auto& item) { return item.file == file && item.modifiedMs == modifiedMs; });
                if (current == safe->tracks.end()) {
                    return;
                }
                current->available = file.existsAsFile();
                if (current->available && file.getLastModificationTime().toMilliseconds() == modifiedMs) {
                    current->analyzed = true;
                    if (!current->key) {
                        current->key = analysis.key;
                    }
                    if (!current->bpm) {
                        current->bpm = analysis.bpm;
                    }
                    safe->persistTrack(*current);
                }
                safe->table.repaint();
                safe->updateMixes();
            });
        }});
}

void CrateLibrary::updateMixes() {
    const auto& decks = context.decks;
    const auto loaded = [](const auto& deck) { return deck.file != juce::File{}; };
    const auto playing = [&](const auto& deck) { return loaded(deck) && deck.playing; };
    const auto visibleEnd = decks.begin() + visibleDecks;
    const auto first = std::find_if(decks.begin(), visibleEnd, loaded);
    saveMix.setEnabled(first != visibleEnd && std::any_of(first + 1, visibleEnd, [&](const auto& deck) {
        return loaded(deck) && !sameSessionTrack(first->file, deck.file);
    }));

    auto source = context.leader < static_cast<std::size_t>(visibleDecks)
            ? decks.begin() + static_cast<std::ptrdiff_t>(context.leader) : visibleEnd;
    if (source == visibleEnd || !playing(*source)) {
        const auto live = std::find_if(decks.begin(), visibleEnd, playing);
        if (live != visibleEnd || source == visibleEnd || !loaded(*source)) {
            source = live != visibleEnd ? live : first;
        }
    }

    const auto sourceFile = source == visibleEnd ? juce::File{} : source->file;
    const auto onDeck = [&](const juce::File& file) {
        return std::any_of(decks.begin(), decks.end(), [&](const auto& deck) {
            return loaded(deck) && sameSessionTrack(deck.file, file);
        });
    };
    std::vector<MixRow> rows;
    const auto alreadyListed = [&](const juce::File& file) {
        return std::any_of(rows.begin(), rows.end(),
                [&](const auto& row) { return sameSessionTrack(row.file, file); });
    };
    const auto resolveSong = [&](const GoodMixxHistory::Song& song) {
        if (song.file.existsAsFile()) {
            return song.file;
        }
        const auto session = std::find_if(tracks.begin(), tracks.end(), [&](const auto& track) {
            return matchesSavedSong(song, track.file) && track.file.existsAsFile();
        });
        if (session != tracks.end()) {
            return session->file;
        }
        for (const auto& path : song.alternatePaths) {
            const juce::File alias(path);
            if (alias.existsAsFile()) {
                return alias;
            }
        }
        return song.file;
    };
    auto status = juce::String("Load a track to find its next mix");
    if (source == visibleEnd) {
        for (const auto& mix : history.mixes()) {
            const auto from = resolveSong(mix.outgoing);
            const auto to = resolveSong(mix.incoming);
            const bool canRecall = from.existsAsFile() && to.existsAsFile() && !sameSessionTrack(from, to);
            rows.push_back({to, savedTitle(mix.incoming),
                    juce::String::fromUTF8("Proven · Saved ") + juce::String(mix.count) + "×",
                    savedTitle(mix.outgoing), canRecall ? 1 : -1, true, canRecall ? 0 : -1, from, mix.points});
        }
        if (!rows.empty()) {
            status = "Saved transitions";
        }
    } else {
        int target = -1;
        for (int i = 0; i < visibleDecks; ++i) {
            const auto& deck = decks[static_cast<std::size_t>(i)];
            if (&deck == &*source || deck.playing) {
                continue;
            }
            if (!loaded(deck)) {
                target = i;
                break;
            }
            if (target < 0) {
                target = i;
            }
        }

        for (const auto& mix : history.mixes()) {
            if (!matchesSavedSong(mix.outgoing, sourceFile) || matchesSavedSong(mix.incoming, sourceFile)) {
                continue;
            }

            const auto& incoming = mix.incoming;
            const auto file = resolveSong(incoming);
            const auto title = savedTitle(incoming);
            if ((file != juce::File{} && alreadyListed(file)) ||
                    std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
                        return row.proven && row.title.equalsIgnoreCase(title);
                    })) {
                continue;
            }
            const auto incomingDeck = std::find_if(decks.begin(), decks.end(),
                    [&](const auto& deck) { return matchesSavedSong(incoming, deck.file); });
            const bool loadedOnDeck = incomingDeck != decks.end();
            const bool available = file.existsAsFile();
            auto details = juce::String::fromUTF8("Proven · Saved ") + juce::String(mix.count) + "×";
            if (loadedOnDeck) {
                details += " · On deck";
            } else if (!available) {
                details += file == juce::File{} ? " · File not matched" : " · File missing";
            }
            const int recallTarget = loadedOnDeck
                    ? (incomingDeck < visibleEnd && !incomingDeck->playing
                            ? static_cast<int>(incomingDeck - decks.begin()) : -1)
                    : available ? target : -1;
            const bool canRecall = !source->playing && recallTarget >= 0;
            rows.push_back({canRecall && loadedOnDeck ? incomingDeck->file : file, title, details, {},
                    canRecall ? recallTarget : available && !loadedOnDeck ? target : -1, true,
                    canRecall ? static_cast<int>(source - decks.begin()) : -1, sourceFile, mix.points});
        }

        const auto sourceDeck = "Deck " + juce::String(source - decks.begin() + 1);
        const auto sourceTrack = std::find_if(tracks.begin(), tracks.end(),
                [&](const auto& track) { return track.file == sourceFile; });
        const auto sourceBpm = source->bpm ? source->bpm
                : sourceTrack != tracks.end() ? sourceTrack->bpm : std::nullopt;
        if (sourceTrack == tracks.end() || !sourceTrack->analyzed) {
            status = "Analyzing " + sourceDeck;
        } else if (!sourceBpm) {
            status = sourceDeck + " has no BPM";
        } else if (!sourceTrack->key) {
            status = sourceDeck + " key undetermined";
        } else {
            status = sourceDeck + " · " + camelotKey(*sourceTrack->key) + " · " +
                    juce::String(*sourceBpm, 1) + " BPM";

            struct Match {
                const CrateTrack* track;
                double score, change;
                std::size_t tempoRatio;
                int keyFit;
            };
            std::vector<Match> matches;
            for (const auto& track : tracks) {
                if (!track.available || !track.bpm || !track.key || onDeck(track.file) || alreadyListed(track.file)) {
                    continue;
                }
                const int keyFit = goodMixxKeyFit(*sourceTrack->key, *track.key);
                if (keyFit == 0) {
                    continue;
                }
                constexpr std::array multipliers{1.0, 2.0, 0.5};
                Match match{&track, 1, 0, 0, keyFit};
                for (std::size_t i = 0; i < multipliers.size(); ++i) {
                    const double change = *sourceBpm / (*track.bpm * multipliers[i]) - 1;
                    const double score = std::abs(change) + (i == 0 ? 0 : 0.012);
                    if (score < match.score && std::abs(change) <= 0.08) {
                        match = {&track, score, change, i, keyFit};
                    }
                }
                if (match.score < 1) {
                    matches.push_back(match);
                }
            }
            std::stable_sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
                return a.keyFit != b.keyFit ? a.keyFit > b.keyFit : a.score < b.score;
            });
            int suggested = 0;
            for (const auto& match : matches) {
                if (alreadyListed(match.track->file)) {
                    continue;
                }
                const auto amount = juce::String(match.change >= 0 ? "+" : "") +
                        juce::String(match.change * 100, 1) + "%";
                constexpr std::array labels{"", " ×2", " ÷2"};
                auto details = camelotKey(*match.track->key) + " · " +
                        juce::String(*match.track->bpm, 1) + labels[match.tempoRatio] + "  " + amount;
                rows.push_back({match.track->file, displayTitle(match.track->file), details, {}, target});
                if (++suggested == 3) {
                    break;
                }
            }
            if (rows.empty()) {
                status = std::any_of(tracks.begin(), tracks.end(), [](const auto& track) { return track.available && !track.analyzed; })
                        ? "Analyzing library tracks" : "No key + tempo matches";
            }
        }
    }
    mixStatus.setText(rows.empty() ? status
                    : juce::String(source == visibleEnd ? "Saved Mixes (" : "Next Tracks (") +
                            juce::String(static_cast<int>(rows.size())) + ")",
            juce::dontSendNotification);
    mixModel.rows = std::move(rows);
    mixList.updateContent();
    if (mixSource != sourceFile) {
        mixSource = sourceFile;
        mixList.scrollToEnsureRowIsOnscreen(0);
    }
    resized();
    mixList.repaint();
}

void CrateLibrary::browse() {
    if (chooser) {
        return;
    }
    chooser = std::make_unique<juce::FileChooser>("Add local tracks");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems;
    chooser->launchAsync(flags, [safe = SafePointer<CrateLibrary>(this)](const auto& picker) {
        if (!safe) {
            return;
        }
        const auto files = picker.getResults();
        safe->chooser.reset();
        for (const auto& file : files) {
            safe->addTrack(file);
        }
        if (safe->getNumRows() > 0) {
            safe->table.selectRow(0);
        }
    });
}

void CrateLibrary::browsePlaylist() {
    if (chooser) {
        return;
    }
    const auto exports = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support/labs.xtragood.crate-os/exports/playlists");
    chooser = std::make_unique<juce::FileChooser>("Import a Crate OS playlist", exports, "*.m3u;*.m3u8");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync(flags, [safe = SafePointer<CrateLibrary>(this)](const auto& picker) {
        if (!safe) {
            return;
        }
        const auto file = picker.getResult();
        safe->chooser.reset();
        if (file == juce::File{}) {
            return;
        }
        safe->importPlaylist.setEnabled(false);
        safe->playlistStatus.setText("Reading playlist", juce::dontSendNotification);
        safe->analysisWorker.addJob(std::function<void()>{[safe, file] {
            juce::MessageManager::callAsync([safe, imported = readPlaylist(file)] {
                if (!safe) {
                    return;
                }
                safe->importPlaylist.setEnabled(true);
                for (const auto& track : imported.playlist.files) {
                    safe->addTrack(track);
                }
                safe->finishPlaylistImport(imported.playlist, imported.missing);
            });
        }});
    });
}

void CrateLibrary::browseOneLibrary() {
    if (chooser) {
        return;
    }
    chooser = std::make_unique<juce::FileChooser>("Choose the OneLibrary drive", juce::File("/Volumes"));
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    chooser->launchAsync(flags, [safe = SafePointer<CrateLibrary>(this)](const auto& picker) {
        if (!safe) {
            return;
        }
        const auto volume = picker.getResult();
        safe->chooser.reset();
        if (volume == juce::File{}) {
            return;
        }
        safe->importOneLibrary.setEnabled(false);
        safe->playlistStatus.setText("Reading OneLibrary", juce::dontSendNotification);
        safe->analysisWorker.addJob(std::function<void()>{[safe, volume] {
            juce::MessageManager::callAsync([safe, imported = readOneLibrary(volume)] {
                if (!safe) {
                    return;
                }
                safe->importOneLibrary.setEnabled(true);
                if (!imported || imported->empty()) {
                    safe->playlistStatus.setText(imported ? "No playlists found" : "Could not read OneLibrary",
                            juce::dontSendNotification);
                    return;
                }
                const auto importSelection = [safe](const OneLibraryPlaylist& entry) {
                    if (safe) {
                        for (const auto& importedTrack : entry.tracks) {
                            const auto& track = importedTrack.track;
                            const auto& performance = importedTrack.metadata;
                            if (performance.grid || !performance.savedLoops.empty() || std::any_of(performance.cues.begin(), performance.cues.end(),
                                        [](const auto& cue) { return cue.has_value(); })) {
                                if (!safe->metadata.edit(track.file, [&](TrackMetadata& saved) {
                                    const bool upgradeFixedImport = saved.grid && performance.grid &&
                                            !saved.grid->markers && performance.grid->markers &&
                                            std::abs(saved.grid->bpm - performance.grid->bpm) < 1e-9 &&
                                            std::abs(saved.grid->anchorSeconds - performance.grid->anchorSeconds) < 1e-9;
                                    if (!saved.grid || upgradeFixedImport) {
                                        saved.grid = performance.grid;
                                    }
                                    for (std::size_t slot = 0; slot < saved.cues.size(); ++slot) {
                                        if (!saved.cues[slot]) {
                                            saved.cues[slot] = performance.cues[slot];
                                            saved.hotLoopEnds[slot] = performance.hotLoopEnds[slot];
                                        }
                                    }
                                    for (const auto& range : performance.savedLoops) {
                                        saved.addSavedLoop(range);
                                    }
                                })) {
                                    safe->saveStatus.setText("Could not save OneLibrary track edits",
                                            juce::dontSendNotification);
                                }
                            }
                            safe->addTrack(track.file, track.bpm, track.seconds, track.key);
                        }
                        safe->finishPlaylistImport(entry.playlist, entry.unavailable);
                    }
                };
                if (imported->size() == 1) {
                    importSelection(imported->front());
                    return;
                }
                safe->playlistStatus.setText("Choose a playlist", juce::dontSendNotification);
                juce::PopupMenu menu;
                for (const auto& entry : *imported) {
                    menu.addItem(entry.playlist.name, [importSelection, entry] { importSelection(entry); });
                }
                menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(safe->importOneLibrary));
            });
        }});
    });
}

void CrateLibrary::browseMixxx() {
    if (chooser) {
        return;
    }
    const auto data = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    const auto production = data.getChildFile("Application Support/Spin Deck/spindeck.sqlite");
    chooser = std::make_unique<juce::FileChooser>("Import your Spin Deck library",
            production.existsAsFile() ? production : data, "*.sqlite;*.db");
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles, [safe = SafePointer<CrateLibrary>(this)](const auto& picker) {
        if (!safe) {
            return;
        }
        const auto source = picker.getResult();
        safe->chooser.reset();
        if (source == juce::File{}) {
            return;
        }
        safe->importMixxx.setEnabled(false);
        safe->playlistStatus.setText("Importing library...", juce::dontSendNotification);
        safe->analysisWorker.addJob(std::function<void()>{[safe, source, target = safe->crateDatabase,
                metadataStore = &safe->metadata] {
            const auto result = importMixxxLibrary(source, target, *metadataStore);
            juce::MessageManager::callAsync([safe, result] {
                if (!safe) {
                    return;
                }
                safe->importMixxx.setEnabled(true);
                if (auto imported = safe->libraryStore.load()) {
                    safe->tracks = std::move(*imported);
                    for (auto& track : safe->tracks) {
                        track.available = track.file.existsAsFile();
                    }
                    safe->applySavedTags();
                    safe->startTagScan();
                }
                safe->playlists = safe->libraryStore.loadPlaylists();
                safe->refreshPlaylistPicker();
                safe->filter();
                safe->updateMixes();
                const auto status = result ? juce::String(result->tracks) + " tracks · " +
                        juce::String(result->missing) + " missing" : "Import incomplete";
                safe->playlistStatus.setText(status, juce::dontSendNotification);
                safe->playlistStatus.setTooltip(result ? juce::String(result->grids) + " grids, " +
                        juce::String(result->cueTracks) + " tracks with cues or loops, " +
                        juce::String(result->crates) + " crates, " + juce::String(result->playlists) +
                        " playlists and " + juce::String(result->history) + " history sessions"
                        : "Check the selected database and try the import again.");
            });
        }});
    });
}

void CrateLibrary::finishPlaylistImport(const CratePlaylist& playlist, int missing) {
    if (playlist.files.empty()) {
        playlistStatus.setText("No local tracks found", juce::dontSendNotification);
        return;
    }
    if (!libraryStore.savePlaylist(playlist)) {
        playlistStatus.setText("Could not save playlist", juce::dontSendNotification);
        return;
    }
    playlists = libraryStore.loadPlaylists();
    refreshPlaylistPicker(playlist.source);
    const auto count = juce::String(static_cast<int>(playlist.files.size()));
    playlistStatus.setText(missing ? count + " added, " + juce::String(missing) + " skipped"
                                   : count + " tracks imported", juce::dontSendNotification);
    filter();
    table.selectRow(0);
}

void CrateLibrary::refreshPlaylistPicker(const juce::String& selection) {
    playlistPicker.clear(juce::dontSendNotification);
    playlistPicker.addItem("Library", 1);
    int selectedId = 1;
    for (std::size_t i = 0; i < playlists.size(); ++i) {
        const auto& source = playlists[i].source;
        const auto id = static_cast<int>(i) + 2;
        playlistPicker.addItem(playlists[i].name, id);
        if (source == selection) {
            selectedId = id;
        }
    }
    playlistPicker.setSelectedId(selectedId, juce::dontSendNotification);
}

void CrateLibrary::filter() {
    const auto query = search.getText().trim();
    const int selected = table.getSelectedRow();
    const auto previous = juce::isPositiveAndBelow(selected, getNumRows())
            ? std::optional{results[static_cast<std::size_t>(selected)]} : std::nullopt;
    results.clear();
    const auto addMatch = [&](std::size_t index) {
        const auto& item = tracks[index];
        const auto matches = [&](const juce::String& value) { return value.containsIgnoreCase(query); };
        if (query.isEmpty() || matches(item.file.getFileName()) ||
                (item.tags && (matches(item.tags->title) || matches(item.tags->artist) ||
                        matches(item.tags->album) || matches(item.tags->genre)))) {
            results.push_back(index);
        }
    };
    const int playlistIndex = playlistPicker.getSelectedId() - 2;
    if (juce::isPositiveAndBelow(playlistIndex, static_cast<int>(playlists.size()))) {
        for (const auto& file : playlists[static_cast<std::size_t>(playlistIndex)].files) {
            const auto found = std::find_if(tracks.begin(), tracks.end(),
                    [&](const auto& track) { return track.file == file; });
            if (found != tracks.end()) {
                addMatch(static_cast<std::size_t>(found - tracks.begin()));
            }
        }
    } else {
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            addMatch(i);
        }
    }
    table.deselectAllRows();
    table.updateContent();
    if (previous) {
        if (const auto same = std::find(results.begin(), results.end(), *previous); same != results.end()) {
            table.selectRow(static_cast<int>(same - results.begin()));
        }
    }
    table.repaint();
    repaint();
}

int CrateLibrary::getNumRows() { return static_cast<int>(results.size()); }

void CrateLibrary::selectedRowsChanged(int row) {
    const bool loadable = juce::isPositiveAndBelow(row, getNumRows()) &&
            tracks[results[static_cast<std::size_t>(row)]].available;
    for (auto& button : route) {
        button.setEnabled(loadable);
    }
}

void CrateLibrary::cellDoubleClicked(int row, int, const juce::MouseEvent&) {
    returnKeyPressed(row);
}

int CrateLibrary::bestDeckForLoading() const {
    const auto state = readTrackContext ? readTrackContext() : context;
    // Prefer an empty deck, then any deck that isn't playing.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < visibleDecks; ++i) {
            const auto& deck = state.decks[static_cast<std::size_t>(i)];
            if (!deck.playing && (pass == 1 || deck.file == juce::File{})) {
                return i;
            }
        }
    }
    return -1;
}

void CrateLibrary::returnKeyPressed(int row) {
    if (juce::isPositiveAndBelow(row, getNumRows()) && onLoad) {
        const auto& track = tracks[results[static_cast<std::size_t>(row)]];
        const int deck = bestDeckForLoading();
        if (track.available && deck >= 0) {
            onLoad(deck, track.file);
        } else if (deck < 0) {
            saveStatus.setColour(juce::Label::textColourId, crate::amber);
            saveStatus.setText("Every deck is playing. Stop one to load.", juce::dontSendNotification);
            resized();
        }
    }
}

void CrateLibrary::cellClicked(int row, int, const juce::MouseEvent& event) {
    if (!event.mods.isPopupMenu() || !juce::isPositiveAndBelow(row, getNumRows())) {
        return;
    }
    table.selectRow(row);
    const auto file = tracks[results[static_cast<std::size_t>(row)]].file;
    const auto state = readTrackContext ? readTrackContext() : context;
    juce::PopupMenu menu;
    for (int i = 0; i < visibleDecks; ++i) {
        const bool playing = state.decks[static_cast<std::size_t>(i)].playing;
        menu.addItem("Load on Deck " + juce::String(i + 1) + (playing ? "  (playing)" : ""),
                !playing && file.existsAsFile(), false, [this, i, file] {
                    if (onLoad) {
                        onLoad(i, file);
                    }
                });
    }
    menu.addSeparator();
    menu.addItem("Show in Finder", file.exists(), false, [file] { file.revealToUser(); });
    menu.showMenuAsync(juce::PopupMenu::Options{});
}

juce::var CrateLibrary::getDragSourceDescription(const juce::SparseSet<int>& rows) {
    if (rows.isEmpty() || !juce::isPositiveAndBelow(rows[0], getNumRows())) {
        return {};
    }
    const auto& track = tracks[results[static_cast<std::size_t>(rows[0])]];
    return track.available ? juce::var(juce::String(dragPrefix) + track.file.getFullPathName()) : juce::var{};
}

namespace {
bool isAudioFile(const juce::File& file) {
    return file.hasFileExtension("mp3;wav;aif;aiff;flac;ogg;opus;m4a;mp4;aac;alac");
}
}

bool CrateLibrary::isInterestedInFileDrag(const juce::StringArray& paths) {
    return std::any_of(paths.begin(), paths.end(), [](const auto& path) {
        const juce::File file(path);
        return file.isDirectory() || isAudioFile(file);
    });
}

void CrateLibrary::fileDragEnter(const juce::StringArray&, int, int) {
    fileDragHover = true;
    repaint();
}

void CrateLibrary::fileDragExit(const juce::StringArray&) {
    fileDragHover = false;
    repaint();
}

void CrateLibrary::filesDropped(const juce::StringArray& paths, int, int) {
    fileDragHover = false;
    int added = 0;
    for (const auto& path : paths) {
        const juce::File file(path);
        if (file.isDirectory()) {
            for (const auto& entry : juce::RangedDirectoryIterator(file, true, "*", juce::File::findFiles)) {
                if (isAudioFile(entry.getFile()) && added < 5000) {
                    addTrack(entry.getFile());
                    ++added;
                }
            }
        } else if (isAudioFile(file)) {
            addTrack(file);
            ++added;
        }
    }
    saveStatus.setColour(juce::Label::textColourId, crate::green);
    saveStatus.setText("Added " + juce::String(added) + (added == 1 ? " track" : " tracks"), juce::dontSendNotification);
    resized();
    repaint();
}

void CrateLibrary::paintOverChildren(juce::Graphics& g) {
    if (fileDragHover) {
        g.setColour(crate::green.withAlpha(0.12f));
        g.fillRect(table.getBounds());
        g.setColour(crate::green);
        g.drawRect(table.getBounds(), 2);
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        g.drawText("Drop to add to the library", table.getBounds(), juce::Justification::centred);
    }
}

juce::String CrateLibrary::getCellTooltip(int row, int) {
    return juce::isPositiveAndBelow(row, getNumRows())
            ? tracks[results[static_cast<std::size_t>(row)]].file.getFullPathName() : juce::String{};
}

void CrateLibrary::paintRowBackground(juce::Graphics& g, int row, int, int, bool selected) {
    g.fillAll(selected ? crate::selection : (row % 2 ? crate::rowAlt : crate::inset));
}

void CrateLibrary::paintCell(juce::Graphics& g, int row, int column, int width, int height, bool selected) {
    if (!juce::isPositiveAndBelow(row, getNumRows())) {
        return;
    }
    const auto& entry = tracks[results[static_cast<std::size_t>(row)]];
    const auto seconds = static_cast<int>(entry.seconds);
    auto [title, artistName] = crate::titleAndArtist(entry.file);
    if (entry.tags && entry.tags->title.isNotEmpty()) {
        title = entry.tags->title;
        artistName = entry.tags->artist.isNotEmpty() ? entry.tags->artist : artistName;
    }
    if (column == 1 && hasStems(entry.file)) {
        // A small STEMS tag: this song loads with its four stems.
        const int tagWidth = 44;
        const auto tag = juce::Rectangle<int>(width - tagWidth - 4, 3, tagWidth, height - 6);
        g.setColour(crate::starFill);
        g.fillRoundedRectangle(tag.toFloat(), 3.0f);
        g.setColour(crate::starText);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("STEMS", tag, juce::Justification::centred);
        width -= tagWidth + 6;
    }
    const bool loaded = std::any_of(context.decks.begin(), context.decks.end(),
            [&](const auto& deck) { return deck.file != juce::File{} && sameSessionTrack(deck.file, entry.file); });
    g.setFont(juce::FontOptions(13.5f, column == 1 && loaded ? juce::Font::bold : juce::Font::plain));
    if (column == 4 && entry.key) {
        const int number = camelotKey(*entry.key).getIntValue();
        g.setColour(juce::Colour::fromHSV(static_cast<float>((number + 7) % 12) / 12.0f, 0.75f, 0.95f, 1.0f));
        g.fillRect(6, 4, 3, height - 8);
        g.setColour(crate::text);
        g.drawText(crate::keyName(*entry.key), 14, 0, width - 16, height, juce::Justification::centredLeft, true);
        return;
    }
    const juce::String value = column == 5 ? juce::String(row + 1)
            : column == 1 ? title
            : column == 6 ? artistName
            : column == 7 ? (entry.tags ? entry.tags->album : juce::String{})
            : column == 8 ? (entry.tags ? entry.tags->genre : juce::String{})
            : column == 9 ? (entry.tags ? entry.tags->year : juce::String{})
            : column == 2 && entry.bpm ? juce::String(*entry.bpm, 1)
            : column == 3 && seconds > 0 ? juce::String(seconds / 60) + ":" +
                    juce::String(seconds % 60).paddedLeft('0', 2) : juce::String{};
    g.setColour(!entry.available ? crate::muted.withAlpha(0.6f)
            : (column == 1 || column == 6 || selected) ? crate::text : crate::text.withAlpha(0.8f));
    g.drawText(value, 8, 0, width - 12, height, juce::Justification::centredLeft, true);
}

int CrateLibrary::Sidebar::getNumRows() {
    return 3 + static_cast<int>(owner.playlists.size());
}

int CrateLibrary::Sidebar::playlistFor(int row) const {
    return row == 1 ? -1 : row >= 3 ? row - 3 : -2;
}

juce::String CrateLibrary::Sidebar::getNameForRow(int row) {
    const int playlist = playlistFor(row);
    return playlist == -1 ? juce::String("Library")
            : playlist >= 0 ? owner.playlists[static_cast<std::size_t>(playlist)].name
            : row == 0 ? juce::String("Crate OS") : juce::String("Playlists");
}

void CrateLibrary::Sidebar::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool) {
    const int playlist = playlistFor(row);
    const int selectedId = owner.playlistPicker.getSelectedId();
    const bool current = (playlist == -1 && selectedId == 1) || (playlist >= 0 && selectedId == playlist + 2);
    if (current) {
        g.setColour(crate::folderSelection);
        g.fillRoundedRectangle(4, 1, static_cast<float>(width - 8), static_cast<float>(height - 2), 3);
    }
    if (playlist == -2) {
        // Heading row with an open-folder arrow and an icon.
        g.setColour(crate::muted);
        juce::Path arrow;
        arrow.addTriangle(8, height * 0.5f - 3, 16, height * 0.5f - 3, 12, height * 0.5f + 3);
        g.fillPath(arrow);
        g.setColour(row == 0 ? crate::amber : crate::amber);
        g.fillRoundedRectangle(22, height * 0.5f - 6, 14, 11, 2);
        g.setColour(crate::text);
        g.setFont(juce::FontOptions(14.5f));
        g.drawText(getNameForRow(row), 42, 0, width - 46, height, juce::Justification::centredLeft, true);
        return;
    }
    g.setColour(crate::muted);
    g.drawRect(40, height / 2 - 5, 12, 10, 1);
    const auto count = playlist == -1 ? static_cast<int>(owner.tracks.size())
                                      : static_cast<int>(owner.playlists[static_cast<std::size_t>(playlist)].files.size());
    g.setColour(crate::text);
    g.setFont(juce::FontOptions(13.5f));
    g.drawText(getNameForRow(row) + " (" + juce::String(count) + ")", 58, 0, width - 62, height,
            juce::Justification::centredLeft, true);
}

void CrateLibrary::Sidebar::listBoxItemClicked(int row, const juce::MouseEvent&) {
    const int playlist = playlistFor(row);
    if (playlist == -2) {
        return;
    }
    owner.playlistPicker.setSelectedId(playlist == -1 ? 1 : playlist + 2, juce::sendNotificationSync);
}

void CrateLibrary::showAddMenu() {
    juce::PopupMenu menu;
    menu.addItem("Add audio files...", [this] { browse(); });
    menu.addSeparator();
    menu.addItem("Import Crate OS playlist (M3U)...", [this] { browsePlaylist(); });
    menu.addItem("Import djay OneLibrary...", [this] { browseOneLibrary(); });
    menu.addItem("Import Spin Deck library...", importMixxx.isEnabled(), false, [this] { browseMixxx(); });
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(addMenu));
}

void CrateLibrary::showMixMenu() {
    juce::PopupMenu menu;
    menu.addItem("Save mix...", saveMix.isEnabled(), false, [this] { chooseMixToSave(); });
    menu.addItem("Relink missing audio...", [this] { chooseAudioToRelink(); });
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(mixMenu));
}

void CrateLibrary::resized() {
    auto area = getLocalBounds();
    sidebar = area.removeFromLeft(228);
    auto side = sidebar.reduced(8, 8);
    search.setBounds(side.removeFromTop(28));
    side.removeFromTop(6);
    auto bottom = side.removeFromBottom(26);
    const int visibleRoutes = static_cast<int>(std::count_if(route.begin(), route.end(),
            [](const auto& button) { return button.isVisible(); }));
    auto loadRow = bottom.removeFromRight(28 * visibleRoutes);
    for (auto& button : route) {
        if (button.isVisible()) {
            button.setBounds(loadRow.removeFromLeft(28).reduced(1, 1));
        }
    }
    addMenu.setBounds(bottom.withTrimmedRight(6).reduced(0, 1));
    side.removeFromBottom(4);
    playlistStatus.setBounds(playlistStatus.getText().isEmpty() ? juce::Rectangle<int>{} : side.removeFromBottom(18));
    sidebarList.setBounds(side);
    sidebarList.updateContent();

    mixPanel = area.removeFromRight(296);
    table.setBounds(area);
    const int flexible = juce::jmax(300, area.getWidth() - 42 - 64 - 64 - 56 - 74 - 18);
    table.getHeader().setColumnWidth(1, flexible * 33 / 100);
    table.getHeader().setColumnWidth(6, flexible * 25 / 100);
    table.getHeader().setColumnWidth(7, flexible * 26 / 100);
    table.getHeader().setColumnWidth(8, flexible * 16 / 100);

    auto mixes = mixPanel.reduced(8, 6);
    mixHeader = mixes.removeFromTop(32);
    mixMenu.setBounds(mixHeader.withTrimmedLeft(mixHeader.getWidth() - 32).reduced(1, 3));
    auto status = mixes.removeFromTop(26);
    mixStatus.setBounds(status);
    saveStatus.setBounds(saveStatus.getText().isEmpty() ? juce::Rectangle<int>{} : mixes.removeFromTop(18));
    mixes.removeFromTop(2);
    mixList.setBounds(mixes);
}

void CrateLibrary::paint(juce::Graphics& g) {
    g.fillAll(crate::panel);
    g.setColour(crate::line);
    g.drawRect(getLocalBounds());
    g.drawVerticalLine(sidebar.getRight(), 0, static_cast<float>(getHeight()));
    g.drawVerticalLine(mixPanel.getX(), 0, static_cast<float>(getHeight()));
    g.setColour(crate::text);
    g.setFont(crate::brandFont(22));
    g.drawText(juce::String::fromUTF8("GOOD MIXX\xE2\x84\xA2"), mixHeader.withTrimmedLeft(4),
            juce::Justification::centredLeft);
}

}
