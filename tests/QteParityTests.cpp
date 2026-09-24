#include "game/GameplayCinematicScheduler.hpp"
#include "game/HostageAttributes.hpp"
#include "game/QteSpriteAnimation.hpp"
#include "game/QteFeedbackGeometry.hpp"
#include "platform/input/QteGamepadAdapter.hpp"
#include "platform/input/XInputStateTranslator.hpp"
#include "reconstructed/input/XperiaKeyRouter.hpp"
#include "diagnostics/AutoplayHarness.hpp"
#include "audio/VoxSoundTable.hpp"
#include "audio/SoundEventCatalog.hpp"
#include "game/LevelHostageRuntime.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/PlayerStateConfig.hpp"
#include "game/QteClock.hpp"
#include "game/QuickTimeEventRuntime.hpp"
#include "game/WallWebTiming.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::uint64_t checks = 0;
void check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        throw std::runtime_error("line " + std::to_string(line) + ": " + expression);
    }
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)
using namespace usm::game;

// Independent instruction-shaped reference. Round after each VFP operation;
// this is a transcription oracle, not execution of the original ARM program.
float round32(double value) { return static_cast<float>(value); }
struct ClockReference {
    float start{}, marker{}, adjustment{};
    void begin(std::uint32_t t) { start = round32(t); adjustment = 0; }
    void pause(bool paused, std::uint32_t t) {
        if (paused) {
            const float elapsed = round32(static_cast<double>(round32(t)) - start);
            marker = round32(static_cast<double>(round32(t)) - elapsed);
        } else {
            adjustment = round32(static_cast<double>(round32(t)) - marker);
            marker = 0;
        }
    }
    float elapsed(std::uint32_t t) {
        const float a = round32(static_cast<double>(round32(t)) - start);
        return round32(static_cast<double>(a) - adjustment);
    }
};

void clockTests() {
    QteClock clock;
    clock.begin(1000);
    CHECK(clock.elapsed(4999) == 3999.0F);
    CHECK(!clock.expired(5000, 4000.0F));
    CHECK(clock.expired(5001, 4000.0F));
    CHECK(clock.displayElapsed(100, 4000) == 0);
    CHECK(clock.displayElapsed(9000, 4000) == 4000);
    clock.setPause(true, 1800);
    clock.setPause(false, 3800);
    CHECK(clock.elapsed(3800) == 0.0F); // Native formula, not conventional pause.
    CHECK(!clock.expired(7800, 4000.0F));
    CHECK(clock.expired(7801, 4000.0F));
    clock.setPause(true, 4000);
    clock.setPause(false, 5000);
    CHECK(clock.elapsed(5000) == 0.0F); // Adjustment is overwritten, not summed.
    clock.begin(10000);
    CHECK(clock.elapsed(10123) == 123.0F);
    clock.begin(0x01000000U);
    CHECK(clock.elapsed(0x01000001U) == 0.0F); // uint->float ties-to-even.
    CHECK(clock.elapsed(0x01000002U) == 2.0F);
    CHECK(!clock.expired(0x01000001U, 0.5F));
    CHECK(clock.expired(0x01000002U, 0.5F));
    clock.begin(0xfffffff0U);
    CHECK(clock.elapsed(0x20U) < 0.0F); // Native is not modular elapsed.
    CHECK(!clock.expired(0x20U, 4000.0F));
    clock.begin(0);
    CHECK(!clock.expired(1234, 1234.5F));
    CHECK(clock.expired(1235, 1234.5F)); // No rounding of the configured duration.

    std::uint32_t rng = 0x7c32a119U;
    const auto next = [&] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    const std::array<std::uint32_t, 12> edges{0, 1, 499, 500, 4000, 4001,
        0x00ffffff, 0x01000000, 0x01000001, 0x7fffffff, 0xfffffff0, 0xffffffff};
    for (std::uint32_t run = 0; run < 10000; ++run) {
        QteClock subject;
        ClockReference reference;
        const auto start = run < edges.size() ? edges[run] : next();
        subject.begin(start); reference.begin(start);
        for (int operation = 0; operation < 20; ++operation) {
            const auto now = next();
            switch (next() % 8) {
                case 0: subject.begin(now); reference.begin(now); break;
                case 1: subject.setPause(true, now); reference.pause(true, now); break;
                case 2: subject.setPause(false, now); reference.pause(false, now); break;
                default: break;
            }
            const float expected = reference.elapsed(now);
            CHECK(std::bit_cast<std::uint32_t>(subject.elapsed(now)) ==
                  std::bit_cast<std::uint32_t>(expected));
            const float duration = static_cast<float>(next() % 100000) + 0.5F;
            CHECK(subject.expired(now, duration) == (expected > duration));
        }
    }
    ButtonMashProgress progress;
    for (int i = 0; i < 7; ++i) { CHECK(!progress.updateManager(0, true, 8, true)); }
    CHECK(progress.updateManager(500, true, 8, true));
    CHECK(progress.completed() == 7); // Success tested before same-update decay.
    progress.reset();
    CHECK(!progress.updateManager(0, true, 8, true));
    CHECK(!progress.updateManager(1500, false, 8, true));
    CHECK(progress.completed() == 0); // At most one decay, no catch-up loop.
    CHECK(!progress.updateManager(0, true, 8, false));
    CHECK(!progress.updateManager(10000, false, 8, false));
    CHECK(progress.completed() == 1); // Ordinary tap QTEs have no mash decay.

    // Player::UpdateQTE uses the same manager timer/order for wall-web.
    CHECK(advanceWallWebPromptClock(2999, 1) == 3000);
    CHECK(!wallWebPromptExpired(3000, 3000.0F));
    CHECK(advanceWallWebPromptClock(3000, 1) == 3001);
    CHECK(wallWebPromptExpired(3001, 3000.0F));
    CHECK(wallWebPromptOutcome(true, 3000, 3000.0F) ==
          WallWebPromptOutcome::Success);
    CHECK(wallWebPromptOutcome(true, 3001, 3000.0F) ==
          WallWebPromptOutcome::Failure); // late eighth press loses
    CHECK(wallWebPromptOutcome(false, 3000, 3000.0F) ==
          WallWebPromptOutcome::Running);
    CHECK(advanceWallWebPromptClock(
              std::numeric_limits<std::uint32_t>::max() - 10U, 50U) ==
          std::numeric_limits<std::uint32_t>::max());
}

CinematicCommand startCommand(int id) {
    CinematicCommand command;
    command.name = "StartQTE";
    command.attributes = {{"int", "QTEID", std::to_string(id)},
        {"int", "^ID^Cinematic^Success", "20006"},
        {"int", "^ID^Cinematic^Fail", "20010"}};
    return command;
}

void finishFeedback(QuickTimeEventRuntime& qte, std::uint32_t now,
                    const QteHandoffHandler& handoff = {}) {
    for (int i = 0; i < 40 && qte.visible(); ++i) {
        qte.update({now, 50}, false, handoff);
        qte.drawStep(handoff);
    }
    CHECK(!qte.visible());
}
void cinematicTests(const LevelOneBootstrap& level) {
    CHECK(level.buttonConfigs().find(6)->interactionType == 1);
    CHECK(level.buttonConfigs().find(6)->durationMilliseconds == 3900.0F);
    CHECK(level.buttonConfigs().find(9)->interactionType == 0);
    QuickTimeEventRuntime qte;
    qte.bind(level.buttonConfigs(), level.hud().interfaceAtlas);
    CHECK(qte.applyCommand(startCommand(9), {10000, 0}, 20005));
    qte.update({13900, 1}, false);
    CHECK(qte.active() && qte.elapsedMilliseconds() == 3900);
    qte.update({13900, 50}, true);
    CHECK(!qte.active() && qte.state() == QteState::SuccessDisplay);
    CHECK(!qte.consumeCinematicRequest());
    CHECK(qte.consumeControlRelease() && !qte.consumeControlRelease());
    CHECK((qte.consumeSoundCues() == std::vector<std::uint16_t>{0x18a, 0x188}));
    // Success Draw compares before increment: 0..8 are nine presentations.
    for (int i = 0; i < 8; ++i) {
        qte.drawStep();
        CHECK(qte.state() == QteState::SuccessDisplay);
        qte.update({13900, 1000}, false); // Updates alone do not count draws.
        CHECK(!qte.consumeCinematicRequest());
    }
    qte.drawStep();
    CHECK(qte.state() == QteState::SuccessHandled);
    CHECK(qte.consumeCinematicRequest() == 20006);
    CHECK(!qte.consumeCinematicRequest());
    CHECK(qte.applyCommand(startCommand(9), {10000, 0}, 20005));
    qte.update({13901, 1}, true);
    CHECK(qte.state() == QteState::FailureDisplay);
    CHECK((qte.consumeSoundCues() == std::vector<std::uint16_t>{0x18a, 0x188, 0x189}));
    finishFeedback(qte, 13901);
    CHECK(qte.consumeCinematicRequest() == 20010);
    CHECK(qte.applyCommand(startCommand(9), {10000, 0}, 20005));
    qte.setPause(true, 10900);
    qte.setPause(false, 13000);
    qte.update({16900, 50}, false);
    CHECK(qte.active());
    qte.update({16901, 1}, false);
    CHECK(qte.state() == QteState::FailureDisplay);
    finishFeedback(qte, 16901);
    CHECK(qte.consumeCinematicRequest() == 20010);

    CHECK(qte.applyCommand(startCommand(11), {20000, 0}));
    qte.update({20000, 0}, true);
    CHECK(qte.completedActionCount() == 1);
    qte.update({20000, 499}, false);
    CHECK(qte.completedActionCount() == 1 && qte.elapsedMilliseconds() == 0);
    qte.update({20000, 1}, false);
    CHECK(qte.completedActionCount() == 0 && qte.elapsedMilliseconds() == 0);
    qte.update({24001, 50}, false);
    CHECK(qte.state() == QteState::FailureDisplay);
    finishFeedback(qte, 24001);
    CHECK(qte.consumeCinematicRequest() == 20010);
    CHECK(qte.applyCommand(startCommand(11), {20000, 0}));
    for (int i = 0; i < 7; ++i) { qte.update({20000, 0}, true); }
    qte.update({24001, 0}, true);
    CHECK(qte.completedActionCount() == 8);
    CHECK(qte.state() == QteState::FailureDisplay); // late eighth tap loses
    finishFeedback(qte, 24001);
    CHECK(qte.consumeCinematicRequest() == 20010);
    CHECK(!qte.applyCommand(startCommand(32000), {0, 0}));
    CHECK(!qte.applyCommand(startCommand(14), {0, 0})); // no invented private RNG
}

void gestureTests(const LevelOneBootstrap& level) {
    constexpr std::array<int, 4> ids{0, 4, 10, 6};
    for (const auto id : ids) {
        QteGesturePath path;
        CHECK(path.bind(*level.buttonConfigs().find(id), level.hud().interfaceAtlas));
        CHECK(path.samples().size() == 14);
        // Exhaust every signed-short coordinate of the deciding axis. The
        // ignored axis varies too. Independent scalar nearest-point oracle.
        for (int v = -32768; v <= 32767; ++v) {
            const auto other = static_cast<std::int16_t>(-1 - v);
            const QtePoint input = path.horizontal()
                ? QtePoint{static_cast<std::int16_t>(v), other}
                : QtePoint{other, static_cast<std::int16_t>(v)};
            int closest = 100000; std::size_t expected = 0;
            for (std::size_t i = 0; i < path.samples().size(); ++i) {
                const auto p = path.samples()[i];
                const int d = std::abs(v - (path.horizontal() ? p.x : p.y));
                if (d < closest) { closest = d; expected = i; }
            }
            CHECK(path.nearest(input) == expected);
        }
        const auto end = path.end();
        CHECK(path.releaseSucceeded({static_cast<std::int16_t>(end.x+10), static_cast<std::int16_t>(end.y-10)}));
        CHECK(!path.releaseSucceeded({static_cast<std::int16_t>(end.x+11), end.y}));
        CHECK(!path.releaseSucceeded({end.x, static_cast<std::int16_t>(end.y-11)}));
    }
    QuickTimeEventRuntime qte;
    qte.bind(level.buttonConfigs(), level.hud().interfaceAtlas);
    CHECK(qte.applyCommand(startCommand(6), {10000, 0}));
    CHECK((qte.gesturePath().start() == QtePoint{360, 101}));
    CHECK((qte.gesturePath().end() == QtePoint{360, 251}));
    qte.update({10050, 50}, true);
    CHECK(qte.state() == QteState::Drag); // A-only shortcut is gone.
    qte.update({10100, 50}, QteInput{false, QtePoint{360,246}, false});
    CHECK(qte.state() == QteState::Drag);
    CHECK((qte.buttonPosition() == QtePoint{367,242}));
    qte.update({10150, 50}, QteInput{false, QtePoint{-32000,247}, false});
    CHECK(qte.state() == QteState::SuccessDisplay); // Y nearest, X ignored then snapped.
    CHECK((qte.buttonPosition() == QtePoint{360,251}));
    finishFeedback(qte, 10150);
    CHECK(qte.consumeCinematicRequest() == 20006);

    CHECK(qte.applyCommand(startCommand(6), {20000, 0}));
    qte.update({23901, 50}, QteInput{false, QtePoint{360,251}, false});
    CHECK(qte.state() == QteState::SuccessDisplay); // timeout THEN nearest success
    CHECK((qte.consumeSoundCues() == std::vector<std::uint16_t>{0x188,0x189,0x188}));
    finishFeedback(qte, 23901);
    CHECK(qte.consumeCinematicRequest() == 20006);
    CHECK(qte.applyCommand(startCommand(6), {30000, 0}));
    qte.update({30050, 50}, QteInput{false, QtePoint{371,251}, true});
    CHECK(qte.state() == QteState::FailureDisplay); // release uses BOTH axes
    finishFeedback(qte, 30050);
    CHECK(qte.consumeCinematicRequest() == 20010);
    CHECK(qte.applyCommand(startCommand(6), {40000, 0}));
    qte.update({40050, 50}, QteInput{false, QtePoint{370,241}, true});
    CHECK(qte.state() == QteState::SuccessDisplay); // inclusive 10x10 box
}

void spriteTests(const LevelOneBootstrap& level) {
    QteSpriteAnimation sprite;
    sprite.bind(level.hud().interfaceAtlas);
    sprite.setAnimation(1, true);
    sprite.update(50);
    CHECK(sprite.frameIndex() == 0 && sprite.ticks() == 0 && sprite.residualMilliseconds() == 50);
    sprite.update(0);
    CHECK(sprite.frameIndex() == 0);
    sprite.update(1);
    CHECK(sprite.frameIndex() == 1 && sprite.ticks() == 0 && sprite.residualMilliseconds() == 1);
    sprite.update(100);
    CHECK(sprite.frameIndex() == 3 && sprite.ticks() == 0 && sprite.residualMilliseconds() == 1);
    sprite.setAnimation(1);
    CHECK(sprite.frameIndex() == 3); // same SetAnim does not restart
    sprite.setAnimation(1, true);
    CHECK(sprite.frameIndex() == 0 && sprite.residualMilliseconds() == 1);
    sprite.update(49);
    CHECK(sprite.frameIndex() == 0 && sprite.residualMilliseconds() == 50);
    sprite.update(1);
    CHECK(sprite.frameIndex() == 1 && sprite.residualMilliseconds() == 1);
    sprite.setAnimation(29, true);
    CHECK(sprite.ended()); // one frame of duration 1, counter==duration-1
    // Independent instruction transcription exercised against original metadata.
    struct Ref {
        int anim{}, frame{}, ticks{}; float residual{}; bool looped{};
        void set(int id, bool safe) {
            if (anim != id || safe) { anim=id; frame=ticks=0; looped=false; }
            looped=false;
        }
    } ref;
    sprite.bind(level.hud().interfaceAtlas);
    std::uint32_t rng=0x543b629d;
    const auto next=[&] {rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng;};
    const std::array<int,9> animations{0,1,2,3,4,5,6,7,29};
    for (int n=0;n<100000;++n) {
        if ((next()%7)==0) {
            const auto id=animations[next()%animations.size()]; const bool safe=next()&1;
            sprite.setAnimation(static_cast<std::int16_t>(id),safe); ref.set(id,safe);
        }
        const std::uint32_t dt=next()%201;
        const auto& anim=level.hud().interfaceAtlas.animations()[ref.anim];
        const auto& frame=level.hud().interfaceAtlas.animationFrames()[anim.firstFrameIndex+ref.frame];
        if (frame.duration && ref.ticks>=0) {
            ref.residual+=static_cast<float>(dt); int steps=0;
            while(ref.residual>50) {ref.residual-=50; ++steps; ++ref.ticks;}
            if(frame.duration<=ref.ticks) {
                ref.frame+=steps; ref.ticks=0;
                if(ref.frame>=anim.frameCount) {ref.frame=0;ref.looped=true;}
            }
        }
        sprite.update(dt);
        CHECK(sprite.frameIndex()==ref.frame);
        CHECK(sprite.ticks()==ref.ticks);
        CHECK(sprite.residualMilliseconds()==ref.residual);
        CHECK(sprite.looped()==ref.looped);
        const auto& f=level.hud().interfaceAtlas.animationFrames()[anim.firstFrameIndex+ref.frame];
        CHECK(sprite.ended()==(ref.looped || (ref.frame==anim.frameCount-1 &&
            (!f.duration || ref.ticks==f.duration-1))));
    }
}

void sequenceTests(const LevelOneBootstrap& level) {
    {
        QuickTimeEventRuntime sequence;
        sequence.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
        CHECK(sequence.applyCommand(startCommand(2),{1000,0}));
        const auto retainedEnd=sequence.gesturePath().end();
        sequence.update({1050,50},QteInput{false,retainedEnd,false});
        CHECK(sequence.configId()==3);
        CHECK(sequence.gesturePath().end()!=retainedEnd);
        // Same-state SetState(Drag) did not recompute the release endpoint.
        sequence.update({1100,0},QteInput{false,retainedEnd,true});
        CHECK(sequence.configId()==1 && sequence.state()==QteState::Drag);
    }

    NativeRandomizer random, reference;
    QuickTimeEventRuntime qte;
    qte.bind(level.buttonConfigs(), level.hud().interfaceAtlas, &random);
    CHECK(qte.applyCommand(startCommand(2), {10000,0}));
    CHECK(qte.configId()==0 && qte.durationMilliseconds()==3000);
    const auto originalEnd=qte.gesturePath().end();
    const auto initialGeneration=qte.generation();
    qte.update({10500,50}, QteInput{false,originalEnd,false});
    CHECK(qte.configId()==3 && qte.durationMilliseconds()==1300);
    CHECK(qte.generation()==initialGeneration+1);
    CHECK(qte.elapsedMilliseconds()==0);
    CHECK(qte.resultSprite().animationId()==2); // SetState(Drag) was a no-op
    qte.update({10600,50}, QteInput{false,qte.gesturePath().end(),false});
    CHECK(qte.configId()==1 && qte.durationMilliseconds()==2500);
    qte.update({10700,50}, QteInput{false,qte.gesturePath().end(),false});
    CHECK(qte.state()==QteState::SuccessDisplay);
    finishFeedback(qte,10700);
    CHECK(qte.consumeCinematicRequest()==20006);
    CHECK(random.state()==reference.state()); // ordinary/drag do not draw RNG
    CHECK(qte.applyCommand(startCommand(14),{20000,0}));
    const int expectedX=240+reference.range(0,240)-120;
    const int expectedY=160+reference.range(0,160)-80;
    CHECK(qte.buttonPosition().x==expectedX && qte.buttonPosition().y==expectedY);
    CHECK(random.state()==reference.state());
    // An active BeginQTE must not replace the current interaction or consume RNG.
    const auto generation=qte.generation();
    CHECK(qte.applyCommand(startCommand(19),{20010,0}));
    CHECK(qte.configId()==14 && qte.generation()==generation);
    CHECK(random.state()==reference.state());
    qte.update({20050,50},true); finishFeedback(qte,20050);
    (void)qte.consumeCinematicRequest();
    CHECK(qte.applyCommand(startCommand(19),{21000,0}));
    (void)reference.range(0,240);(void)reference.range(0,160);
    CHECK(random.state()==reference.state());
    qte.update({21050,50},true);
    CHECK(qte.state()==QteState::SuccessDisplay);
    qte.update({21100,50},false);
    CHECK(qte.state()==QteState::SuccessHandled); // photo's one-frame result ends via Update
}

void gamepadTests(const LevelOneBootstrap& level) {
    using Pad = usm::platform::QteGamepadAdapter;
    QuickTimeEventRuntime qte;
    qte.bind(level.buttonConfigs(), level.hud().interfaceAtlas);
    Pad pad;
    CHECK(qte.applyCommand(startCommand(6), {1000,0}));
    auto input = pad.translate(qte, true, true, 0, 0);
    CHECK(!input.actionPressed && !input.dragPosition && !input.dragReleased);
    CHECK(input.dragDirection == QteDirection::None);
    qte.update({1050,50}, input); CHECK(qte.active());
    input = pad.translate(qte, true, true, 0, 1); // opposite direction and A ignored
    qte.update({1100,50}, input); CHECK(qte.active());
    input = pad.translate(qte, true, false, 0.7F, -0.7F); // diagonal, no A
    CHECK(input.dragDirection == QteDirection::Down);
    CHECK(!input.dragPosition && !input.dragReleased && !input.actionPressed);
    qte.update({1150,50}, input); CHECK(qte.state() == QteState::SuccessDisplay);
    CHECK((qte.consumeSoundCues() == std::vector<std::uint16_t>{0x188}));
    finishFeedback(qte,1150); CHECK(qte.consumeCinematicRequest() == 20006);
    CHECK(qte.applyCommand(startCommand(6), {2000,0}));
    input = pad.translate(qte, true, false, 0, -1); // held across activation is not new
    CHECK(input.dragDirection == QteDirection::None);
    qte.update({2050,50}, input); CHECK(qte.active());
    (void)pad.translate(qte, true, false, 0, 0);
    input = pad.translate(qte, true, false, 0, -0.75F);
    qte.update({2100,50}, input); CHECK(qte.state() == QteState::SuccessDisplay);
    finishFeedback(qte,2100); (void)qte.consumeCinematicRequest();
    CHECK(qte.applyCommand(startCommand(6), {3000,0}));
    (void)pad.translate(qte, true, false, 0, 0);
    input = pad.translate(qte, false, true, 0, -1);
    CHECK(!input.dragPosition && !input.dragReleased && !input.actionPressed);
    CHECK(input.dragDirection == QteDirection::None);
    input = pad.translate(qte, true, false, 0, -1);
    CHECK(input.dragDirection == QteDirection::None); // reconnect needs neutral
    input = pad.translate(qte, true, false, 0, 0);
    CHECK(input.dragDirection == QteDirection::None);
    input = pad.translate(qte, true, false, 0, -1);
    CHECK(input.dragDirection == QteDirection::Down);

    // The application diagnostic harness now uses exactly this adapter too.
    usm::diagnostics::AutoplayHarness autoplay;
    input = autoplay.quickTimeInput(qte, true);
    CHECK(!input.dragPosition && !input.dragReleased && !input.actionPressed);
    CHECK(input.dragDirection == QteDirection::None);
    qte.update({3050,50}, input); CHECK(qte.active());
    input = autoplay.quickTimeInput(qte, false);
    CHECK(input.dragDirection == QteDirection::Down);
    CHECK(!input.dragPosition && !input.dragReleased && !input.actionPressed);
    qte.update({3100,50}, input); CHECK(qte.state() == QteState::SuccessDisplay);

    // All shipped drag records, not just the first-level DOWN instruction.
    int directions = 0;
    for (const int id : {0,4,10,6}) {
        QuickTimeEventRuntime directional;
        directional.bind(level.buttonConfigs(), level.hud().interfaceAtlas);
        CHECK(directional.applyCommand(startCommand(id),{10000,0}));
        Pad controller;
        (void)controller.translate(directional,true,false,0,0);
        float x=0,y=0;
        const auto direction=directional.gesturePath().direction();
        switch(direction) {
        case QteDirection::Left: x=-0.8F; y=0.4F; directions|=1; break;
        case QteDirection::Right: x=0.8F; y=-0.4F; directions|=2; break;
        case QteDirection::Up: x=0.4F; y=0.8F; directions|=4; break;
        case QteDirection::Down: x=-0.4F; y=-0.8F; directions|=8; break;
        case QteDirection::None: CHECK(false); break;
        }
        const auto prompt=Pad::prompt(direction);
        CHECK(!prompt.empty() && prompt.find(u"[A]")==std::u16string_view::npos);
        CHECK(prompt.find(u"Hold")==std::u16string_view::npos);
        input=controller.translate(directional,true,false,x,y);
        CHECK(input.dragDirection==direction && !input.dragPosition);
        directional.update({10050,50},input);
        CHECK(directional.state()==QteState::SuccessDisplay);
    }
    CHECK(directions==15);

    // A remains required for tap/mash; stick intent cannot decrement actions.
    for (const int id : {11,14}) {
        NativeRandomizer random;
        QuickTimeEventRuntime tap;
        tap.bind(level.buttonConfigs(),level.hud().interfaceAtlas,&random);
        CHECK(tap.applyCommand(startCommand(id),{20000,0}));
        Pad controller;
        input=controller.translate(tap,true,false,0,-1);
        CHECK(input.dragDirection==QteDirection::None && !input.actionPressed);
        tap.update({20050,50},input); CHECK(tap.completedActionCount()==0);
        input=controller.translate(tap,true,true,0,-1);
        CHECK(input.actionPressed && input.dragDirection==QteDirection::None);
        tap.update({20100,50},input); CHECK(tap.completedActionCount()==1);
    }
}

void controllerBoundaryTests(const LevelOneBootstrap& level) {
    using Pad=usm::platform::QteGamepadAdapter;
    CHECK(!Pad::matchesDirection(QteDirection::None,1,0));
    CHECK(Pad::matchesDirection(QteDirection::Right,Pad::EngageMagnitude,0));
    CHECK(!Pad::matchesDirection(QteDirection::Right,
        std::nextafter(Pad::EngageMagnitude,0.0F),0));
    for (const float invalid : {std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        CHECK(!Pad::matchesDirection(QteDirection::Right,invalid,0));
        CHECK(!Pad::matchesDirection(QteDirection::Down,0,invalid));
    }
    // Analytical angle oracle, independent of the squared-dot implementation.
    constexpr double pi=3.14159265358979323846;
    const std::array<QteDirection,4> dirs{QteDirection::Right,QteDirection::Up,
                                        QteDirection::Left,QteDirection::Down};
    for (int d=0;d<4;++d) {
        for (int halfDegrees=-360;halfDegrees<=360;++halfDegrees) {
            const double relative=halfDegrees*0.5;
            if(std::abs(std::abs(relative)-60.0)<0.001) {continue;}
            const double angle=(d*90.0+relative)*pi/180.0;
            CHECK(Pad::matchesDirection(dirs[d],
                static_cast<float>(0.9*std::cos(angle)),
                static_cast<float>(0.9*std::sin(angle)))==(std::abs(relative)<60));
        }
    }
    QuickTimeEventRuntime qte;
    qte.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
    CHECK(qte.applyCommand(startCommand(6),{1000,0}));
    Pad pad;
    (void)pad.translate(qte,true,false,0,-1); // never observed neutral
    CHECK(!pad.armed());
    (void)pad.translate(qte,true,false,0,Pad::NeutralMagnitude+0.001F);
    CHECK(!pad.armed());
    (void)pad.translate(qte,true,false,0,Pad::NeutralMagnitude);
    CHECK(pad.armed());
    auto input=pad.translate(qte,true,false,0,-Pad::EngageMagnitude);
    CHECK(input.dragDirection==QteDirection::Down && !pad.armed());
    for(int i=0;i<100;++i) {
        input=pad.translate(qte,true,true,0,-1);
        CHECK(input.dragDirection==QteDirection::None && !input.actionPressed);
    }
    // Invalid data must not manufacture the neutral observation needed to rearm.
    CHECK(!pad.translate(qte,true,true,std::numeric_limits<float>::quiet_NaN(),0).actionPressed);
    CHECK(pad.translate(qte,true,false,0,-1).dragDirection==QteDirection::None);
    (void)pad.translate(qte,true,false,0,0);
    CHECK(pad.translate(qte,true,false,0,-1).dragDirection==QteDirection::Down);

    // A fresh movement at activation counts when the immediately prior sample
    // was neutral, even though BeginNowQTE changed the generation meanwhile.
    QuickTimeEventRuntime newQte; newQte.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
    Pad fresh;
    (void)fresh.translate(newQte,true,false,0,0);
    CHECK(newQte.applyCommand(startCommand(6),{5000,0}));
    CHECK(fresh.translate(newQte,true,false,0,-1).dragDirection==QteDirection::Down);

    // All 65,536 raw signed axis values for each authored direction. This
    // drives the production radial-deadzone translator and intent adapter;
    // 21,554 is the first integer above 7,849 + .55*(32,767-7,849).
    for (const int id : {0,4,10,6}) {
        QuickTimeEventRuntime axisQte;
        axisQte.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
        CHECK(axisQte.applyCommand(startCommand(id),{25000,0}));
        const auto direction=axisQte.gesturePath().direction();
        const bool horizontal=direction==QteDirection::Left||direction==QteDirection::Right;
        const bool positive=direction==QteDirection::Right||direction==QteDirection::Up;
        usm::platform::XInputStateTranslator raw;
        Pad axisPad;
        for (int value=-32768; value<=32767; ++value) {
            (void)axisPad.translate(axisQte,true,false,0,0);
            raw.update(true,0,horizontal?static_cast<std::int16_t>(value):0,
                horizontal?0:static_cast<std::int16_t>(value),{});
            const auto stick=raw.leftStick();
            const auto event=axisPad.translate(axisQte,raw.connected(),true,stick.x,stick.y);
            const bool expected=positive?value>=21554:value<=-21554;
            CHECK((event.dragDirection==direction)==expected);
            CHECK(!event.actionPressed && !event.dragPosition && !event.dragReleased);
            // A held deflection, even after the first accepted event, is not
            // another gesture. An incorrect/weak deflection also stays inert.
            const auto held=axisPad.translate(axisQte,true,true,stick.x,stick.y);
            CHECK(held.dragDirection==QteDirection::None);
        }
    }

    // Original strict timeout and input order are unchanged by adaptation.
    for(const std::uint32_t elapsed:{3899U,3900U,3901U}) {
        QuickTimeEventRuntime timed; timed.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
        CHECK(timed.applyCommand(startCommand(6),{10000,0}));
        Pad controller; (void)controller.translate(timed,true,false,0,0);
        timed.update({10000+elapsed,50},controller.translate(timed,true,false,0,-1));
        CHECK(timed.state()==QteState::SuccessDisplay);
        const auto sounds=timed.consumeSoundCues();
        CHECK(sounds==(elapsed>3900?std::vector<std::uint16_t>{0x189,0x188}
                                  :std::vector<std::uint16_t>{0x188}));
    }
    // No gesture has a time extension, including device loss.
    for(const bool connected:{true,false}) {
        QuickTimeEventRuntime timed; timed.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
        CHECK(timed.applyCommand(startCommand(6),{10000,0}));
        Pad controller;
        timed.update({13900,50},controller.translate(timed,connected,true,0,1));
        CHECK(timed.state()==QteState::Drag);
        timed.update({13901,1},controller.translate(timed,connected,true,0,1));
        CHECK(timed.state()==QteState::FailureDisplay);
    }

    // Compound [0,3,1]: retained held direction, A, and native same-state
    // animation quirks cannot complete a second child without another gesture.
    QuickTimeEventRuntime sequence;sequence.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
    CHECK(sequence.applyCommand(startCommand(2),{20000,0}));
    Pad controller; std::uint32_t now=20000;
    for(const int child:{0,3,1}) {
        CHECK(sequence.configId()==child && sequence.state()==QteState::Drag);
        // A fresh neutral frame is necessary for each authored child.
        sequence.update({++now,0},controller.translate(sequence,true,true,0,0));
        const auto target=sequence.gesturePath().direction();
        float x=0,y=0;
        if(target==QteDirection::Left)x=-1;
        if(target==QteDirection::Right)x=1;
        if(target==QteDirection::Up)y=1;
        if(target==QteDirection::Down)y=-1;
        input=controller.translate(sequence,true,true,x,y);
        CHECK(input.dragDirection==target && !input.actionPressed);
        sequence.update({++now,0},input);
        input=controller.translate(sequence,true,true,x,y);
        CHECK(input.dragDirection==QteDirection::None);
        if(sequence.active()) {
            const auto before=sequence.configId();
            sequence.update({++now,0},input); CHECK(sequence.configId()==before);
        }
    }
    CHECK(sequence.state()==QteState::SuccessDisplay);
}

void firstLevelFlowTests(const LevelOneBootstrap& level, bool controllerInput = false) {
    // This tuple comes from the actual linked first-level CFF, not the
    // inherited smoke fixture's synthetic source ID 20005. Five further CFFs
    // containing StartQTE exist in the pack but their rooms are NOT linked by
    // levelnew_01.irr; do not silently promote unused assets into gameplay.
    struct Authored { int source, config, success, failure, stamp; };
    constexpr std::array records{Authored{20004,6,20006,20010,2000}};
    CHECK(level.mainScene().linkedSceneFiles().size()==13);
    for(const int unusedSource:{20016,20045,20051,20062,20081}) {
        CHECK(std::none_of(level.cinematics().begin(),level.cinematics().end(),
            [&](const auto& c) {return c.objectId==unusedSource;}));
    }
    int actualCommands=0;
    for(const auto& asset:level.cinematics()) {
        for(const auto& thread:asset.script.threads()) {
            for(const auto& command:thread.commands) {
                if(command.name=="StartQTE") {
                    ++actualCommands;
                    std::cout << "Loaded StartQTE: source " << asset.objectId
                        << " / file " << asset.scriptFile << '\n';
                }
            }
        }
    }
    CHECK(actualCommands==static_cast<int>(records.size()));
    for(const auto& record:records) {
        const auto source=std::find_if(level.cinematics().begin(),level.cinematics().end(),
            [&](const auto& c) {return c.objectId==record.source;});
        CHECK(source!=level.cinematics().end() && source->scriptAvailable);
        const CinematicCommand* authored=nullptr;
        for(const auto& thread:source->script.threads()) {
            for(const auto& command:thread.commands) {
                if(command.name=="StartQTE") { CHECK(!authored); authored=&command; }
            }
        }
        CHECK(authored && authored->timestampMilliseconds==static_cast<unsigned>(record.stamp));
        const auto attribute=[&](std::string_view key,int expected) {
            const auto* found=authored->findAttribute(key);
            CHECK(found && found->value==std::to_string(expected));
        };
        attribute("QTEID",record.config);
        attribute("^ID^Cinematic^Success",record.success);
        attribute("^ID^Cinematic^Fail",record.failure);
        for(const bool succeed:{false,true}) {
            GameplayCinematicScheduler scheduler;
            scheduler.bind(level.cinematics()); CHECK(scheduler.start(record.source));
            QuickTimeEventRuntime qte; qte.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
            std::uint32_t now=10000;
            int commands=0, starts=0;
            while(!qte.active() && now<30000) {
                now+=50;
                CHECK(scheduler.update(50,[&](const auto& asset,const auto&,const auto& command) {
                    ++commands;
                    if(command.name=="StartQTE") {
                        CHECK(qte.applyCommand(command,{now,50},asset.objectId)); ++starts;
                    }
                    // Scheduler/manager integration only: other commands are
                    // observed, not executed against a simulated world here.
                    return true;
                }));
            }
            CHECK(qte.active() && starts==1 && commands>0);
            CHECK(qte.sourceCinematicId()==record.source && qte.configId()==record.config);
            const auto started=now;
            const auto duration=qte.durationMilliseconds();
            usm::platform::XInputStateTranslator raw;
            usm::reconstructed::XperiaKeyRouter router;
            usm::platform::QteGamepadAdapter controller;
            int directionalEvents=0;
            for(int frame=0;frame<100 && qte.active();++frame) {
                now+=50;
                QteInput input;
                if(controllerInput) {
                    router.beginFrame();
                    // Same raw XINPUT_GAMEPAD fields and translator used by the
                    // Windows poller. Success: diagonal DOWN without A; failure:
                    // UP plus A never becomes a drag or a virtual-touch release.
                    raw.update(true,succeed?0:usm::platform::xinput::A,
                        frame==0?0:static_cast<std::int16_t>(succeed?22000:0),
                        frame==0?0:static_cast<std::int16_t>(succeed?-22000:32767),
                        [&](const auto& event) {
                            router.route(event,usm::reconstructed::InputContext::Gameplay);
                        });
                    const auto stick=raw.leftStick();
                    input=controller.translate(qte,raw.connected(),
                        router.state().quickTimeEvent.pressed,stick.x,stick.y);
                    CHECK(!input.dragPosition && !input.dragReleased && !input.actionPressed);
                    if(input.dragDirection!=QteDirection::None) {++directionalEvents;}
                } else if(qte.state()==QteState::Drag) {
                    // A alone cannot win a drag even in the failure scenario.
                    input.actionPressed=true;
                    if(succeed) {
                        const auto path=qte.gesturePath().samples();
                        input.dragPosition=path[std::min<std::size_t>(frame,path.size()-1)];
                    }
                } else {input.actionPressed=succeed;}
                qte.update({now,50},input);
            }
            CHECK(!qte.active());
            if(controllerInput) {CHECK(directionalEvents==(succeed?1:0));}
            CHECK(qte.state()==(succeed?QteState::SuccessDisplay:QteState::FailureDisplay));
            CHECK(succeed || now==started+(duration/50+1)*50);
            CHECK(qte.consumeControlRelease());
            const int next=succeed?record.success:record.failure;
            CHECK(scheduler.active(record.source));
            usm::Result handoffResult=usm::Result::failure("No handoff");
            QteState stateDuringHandoff=QteState::Inactive;
            int handoffs=0,step=0,earlyCommands=0,removed=-1;
            bool idsMatch=false;
            const QteHandoffHandler handoff=[&](const QteCinematicHandoff& h) {
                ++handoffs; stateDuringHandoff=qte.state();
                idsMatch=h.sourceCinematicId==record.source && h.outcomeCinematicId==next;
                handoffResult=dispatchQteCinematicHandoff(h,scheduler,
                    [&](const auto& source){removed=source.asset->objectId;},
                    [&](int id){return scheduler.start(id);},
                    [&](unsigned delta){
                        step=static_cast<int>(delta);
                        return scheduler.update(delta,[&](const auto&,const auto&,const auto&){++earlyCommands;return true;});
                    });
            };
            finishFeedback(qte,now,handoff);
            CHECK(handoffResult && handoffs==1 && idsMatch && removed==record.source);
            CHECK(step==50 && earlyCommands>0);
            CHECK(stateDuringHandoff==(succeed?QteState::SuccessDisplay:QteState::FailureDisplay));
            CHECK(!scheduler.active(record.source) && scheduler.active(next));
            CHECK(scheduler.playback(next)->asset->scriptAvailable);
            CHECK(scheduler.playback(next)->elapsedMilliseconds==50);
            CHECK(!qte.consumeSourceRemoval() && !qte.consumeCinematicRequest());
            std::cout << (controllerInput?"Raw XInput level 1 QTE: ":"Authored level 1 QTE: ") << record.source << " / config "
                << record.config << " -> " << next << (succeed?" success":" failure")
                << "; immediate cinematic step=" << step << "; commands=" << earlyCommands << '\n';
        }
    }
}

void audioTests(const std::filesystem::path& root) {
    usm::audio::VoxSoundTable table;
    usm::audio::SoundEventCatalog catalog;
    CHECK(table.load(root)); CHECK(catalog.index(root/"sound",&table));
    for (const std::uint16_t id : {0x188,0x189,0x18a,0x18b}) {
        const auto* record=table.find(id); CHECK(record);
        usm::audio::PcmAudio clip;
        const auto decoded=catalog.decode(record->eventName,clip);
        if(!decoded) {throw std::runtime_error(decoded.message());}
        CHECK(decoded);
        std::cout << "Decoded original QTE sound " << id << ": " << record->eventName << '\n';
    }
}

struct HostageFixture {
    LevelObjectRuntime objects;
    LevelBonusRuntime bonuses;
    QuickTimeEventRuntime qte;
    LevelHostageRuntime hostages;
    GameplayPlayer player;
    std::uint32_t now{10000};
    std::size_t initialBonuses{};
    HostageFixture(const LevelOneBootstrap& level, const PlayerStateConfigDatabase& configs) {
        CHECK(objects.initialize(level));
        CHECK(bonuses.initialize(level.bonuses()));
        qte.bind(level.buttonConfigs(), level.hud().interfaceAtlas);
        CHECK(hostages.initialize(level, qte, &objects));
        CHECK(player.initialize(level.player(), nullptr, &configs));
        const auto* captive = hostages.find(30018);
        CHECK(captive && captive->asset);
        CHECK(captive->asset->hostageButtonHeight == 85.0F);
        player.restoreAt(captive->asset->position, {1, 0, 0});
        initialBonuses = bonuses.states().size();
    }
    const LevelHostageState& state() const { return *hostages.find(30018); }
    void objectStep(bool rescue = false, std::uint32_t simulation = 0) {
        CHECK(hostages.updateObjects(player, objects, bonuses, {simulation, 0, now}, rescue));
    }
    void qteStep(bool tap = false, std::uint32_t real = 0) {
        hostages.updateQuickTime({now, real}, tap);
    }
    void start(bool tapOnActivation = false) {
        objectStep();
        CHECK(hostages.contextPromptVisible());
        objectStep(true);
        CHECK(player.activeStateId() == 27);
        CHECK(hostages.consumeSoundCues().empty());
        player.update({}, {}, 1000);
        objectStep();
        CHECK(state().phase == HostageRescuePhase::QuickTime);
        CHECK(state().completedActions == 0);
        CHECK(hostages.consumeSoundCues().empty());
        qteStep(tapOnActivation);
    }
    void finishInput(std::uint32_t at) {
        now = at;
        for (int i = 0; i < 8; ++i) { objectStep(); qteStep(true); }
    }
};

void hostageTests(const LevelOneBootstrap& level, const PlayerStateConfigDatabase& configs) {
    {
        HostageFixture f(level, configs);
        f.start(true); // A can be consumed on the exact activation update.
        CHECK(f.state().completedActions == 1);
        CHECK(f.hostages.consumeSoundCues().empty());
        f.objectStep();
        const auto start = f.hostages.consumeSoundCues();
        CHECK(start.size() == 1 && start[0].voxSoundId == 0x18b);
        CHECK(start[0].action == HostageSoundAction::PlayOnceIfStopped);
        f.objectStep();
        const auto repeatedQuery = f.hostages.consumeSoundCues();
        CHECK(repeatedQuery.size() == 1 && repeatedQuery[0].action == HostageSoundAction::PlayOnceIfStopped);
        f.qteStep(false, 500);
        CHECK(f.state().completedActions == 0);
        f.objectStep();
        CHECK(f.hostages.consumeSoundCues().empty()); // Decay issues no stop or new play query.
        f.now = 14000;
        f.qteStep(false);
        CHECK(f.state().quickTimeOutcome == HostageQteOutcome::Running);
        f.now = 14001;
        f.qteStep(false);
        CHECK(f.state().quickTimeOutcome == HostageQteOutcome::Failure);
        CHECK(f.player.activeStateId() == 28); // Not an eager player transition.
        CHECK(f.hostages.consumeSoundCues().empty());
        f.objectStep();
        CHECK(f.state().phase == HostageRescuePhase::Tied);
        const auto stop = f.hostages.consumeSoundCues();
        CHECK(stop.size() == 1 && stop[0].action == HostageSoundAction::Stop);
        CHECK(f.bonuses.states().size() == f.initialBonuses);
        f.start();
        CHECK(f.state().completedActions == 0);
    }
    {
        HostageFixture f(level, configs);
        f.start();
        for (int i = 0; i < 20; ++i) { f.objectStep(); f.qteStep(false, 50); }
        CHECK(f.hostages.consumeSoundCues().empty()); // No input, no cutting sound.
        f.finishInput(14000);
        CHECK(f.state().quickTimeOutcome == HostageQteOutcome::Success);
        CHECK(f.player.activeStateId() == 28);
        f.objectStep();
        CHECK(f.player.activeStateId() == 29);
        CHECK(f.state().phase == HostageRescuePhase::RescueEnd);
        f.player.restoreAt(f.state().asset->position, {1, 0, 0}); // Interrupted end clip.
        f.objectStep();
        CHECK(f.state().phase == HostageRescuePhase::Tied);
        CHECK(f.bonuses.states().size() == f.initialBonuses);
    }
    {
        HostageFixture f(level, configs);
        f.start();
        for (int i = 0; i < 7; ++i) { f.objectStep(); f.qteStep(true); }
        f.now = 14001;
        f.objectStep(); f.qteStep(true);
        CHECK(f.state().completedActions == 8);
        CHECK(f.state().quickTimeOutcome == HostageQteOutcome::Failure);
        f.objectStep();
        CHECK(f.state().phase == HostageRescuePhase::Tied);
        CHECK(f.bonuses.states().size() == f.initialBonuses);
    }
    {
        HostageFixture f(level, configs);
        f.start(true);
        f.objectStep();
        (void)f.hostages.consumeSoundCues();
        CHECK(f.player.enterScriptedState(27, false)); // Interrupt with another clip.
        f.objectStep();
        CHECK(f.state().phase == HostageRescuePhase::Tied);
        CHECK(f.player.activeStateId() == 27); // Do not clobber the interrupting state.
        CHECK(f.bonuses.states().size() == f.initialBonuses);
        const auto stop = f.hostages.consumeSoundCues();
        CHECK(stop.size() == 1 && stop[0].action == HostageSoundAction::Stop);
    }
    {
        HostageFixture f(level, configs);
        CHECK(f.player.enterScriptedState(107, true));
        CHECK(f.player.isUltimateState());
        f.objectStep(true);
        CHECK(f.state().phase == HostageRescuePhase::Tied);
        CHECK(!f.hostages.contextPromptVisible());
        CHECK(!f.hostages.canStartRescue(f.player));
    }
    {
        HostageFixture f(level, configs);
        f.start();
        f.hostages.setPause(true, 10900);
        f.hostages.setPause(false, 13000);
        f.now = 17000; f.qteStep();
        CHECK(f.state().quickTimeOutcome == HostageQteOutcome::Running);
        f.now = 17001; f.qteStep();
        CHECK(f.state().quickTimeOutcome == HostageQteOutcome::Failure);
    }
    {
        HostageFixture f(level, configs);
        f.start(true);
        CHECK((f.hostages.consumeQuickTimeSoundCues() == std::vector<std::uint16_t>{0x18a}));
        CHECK(!f.hostages.consumeControlRelease());
        for (int i=0;i<6;++i) { f.qteStep(true); }
        CHECK(f.hostages.consumeQuickTimeSoundCues() == std::vector<std::uint16_t>(6,0x18a));
        f.now=14001; f.qteStep(true);
        CHECK((f.hostages.consumeQuickTimeSoundCues() == std::vector<std::uint16_t>{0x18a,0x188,0x189}));
        CHECK(f.hostages.consumeControlRelease() && !f.hostages.consumeControlRelease());
        CHECK(f.state().quickTimeOutcome==HostageQteOutcome::Failure);
        f.qteStep(true); CHECK(f.hostages.consumeQuickTimeSoundCues().empty());
    }

}

// RE04: direct use of the single CLevel+0x54 manager by cinematic commands
// and CHostage. This is source/asset integration, not an ARM execution oracle.
void prepareManagerState(QuickTimeEventRuntime& qte,
                         const LevelOneBootstrap& level, QteState state) {
    qte.bind(level.buttonConfigs(), level.hud().interfaceAtlas);
    if (state == QteState::Inactive) { return; }
    const int config = state == QteState::Drag ? 6 : state == QteState::Mash ? 11 : 9;
    CHECK(qte.begin(config, {10000,0}, 20006, 20010, 20004));
    if (state == QteState::SuccessDisplay || state == QteState::SuccessHandled) {
        qte.update({10000,0},true);
        if (state == QteState::SuccessHandled) {
            for (int i=0;i<9;++i) { qte.drawStep(); }
        }
    } else if (state == QteState::FailureDisplay || state == QteState::FailureHandled) {
        qte.forceFail();
        if (state == QteState::FailureHandled) { finishFeedback(qte,10000); }
    }
    CHECK(qte.state() == state);
    (void)qte.consumeSoundCues();
    (void)qte.consumeControlRelease();
    (void)qte.consumeSourceRemoval();
    (void)qte.consumeCinematicRequest();
}

void sharedManagerTests(const LevelOneBootstrap& level,
                        const PlayerStateConfigDatabase& configs) {
    for (int nativeState=-1;nativeState<=6;++nativeState) {
        for (const int request : {6,9,11}) {
            QuickTimeEventRuntime qte;
            const auto before = static_cast<QteState>(nativeState);
            prepareManagerState(qte,level,before);
            const auto generation = qte.generation();
            const auto previousConfig = qte.configId();
            const auto previousRemaining = qte.remainingActionCount();
            const auto previousSource = qte.sourceCinematicId();
            CHECK(qte.begin(request,{12000,50},21000,21001,21002));
            // Native unsigned-state >3: -1 and 4..6 can start; 0..3 cannot.
            const bool accepted = nativeState < 0 || nativeState > 3;
            CHECK(qte.generation() == generation + (accepted ? 1 : 0));
            if (accepted) {
                CHECK(qte.configId() == request);
                CHECK(qte.sourceCinematicId() == 21002);
                CHECK(qte.completedActionCount() == 0);
                CHECK(qte.elapsedMilliseconds() == 0);
            } else {
                CHECK(qte.configId() == previousConfig);
                CHECK(qte.remainingActionCount() == previousRemaining);
                CHECK(qte.sourceCinematicId() == previousSource);
                CHECK(qte.state() == before);
                // Lookup follows the native guard, so even an unknown request
                // is ignored while these states own the slot.
                CHECK(qte.begin(32000,{19000,500},7,8,9));
                CHECK(qte.generation() == generation);
            }
            CHECK(qte.consumeSoundCues().empty());
            CHECK(!qte.consumeSourceRemoval() && !qte.consumeCinematicRequest());
        }
    }
    // The -1 sentinel applies separately to removal and each outcome. These
    // combinations are valid calls, not malformed authoring experiments.
    for (const bool success : {false,true}) {
        for (const int source : {-1,20004}) {
            for (const int outcome : {-1,20006}) {
                QuickTimeEventRuntime qte;
                qte.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
                CHECK(qte.begin(9,{10000,0},outcome,outcome,source));
                if (success) { qte.update({10000,0},true); }
                else { qte.forceFail(); }
                CHECK(qte.consumeControlRelease());
                finishFeedback(qte,10000);
                const auto removed=qte.consumeSourceRemoval();
                const auto next=qte.consumeCinematicRequest();
                CHECK(removed == (source == -1 ? std::optional<int>{} : source));
                CHECK(next == (outcome == -1 ? std::optional<int>{} : outcome));
                CHECK(!qte.consumeSourceRemoval() && !qte.consumeCinematicRequest());
                qte.drawStep(); qte.update({10000,0},true);
                CHECK(!qte.consumeSourceRemoval() && !qte.consumeCinematicRequest());
            }
        }
    }
    {
        // Host-delivery queues describe side effects already issued in native
        // code. Begin of another request cannot erase an undrained handoff.
        QuickTimeEventRuntime qte;
        qte.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
        CHECK(qte.begin(9,{10000,0},20006,20010,20004));
        qte.update({10000,0},true);
        for(int i=0;i<9;++i){qte.drawStep();}
        CHECK(qte.begin(11,{11000,0}));
        CHECK(qte.consumeSourceRemoval()==20004);
        CHECK(qte.consumeCinematicRequest()==20006);
        CHECK(qte.configId()==11 && qte.state()==QteState::Mash);
        CHECK(!qte.consumeSourceRemoval() && !qte.consumeCinematicRequest());
    }
    {
        HostageFixture f(level,configs);
        f.start(true);
        CHECK(f.qte.configId()==11 && f.qte.sourceCinematicId()==-1);
        CHECK(f.qte.completedActionCount()==1 && f.state().completedActions==1);
        CHECK((f.qte.consumeSoundCues()==std::vector<std::uint16_t>{0x18a}));
        CHECK(f.hostages.consumeQuickTimeSoundCues().empty()); // Same queue.
        f.objectStep();
        const auto cut=f.hostages.consumeSoundCues();
        CHECK(cut.size()==1 && cut[0].action==HostageSoundAction::PlayOnceIfStopped);
        const auto generation=f.qte.generation();
        for(int attempt=0;attempt<256;++attempt) {
            CHECK(f.qte.applyCommand(startCommand(6),{12000,50},20004));
            CHECK(f.qte.generation()==generation && f.qte.configId()==11);
            CHECK(f.qte.remainingActionCount()==7 && f.qte.sourceCinematicId()==-1);
        }
        // Live application phase order: one object step, one manager step,
        // diagnostic observation. Observation itself never ticks the manager.
        for(int press=2;press<=8;++press) {
            f.objectStep();
            const auto consumed=f.qte.update({10000,0},true);
            CHECK(consumed.quickTimePress && consumed.jumpPress && consumed.rescuePress);
            CHECK(f.state().completedActions==press-1);
            f.hostages.observeQuickTime();
            CHECK(f.state().completedActions==press);
            f.hostages.observeQuickTime();
            CHECK(f.qte.completedActionCount()==press);
            CHECK(f.player.activeStateId()==28);
        }
        CHECK(f.qte.state()==QteState::SuccessDisplay);
        CHECK(f.qte.feedbackFrame().count==3); // Result + original full mash ring.
        CHECK(f.state().phase==HostageRescuePhase::QuickTime);
        CHECK(f.hostages.consumeControlRelease() && !f.qte.consumeControlRelease());
        f.objectStep();
        CHECK(f.player.activeStateId()==29 && f.state().phase==HostageRescuePhase::RescueEnd);
        for(int draw=0;draw<8;++draw) {
            f.qte.drawStep();
            CHECK(f.qte.state()==QteState::SuccessDisplay);
        }
        f.qte.drawStep();
        CHECK(f.qte.state()==QteState::SuccessHandled);
        CHECK(!f.qte.consumeSourceRemoval() && !f.qte.consumeCinematicRequest());
        CHECK(f.bonuses.states().size()==f.initialBonuses); // Award after release, not QTE.
    }
    {
        HostageFixture f(level,configs);
        f.start(true);
        (void)f.qte.consumeSoundCues();
        f.now=14001; f.qteStep();
        CHECK(f.qte.state()==QteState::FailureDisplay);
        CHECK(f.qte.feedbackFrame().count==1);
        f.objectStep();
        CHECK(f.state().phase==HostageRescuePhase::Tied);
        CHECK(f.qte.state()==QteState::FailureDisplay); // Reset hostage is not reset manager.
        const auto stop=f.hostages.consumeSoundCues();
        CHECK(stop.size()==1 && stop[0].action==HostageSoundAction::Stop);
        const auto generation=f.qte.generation();
        f.start();
        CHECK(f.qte.generation()==generation+1 && f.qte.state()==QteState::Mash);
        CHECK(f.qte.failureAlpha()==255 && f.qte.completedActionCount()==0);
        CHECK(!f.qte.consumeCinematicRequest());
    }
    {
        HostageFixture f(level,configs);
        f.start();
        CHECK(f.player.enterScriptedState(27,false));
        f.objectStep();
        CHECK(f.qte.state()==QteState::FailureDisplay);
        CHECK(f.player.activeStateId()==27 && f.state().phase==HostageRescuePhase::Tied);
        CHECK((f.qte.consumeSoundCues()==std::vector<std::uint16_t>{0x189}));
        CHECK(f.hostages.consumeQuickTimeSoundCues().empty());
        CHECK(f.qte.consumeControlRelease());
        f.qte.forceFail(); // Same-state return prevents duplicate failure audio.
        CHECK(f.qte.consumeSoundCues().empty() && !f.qte.consumeControlRelease());
    }
    {
        // Reverse collision: hostage's void BeginQTE request is refused while
        // a cinematic is active. Native CHostage still reads that singleton's
        // state, without inventing an owner-ID check or priority override.
        HostageFixture f(level,configs);
        CHECK(f.qte.begin(6,{10000,0},20006,20010,20004));
        const auto generation=f.qte.generation();
        f.start();
        CHECK(f.qte.generation()==generation && f.qte.configId()==6);
        CHECK(f.qte.sourceCinematicId()==20004 && f.player.activeStateId()==28);
        QteInput gesture; gesture.dragDirection=f.qte.gesturePath().direction();
        f.qte.update({10001,0},gesture);
        f.hostages.observeQuickTime();
        CHECK(f.state().quickTimeOutcome==HostageQteOutcome::Success);
        f.objectStep();
        CHECK(f.player.activeStateId()==29);
        for(int i=0;i<9;++i){f.qte.drawStep();}
        CHECK(f.qte.consumeSourceRemoval()==20004 && f.qte.consumeCinematicRequest()==20006);
    }
    {
        // Checkpoint host snapshots retain a reference to the level manager;
        // copying a hostage does not clone clocks, queues or gesture ownership.
        HostageFixture f(level,configs);
        const auto before=f.hostages;
        f.start(true);
        f.qte.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
        f.hostages=before;
        f.player.clearScriptedState();
        f.start(true);
        CHECK(f.qte.completedActionCount()==1 && f.state().completedActions==1);
        CHECK((f.qte.consumeSoundCues()==std::vector<std::uint16_t>{0x18a}));
        CHECK(f.hostages.consumeQuickTimeSoundCues().empty());
        f.hostages.setPause(true,10900); f.hostages.setPause(false,13000);
        f.qte.update({17000,0},false); f.hostages.observeQuickTime();
        CHECK(f.state().quickTimeOutcome==HostageQteOutcome::Running);
        f.qte.update({17001,0},false); f.hostages.observeQuickTime();
        CHECK(f.state().quickTimeOutcome==HostageQteOutcome::Failure);
    }
}

void consumptionTests(const LevelOneBootstrap& level) {
    for (int nativeState=-1;nativeState<=6;++nativeState) {
        for (const bool pressed : {false,true}) {
            QuickTimeEventRuntime qte;
            prepareManagerState(qte,level,static_cast<QteState>(nativeState));
            const auto used=qte.update({10000,0},pressed);
            CHECK(used.quickTimePress==(pressed && (nativeState==0 || nativeState==2)));
            CHECK(used.jumpPress==(pressed && nativeState==2));
            CHECK(used.rescuePress==(nativeState==2));
        }
    }
    using usm::platform::XInputStateTranslator;
    using usm::reconstructed::XperiaKeyRouter;
    using usm::reconstructed::InputContext;
    namespace xi=usm::platform::xinput;
    const auto apply=[](XperiaKeyRouter& router, QteInputConsumption used) {
        if(used.quickTimePress){router.consumeQuickTimePress();}
        if(used.jumpPress){router.consumeJumpPress();}
        if(used.rescuePress){router.consumeRescueRequest();}
    };
    // All raw wButtons values, including unmapped bits. Verify a consumed tap
    // does not become a synthetic key-up, erase other controls, clear switch=4,
    // or advance a later QTEAction consumer in the same update.
    for (unsigned bits=0;bits<=65535;++bits) {
        XperiaKeyRouter router;
        XInputStateTranslator raw;
        const auto route=[&](const auto& event){router.route(event,InputContext::Gameplay);};
        raw.update(true,xi::RightShoulder,0,0,route);
        router.beginFrame();
        raw.update(true,static_cast<std::uint16_t>(bits),0,0,route);
        const auto before=router.state();
        const bool cross=(bits & xi::A)!=0;
        apply(router,{cross,cross,true});
        const auto& after=router.state();
        CHECK(!after.quickTimeEvent.pressed && !after.jump.pressed && !after.rescueRequested);
        CHECK(after.quickTimeEvent.held==before.quickTimeEvent.held);
        CHECK(after.quickTimeEvent.released==before.quickTimeEvent.released);
        CHECK(after.jump.held==before.jump.held && after.jump.released==before.jump.released);
        CHECK(after.jump.pressedFramesRemaining==before.jump.pressedFramesRemaining);
        CHECK(after.punch.pressed==before.punch.pressed && after.web.pressed==before.web.pressed);
        CHECK(after.spiderSense.pressed==before.spiderSense.pressed && after.superAttack.pressed==before.superAttack.pressed);
        CHECK(after.pause.pressed==before.pause.pressed);
        CHECK(after.interactiveButtonRequested==before.interactiveButtonRequested && after.switchCounter==before.switchCounter);
        CHECK(after.moveUp.pressed==before.moveUp.pressed && after.moveDown.pressed==before.moveDown.pressed);
        CHECK(after.moveLeft.pressed==before.moveLeft.pressed && after.moveRight.pressed==before.moveRight.pressed);
        router.beginFrame();
        raw.update(true,static_cast<std::uint16_t>(bits),0,0,route);
        CHECK(!router.state().quickTimeEvent.pressed); // Hold is not another QTE action.
        CHECK(router.state().jump.pressed==cross); // Independent CKeyPad history preserved.
    }
    {
        QuickTimeEventRuntime qte;
        qte.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
        CHECK(qte.begin(11,{10000,0}));
        XperiaKeyRouter router; XInputStateTranslator raw;
        usm::platform::QteGamepadAdapter adapter;
        int laterActions=0;
        for(int frame=0;frame<16;++frame) {
            router.beginFrame();
            raw.update(true,(frame%2==0)?xi::A:0,0,0,[&](const auto& event){router.route(event,InputContext::Gameplay);});
            const auto stick=raw.leftStick();
            const auto input=adapter.translate(qte,true,router.state().quickTimeEvent.pressed,stick.x,stick.y);
            const auto used=qte.update({10000+static_cast<unsigned>(50*frame),50},input);
            apply(router,used);
            if(router.state().quickTimeEvent.pressed){++laterActions;}
            CHECK(qte.completedActionCount()==(frame/2+1));
        }
        CHECK(qte.state()==QteState::SuccessDisplay && laterActions==0);
        const auto sounds=qte.consumeSoundCues();
        CHECK(sounds.size()==9 && sounds.back()==0x188);
        for(std::size_t i=0;i<8;++i){CHECK(sounds[i]==0x18a);}
        CHECK(qte.consumeControlRelease() && !qte.consumeControlRelease());
    }
}

void handoffTests(const LevelOneBootstrap& level) {
    using usm::Result;
    for(const bool success : {false,true}) {
        QuickTimeEventRuntime qte;
        prepareManagerState(qte,level,success?QteState::SuccessDisplay:QteState::FailureDisplay);
        GameplayCinematicScheduler scheduler;
        scheduler.bind(level.cinematics()); CHECK(scheduler.start(20004));
        const auto oldGeneration=qte.generation();
        QteState during=QteState::Inactive;
        QteState afterNestedBegin=QteState::Inactive;
        std::vector<std::string> order;
        Result dispatch=Result::failure("Callback not executed");
        Result nested=Result::failure("Nested request not executed");
        unsigned callbackCount=0, suppliedStep=0;
        const QteHandoffHandler handoff=[&](const QteCinematicHandoff& h) {
            ++callbackCount; during=qte.state();
            dispatch=dispatchQteCinematicHandoff(h,scheduler,
                [&](const auto& old) { order.push_back("remove:"+std::to_string(old.asset->objectId)); },
                [&](int id) { order.push_back("start:"+std::to_string(id)); return scheduler.start(id); },
                [&](unsigned delta) {
                    suppliedStep=delta; order.push_back("advance");
                    // Synthetic reentrant BeginQTE at the real native handoff
                    // callback point. This is NOT claimed to be a command in
                    // the first-level outcome assets.
                    nested=qte.begin(11,{12000,0});
                    afterNestedBegin=qte.state();
                    return scheduler.update(delta,[](const auto&,const auto&,const auto&){return true;});
                });
        };
        finishFeedback(qte,10000,handoff);
        CHECK(dispatch && nested);
        CHECK(callbackCount==1 && suppliedStep==50);
        CHECK(during==(success?QteState::SuccessDisplay:QteState::FailureDisplay));
        CHECK(afterNestedBegin==(success?QteState::SuccessDisplay:QteState::Mash));
        CHECK(qte.generation()==oldGeneration+(success?0:1));
        CHECK(qte.state()==(success?QteState::SuccessHandled:QteState::FailureHandled));
        CHECK(qte.configId()==(success?9:11));
        CHECK((order==std::vector<std::string>{"remove:20004",success?"start:20006":"start:20010","advance"}));
        CHECK(!scheduler.active(20004));
        CHECK(scheduler.active(success?20006:20010));
        CHECK(!qte.consumeSourceRemoval() && !qte.consumeCinematicRequest());
    }
    for(const int target : {-1,32000}) {
        GameplayCinematicScheduler scheduler;
        scheduler.bind(level.cinematics()); CHECK(scheduler.start(20004));
        int removes=0,starts=0,advances=0;
        CHECK(dispatchQteCinematicHandoff({20004,target},scheduler,
            [&](const auto&){++removes;},
            [&](int){++starts;return Result::success();},
            [&](unsigned){++advances;return Result::success();}));
        CHECK(removes==1 && starts==0 && advances==0 && !scheduler.active(20004));
        CHECK(dispatchQteCinematicHandoff({20004,target},scheduler,{},{},{}));
        CHECK(dispatchQteCinematicHandoff({-1,target},scheduler,{},{},{}));
    }
    {
        // The fixed step advances all active cinematic instances, including
        // existing background commands and an already-active outcome.
        GameplayCinematicScheduler scheduler;
        scheduler.bind(level.cinematics());
        CHECK(scheduler.start(20004)); CHECK(scheduler.start(20006));
        const auto* a=scheduler.findAsset(20004);
        const auto* b=scheduler.findAsset(20006);
        CHECK(a && b && scheduler.findAsset(32000)==nullptr);
        int commands=0;
        const auto advance=[&](unsigned delta) {
            CHECK(delta==50);
            return scheduler.update(delta,[&](const auto&,const auto&,const auto&){++commands;return true;});
        };
        const auto start=[&](int id){return scheduler.start(id);};
        CHECK(dispatchQteCinematicHandoff({-1,20006},scheduler,{},start,advance));
        CHECK(scheduler.playback(20004)->elapsedMilliseconds==50);
        CHECK(scheduler.playback(20006)->elapsedMilliseconds==50);
        CHECK(commands>0);
        CHECK(dispatchQteCinematicHandoff({-1,20006},scheduler,{},start,advance));
        CHECK(scheduler.playback(20004)->elapsedMilliseconds==100);
        CHECK(scheduler.playback(20006)->elapsedMilliseconds==100);
        CHECK(scheduler.activeIds().size()==2);
    }
    {
        GameplayCinematicScheduler scheduler;
        scheduler.bind(level.cinematics()); CHECK(scheduler.start(20004));
        CHECK(!dispatchQteCinematicHandoff({20004,20006},scheduler,{},{},{}));
        CHECK(!scheduler.active(20004) && !scheduler.active(20006));
        int advances=0;
        CHECK(!dispatchQteCinematicHandoff({-1,20006},scheduler,{},
            [](int){return Result::failure("test start error");},
            [&](unsigned){++advances;return Result::success();}));
        CHECK(advances==0);
        CHECK(!dispatchQteCinematicHandoff({-1,20006},scheduler,{},
            [&](int id){return scheduler.start(id);},
            [&](unsigned){++advances;return Result::failure("test update error");}));
        CHECK(advances==1 && scheduler.active(20006));
    }
    {
        // No cinematic sentinels still invoke SetState's IGM-clear host hook;
        // held Start/CKeyPad history are not turned into a physical key-up.
        QuickTimeEventRuntime qte;
        qte.bind(level.buttonConfigs(),level.hud().interfaceAtlas);
        CHECK(qte.begin(11,{10000,0}));
        for(int i=0;i<8;++i){qte.update({10000,0},true);}
        usm::reconstructed::XperiaKeyRouter router;
        usm::platform::XInputStateTranslator raw;
        raw.update(true,usm::platform::xinput::Start,0,0,[&](const auto& event) {
            router.route(event,usm::reconstructed::InputContext::Gameplay);
        });
        CHECK(router.state().pause.pressed && router.state().pause.held);
        int calls=0;
        bool idsValid=false,oldStateVisible=false;
        const QteHandoffHandler handoff=[&](const QteCinematicHandoff& h) {
            ++calls;router.consumePausePress();
            idsValid=h.sourceCinematicId==-1 && h.outcomeCinematicId==-1;
            oldStateVisible=qte.state()==QteState::SuccessDisplay;
        };
        for(int i=0;i<8;++i){qte.drawStep(handoff);}
        CHECK(calls==0 && router.state().pause.pressed);
        qte.drawStep(handoff);
        CHECK(calls==1 && idsValid && oldStateVisible);
        CHECK(!router.state().pause.pressed && router.state().pause.held && !router.state().pause.released);
        CHECK(router.state().pause.pressedFramesRemaining==1);
        qte.drawStep(handoff);qte.update({10000,0},true,handoff);
        CHECK(calls==1 && !qte.consumeSourceRemoval() && !qte.consumeCinematicRequest());
    }
}

void feedbackTests(const LevelOneBootstrap& level) {
    const auto& atlas = level.hud().interfaceAtlas;
    const auto& image = level.hud().interfaceTexture.image();
    QuickTimeEventRuntime qte;
    qte.bind(level.buttonConfigs(), atlas);
    CHECK(qte.feedbackFrame().count == 0);
    CHECK(qte.applyCommand(startCommand(6), {1000,0}));
    CHECK(qte.feedbackFrame().count == 0); // Never resurrect touch UI for active sticks.
    QteInput gesture;
    gesture.dragDirection = qte.gesturePath().direction();
    qte.update({1001,0}, gesture);
    CHECK(qte.state() == QteState::SuccessDisplay);
    for (int draw = 0; draw < 9; ++draw) {
        const auto frame = qte.feedbackFrame();
        CHECK(frame.count == 1);
        CHECK(frame.sprites[0].frameIndex == 33);
        CHECK(frame.sprites[0].alpha == 255);
        CHECK(frame.sprites[0].position.x == 360);
        CHECK(frame.sprites[0].position.y == 251);
        qte.drawStep();
    }
    CHECK(qte.feedbackFrame().count == 0);
    CHECK(qte.consumeCinematicRequest() == 20006);
    CHECK(!qte.consumeCinematicRequest());

    CHECK(qte.applyCommand(startCommand(6), {2000,0}));
    qte.update({5901,0}, false);
    CHECK(qte.state() == QteState::FailureDisplay);
    CHECK(qte.failureAlpha() == 255);
    // Shipped animation 1 duration sum is 10; ARM signed integer division
    // produces 25, not 25.5. Draw subtracts before painting, down to zero.
    for (int draw = 1; draw <= 14; ++draw) {
        const auto snapshot = qte.feedbackFrame();
        const auto again = qte.feedbackFrame();
        CHECK(snapshot.count == 1 && again.count == 1);
        CHECK(snapshot.sprites[0].frameIndex == 34);
        CHECK(snapshot.sprites[0].alpha == std::max(0,255-25*draw));
        CHECK(again.sprites[0].alpha == snapshot.sprites[0].alpha);
        CHECK(snapshot.sprites[0].position.x == 360);
        CHECK(snapshot.sprites[0].position.y == 101);
        qte.drawStep();
        CHECK(qte.failureAlpha() == snapshot.sprites[0].alpha);
        CHECK(qte.state() == QteState::FailureDisplay); // Draw is not the animation clock.
    }
    // Re-entry from failure resets alpha, as BeginQTE does, not BeginNowQTE.
    CHECK(qte.applyCommand(startCommand(6), {6000,0}));
    CHECK(qte.failureAlpha() == 255);
    qte.update({9901,0}, false);
    std::vector<usm::game::QteFeedbackVertex> vertices;
    std::array<bool,6> seen{};
    for (int n = 0; n < 30 && qte.visible(); ++n) {
        const auto frame = qte.feedbackFrame();
        if (frame.count != 0) {
            const auto& draw = frame.sprites[0];
            CHECK(draw.frameIndex >= 34 && draw.frameIndex <= 39);
            seen[draw.frameIndex-34] = true;
            CHECK(draw.alpha == std::max(0,255-25*(n+1)));
            for (auto size : {std::pair{480U,320U}, {1280U,720U}, {1920U,1080U}, {1000U,1000U}, {3440U,1440U}}) {
                CHECK(buildQteFeedbackGeometry(frame,atlas,image.width,image.height,size.first,size.second,vertices));
                CHECK(vertices.size() == 24);
                // Independent coordinate/UV reference using long-double
                // host viewport mapping and the original module facts.
                const long double sx = std::min(size.first/480.0L,size.second/320.0L);
                const long double ox = (size.first - 480.0L*sx)/2;
                const long double oy = (size.second - 320.0L*sx)/2;
                std::size_t at=0;
                for (const auto& fm : atlas.modulesForFrame(draw.frameIndex)) {
                    const auto& m=atlas.modules()[fm.moduleIndex];
                    for (const auto corner : {std::pair{0,0},{1,0},{0,1},{1,0},{1,1},{0,1}}) {
                        const long double px=ox+(draw.position.x+fm.x+corner.first*m.width)*sx;
                        const long double py=oy+(draw.position.y+fm.y+corner.second*m.height)*sx;
                        const auto& v=vertices[at++];
                        CHECK(std::abs(v.x-(2*px/size.first-1)) < 0.000002L);
                        CHECK(std::abs(v.y-(1-2*py/size.second)) < 0.000002L);
                        CHECK(std::abs(v.u-(m.x+corner.first*m.width)/static_cast<long double>(image.width)) < 0.000002L);
                        CHECK(std::abs(v.v-(m.y+corner.second*m.height)/static_cast<long double>(image.height)) < 0.000002L);
                        CHECK(v.rgba == (0xffffffU | (static_cast<std::uint32_t>(draw.alpha)<<24)));
                    }
                }
            }
        }
        qte.drawStep();
        qte.update({9901,50},false);
    }
    for (const bool observed : seen) { CHECK(observed); }
    CHECK(!qte.visible());
    CHECK(qte.feedbackFrame().count == 0);
    CHECK(qte.consumeCinematicRequest() == 20010);

    // Completed mash adds the original two fixed full-ring frames in order.
    CHECK(qte.applyCommand(startCommand(11),{20000,0}));
    for(int i=0;i<8;++i){qte.update({20000,0},true);}
    auto full=qte.feedbackFrame();
    CHECK(full.count==3);
    CHECK(full.sprites[0].frameIndex==33);
    CHECK(full.sprites[1].frameIndex==85);
    CHECK(full.sprites[2].frameIndex==93);
    CHECK(buildQteFeedbackGeometry(full,atlas,image.width,image.height,480,320,vertices));
    CHECK(!vertices.empty());
    // Authored photo config 19 uses value 16. Its one-frame animation 29
    // has duration 1, so the native IsAnimEnded test is already true at
    // tick zero: failure dispatches without painting a made-up fade.
    finishFeedback(qte,20000);
    NativeRandomizer random;
    qte.bind(level.buttonConfigs(),atlas,&random);
    CHECK(qte.applyCommand(startCommand(19),{30000,0}));
    CHECK(level.buttonConfigs().find(19)->interactionValue==16);
    qte.update({40000,0},false);
    CHECK(qte.resultSprite().animationId()==29);
    full=qte.feedbackFrame();
    CHECK(full.count==0 && qte.resultSprite().ended());
    qte.drawStep();
    CHECK(qte.state()==QteState::FailureHandled && qte.failureAlpha()==255);
    CHECK(qte.consumeCinematicRequest()==20010);
    // Valid baseline for the following host-side bounds checks.
    full.count=1; full.sprites[0]={34,{360,101},0,230};
    CHECK(buildQteFeedbackGeometry(full,atlas,image.width,image.height,480,320,vertices));

    auto invalid=full; invalid.count=4;
    CHECK(!buildQteFeedbackGeometry(invalid,atlas,image.width,image.height,480,320,vertices));
    CHECK(vertices.empty());
    invalid=full; invalid.sprites[0].flags=0x80;
    CHECK(!buildQteFeedbackGeometry(invalid,atlas,image.width,image.height,480,320,vertices));
    CHECK(vertices.empty());
    invalid=full; invalid.sprites[0].frameIndex=65535;
    CHECK(!buildQteFeedbackGeometry(invalid,atlas,image.width,image.height,480,320,vertices));
    CHECK(vertices.empty());
    CHECK(!buildQteFeedbackGeometry(full,atlas,0,image.height,480,320,vertices));
    CHECK(!buildQteFeedbackGeometry(full,atlas,1,1,480,320,vertices));
    CHECK(vertices.empty());
    CHECK(buildQteFeedbackGeometry(full,atlas,image.width,image.height,0,0,vertices));
    CHECK(vertices.empty());
}

void attributesTests() {
    // CHostage ctor/ProcessUserAttr retained attribute behavior.
    CHECK(hostageButtonHeight(-1) == 85);
    CHECK(hostageButtonHeight(-100) == 85);
    CHECK(hostageButtonHeight(0) == 85);
    CHECK(hostageButtonHeight(185) == 85);
    CHECK(hostageButtonHeight(100) == 0);
    CHECK(hostageButtonHeight(50) == -50);
    CHECK(hostageButtonHeight(std::numeric_limits<float>::quiet_NaN()) == 85);

    // Player::IsUltimate(-1), ELF 0x0033002c: inclusive 107..113 only.
    CHECK(!isPlayerUltimateStateId(106));
    for (std::uint16_t state = 107; state <= 113; ++state) {
        CHECK(isPlayerUltimateStateId(state));
    }
    CHECK(!isPlayerUltimateStateId(114));
    CHECK(!isPlayerUltimateStateId(0));
    CHECK(!isPlayerUltimateStateId(std::numeric_limits<std::uint16_t>::max()));
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::runtime_error("Expected clock, cinematic, hostage, or attributes"); }
        const std::string_view group = argv[1];
        if (group == "clock") { clockTests(); }
        else if (group == "attributes") { attributesTests(); }
        else {
            const std::filesystem::path root = USM_TEST_GAME_DATA_ROOT;
            CHECK(std::filesystem::is_regular_file(root / "configs.pack"));
            LevelOneBootstrap level;
            CHECK(level.load(root));
            if (group == "cinematic") { cinematicTests(level); }
            else if (group == "gesture") { gestureTests(level); }
            else if (group == "sprite") { spriteTests(level); }
            else if (group == "sequence") { sequenceTests(level); }
            else if (group == "consumption") { consumptionTests(level); }
            else if (group == "handoff") { handoffTests(level); }
            else if (group == "feedback") { feedbackTests(level); }
            else if (group == "gamepad") { gamepadTests(level); }
            else if (group == "controller_boundaries") { controllerBoundaryTests(level); }
            else if (group == "controller_first_level") { firstLevelFlowTests(level, true); }
            else if (group == "audio") { audioTests(root); }
            else if (group == "first_level") { firstLevelFlowTests(level); }
            else if (group == "hostage" || group == "shared_manager") {
                PlayerStateConfigDatabase configs;
                CHECK(configs.load(root));
                if (group == "shared_manager") { sharedManagerTests(level, configs); }
                else { hostageTests(level, configs); }
            } else { throw std::runtime_error("Unknown QTE parity group"); }
        }
        std::cout << "PASS " << group << ": " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
