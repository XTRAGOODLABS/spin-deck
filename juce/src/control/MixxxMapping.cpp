// AI-written (Claude). Review before relying on it.
// Parsing follows Mixxx's LegacyMidiControllerMappingFileHandler.
// AI-written (Claude). End.

#include "control/MixxxMapping.h"

#include <algorithm>
#include <cstdlib>
#include <functional>

namespace spindeck::mixxx {

namespace {

int parseNumber(const juce::String& text, int fallback) {
    const auto t = text.trim();
    if (t.isEmpty()) return fallback;
    if (t.startsWithIgnoreCase("0x")) {
        const auto digits = t.substring(2);
        return digits.containsOnly("0123456789abcdefABCDEF") && digits.isNotEmpty() ? digits.getHexValue32() : fallback;
    }
    if (t.length() > 1 && t.startsWith("0") && t.containsOnly("01234567")) {
        int value = 0;
        for (auto c : t) value = value * 8 + (static_cast<int>(c) - '0');
        return value;
    }
    return t.containsOnly("-0123456789") ? t.getIntValue() : fallback;
}

juce::String childText(const juce::XmlElement& element, const char* name) {
    if (auto* child = element.getChildByName(name)) return child->getAllSubText().trim();
    return {};
}

juce::File findScript(const juce::File& mappingFile, const juce::String& name) {
    const auto local = mappingFile.getParentDirectory().getChildFile(name);
    if (local.existsAsFile()) return local;
    for (const auto& folder : mappingFolders()) {
        const auto candidate = folder.getChildFile(name);
        if (candidate.existsAsFile()) return candidate;
    }
    return {};
}

} // namespace

juce::Array<juce::File> mappingFolders() {
    juce::Array<juce::File> folders;
    const auto add = [&](const juce::File& f) { if (f.isDirectory() && !folders.contains(f)) folders.add(f); };
    const auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    add(appData.getChildFile("Application Support/Spin Deck JUCE/controllers"));
    add(appData.getChildFile("Spin Deck JUCE/controllers"));
    // Mixxx keeps user mappings here on a Mac; reuse them so custom mappings come along.
    add(appData.getChildFile("Application Support/Mixxx/controllers"));
    add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                    .getChildFile("Library/Containers/org.mixxx.mixxx/Data/Library/Application Support/Mixxx/controllers"));
    const auto app = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    add(app.getChildFile("Contents/Resources/controllers"));
    add(app.getParentDirectory().getChildFile("controllers"));
    if (const auto* env = std::getenv("SPINDECK_CONTROLLERS")) add(juce::File(env));
    return folders;
}

std::vector<MappingInfo> listMappings() {
    std::vector<MappingInfo> result;
    juce::StringArray seen;
    for (const auto& folder : mappingFolders()) {
        for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.midi.xml")) {
            if (seen.contains(file.getFileName())) continue; // a user copy wins over the built-in one
            seen.add(file.getFileName());
            MappingInfo info{file, file.getFileName().upToFirstOccurrenceOf(".midi.xml", false, true), {}};
            // Only the <info> and <controller> headers are needed for the list.
            juce::FileInputStream stream(file);
            if (stream.openedOk()) {
                const auto head = stream.readString().substring(0, 4000);
                const auto name = head.fromFirstOccurrenceOf("<name>", false, false).upToFirstOccurrenceOf("</name>", false, false);
                if (name.isNotEmpty() && name.length() < 120) info.name = name.trim();
                info.controllerId = head.fromFirstOccurrenceOf("<controller id=\"", false, false).upToFirstOccurrenceOf("\"", false, false);
            }
            result.push_back(info);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    return result;
}

std::optional<Mapping> loadMapping(const juce::File& xmlFile, juce::String* error) {
    const auto xml = juce::XmlDocument::parse(xmlFile);
    if (xml == nullptr || !xml->hasTagName("MixxxControllerPreset") && !xml->hasTagName("MixxxMIDIPreset")) {
        if (error) *error = "Not a Mixxx controller mapping: " + xmlFile.getFileName();
        return std::nullopt;
    }
    Mapping mapping;
    mapping.file = xmlFile;
    if (auto* info = xml->getChildByName("info")) {
        mapping.name = childText(*info, "name");
        mapping.author = childText(*info, "author");
    }
    if (mapping.name.isEmpty()) mapping.name = xmlFile.getFileNameWithoutExtension();
    auto* controller = xml->getChildByName("controller");
    if (controller == nullptr) {
        if (error) *error = "The mapping has no <controller> section.";
        return std::nullopt;
    }
    mapping.controllerId = controller->getStringAttribute("id");

    // Mixxx always loads this one first.
    if (const auto common = findScript(xmlFile, "common-controller-scripts.js"); common.existsAsFile()) {
        mapping.scripts.push_back({common, {}});
    } else {
        mapping.missingScripts.push_back("common-controller-scripts.js");
    }
    if (auto* files = controller->getChildByName("scriptfiles")) {
        for (auto* f : files->getChildWithTagNameIterator("file")) {
            const auto name = f->getStringAttribute("filename");
            if (name.endsWithIgnoreCase(".qml") || name.isEmpty()) continue;
            const auto found = findScript(xmlFile, name);
            if (!found.existsAsFile()) {
                mapping.missingScripts.push_back(name);
                continue;
            }
            mapping.scripts.push_back({found, f->getStringAttribute("functionprefix")});
        }
    }
    if (auto* controls = controller->getChildByName("controls")) {
        for (auto* c : controls->getChildWithTagNameIterator("control")) {
            MappingControl control;
            control.status = parseNumber(childText(*c, "status"), 0) & 0xff;
            control.midino = parseNumber(childText(*c, "midino"), 0) & 0xff;
            control.group = childText(*c, "group");
            control.key = childText(*c, "key");
            if (auto* options = c->getChildByName("options")) {
                for (auto* o : options->getChildIterator()) {
                    const auto n = o->getTagName().toLowerCase();
                    if (n == "invert") control.options |= invert;
                    else if (n == "rot64") control.options |= rot64;
                    else if (n == "rot64inv") control.options |= rot64Invert;
                    else if (n == "rot64fast") control.options |= rot64Fast;
                    else if (n == "diff") control.options |= diff;
                    else if (n == "button") control.options |= button;
                    else if (n == "switch") control.options |= switchMode;
                    else if (n == "hercjog") control.options |= hercJog;
                    else if (n == "hercjogfast") control.options |= hercJogFast;
                    else if (n == "spread64") control.options |= spread64;
                    else if (n == "selectknob") control.options |= selectKnob;
                    else if (n == "soft-takeover") control.options |= softTakeover;
                    else if (n == "script-binding") control.options |= script;
                    else if (n == "fourteen-bit-msb") control.options |= fourteenBitMsb;
                    else if (n == "fourteen-bit-lsb") control.options |= fourteenBitLsb;
                }
            }
            if (control.key.isNotEmpty()) mapping.controls.push_back(control);
        }
    }
    std::function<void(const juce::XmlElement&)> readSettings = [&](const juce::XmlElement& e) {
        for (auto* child : e.getChildIterator()) {
            if (child->hasTagName("option") && child->hasAttribute("variable")) {
                const auto type = child->getStringAttribute("type");
                const auto fallback = child->getStringAttribute("default");
                juce::var value;
                if (type == "boolean") value = fallback == "true";
                else if (type == "integer") value = fallback.getIntValue();
                else if (type == "real") value = fallback.getDoubleValue();
                else if (type == "enum") {
                    juce::String chosen;
                    for (auto* v : child->getChildWithTagNameIterator("value")) {
                        if (chosen.isEmpty() || v->getStringAttribute("default") == "true") chosen = v->getAllSubText().trim();
                        if (v->getStringAttribute("default") == "true") break;
                    }
                    value = chosen;
                } else value = fallback;
                mapping.settings.set(child->getStringAttribute("variable"), value);
            } else {
                readSettings(*child);
            }
        }
    };
    if (auto* settings = xml->getChildByName("settings")) readSettings(*settings);
    if (auto* outputs = controller->getChildByName("outputs")) {
        for (auto* o : outputs->getChildWithTagNameIterator("output")) {
            MappingOutput output;
            output.group = childText(*o, "group");
            output.key = childText(*o, "key");
            output.status = parseNumber(childText(*o, "status"), 0) & 0xff;
            output.midino = parseNumber(childText(*o, "midino"), 0) & 0xff;
            output.on = parseNumber(childText(*o, "on"), 0x7f);
            output.off = parseNumber(childText(*o, "off"), 0);
            const auto minText = childText(*o, "minimum"), maxText = childText(*o, "maximum");
            output.minimum = minText.isNotEmpty() ? minText.getDoubleValue() : 0.0;
            output.maximum = maxText.isNotEmpty() ? maxText.getDoubleValue() : 1.0;
            if (output.key.isNotEmpty()) mapping.outputs.push_back(output);
        }
    }
    return mapping;
}

int matchScore(const MappingInfo& info, const juce::String& deviceName) {
    const auto simplify = [](juce::String s) {
        return s.toLowerCase().replace("djcontrol", "djcontrol ").retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789 ");
    };
    const auto device = simplify(deviceName);
    if (device.isEmpty()) return 0;
    if (simplify(info.controllerId).trim() == device.trim()) return 1000;
    juce::StringArray words;
    words.addTokens(simplify(info.name + " " + info.controllerId), " ", "");
    words.removeEmptyStrings();
    words.removeDuplicates(true);
    int score = 0;
    for (const auto& w : words) {
        if (w.length() >= 2 && device.contains(w)) score += w.length() + (w.containsAnyOf("0123456789") ? 6 : 0);
    }
    return score;
}

}
