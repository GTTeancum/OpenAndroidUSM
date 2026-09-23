// RE05: original control-call ordering plus the explicitly separate PC keypad
// publication boundary. Reference fingerprints and addresses live in docs/.
// These tests execute native C++, not the original ARM instructions.
#include "game/LevelCinematicRuntime.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/LevelHostageRuntime.hpp"
#include "game/PlayerStateConfig.hpp"
#include "game/QuickTimeEventRuntime.hpp"
#include "platform/input/QteGamepadAdapter.hpp"
#include "platform/input/XInputStateTranslator.hpp"
#include "reconstructed/input/XperiaKeyRouter.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace usm::game;
using usm::platform::XInputStateTranslator;
using usm::platform::QteGamepadAdapter;
using usm::reconstructed::XperiaKeyRouter;
using usm::reconstructed::InputContext;
using usm::reconstructed::GameplayInputState;
namespace xi = usm::platform::xinput;
std::uint64_t checks{};
void check(bool value, const char* text, int line) {
    ++checks;
    if (!value) { throw std::runtime_error("line " + std::to_string(line) + ": " + text); }
}
#define CHECK(x) check(static_cast<bool>(x), #x, __LINE__)

struct Observation {
    std::string_view event;
    QteState state;
    int sprite;
    bool controls, pause, qteControl;
    float slow, hold;
};
struct Fixture final : QteControlHost {
    LevelTriggerRuntime triggers;
    GameplayCamera camera;
    LevelCinematicRuntime controls;
    QuickTimeEventRuntime qte;
    XperiaKeyRouter router;
    NativeRandomizer random;
    std::vector<Observation> observations;
    std::vector<std::uint16_t> atDisableCues;
    explicit Fixture(const LevelOneBootstrap& level) {
        observations.reserve(4096); atDisableCues.reserve(4096);
        controls.bind(triggers, camera);
        controls.setInputResetHandler([this]() noexcept {
            observe("keys");
            router.resetGameplayKeypad();
        });
        qte.bind(level.buttonConfigs(), level.hud().interfaceAtlas, &random, this);
    }
    void observe(std::string_view event) noexcept {
        observations.push_back({event, qte.state(), qte.resultSprite().animationId(),
            controls.controlsEnabled(), controls.pauseButtonEnabled(),
            controls.quickTimeControlEnabled(), controls.slowMotionDenominator(),
            controls.slowMotionHoldMilliseconds()});
    }
    void beginQuickTimeEvent() noexcept override {
        observe("begin"); controls.beginQuickTimeEvent();
    }
    void setQuickTimeControlEnabled(bool enabled) noexcept override {
        if (!enabled) {
            auto cues = qte.consumeSoundCues();
            atDisableCues.insert(atDisableCues.end(), cues.begin(), cues.end());
        }
        controls.setQuickTimeControlEnabled(enabled);
        observe(enabled ? "qte-on" : "qte-off");
    }
    void endQuickTimeEvent() noexcept override {
        observe("end"); controls.endQuickTimeEvent();
    }
    void clear() { observations.clear(); atDisableCues.clear(); }
    void event(std::size_t i, std::string_view name, QteState state) const {
        CHECK(i < observations.size());
        CHECK(observations[i].event == name && observations[i].state == state);
    }
};

void finish(QuickTimeEventRuntime& qte) {
    for (unsigned i=0; i<50 && qte.visible(); ++i) {
        qte.update({10000, 50}, false); qte.drawStep();
    }
    CHECK(!qte.visible());
}
void prepare(Fixture& f, QteState state) {
    if (state != QteState::Inactive) {
        const int id = state == QteState::Drag ? 6 : state == QteState::Mash ? 11 : 9;
        CHECK(f.qte.begin(id,{10000,0},20006,20010,20004));
        if (state == QteState::SuccessDisplay || state == QteState::SuccessHandled) {
            f.qte.update({10000,0},true);
            if (state == QteState::SuccessHandled) { finish(f.qte); }
        } else if (state == QteState::FailureDisplay || state == QteState::FailureHandled) {
            f.qte.forceFail();
            if (state == QteState::FailureHandled) { finish(f.qte); }
        }
    }
    CHECK(f.qte.state() == state);
    (void)f.qte.consumeSoundCues();
    (void)f.qte.consumeCinematicRequest(); (void)f.qte.consumeSourceRemoval();
    f.clear();
}

void lifecycle(const LevelOneBootstrap& level) {
    // Native BeginQTE's pre-guard EnableControls(false,true) happens even for
    // refused requests. The manager's config, state and generation stay put.
    for (int s=-1; s<=6; ++s) {
        for (int id : {6,9,11}) {
            Fixture f(level); const auto state=static_cast<QteState>(s);
            prepare(f,state);
            f.router.publishGameplayStick(.75F,-.75F);
            const auto generation=f.qte.generation();
            const auto resets=f.router.gameplayKeypadResetCount();
            const auto buttonResets=f.controls.quickTimeButtonResetCount();
            const auto pauseResets=f.controls.pauseButtonResetCount();
            const auto pause=f.controls.pauseButtonEnabled();
            const auto config=f.qte.configId();
            CHECK(f.qte.begin(id,{12000,0},81,82,83));
            const bool accepted=s<0 || s>3;
            CHECK(f.observations.size()==(accepted?3U:2U));
            f.event(0,"begin",state); f.event(1,"keys",state);
            CHECK(!f.observations[1].controls && f.observations[1].pause==pause);
            CHECK(f.router.gameplayKeypadResetCount()==resets+1);
            CHECK(f.router.gameplayStickX()==0 && f.router.gameplayStickY()==0);
            CHECK(f.controls.pauseButtonResetCount()==pauseResets);
            CHECK(f.controls.quickTimeButtonResetCount()==buttonResets+(accepted?1:0));
            CHECK(f.qte.generation()==generation+(accepted?1:0));
            CHECK(f.qte.configId()==(accepted?id:config));
            CHECK(!f.controls.controlsEnabled());
            if (accepted) {
                f.event(2,"qte-on",f.qte.state());
                CHECK(f.observations[2].qteControl);
            } else {
                f.clear();
                CHECK(f.qte.begin(32000,{20000,0})); // no lookup while rejected
                CHECK(f.observations.size()==2);
                f.event(0,"begin",state); f.event(1,"keys",state);
                CHECK(f.qte.configId()==config && f.qte.generation()==generation);
            }
            CHECK(!f.qte.consumeControlRelease());
        }
    }
    for (bool success : {false,true}) {
        Fixture f(level);
        CHECK(f.qte.begin(9,{10000,0},20006,20010,20004));
        f.controls.setSlowMotion(4,1500,900,true);
        (void)f.controls.consumeSlowMotionSoundCues();
        f.clear();
        if (success) { f.qte.update({10000,0},true); }
        else { f.qte.forceFail(); }
        CHECK(f.observations.size()==3);
        // SetState disables the QTE button, initializes its result sprite,
        // then EndQTE resets slow motion and controls before committing state.
        f.event(0,"qte-off",QteState::Tap);
        f.event(1,"end",QteState::Tap);
        f.event(2,"keys",QteState::Tap);
        CHECK(!f.observations[0].qteControl);
        CHECK(f.observations[1].sprite==(success?7:1));
        CHECK(f.observations[1].slow==4 && f.observations[1].hold==1500);
        CHECK(f.observations[2].slow==1 && f.observations[2].hold==0);
        CHECK(f.observations[2].controls && f.observations[2].pause);
        CHECK(f.qte.state()==(success?QteState::SuccessDisplay:QteState::FailureDisplay));
        CHECK(f.controls.controlsEnabled() && !f.controls.quickTimeControlEnabled());
        CHECK(f.atDisableCues.back()==(success?0x188:0x189));
        CHECK(f.atDisableCues.size()==(success?2U:1U));
        CHECK((f.controls.consumeSlowMotionSoundCues()==std::vector<SlowMotionSoundCue>{SlowMotionSoundCue::Exit}));
        CHECK(!f.qte.consumeControlRelease());
        f.controls.enableControls(false,false); // later authored UI change
        CHECK(!f.qte.consumeControlRelease()); // no stale release can replay
        CHECK(!f.controls.controlsEnabled());
        if (!success) {
            f.clear(); f.qte.forceFail();
            CHECK(f.observations.empty()); // SetState's same-state guard
        }
    }
    {
        Fixture late(level); CHECK(late.qte.begin(11,{10000,0}));
        for(int i=0;i<7;++i){late.qte.update({10000,0},true);}
        late.clear(); const auto before=late.router.gameplayKeypadResetCount();
        late.qte.update({14001,0},true);
        CHECK(late.qte.state()==QteState::FailureDisplay);
        CHECK(late.observations.size()==6);
        late.event(0,"qte-off",QteState::Mash); late.event(1,"end",QteState::Mash);
        late.event(2,"keys",QteState::Mash);
        late.event(3,"qte-off",QteState::SuccessDisplay);
        late.event(4,"end",QteState::SuccessDisplay); late.event(5,"keys",QteState::SuccessDisplay);
        CHECK(late.router.gameplayKeypadResetCount()==before+2);
        CHECK(late.atDisableCues.size()==10 && late.atDisableCues[8]==0x188 && late.atDisableCues[9]==0x189);
        CHECK(!late.qte.consumeControlRelease());
    }
    // Compound BeginNowQTE children advance clock/config but do not run outer
    // Begin's enable/reset calls a second time. Direction adaptation unchanged.
    Fixture f(level);
    CHECK(f.qte.begin(2,{10000,0}));
    CHECK(f.qte.configId()==0);
    const auto resets=f.router.gameplayKeypadResetCount();
    const auto qr=f.controls.quickTimeButtonResetCount();
    f.clear();
    for (const int expected : {3,1}) {
        QteInput input; input.dragDirection=f.qte.gesturePath().direction();
        f.qte.update({10000,0},input);
        CHECK(f.qte.configId()==expected && f.qte.state()==QteState::Drag);
        CHECK(f.observations.empty());
        CHECK(f.router.gameplayKeypadResetCount()==resets);
        CHECK(f.controls.quickTimeButtonResetCount()==qr);
    }
    QteInput last; last.dragDirection=f.qte.gesturePath().direction();
    f.qte.update({10000,0},last);
    CHECK(f.qte.state()==QteState::SuccessDisplay);
    CHECK(f.router.gameplayKeypadResetCount()==resets+1);
    CHECK(f.controls.quickTimeButtonResetCount()==qr+1);
    CHECK(!f.qte.consumeControlRelease());
}

CinematicCommand interfaceCommand(bool enabled) {
    CinematicCommand result; result.name="InterfaceControl";
    result.attributes={{"bool","ControlEnable",enabled?"true":"false"},
        {"bool","AttributionEnable","false"},{"bool","ArrowEnable","false"},
        {"bool","BlackEnable","true"},{"bool","SkipEnable","true"}};
    return result;
}
void hostageControls(const LevelOneBootstrap& level) {
    PlayerStateConfigDatabase states;
    CHECK(states.load(std::filesystem::path(USM_TEST_GAME_DATA_ROOT)));
    for (bool interrupt : {false,true}) {
        Fixture f(level);
        LevelObjectRuntime objects; LevelBonusRuntime bonuses;
        LevelHostageRuntime hostages; GameplayPlayer player;
        CHECK(objects.initialize(level)); CHECK(bonuses.initialize(level.bonuses()));
        CHECK(hostages.initialize(level,f.qte,&objects));
        CHECK(player.initialize(level.player(),nullptr,&states));
        const auto* captive=hostages.find(30018);
        CHECK(captive && captive->asset);
        player.restoreAt(captive->asset->position,{1,0,0});
        const auto bonusCount=bonuses.states().size();
        const auto objectStep=[&](bool rescue=false) {
            CHECK(hostages.updateObjects(player,objects,bonuses,{0,0,10000},rescue));
        };
        XInputStateTranslator raw;
        const auto poll=[&](std::uint16_t buttons) {
            f.router.beginFrame();
            raw.update(true,buttons,0,0,[&](const auto& e){f.router.route(e,InputContext::Gameplay);});
        };
        objectStep(); CHECK(hostages.contextPromptVisible());
        poll(xi::RightShoulder); poll(0);
        CHECK(f.router.state().rescueRequested);
        objectStep(f.router.state().rescueRequested);
        CHECK(player.activeStateId()==27);
        player.update({}, {}, 1000);
        poll(xi::A); // exact update on which CHostage calls BeginQTE
        CHECK(f.router.gameplayKeypad().jump.pressed && f.router.state().quickTimeEvent.pressed);
        objectStep();
        CHECK(captive->phase==HostageRescuePhase::QuickTime && f.qte.configId()==11);
        CHECK(!f.router.gameplayKeypad().jump.pressed);
        CHECK(f.router.state().quickTimeEvent.pressed && f.router.state().quickTimeEvent.held);
        CHECK(!f.controls.controlsEnabled() && f.controls.quickTimeControlEnabled());
        const auto managerStep=[&] {
            auto used=f.qte.update({10000,0},f.router.state().quickTimeEvent.pressed);
            if(used.quickTimePress){f.router.consumeQuickTimePress();}
            if(used.jumpPress){f.router.consumeJumpPress();}
            if(used.rescuePress){f.router.consumeRescueRequest();}
            hostages.observeQuickTime();
        };
        managerStep();
        CHECK(captive->completedActions==1);
        CHECK(!f.router.state().quickTimeEvent.pressed && f.router.state().quickTimeEvent.held);
        if(interrupt) {
            CHECK(player.enterScriptedState(27,false));
            objectStep();
            CHECK(player.activeStateId()==27 && captive->phase==HostageRescuePhase::Tied);
            CHECK(f.qte.state()==QteState::FailureDisplay);
            CHECK(bonuses.states().size()==bonusCount);
        } else {
            for(int i=0;i<7;++i){poll(0);poll(xi::A);objectStep();managerStep();}
            CHECK(f.qte.state()==QteState::SuccessDisplay);
            CHECK(captive->quickTimeOutcome==HostageQteOutcome::Success);
            CHECK(player.activeStateId()==28); // next object update, not eager
            objectStep(); CHECK(player.activeStateId()==29);
            CHECK(captive->phase==HostageRescuePhase::RescueEnd);
        }
        CHECK(f.controls.controlsEnabled() && !f.controls.quickTimeControlEnabled());
        CHECK(!f.qte.consumeControlRelease() && !hostages.consumeControlRelease());
    }
}

void integration(const LevelOneBootstrap& level) {
    hostageControls(level);
    // Ratio 1 is a real native boundary, not a blanket reset request.
    for (const float ratio : {1.0F,1.000001F,2.0F,16.0F}) {
        Fixture f(level);
        CHECK(f.controls.applyCommand(interfaceCommand(false)));
        f.controls.setSlowMotion(ratio,1500,900,true);
        (void)f.controls.consumeSlowMotionSoundCues();
        f.controls.endQuickTimeEvent();
        CHECK(f.controls.controlsEnabled() && f.controls.pauseButtonEnabled());
        CHECK(!f.controls.attributionEnabled() && !f.controls.objectiveArrowEnabled());
        CHECK(f.controls.blackOverlayEnabled() && f.controls.skipEnabled());
        CHECK(f.controls.slowMotionDenominator()==1);
        CHECK(f.controls.slowMotionHoldMilliseconds()==(ratio>1?0:1500));
        const auto cues=f.controls.consumeSlowMotionSoundCues();
        CHECK(cues.size()==(ratio>1?1U:0U));
        CHECK(f.observations.back().slow==1);
        CHECK(f.observations.back().hold==(ratio>1?0:1500));
    }
    Fixture f(level);
    CHECK(f.controls.applyCommand(interfaceCommand(false)));
    const auto pauseResets=f.controls.pauseButtonResetCount();
    CHECK(!f.controls.pauseButtonEnabled());
    const auto before=f.router.gameplayKeypadResetCount();
    CHECK(f.qte.begin(9,{10000,0}));
    CHECK(!f.controls.pauseButtonEnabled()); // preserve disabled as well as enabled
    CHECK(f.controls.pauseButtonResetCount()==pauseResets);
    f.qte.update({10000,0},true);
    CHECK(f.controls.pauseButtonEnabled());
    CHECK(f.controls.pauseButtonResetCount()==pauseResets+1);
    CHECK(f.router.gameplayKeypadResetCount()==before+2);
    // Native EnableControls has no same-value early return.
    f.controls.enableControls(true); f.controls.enableControls(true);
    CHECK(f.router.gameplayKeypadResetCount()==before+4);
    CHECK(f.controls.pauseButtonResetCount()==pauseResets+3);
    // A nested Begin in SuccessHandle observes SuccessDisplay. Its pre-guard
    // reset/disable must run, and must not be erased by a delayed EndQTE.
    unsigned handoffs=0; f.clear();
    bool nestedOk=false;
    const QteHandoffHandler handoff=[&](const QteCinematicHandoff&) noexcept {
        ++handoffs;
        nestedOk=f.qte.state()==QteState::SuccessDisplay &&
                 static_cast<bool>(f.qte.begin(6,{10000,0}));
    };
    for (int i=0;i<9;++i) { f.qte.drawStep(handoff); }
    CHECK(handoffs==1 && nestedOk);
    CHECK(f.qte.state()==QteState::SuccessHandled && f.qte.configId()==9);
    CHECK(!f.controls.controlsEnabled() && f.controls.pauseButtonEnabled());
    CHECK(!f.qte.consumeControlRelease());
    CHECK(f.observations.size()==2);
    f.event(0,"begin",QteState::SuccessDisplay); f.event(1,"keys",QteState::SuccessDisplay);
    // Checkpoint value copies keep the level host binding, not a local lambda
    // or pointer to the copied manager. The application's host is a stable member.
    QuickTimeEventRuntime saved=f.qte;
    f.qte=saved;
    f.clear();
    CHECK(f.qte.begin(11,{10000,0}));
    CHECK(f.controls.quickTimeControlEnabled());
    f.qte.forceFail();
    CHECK(!f.controls.quickTimeControlEnabled() && f.controls.controlsEnabled());
    CHECK(!f.qte.consumeControlRelease());

    // Exercise the real level-one drag config through production raw XInput
    // normalization and the approved diagonal stick policy after control reset.
    for (bool correct : {false,true}) {
        Fixture g(level); XInputStateTranslator raw; QteGamepadAdapter adapter;
        const auto route=[&](const auto& e){g.router.route(e,InputContext::Gameplay);};
        raw.update(true,0,0,0,route);
        (void)adapter.translate(g.qte,true,false,0,0);
        CHECK(g.qte.begin(6,{10000,0},20006,20010,20004));
        auto direction=g.qte.gesturePath().direction();
        std::int16_t x=32767,y=32767;
        if(direction==QteDirection::Left){x=-32767;}
        if(direction==QteDirection::Down){y=-32767;}
        if(!correct){x=static_cast<std::int16_t>(-x); y=static_cast<std::int16_t>(-y);}
        raw.update(true,correct?0:xi::A,x,y,route);
        const auto stick=raw.leftStick();
        g.router.publishGameplayStick(stick.x,stick.y);
        g.router.resetGameplayKeypad();
        CHECK(g.router.gameplayStickX()==0 && g.router.gameplayStickY()==0);
        CHECK(raw.leftStick().x==stick.x && raw.leftStick().y==stick.y);
        const auto input=adapter.translate(g.qte,true,g.router.state().quickTimeEvent.pressed,
                                           raw.leftStick().x,raw.leftStick().y);
        g.qte.update({10050,50},input);
        if(correct) { CHECK(g.qte.state()==QteState::SuccessDisplay); }
        else {
            CHECK(g.qte.state()==QteState::Drag);
            g.qte.update({13901,0},false);
            CHECK(g.qte.state()==QteState::FailureDisplay);
        }
        CHECK(g.controls.controlsEnabled() && !g.controls.quickTimeControlEnabled());
        CHECK(!g.qte.consumeControlRelease());
        finish(g.qte);
        CHECK(g.qte.consumeCinematicRequest()==(correct?20006:20010));
        CHECK(g.qte.consumeSourceRemoval()==20004);
    }
}

// Compare members, never memcmp a C++ struct with padding bytes.
void same(const GameplayInputState& a, const GameplayInputState& b) {
    const auto action=[](const auto& x,const auto& y){
        CHECK(x.held==y.held && x.pressed==y.pressed && x.released==y.released);
        CHECK(x.pressedFramesRemaining==y.pressedFramesRemaining);
    };
    action(a.jump,b.jump); action(a.web,b.web); action(a.punch,b.punch);
    action(a.spiderSense,b.spiderSense); action(a.superAttack,b.superAttack);
    action(a.moveUp,b.moveUp); action(a.moveDown,b.moveDown);
    action(a.moveLeft,b.moveLeft); action(a.moveRight,b.moveRight);
    action(a.upgrade,b.upgrade); action(a.upgradeProceed,b.upgradeProceed);
    action(a.pause,b.pause); action(a.menuSelected,b.menuSelected);
    CHECK(a.quickTimeEvent.held==b.quickTimeEvent.held &&
          a.quickTimeEvent.pressed==b.quickTimeEvent.pressed &&
          a.quickTimeEvent.released==b.quickTimeEvent.released);
    CHECK(a.rescueRequested==b.rescueRequested && a.switchRequested==b.switchRequested &&
          a.switchRequestValue==b.switchRequestValue);
}
void keypad() {
    // Exhaust all wButtons bit patterns, including unused/reserved bits. This
    // checks PC adapter isolation, NOT execution of a physical controller/API.
    for (unsigned mask=0;mask<=65535;++mask) {
        XperiaKeyRouter router; XInputStateTranslator raw;
        const auto route=[&](const auto& e){router.route(e,InputContext::Gameplay);};
        raw.update(true,xi::RightShoulder,0,0,route);
        router.beginFrame();
        raw.update(true,static_cast<std::uint16_t>(mask),25000,-25000,route);
        auto physical=raw.leftStick();
        router.publishGameplayStick(physical.x,physical.y);
        same(router.state(),router.gameplayKeypad());
        const auto before=router.state();
        router.resetGameplayKeypad();
        same(router.state(),before); same(router.gameplayKeypad(),{});
        CHECK(raw.connected() && raw.leftStick().x==physical.x && raw.leftStick().y==physical.y);
        CHECK(router.gameplayStickX()==0 && router.gameplayStickY()==0);
        CHECK(router.gameplayKeypadResetCount()==1);
        router.beginFrame();
        raw.update(true,static_cast<std::uint16_t>(mask),25000,-25000,route);
        router.publishGameplayStick(raw.leftStick().x,raw.leftStick().y);
        same(router.gameplayKeypad(),{}); // held hardware is not a new down
        CHECK(router.state().quickTimeEvent.held==before.quickTimeEvent.held);
        CHECK(!router.state().quickTimeEvent.pressed);
        CHECK(router.gameplayStickX()==physical.x && router.gameplayStickY()==physical.y);
        // Only a real release and fresh press publishes the keypad again.
        router.beginFrame(); raw.update(true,0,0,0,route);
        router.beginFrame(); raw.update(true,static_cast<std::uint16_t>(mask),0,0,route);
        auto expectedKeypad=router.state();
        // The earlier R1-up payload remains in the direct native-event view
        // after its request flag is consumed. A reset keypad does not own that
        // dormant payload; only a later genuine R1-up publishes one there.
        CHECK(!expectedKeypad.rescueRequested && !expectedKeypad.switchRequested);
        CHECK(expectedKeypad.switchRequestValue==4);
        expectedKeypad.switchRequestValue=(mask & xi::RightShoulder)?4:0;
        same(router.gameplayKeypad(),expectedKeypad);
        router.beginFrame();
        raw.update(false,0,0,0,route);
        same(router.gameplayKeypad(),{}); same(router.state(),{});
        CHECK(!raw.connected() && !router.state().rescueRequested);
    }
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc!=2){throw std::runtime_error("Expected lifecycle, integration, or keypad");}
        const std::string_view group=argv[1];
        if(group=="keypad"){keypad();}
        else {
            LevelOneBootstrap level;
            CHECK(level.load(std::filesystem::path(USM_TEST_GAME_DATA_ROOT)));
            if(group=="lifecycle"){lifecycle(level);}
            else if(group=="integration"){integration(level);}
            else{throw std::runtime_error("Unknown control group");}
        }
        std::cout<<"PASS "<<group<<": "<<checks<<" checks\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
