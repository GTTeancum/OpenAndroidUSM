// RE06: original hostage control calls and transient Audible query/play/stop.
// Source/asset fixtures only, not an ARM differential or an audible XAudio test.
#include "game/HostageSound.hpp"
#include "game/LevelHostageRuntime.hpp"
#include "game/LevelCinematicRuntime.hpp"
#include "game/PlayerStateConfig.hpp"
#include "reconstructed/input/XperiaKeyRouter.hpp"
#include "platform/input/XInputStateTranslator.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace usm::game;
using usm::Result;
std::size_t checks{};
void check(bool value, const char* text, int line) {
    ++checks;
    if (!value) { throw std::runtime_error("line " + std::to_string(line) + ": " + text); }
}
#define CHECK(x) check(static_cast<bool>(x), #x, __LINE__)

struct Sink {
    bool playing{}, culled{}, playFailure{}, stopFailure{};
    int queries{}, plays{}, stops{};
    int source{-1};
    std::vector<std::string> events;
    HostageSoundCallbacks callbacks{
        [this](std::uint16_t id) {
            CHECK(id == 0x18b); ++queries; events.push_back("query"); return playing;
        },
        [this](std::int32_t object, std::uint16_t id) {
            CHECK(id == 0x18b || id == 0xa3 || id == 0xa5);
            ++plays; source = object; events.push_back("play");
            if (playFailure) { return Result::failure("injected play failure"); }
            if (!culled) { playing = true; }
            return Result::success();
        },
        [this](std::uint16_t id) {
            CHECK(id == 0x18b); ++stops; events.push_back("stop");
            if (stopFailure) { return Result::failure("injected stop failure"); }
            playing = false; return Result::success();
        }
    };
};
void dispatchTests() {
    const HostageSoundCue query{30018,0x18b,HostageSoundAction::PlayOnceIfStopped};
    const HostageSoundCue stop{30018,0x18b,HostageSoundAction::Stop};
    Sink sink;
    CHECK(sink.callbacks.dispatch(query));
    CHECK((sink.events==std::vector<std::string>{"query","play"}));
    CHECK(sink.plays==1 && sink.playing);
    for (int i=0;i<32;++i) { CHECK(sink.callbacks.dispatch(query)); }
    CHECK(sink.queries==33 && sink.plays==1);
    sink.playing=false; // actual sink completion, not elapsed game time
    CHECK(sink.callbacks.dispatch(query)); CHECK(sink.plays==2);
    CHECK(sink.callbacks.dispatch(stop)); CHECK(!sink.playing);
    CHECK(sink.callbacks.dispatch(stop)); CHECK(sink.stops==2);
    // A culled successful request is NOT an active voice; retry may be needed.
    sink.culled=true;
    CHECK(sink.callbacks.dispatch(query)); CHECK(!sink.playing);
    CHECK(sink.callbacks.dispatch(query)); CHECK(sink.plays==4);
    sink.playFailure=true;
    CHECK(!sink.callbacks.dispatch(query)); CHECK(!sink.playing);
    sink.playFailure=false; sink.culled=false;
    CHECK(sink.callbacks.dispatch(query)); CHECK(sink.playing && sink.plays==6);
    // The native transient fallback has sound-media scope, not object scope.
    CHECK(sink.callbacks.dispatch({1234,0x18b,HostageSoundAction::PlayOnceIfStopped}));
    CHECK(sink.plays==6);
    CHECK(sink.callbacks.dispatch({1234,0x18b,HostageSoundAction::Stop}));
    CHECK(!sink.playing);
    CHECK(sink.callbacks.dispatch({30018,0xa3,HostageSoundAction::PlayOnce}));
    CHECK(sink.source==30018 && sink.plays==7);
    // Broken host bindings fail explicitly, not an invented playback state.
    HostageSoundCallbacks empty;
    CHECK(!empty.dispatch(query)); CHECK(!empty.dispatch(stop));
    CHECK(!empty.dispatch({30018,0xa5,HostageSoundAction::PlayOnce}));
    CHECK(!sink.callbacks.dispatch({30018,0x18b,static_cast<HostageSoundAction>(100)}));
}

struct Fixture {
    QuickTimeEventRuntime qte;
    LevelTriggerRuntime triggers;
    GameplayCamera camera;
    LevelCinematicRuntime controls;
    LevelObjectRuntime objects;
    LevelBonusRuntime bonuses;
    LevelHostageRuntime hostages;
    GameplayPlayer player;
    usm::reconstructed::XperiaKeyRouter keys;
    usm::platform::XInputStateTranslator raw;
    Sink sink;
    int resets{};
    std::vector<std::string> order;
    std::uint32_t now{10000};
    HostageUpdateHooks hooks;
    const LevelHostageState& state() const { return *hostages.find(30018); }
    Fixture(const LevelOneBootstrap& level, const PlayerStateConfigDatabase& states) {
        controls.bind(triggers,camera);
        controls.setInputResetHandler([this] { ++resets; keys.resetGameplayKeypad(); order.push_back("reset"); });
        qte.bind(level.buttonConfigs(),level.hud().interfaceAtlas,nullptr,&controls);
        CHECK(objects.initialize(level)); CHECK(bonuses.initialize(level.bonuses()));
        CHECK(hostages.initialize(level,qte,&objects));
        CHECK(player.initialize(level.player(),nullptr,&states));
        CHECK(hostages.find(30018) && state().asset);
        player.restoreAt(state().asset->position,{1,0,0});
        hooks.sound=[this](const HostageSoundCue& cue) {
            // Production bridge drains prior manager cues before the hostage sound.
            for (const auto id:qte.consumeSoundCues()) { order.push_back("qte:"+std::to_string(id)); }
            order.push_back(cue.action==HostageSoundAction::Stop ? "stop" : "query/play");
            return sink.callbacks.dispatch(cue);
        };
        hooks.enableControls=[this](bool enabled,bool preservePause) {
            order.push_back(enabled ? "enable" : "disable");
            CHECK(preservePause == !enabled);
            controls.enableControls(enabled,preservePause);
        };
    }
    Result tryStep(bool rescue=false) { return hostages.updateObjects(player,objects,bonuses,{0,0,now},rescue,hooks); }
    void step(bool rescue=false) { CHECK(tryStep(rescue)); }
    void poll(std::uint16_t buttons) {
        keys.beginFrame(); raw.update(true,buttons,0,0,[this](const auto& e){keys.route(e,usm::reconstructed::InputContext::Gameplay);});
    }
    void tap() { qte.update({now,0},true); hostages.observeQuickTime(); }
    void begin(bool activationTap=false) {
        step(); CHECK(hostages.contextPromptVisible());
        step(true); CHECK(player.activeStateId()==27);
        CHECK(!controls.controlsEnabled() && controls.pauseButtonEnabled());
        CHECK(resets==1); const auto n=resets;
        step(); CHECK(resets==n+1); // repeated native disable during rescue-start
        player.update({}, {}, 1000);
        poll(usm::platform::xinput::A);
        CHECK(keys.gameplayKeypad().jump.pressed && keys.state().quickTimeEvent.pressed);
        const auto before=resets;
        step(); CHECK(state().phase==HostageRescuePhase::QuickTime);
        CHECK(resets==before+2); // hostage disable, then outer BeginQTE disable
        CHECK(!keys.gameplayKeypad().jump.pressed && keys.state().quickTimeEvent.pressed);
        CHECK(qte.remainingActionCount()==8 && sink.queries==0 && sink.plays==0);
        if (activationTap) { tap(); }
    }
};

void recoveryTests(const LevelOneBootstrap& level,const PlayerStateConfigDatabase& states) {
    Fixture f(level,states); f.begin(true); CHECK(f.qte.remainingActionCount()==7);
    f.step(); CHECK(f.sink.plays==1 && f.sink.playing && f.sink.queries==1);
    CHECK(f.hostages.consumeSoundCues().empty()); // no double dispatch in bound path
    for (int i=0;i<20;++i) { f.step(); }
    CHECK(f.sink.plays==1 && f.sink.queries==21);
    f.sink.playing=false;
    f.step(); CHECK(f.sink.plays==2 && f.sink.queries==22);
    f.tap(); CHECK(f.qte.remainingActionCount()==6);
    f.sink.playing=false; f.step(); CHECK(f.sink.plays==2 && f.sink.queries==22);
    // Mash idle decay can return the native counter to 7: query again there.
    f.qte.update({f.now,500},false); f.hostages.observeQuickTime();
    CHECK(f.qte.remainingActionCount()==7);
    f.sink.culled=true; f.step(); CHECK(!f.sink.playing && f.sink.plays==3);
    f.step(); CHECK(!f.sink.playing && f.sink.plays==4);
    f.sink.playFailure=true; CHECK(!f.tryStep());
    CHECK(f.state().phase==HostageRescuePhase::QuickTime && f.qte.remainingActionCount()==7);
    f.sink.playFailure=false; f.sink.culled=false; f.step(); CHECK(f.sink.playing);
    auto saved=f.hostages; // no pointers to the call-scoped hooks or sink state
    f.sink.playing=false; f.hostages=saved; f.hostages.clearTransientCues();
    const auto plays=f.sink.plays; f.step(); CHECK(f.sink.plays==plays+1);
    for (int i=0;i<7;++i) { f.tap(); }
    CHECK(f.qte.state()==QteState::SuccessDisplay);
    f.sink.stopFailure=true; CHECK(!f.tryStep());
    CHECK(f.state().phase==HostageRescuePhase::QuickTime && f.player.activeStateId()==28);
    f.sink.stopFailure=false; f.step();
    CHECK(f.state().phase==HostageRescuePhase::RescueEnd && !f.sink.playing);
    const int stops=f.sink.stops;
    f.step(); f.step(); CHECK(f.sink.stops==stops+2);
    // Detached mode publishes repeatable queries, not asserted playback.
    Fixture detached(level,states); detached.begin(true);
    HostageUpdateHooks none;
    for(int i=0;i<2;++i) { CHECK(detached.hostages.updateObjects(detached.player,detached.objects,detached.bonuses,{0,0,10000},false,none)); }
    auto cmds=detached.hostages.consumeSoundCues();
    CHECK(cmds.size()==2 && cmds[0].action==HostageSoundAction::PlayOnceIfStopped && cmds[1].action==cmds[0].action);
}

void controlTests(const LevelOneBootstrap& level,const PlayerStateConfigDatabase& states) {
    for (const bool interrupt : {false,true}) {
        Fixture f(level,states); f.begin(true); const auto base=f.bonuses.states().size();
        f.order.clear(); auto before=f.resets;
        f.step(); CHECK(f.resets==before+1);
        CHECK(f.order.size()>=3 && f.order[0]=="disable" && f.order[1]=="reset");
        CHECK(!f.controls.controlsEnabled());
        if (interrupt) {
            CHECK(f.player.enterScriptedState(27,false));
            f.order.clear(); before=f.resets; f.step();
            CHECK(f.resets==before+2); // EndQTE + CHostage restoration, do not coalesce
            CHECK(f.qte.state()==QteState::FailureDisplay && f.player.activeStateId()==27);
            CHECK(f.state().phase==HostageRescuePhase::Tied && f.bonuses.states().size()==base);
            const auto cue=std::find(f.order.begin(),f.order.end(),"qte:393");
            const auto stop=std::find(f.order.begin(),f.order.end(),"stop");
            CHECK(cue!=f.order.end() && stop!=f.order.end() && cue<stop);
        } else {
            for (int i=0;i<7;++i) { f.tap(); }
            before=f.resets; f.step(); CHECK(f.resets==before);
            CHECK(f.state().phase==HostageRescuePhase::RescueEnd);
            CHECK(f.controls.controlsEnabled());
            f.player.update({}, {},1000); before=f.resets; f.step();
            CHECK(f.resets==before+1 && f.state().phase==HostageRescuePhase::Release);
            const auto rewardCount = static_cast<std::size_t>(
                f.state().asset->hostageHealthOrbCount +
                f.state().asset->hostageSkillPointOrbCount);
            CHECK(f.bonuses.states().size() == base + rewardCount);
            const auto priorPlays = f.sink.plays;
            f.objects.advanceAnimations(100000);
            f.step();
            CHECK(f.state().phase == HostageRescuePhase::Thank);
            CHECK(f.sink.plays == priorPlays + 1 && f.sink.source == 30018);
            CHECK(f.hostages.consumeSoundCues().empty());
            f.step();
            CHECK(f.sink.plays == priorPlays + 1);
            f.objects.advanceAnimations(100000);
            f.step();
            CHECK(f.state().phase == HostageRescuePhase::Freed);
            f.step();
            CHECK(f.bonuses.states().size() == base + rewardCount);
            CHECK(f.sink.plays == priorPlays + 1);
        }
        CHECK(f.controls.controlsEnabled() && f.controls.pauseButtonEnabled());
    }
    // A timeout restores via EndQTE and then the hostage failure branch.
    {
        Fixture timedOut(level, states);
        timedOut.begin(true);
        timedOut.step();
        const auto count = timedOut.bonuses.states().size();
        timedOut.qte.update({14001, 0}, false);
        timedOut.hostages.observeQuickTime();
        CHECK(timedOut.qte.state() == QteState::FailureDisplay);
        const auto resets = timedOut.resets;
        timedOut.step();
        CHECK(timedOut.resets == resets + 1);
        CHECK(timedOut.controls.controlsEnabled());
        CHECK(timedOut.state().phase == HostageRescuePhase::Tied);
        CHECK(!timedOut.sink.playing && timedOut.sink.stops == 1);
        CHECK(timedOut.bonuses.states().size() == count);
    }
    // Composed entry must forward hooks, not silently run detached.
    Fixture f(level,states); f.step();
    CHECK(f.hostages.update(f.player,f.objects,f.bonuses,{0,0,10000},{true,false},f.hooks));
    CHECK(f.resets==1 && !f.controls.controlsEnabled());
    // Interrupted start restores normal controls without starting QTE.
    f.player.restoreAt(f.state().asset->position,{1,0,0});
    const auto n=f.resets; f.step(); CHECK(f.resets==n+1);
    CHECK(f.controls.controlsEnabled() && f.state().phase==HostageRescuePhase::Tied);
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc!=2) { throw std::runtime_error("Select dispatch, recovery or controls"); }
        const std::string_view group=argv[1];
        if(group=="dispatch") { dispatchTests(); }
        else {
            LevelOneBootstrap level; PlayerStateConfigDatabase states;
            const auto root=std::filesystem::path(USM_TEST_GAME_DATA_ROOT);
            CHECK(level.load(root)); CHECK(states.load(root));
            if(group=="recovery") { recoveryTests(level,states); }
            else if(group=="controls") { controlTests(level,states); }
            else { throw std::runtime_error("Unknown group"); }
        }
        std::cout<<group<<": "<<checks<<" checks passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
