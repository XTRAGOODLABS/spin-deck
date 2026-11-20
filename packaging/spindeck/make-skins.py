"""Spin Deck skins by XTRAGOODLABS: CRATE, BOOTH, ON AIR and POCKET.

Each skin is its own layout, written from scratch on Mixxx's skin system
(original designs). Run from the repo root to (re)write them:

    python3 packaging/spindeck/make-skins.py

Writes res/skins/Spin Deck <NAME>/ (skin.xml, style.qss and its own images).
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SKINS = os.path.join(ROOT, "res/skins")

# ---------------------------------------------------------------- helpers


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def group(children, layout="horizontal", name=None, size=None, policy=None, extra=""):
    out = "<WidgetGroup>"
    if name:
        out += f"<ObjectName>{name}</ObjectName>"
    out += f"<Layout>{layout}</Layout>"
    if size:
        out += f"<Size>{size}</Size>"
    if policy:
        out += f"<SizePolicy>{policy}</SizePolicy>"
    out += extra
    out += "<Children>" + "".join(children) + "</Children></WidgetGroup>"
    return out


def shown_by(key):
    return f"<Connection><ConfigKey>{key}</ConfigKey><BindProperty>visible</BindProperty></Connection>"


def spacer(size="0me,0me"):
    return f"<WidgetGroup><Size>{size}</Size><Layout>horizontal</Layout><Children></Children></WidgetGroup>"


def label(text, name, size=None):
    s = f"<Size>{size}</Size>" if size else ""
    return f"<Label><ObjectName>{name}</ObjectName>{s}<Text>{esc(text)}</Text></Label>"


def button(text, name, key, size, kind="toggle", show_key=None, tooltip=None, text_on=None):
    """kind: toggle (click flips), push (pressed while held), trigger (one shot)."""
    t = f"<TooltipId>{tooltip}</TooltipId>" if tooltip else ""
    on = esc(text_on or text)
    out = (f"<PushButton>{t}<ObjectName>{name}</ObjectName><Size>{size}</Size>"
           f"<NumberStates>2</NumberStates>"
           f"<State><Number>0</Number><Text>{esc(text)}</Text></State>"
           f"<State><Number>1</Number><Text>{on}</Text></State>")
    if kind == "toggle":
        out += f"<Connection><ConfigKey>{key}</ConfigKey><ButtonState>LeftButton</ButtonState></Connection>"
    else:
        out += (f"<Connection><ConfigKey>{key}</ConfigKey><EmitOnPressAndRelease>true</EmitOnPressAndRelease>"
                f"<ButtonState>LeftButton</ButtonState></Connection>")
    if show_key:
        out += (f"<Connection><ConfigKey>{show_key}</ConfigKey>"
                f"<ConnectValueFromWidget>false</ConnectValueFromWidget></Connection>")
    return out + "</PushButton>"


def hotcue(g, n, size):
    return (f"<HotcueButton><TooltipId>hotcue</TooltipId><ObjectName>Hotcue</ObjectName><Size>{size}</Size>"
            f"<Group>{g}</Group><Hotcue>{n}</Hotcue><Hover>true</Hover><NumberStates>3</NumberStates>"
            + "".join(f"<State><Number>{i}</Number><Text>{n}</Text></State>" for i in range(3))
            + "</HotcueButton>")


def prop(g, p, name, size, align="left"):
    return (f"<TrackProperty><ObjectName>{name}</ObjectName><Size>{size}</Size><Property>{p}</Property>"
            f"<Group>{g}</Group><Alignment>{align}</Alignment><Elide>right</Elide></TrackProperty>")


def bpm(g, name, size):
    return (f"<NumberBpm><TooltipId>visual_bpm</TooltipId><ObjectName>{name}</ObjectName><Size>{size}</Size>"
            f"<Group>{g}</Group><Alignment>center</Alignment><NumberOfDigits>1</NumberOfDigits>"
            f"<Connection><ConfigKey>{g},visual_bpm</ConfigKey></Connection></NumberBpm>")


def key(g, name, size):
    return (f"<Key><TooltipId>visual_key</TooltipId><ObjectName>{name}</ObjectName><Size>{size}</Size>"
            f"<Group>{g}</Group><Alignment>center</Alignment>"
            f"<Connection><ConfigKey>{g},visual_key</ConfigKey></Connection></Key>")


def time_pos(g, name, size):
    return (f"<NumberPos><TooltipId>track_time</TooltipId><ObjectName>{name}</ObjectName><Size>{size}</Size>"
            f"<Alignment>center</Alignment><Channel>{g[8:-1]}</Channel>"
            f"<Connection><ConfigKey>{g},playposition</ConfigKey></Connection></NumberPos>")


def marks():
    return ("<DefaultMark><Align>top</Align><Color>#ff8a00</Color><TextColor>#000000</TextColor><Text> %1 </Text></DefaultMark>"
            "<Mark><Control>cue_point</Control><Text>CUE</Text><Align>bottom</Align><Color>#ff3b30</Color><TextColor>#ffffff</TextColor></Mark>"
            "<MarkRange><StartControl>loop_start_position</StartControl><EndControl>loop_end_position</EndControl>"
            "<EnabledControl>loop_enabled</EnabledControl><Color>#3ddc84</Color><Opacity>0.6</Opacity>"
            "<DisabledColor>#ffffff</DisabledColor><DisabledOpacity>0.3</DisabledOpacity></MarkRange>")


def waveform(g, p, d, size):
    return (f"<Visual><TooltipId>waveform_display</TooltipId><ObjectName>Waveform</ObjectName><Size>{size}</Size>"
            f"<Group>{g}</Group><BgColor>{p['wave_bg']}</BgColor><SignalColor>{p['deck'][d]}</SignalColor><SignalHighColor>#ffffff</SignalHighColor><SignalMidColor>{p['deck'][d]}</SignalMidColor><SignalLowColor>{p['deck'][d]}</SignalLowColor><SignalRGBHighColor>#ffffff</SignalRGBHighColor><SignalRGBMidColor>{p['deck'][d]}</SignalRGBMidColor><SignalRGBLowColor>{p['deck'][d]}</SignalRGBLowColor>"
            f"<BeatColor>{p['beat']}</BeatColor><PlayPosColor>{p['playpos']}</PlayPosColor>"
            f"<EndOfTrackColor>#ff3b30</EndOfTrackColor><AxesColor></AxesColor>"
            f"<SlipBorderOutlineColor>#f08c00</SlipBorderOutlineColor>{marks()}</Visual>")


def overview(g, p, d, size):
    return (f"<Overview><TooltipId>waveform_overview</TooltipId><ObjectName>Overview</ObjectName><Size>{size}</Size>"
            f"<Group>{g}</Group><BgColor>{p['wave_bg']}</BgColor><SignalColor>{p['deck'][d]}</SignalColor><SignalHighColor>#ffffff</SignalHighColor><SignalMidColor>{p['deck'][d]}</SignalMidColor><SignalLowColor>{p['deck'][d]}</SignalLowColor><SignalRGBHighColor>#ffffff</SignalRGBHighColor><SignalRGBMidColor>{p['deck'][d]}</SignalRGBMidColor><SignalRGBLowColor>{p['deck'][d]}</SignalRGBLowColor>"
            f"<PlayedOverlayColor>#99000000</PlayedOverlayColor><PlayPosColor>{p['playpos']}</PlayPosColor>"
            f"{marks()}<Connection><ConfigKey>{g},playposition</ConfigKey></Connection></Overview>")


def spinny(g, n, size):
    return (f"<Spinny><TooltipId>spinny</TooltipId><ObjectName>Spinny{n}</ObjectName><Size>{size}</Size>"
            f"<Group>{g}</Group>"
            f"<PathBackground scalemode=\"STRETCH\">img/spinny_bg.svg</PathBackground>"
            f"<PathForeground scalemode=\"STRETCH\">img/spinny_fg.svg</PathForeground>"
            f"<PathGhost scalemode=\"STRETCH\">img/spinny_ghost.svg</PathGhost>"
            f"<PathMask scalemode=\"STRETCH\">img/spinny_mask.svg</PathMask>"
            f"<ShowCover>true</ShowCover></Spinny>")


def vslider(cfg, size, tooltip, handle="handle_v.svg"):
    return (f"<SliderComposed><TooltipId>{tooltip}</TooltipId><Size>{size}</Size>"
            f"<Slider scalemode=\"STRETCH\">img/slider_v.svg</Slider>"
            f"<Handle scalemode=\"STRETCH_ASPECT\">img/{handle}</Handle><Horizontal>false</Horizontal>"
            f"<Connection><ConfigKey>{cfg}</ConfigKey></Connection></SliderComposed>")


def hslider(cfg, size, tooltip):
    return (f"<SliderComposed><TooltipId>{tooltip}</TooltipId><Size>{size}</Size>"
            f"<Slider scalemode=\"STRETCH\">img/slider_h.svg</Slider>"
            f"<Handle scalemode=\"STRETCH_ASPECT\">img/handle_h.svg</Handle><Horizontal>true</Horizontal>"
            f"<Connection><ConfigKey>{cfg}</ConfigKey></Connection></SliderComposed>")


def knob(cfg, size, tooltip):
    return (f"<KnobComposed><TooltipId>{tooltip}</TooltipId><Size>{size}</Size>"
            f"<Knob>img/knob.svg</Knob><BackPath>img/knob_bg.svg</BackPath>"
            f"<MinAngle>-135</MinAngle><MaxAngle>135</MaxAngle>"
            f"<Connection><ConfigKey>{cfg}</ConfigKey></Connection></KnobComposed>")


def vu(cfg, size, horizontal=False):
    h = "true" if horizontal else "false"
    bg, fg = ("vu_h_bg.svg", "vu_h.svg") if horizontal else ("vu_bg.svg", "vu.svg")
    return (f"<VuMeter><TooltipId>channel_VuMeter</TooltipId><Size>{size}</Size>"
            f"<PathBack scalemode=\"STRETCH\">img/{bg}</PathBack><PathVu scalemode=\"STRETCH\">img/{fg}</PathVu>"
            f"<Horizontal>{h}</Horizontal><PeakHoldSize>4</PeakHoldSize><PeakHoldTime>400</PeakHoldTime>"
            f"<PeakFallTime>40</PeakFallTime><PeakFallStep>2</PeakFallStep>"
            f"<Connection><ConfigKey>{cfg}</ConfigKey></Connection></VuMeter>")


def library(sidebar=True, sidebar_width=220):
    side = ""
    if sidebar:
        side = group(["<SearchBox></SearchBox>", "<LibrarySidebar></LibrarySidebar>"], "vertical", "LibSide",
                     f"{sidebar_width}f,0me", "f,me")
    else:
        side = ""
    table = "<Library><ShowButtonText>false</ShowButtonText><TrackTableBackgroundColorOpacity>0.15</TrackTableBackgroundColorOpacity></Library>"
    if sidebar:
        return group([side, table], "horizontal", "LibraryBox", None, "me,me")
    return group(["<SearchBox></SearchBox>", table], "vertical", "LibraryBox", None, "me,me")


def eq(g, n, size):
    return f"[EqualizerRack1_{g}_Effect1],parameter{n}"


def stems(g, size):
    names = [("DRM", "StemDrums"), ("BAS", "StemBass"), ("OTH", "StemOther"), ("VOX", "StemVocals")]
    base = g[:-1]
    return [button(t, n, f"{base}_Stem{i + 1}],mute", size, "toggle", tooltip=None) for i, (t, n) in enumerate(names)]


def good_mix(n, size):
    return button("GOOD\nMIX", "GoodMix", f"[GoodMixx],save_deck{n}", size.replace("60f","92f").replace("64f","92f"), "push",
                  show_key="[GoodMixx],saved_deck")

# ---------------------------------------------------------------- images


def svg(w, h, body):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">{body}</svg>'


def images(p):
    a, line, panel = p["accent"], p["line"], p["panel"]
    return {
        "knob.svg": svg(40, 40, f'<circle cx="20" cy="20" r="13" fill="{p["knob"]}" stroke="{line}" stroke-width="1.5"/>'
                                 f'<rect x="19" y="8" width="2" height="9" rx="1" fill="{p["text"]}"/>'),
        "knob_bg.svg": svg(40, 40, f'<circle cx="20" cy="20" r="18" fill="none" stroke="{line}" stroke-width="2.5"/>'),
        "slider_v.svg": svg(20, 200, f'<rect x="8" y="4" width="4" height="192" rx="2" fill="{line}"/>'),
        "handle_v.svg": svg(36, 22, f'<rect x="1" y="1" width="34" height="20" rx="4" fill="{p["handle"]}" stroke="{a}" stroke-width="2"/>'
                                     f'<rect x="4" y="10" width="28" height="2" fill="{a}"/>'),
        "handle_v_big.svg": svg(56, 30, f'<rect x="1" y="1" width="54" height="28" rx="6" fill="{p["handle"]}" stroke="{a}" stroke-width="3"/>'
                                         f'<rect x="6" y="14" width="44" height="2" fill="{a}"/>'),
        "slider_h.svg": svg(300, 20, f'<rect x="4" y="8" width="292" height="4" rx="2" fill="{line}"/>'
                                      f'<rect x="149" y="4" width="2" height="12" fill="{p["soft"]}"/>'),
        "handle_h.svg": svg(22, 36, f'<rect x="1" y="1" width="20" height="34" rx="4" fill="{p["handle"]}" stroke="{a}" stroke-width="2"/>'
                                     f'<rect x="10" y="4" width="2" height="28" fill="{a}"/>'),
        "spinny_bg.svg": svg(200, 200, f'<circle cx="100" cy="100" r="98" fill="#050505"/>'
                                        + "".join(f'<circle cx="100" cy="100" r="{r}" fill="none" stroke="#1d1d1d" stroke-width="1"/>' for r in range(40, 96, 6))
                                        + f'<circle cx="100" cy="100" r="34" fill="{panel}"/><circle cx="100" cy="100" r="3" fill="{p["soft"]}"/>'),
        "spinny_fg.svg": svg(200, 200, f'<rect x="97" y="4" width="6" height="40" rx="3" fill="{a}"/>'),
        "spinny_ghost.svg": svg(200, 200, f'<rect x="97" y="4" width="6" height="40" rx="3" fill="{a}" opacity="0.35"/>'),
        "spinny_mask.svg": svg(200, 200, '<circle cx="100" cy="100" r="34" fill="#000"/>'),
        "vu_bg.svg": svg(10, 100, f'<rect width="10" height="100" rx="2" fill="{line}"/>'),
        "vu.svg": svg(10, 100, '<defs><linearGradient id="g" x1="0" y1="1" x2="0" y2="0">'
                               '<stop offset="0" stop-color="#3ddc84"/><stop offset=".75" stop-color="#ffd400"/>'
                               '<stop offset="1" stop-color="#ff3b30"/></linearGradient></defs>'
                               '<rect width="10" height="100" rx="2" fill="url(#g)"/>'),
        "vu_h_bg.svg": svg(100, 10, f'<rect width="100" height="10" rx="2" fill="{line}"/>'),
        "vu_h.svg": svg(100, 10, '<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="0">'
                                 '<stop offset="0" stop-color="#3ddc84"/><stop offset=".75" stop-color="#ffd400"/>'
                                 '<stop offset="1" stop-color="#ff3b30"/></linearGradient></defs>'
                                 '<rect width="100" height="10" rx="2" fill="url(#g)"/>'),
    }

# ---------------------------------------------------------------- style


def style(p, big=1.0):
    fs = lambda v: int(round(v * big))
    a, a2 = p["accent"], p.get("accent2", p["accent"])
    return f"""/* Spin Deck skin: {p['title']}. Made by packaging/spindeck/make-skins.py */
#Mixxx, WWidget, QWidget#SkinBody {{ background-color: {p['bg']}; color: {p['text']}; }}
QWidget {{ font-family: "Archivo", "Open Sans", "Helvetica Neue", Arial, sans-serif; color: {p['text']}; }}
#Panel, #DeckPanel, #MixerPanel, #TopBar, #LibSide, #BottomBar {{ background-color: {p['panel']}; border: 1px solid {p['line']}; border-radius: 10px; }}
#TopBar {{ border-radius: 0px; border-width: 0px 0px 1px 0px; }}
#Brand {{ font-family: "Anton", "Impact", sans-serif; font-size: {fs(22)}px; color: {p['text']}; padding: 0px 10px; }}
#SkinName {{ font-size: {fs(12)}px; color: {p['soft']}; font-weight: bold; padding: 0px 6px; }}
#Title {{ font-size: {fs(17)}px; font-weight: bold; color: {p['text']}; }}
#Artist {{ font-size: {fs(14)}px; color: {p['soft']}; }}
#Bpm, #KeyText, #TimeText {{ font-size: {fs(20)}px; font-weight: bold; color: {p['text']}; background-color: {p['bg']}; border: 1px solid {p['line']}; border-radius: 6px; padding: 2px 6px; }}
#BigBpm, #BigKey, #BigTime {{ font-family: "Anton", "Impact", sans-serif; font-size: {fs(44)}px; color: {p['text']}; background-color: {p['bg']}; border: 2px solid {p['line']}; border-radius: 10px; }}
#BigTime {{ color: {a}; }}
#Clock, Time {{ font-family: "Anton", "Impact", sans-serif; font-size: {fs(22)}px; color: {p['text']}; }}
#BigClock {{ font-family: "Anton", "Impact", sans-serif; font-size: {fs(54)}px; color: {p['text']}; }}
#Small {{ font-size: {fs(11)}px; color: {p['soft']}; font-weight: bold; }}
WPushButton {{ background-color: {p['btn']}; color: {p['text']}; border: 1px solid {p['line']}; border-radius: 6px; font-weight: bold; font-size: {fs(12)}px; padding: 0px; }}
WPushButton:hover {{ border-color: {p['soft']}; }}
WPushButton[displayValue="1"] {{ background-color: {a}; color: {p['on_text']}; border-color: {a}; }}
#Play {{ font-size: {fs(16)}px; }}
#Loop {{ font-size: {fs(11)}px; }}
#GoodMix {{ font-size: {fs(10)}px; }}
#Play[displayValue="1"] {{ background-color: {p['play']}; border-color: {p['play']}; color: {p['on_text']}; }}
#Cue[displayValue="1"] {{ background-color: #ff8a00; border-color: #ff8a00; color: #000000; }}
#Sync[displayValue="1"] {{ background-color: {a2}; border-color: {a2}; color: {p['on_text']}; }}
#GoodMix {{ border: 2px solid #3ddc84; color: #3ddc84; }}
#GoodMix[displayValue="1"], #GoodMix:pressed {{ background-color: #3ddc84; color: #000000; }}
#Rec[displayValue="1"], #OnAir[displayValue="1"] {{ background-color: #ff3b30; border-color: #ff3b30; color: #ffffff; }}
#StemDrums[displayValue="0"] {{ color: #3ddc84; border-color: #3ddc84; }}
#StemBass[displayValue="0"] {{ color: #f2a65a; border-color: #f2a65a; }}
#StemOther[displayValue="0"] {{ color: #f0a6cf; border-color: #f0a6cf; }}
#StemVocals[displayValue="0"] {{ color: #8fcdf2; border-color: #8fcdf2; }}
#StemDrums[displayValue="1"], #StemBass[displayValue="1"], #StemOther[displayValue="1"], #StemVocals[displayValue="1"] {{ background-color: #5a1a1a; color: #ff8a80; border-color: #ff3b30; }}
WHotcueButton {{ background-color: {p['btn']}; border: 1px solid {p['line']}; border-radius: 6px; font-weight: bold; font-size: {fs(12)}px; color: {p['soft']}; }}
WHotcueButton[displayValue="1"], WHotcueButton[displayValue="2"] {{ color: #000000; background-color: {p['cue_set']}; }}
WTrackProperty, WLabel, WNumber {{ background: transparent; }}
WSearchLineEdit {{ background-color: {p['bg']}; color: {p['text']}; border: 1px solid {p['line']}; border-radius: 6px; padding: 4px 8px; font-size: {fs(13)}px; margin: 4px; }}
WLibrarySidebar {{ background-color: {p['panel']}; color: {p['text']}; border: none; font-size: {fs(13)}px; outline: none; }}
WLibrarySidebar::item:selected {{ background-color: {p['select']}; color: {p['text']}; }}
WLibraryTableView, WTrackTableView {{ background-color: {p['bg']}; alternate-background-color: {p['row2']}; color: {p['text']}; border: none; font-size: {fs(13)}px; selection-background-color: {p['select']}; selection-color: {p['text']}; gridline-color: {p['line']}; }}
QHeaderView {{ background-color: {p['panel']}; }}
QHeaderView::section {{ background-color: {p['panel']}; color: {p['soft']}; border: none; border-right: 1px solid {p['line']}; padding: 3px 6px; font-weight: bold; font-size: {fs(12)}px; }}
QScrollBar:vertical {{ background: {p['bg']}; width: 10px; }}
QScrollBar::handle:vertical {{ background: {p['line']}; border-radius: 4px; min-height: 30px; }}
QScrollBar:horizontal {{ background: {p['bg']}; height: 10px; }}
QScrollBar::handle:horizontal {{ background: {p['line']}; border-radius: 4px; min-width: 30px; }}
QScrollBar::add-line, QScrollBar::sub-line {{ width: 0px; height: 0px; }}
QMenu {{ background-color: {p['panel']}; color: {p['text']}; border: 1px solid {p['line']}; }}
QMenu::item:selected {{ background-color: {p['select']}; }}
QToolTip {{ background-color: {p['panel']}; color: {p['text']}; border: 1px solid {p['line']}; }}
QSplitter::handle {{ background-color: {p['bg']}; }}
"""

# ---------------------------------------------------------------- shared parts


def manifest(title, desc, extra_attrs=""):
    return (f"<manifest><title>{esc(title)}</title><author>XTRAGOODLABS</author><version>1.0</version>"
            f"<description>{esc(desc)}</description><language>en</language>"
            f"<license>GPL-2.0-or-later</license><attributes>"
            f"<attribute config_key=\"[App],num_decks\">2</attribute>"
            f"<attribute config_key=\"[App],num_samplers\">4</attribute>"
            f"<attribute config_key=\"[App],num_preview_decks\">1</attribute>"
            f"<attribute config_key=\"[Controls],ShowDurationRemaining\">1</attribute>"
            f"{extra_attrs}</attributes></manifest>")


def top_bar(p, name, height=40, clock_name="Clock", extra=()):
    return group([label("SPIN DECK", "Brand"), label(name, "SkinName"), spacer("0me,0f"), *extra,
                  "<Time><ObjectName>" + clock_name + "</ObjectName><Size>110f,0me</Size></Time>",
                  button("REC", "Rec", "[Recording],toggle_recording", "54f,28f", "push",
                         show_key="[Recording],status", tooltip="toggle_recording")],
                 "horizontal", "TopBar", f"0me,{height}f", "me,f")


def mixer(p, big=False):
    ks = "52f,52f" if big else "40f,40f"
    sv = "64f,220me" if big else "40f,150me"
    handle = "handle_v_big.svg" if big else "handle_v.svg"
    cols = []
    for d in (1, 2):
        g = f"[Channel{d}]"
        col = group([
            knob(f"{g},pregain", ks, "pregain"),
            knob(eq(g, 3, ks), ks, "filterHigh"),
            knob(eq(g, 2, ks), ks, "filterMid"),
            knob(eq(g, 1, ks), ks, "filterLow"),
            knob(f"[QuickEffectRack1_{g}],super1", ks, "QuickEffectRack_super1"),
            button("CUE", "Pfl", f"{g},pfl", "44f,24f" if not big else "56f,32f", "toggle", tooltip="pfl"),
        ], "vertical", None, None, "min,min")
        fader = group([vslider(f"{g},volume", sv, "channel_volume", handle), vu(f"{g},vu_meter", "8f,0me")],
                      "horizontal", None, None, "min,me")
        cols.append(col if d == 1 else fader)
        if d == 1:
            cols.append(fader)
        else:
            cols.append(col)
    xf = hslider("[Master],crossfader", "0me,36f" if not big else "0me,48f", "crossfader")
    master = group([knob("[Master],gain", ks, "main_gain"), knob("[Master],headMix", ks, "headMix"),
                    knob("[Master],headGain", ks, "headphone_gain")], "horizontal", None, None, "min,min")
    return group([group(cols, "horizontal", None, None, "min,me"), xf, master], "vertical", "MixerPanel", None, "min,me")


def transport(d, size, play_size=None, with_goodmix=True):
    g = f"[Channel{d}]"
    ps = play_size or size
    items = [button("CUE", "Cue", f"{g},cue_default", size, "push", show_key=f"{g},cue_indicator", tooltip="cue_default_cue_gotoandstop"),
             button("PLAY", "Play", f"{g},play", ps, "toggle", show_key=f"{g},play_indicator", tooltip="play_cue_set"),
             button("SYNC", "Sync", f"{g},sync_enabled", size, "toggle", tooltip="sync_enabled")]
    if with_goodmix:
        items.append(good_mix(d, size))
    return items


def loops(d, size):
    g = f"[Channel{d}]"
    return [button("½", "Loop", f"{g},loop_halve", size, "push", tooltip="loop_halve"),
            button("LOOP", "Loop", f"{g},beatloop_4_toggle", size.replace("44f", "74f").replace("60f", "96f"),
                   "push", show_key=f"{g},beatloop_4_enabled", tooltip="beatloop_activate"),
            button("2×", "Loop", f"{g},loop_double", size, "push", tooltip="loop_double")]

# ---------------------------------------------------------------- CRATE


def deck_crate(d, p):
    g = f"[Channel{d}]"
    info = group([
        group([prop(g, "title", "Title", "0me,24f"), bpm(g, "Bpm", "84f,30f"), key(g, "KeyText", "64f,30f"),
               time_pos(g, "TimeText", "124f,30f")], "horizontal", None, None, "me,min"),
        prop(g, "artist", "Artist", "0me,20f"),
        overview(g, p, d, "0me,46f"),
        group(transport(d, "60f,34f", "70f,34f") + [spacer("0me,0f")] + loops(d, "44f,34f"), "horizontal", None, None, "me,min"),
        group([hotcue(g, i, "0me,30f") for i in range(1, 9)], "horizontal", None, None, "me,min"),
        group(stems(g, "0me,26f"), "horizontal", None, None, "me,min"),
    ], "vertical", None, None, "me,min")
    rate = vslider(f"{g},rate", "36f,160me", "rate")
    parts = [spinny(g, d, "150f,150f"), info, rate] if d == 1 else [rate, info, spinny(g, d, "150f,150f")]
    return group(parts, "horizontal", "DeckPanel", None, "me,min")


def skin_crate():
    p = dict(title="CRATE", bg="#0b0b0b", panel="#141414", row2="#111111", line="#2c2c2c", text="#f2f2ee",
             soft="#a6a6a0", accent="#3ddc84", accent2="#3ddc84", play="#3ddc84", on_text="#000000",
             btn="#1b1b1b", select="#1f3a2a", cue_set="#3ddc84", knob="#1e1e1e", handle="#222222",
             wave_bg="#070707", beat="#2f2f2f", playpos="#ffffff", deck={1: "#3ddc84", 2: "#e8e8e2"})
    body = [
        top_bar(p, "CRATE"),
        group([waveform("[Channel1]", p, 1, "0me,110me"), waveform("[Channel2]", p, 2, "0me,110me")],
              "vertical", "Panel", None, "me,me"),
        group([deck_crate(1, p), mixer(p), deck_crate(2, p)], "horizontal", None, None, "me,min"),
        library(True, 230),
    ]
    xml = (f"<!DOCTYPE skin><skin>{manifest('Spin Deck CRATE', 'Spin Deck flagship: big waveforms, platters, GOOD MIX and stems buttons.')}"
           f"<ObjectName>Mixxx</ObjectName><Style src=\"skin:style.qss\"/><Size>1280me,780me</Size><Layout>vertical</Layout>"
           f"<Children>{group(body, 'vertical', 'SkinBody', None, 'me,me')}</Children></skin>")
    return "Spin Deck CRATE", p, xml, style(p)

# ---------------------------------------------------------------- BOOTH


def deck_booth(d, p):
    g = f"[Channel{d}]"
    big = group([bpm(g, "BigBpm", "0me,70f"), key(g, "BigKey", "0me,70f"), time_pos(g, "BigTime", "0me,70f")],
                "horizontal", None, None, "me,min")
    hot = group([hotcue(g, i, "0me,64f") for i in range(1, 5)], "horizontal", None, None, "me,min")
    hot2 = group([hotcue(g, i, "0me,64f") for i in range(5, 9)], "horizontal", None, None, "me,min")
    trans = group(transport(d, "0me,84f", "0me,84f"), "horizontal", None, None, "me,min")
    return group([prop(g, "title", "Title", "0me,30f"), big, trans, hot, hot2,
                  group(stems(g, "0me,40f"), "horizontal", None, None, "me,min")],
                 "vertical", "DeckPanel", None, "me,min")


def skin_booth():
    p = dict(title="BOOTH", bg="#000000", panel="#0c0c0c", row2="#080808", line="#333333", text="#ffffff",
             soft="#bdbdbd", accent="#ffd400", accent2="#00e5ff", play="#3ddc84", on_text="#000000",
             btn="#151515", select="#3a3200", cue_set="#ffd400", knob="#1a1a1a", handle="#1a1a1a",
             wave_bg="#000000", beat="#3a3a3a", playpos="#ffffff", deck={1: "#00e5ff", 2: "#ff3d7f"})
    lib = group([library(True, 200)], "vertical", None, "0me,240me", "me,me",
                shown_by("[SpinDeckBooth],show_library"))
    body = [
        top_bar(p, "BOOTH", 48, extra=(button("LIBRARY", "LibToggle", "[SpinDeckBooth],show_library", "110f,32f", "toggle"),)),
        waveform("[Channel1]", p, 1, "0me,150me"),
        waveform("[Channel2]", p, 2, "0me,150me"),
        group([deck_booth(1, p), mixer(p, big=True), deck_booth(2, p)], "horizontal", None, None, "me,min"),
        lib,
    ]
    attrs = '<attribute persist="true" config_key="[SpinDeckBooth],show_library">0</attribute>'
    xml = (f"<!DOCTYPE skin><skin>{manifest('Spin Deck BOOTH', 'Club skin: huge buttons and readouts, max contrast, library on demand.', attrs)}"
           f"<ObjectName>Mixxx</ObjectName><Style src=\"skin:style.qss\"/><Size>1280me,780me</Size><Layout>vertical</Layout>"
           f"<Children>{group(body, 'vertical', 'SkinBody', None, 'me,me')}</Children></skin>")
    return "Spin Deck BOOTH", p, xml, style(p, 1.35)

# ---------------------------------------------------------------- ON AIR


def deck_strip(d, p):
    g = f"[Channel{d}]"
    return group([
        label("A" if d == 1 else "B", "Brand", "28f,0me"),
        button("PLAY", "Play", f"{g},play", "70f,40f", "toggle", show_key=f"{g},play_indicator"),
        button("CUE", "Cue", f"{g},cue_default", "54f,40f", "push", show_key=f"{g},cue_indicator"),
        group([prop(g, "title", "Title", "0me,22f"), prop(g, "artist", "Artist", "0me,18f")], "vertical", None, "220f,0me", "f,me"),
        overview(g, p, d, "0me,44f"),
        time_pos(g, "TimeText", "100f,34f"),
        bpm(g, "Bpm", "76f,34f"),
        vslider(f"{g},volume", "34f,56f", "channel_volume"),
        vu(f"{g},vu_meter", "8f,56f"),
    ], "horizontal", "DeckPanel", "0me,64f", "me,f")


def skin_onair():
    p = dict(title="ON AIR", bg="#111316", panel="#1a1d22", row2="#15181c", line="#2d323a", text="#e9edf2",
             soft="#9aa3ad", accent="#4fa3ff", accent2="#4fa3ff", play="#3ddc84", on_text="#000000",
             btn="#22262c", select="#1f3550", cue_set="#4fa3ff", knob="#22262c", handle="#2a2f36",
             wave_bg="#0c0e10", beat="#2a2f36", playpos="#ffffff", deck={1: "#4fa3ff", 2: "#f2a65a"})
    head = group([
        label("SPIN DECK", "Brand"), label("ON AIR", "SkinName"),
        button("ON AIR", "OnAir", "[Microphone],talkover", "110f,44f", "toggle", tooltip="microphone_talkover"),
        knob("[Microphone],pregain", "40f,40f", "pregain"),
        vu("[Microphone],vu_meter", "120f,10f", horizontal=True),
        spacer("0me,0f"),
        button("AUTO DJ", "AutoDj", "[AutoDJ],enabled", "100f,40f", "toggle"),
        button("FADE NOW", "Loop", "[AutoDJ],fade_now", "100f,40f", "push"),
        button("SKIP", "Loop", "[AutoDJ],skip_next", "70f,40f", "push"),
        spacer("20f,0f"),
        "<Time><ObjectName>BigClock</ObjectName><Size>200f,0me</Size></Time>",
        button("REC", "Rec", "[Recording],toggle_recording", "60f,40f", "push", show_key="[Recording],status"),
    ], "horizontal", "TopBar", "0me,76f", "me,f")
    foot = group([deck_strip(1, p), deck_strip(2, p),
                  group([label("XFADE", "Small", "50f,0me"), hslider("[Master],crossfader", "0me,30f", "crossfader"),
                         label("MAIN", "Small", "40f,0me"), knob("[Master],gain", "36f,36f", "main_gain")],
                        "horizontal", None, "0me,40f", "me,f")],
                 "vertical", "BottomBar", None, "me,min")
    body = [head, library(True, 260), foot]
    xml = (f"<!DOCTYPE skin><skin>{manifest('Spin Deck ON AIR', 'Radio skin: library and Auto DJ first, on-air mic, big clock.')}"
           f"<ObjectName>Mixxx</ObjectName><Style src=\"skin:style.qss\"/><Size>1100me,700me</Size><Layout>vertical</Layout>"
           f"<Children>{group(body, 'vertical', 'SkinBody', None, 'me,me')}</Children></skin>")
    return "Spin Deck ON AIR", p, xml, style(p, 1.05)

# ---------------------------------------------------------------- POCKET


def deck_pocket(d, p):
    g = f"[Channel{d}]"
    return group([
        button("▶", "Play", f"{g},play", "40f,40f", "toggle", show_key=f"{g},play_indicator"),
        button("CUE", "Cue", f"{g},cue_default", "40f,40f", "push", show_key=f"{g},cue_indicator"),
        group([group([prop(g, "title", "Title", "0me,20f"), bpm(g, "Bpm", "62f,22f")], "horizontal", None, None, "me,min"),
               overview(g, p, d, "0me,28f")], "vertical", None, None, "me,min"),
        time_pos(g, "TimeText", "92f,28f"),
        button("SYNC", "Sync", f"{g},sync_enabled", "46f,40f", "toggle"),
        vslider(f"{g},volume", "28f,44f", "channel_volume"),
    ], "horizontal", "DeckPanel", "0me,52f", "me,f")


def skin_pocket():
    p = dict(title="POCKET", bg="#101010", panel="#1a1a1a", row2="#141414", line="#2e2e2e", text="#eeeeea",
             soft="#9e9e98", accent="#3ddc84", accent2="#3ddc84", play="#3ddc84", on_text="#000000",
             btn="#222222", select="#1f3a2a", cue_set="#3ddc84", knob="#222222", handle="#262626",
             wave_bg="#0a0a0a", beat="#2a2a2a", playpos="#ffffff", deck={1: "#3ddc84", 2: "#e8e8e2"})
    body = [
        group([label("SPIN DECK", "Brand"), label("POCKET", "SkinName"), spacer("0me,0f"),
               "<Time><ObjectName>Clock</ObjectName><Size>80f,0me</Size></Time>"], "horizontal", "TopBar", "0me,30f", "me,f"),
        deck_pocket(1, p), deck_pocket(2, p),
        group([label("A", "Small", "14f,0me"), hslider("[Master],crossfader", "0me,28f", "crossfader"),
               label("B", "Small", "14f,0me"), knob("[Master],gain", "32f,32f", "main_gain")],
              "horizontal", None, "0me,34f", "me,f"),
        library(False),
    ]
    xml = (f"<!DOCTYPE skin><skin>{manifest('Spin Deck POCKET', 'Mini skin: two decks, crossfader and browse in a small window.')}"
           f"<ObjectName>Mixxx</ObjectName><Style src=\"skin:style.qss\"/><Size>640me,420me</Size><Layout>vertical</Layout>"
           f"<Children>{group(body, 'vertical', 'SkinBody', None, 'me,me')}</Children></skin>")
    return "Spin Deck POCKET", p, xml, style(p, 0.92)

# ---------------------------------------------------------------- write


def main():
    for make in (skin_crate, skin_booth, skin_onair, skin_pocket):
        folder, p, xml, qss = make()
        out = os.path.join(SKINS, folder)
        os.makedirs(os.path.join(out, "img"), exist_ok=True)
        open(os.path.join(out, "skin.xml"), "w", encoding="utf-8").write(xml)
        open(os.path.join(out, "style.qss"), "w", encoding="utf-8").write(qss)
        for name, data in images(p).items():
            open(os.path.join(out, "img", name), "w", encoding="utf-8").write(data)
        print("wrote", folder)


if __name__ == "__main__":
    main()
