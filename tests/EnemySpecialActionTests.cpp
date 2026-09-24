#include "game/AttackConfig.hpp"
#include "game/EnemySpecialActionConfig.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t checks{};

void check(bool value, const char* expression, int line) {
    ++checks;
    if (!value) {
        throw std::runtime_error(
            "line " + std::to_string(line) + ": " + expression);
    }
}

#define CHECK(expression) \
    check(static_cast<bool>(expression), #expression, __LINE__)

void appendU16(std::vector<std::byte>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void appendS16(std::vector<std::byte>& bytes, std::int16_t value) {
    appendU16(bytes, static_cast<std::uint16_t>(value));
}

void appendU32(std::vector<std::byte>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

void appendS32(std::vector<std::byte>& bytes, std::int32_t value) {
    appendU32(bytes, static_cast<std::uint32_t>(value));
}

void appendF32(std::vector<std::byte>& bytes, float value) {
    appendU32(bytes, std::bit_cast<std::uint32_t>(value));
}

void appendString(std::vector<std::byte>& bytes, std::string_view value) {
    if (value.size() > 32767U) {
        throw std::runtime_error("synthetic string is too large");
    }
    appendS16(bytes, static_cast<std::int16_t>(value.size()));
    for (char character : value) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
}

void appendAttack(std::vector<std::byte>& bytes, std::int16_t id,
                  std::string_view name, float damage,
                  bool permitsSpecialAnimationSuccessor) {
    appendS16(bytes, id);
    appendString(bytes, name);
    appendS32(bytes, 0);       // ignored integer
    appendS32(bytes, 100);     // hit type
    appendF32(bytes, 1000.0F); // startup milliseconds
    appendS32(bytes, 1);       // turn toward target during startup
    appendS32(bytes, 0);       // quick-time enabled
    appendS32(bytes, -1);      // quick-time action
    appendF32(bytes, damage);
    appendF32(bytes, 0.0F);    // hit protection
    appendF32(bytes, 0.0F);    // horizontal force
    appendF32(bytes, 0.0F);    // vertical force
    appendF32(bytes, 100.0F);
    appendF32(bytes, 200.0F);
    appendF32(bytes, 300.0F);
    appendF32(bytes, 400.0F);
    appendF32(bytes, -90.0F);
    appendF32(bytes, 90.0F);
    appendS32(bytes, 0); // ignored integer
    appendS32(bytes, 1); // interruptible during execution
    appendS32(bytes, 0); // ignored boolean
    appendS32(bytes, 0); // ignored integer
    appendS32(bytes, 0); // target-relative movement
    appendS32(bytes, 0); // movement ends at special action
    appendS32(bytes, permitsSpecialAnimationSuccessor ? 1 : 0);
    appendS32(bytes, 1); // sense reaction type
    appendS32(bytes, 0); // ignored boolean
    appendF32(bytes, 3.0F);
    appendS32(bytes, -1); // forced sense action
    appendS32(bytes, -1); // photo target
    appendString(bytes, "ignored-effect-a");
    appendString(bytes, "ignored-effect-b");
    appendS32(bytes, 0);
    appendF32(bytes, 0.0F);
    appendS32(bytes, 0);
    appendS32(bytes, 0);
    appendS32(bytes, 0);
    appendS32(bytes, 0);
}

void appendSpecialAction(std::vector<std::byte>& bytes,
                         std::int16_t recordId, std::string_view name,
                         std::int16_t enemyTypeId,
                         std::string_view animationName,
                         std::int32_t actionType,
                         std::int32_t keyFramePercent,
                         std::int32_t attackId,
                         std::initializer_list<std::int16_t> soundMapIds,
                         std::string_view nextAnimationName) {
    appendS16(bytes, recordId);
    appendString(bytes, name);
    appendS16(bytes, enemyTypeId);
    appendString(bytes, animationName);
    appendS32(bytes, actionType);
    appendS32(bytes, keyFramePercent);
    appendS32(bytes, attackId);
    appendS16(bytes, static_cast<std::int16_t>(soundMapIds.size()));
    for (std::int16_t soundMapId : soundMapIds) {
        appendS16(bytes, soundMapId);
    }
    appendString(bytes, nextAnimationName);
}

} // namespace

int main() {
    try {
        std::vector<std::byte> attackBytes;
        appendS16(attackBytes, 3);
        appendAttack(attackBytes, 69, "SANDMAN_GROUND", 75.0F, true);
        appendAttack(attackBytes, 70, "SANDMAN_BLOCKED", 25.0F, false);
        appendAttack(attackBytes, 71, "SANDMAN_ZERO_DAMAGE", 0.0F, true);

        usm::game::AttackConfigDatabase attacks;
        CHECK(attacks.load(attackBytes));
        const auto* permittedAttack = attacks.find(69);
        const auto* blockedAttack = attacks.find(70);
        const auto* zeroDamageAttack = attacks.find(71);
        CHECK(permittedAttack != nullptr);
        CHECK(permittedAttack->permitsSpecialAnimationSuccessor);
        CHECK(permittedAttack->damage == 75.0F);
        CHECK(permittedAttack->maximumReach() == 400.0F);
        CHECK(blockedAttack != nullptr);
        CHECK(!blockedAttack->permitsSpecialAnimationSuccessor);
        CHECK(zeroDamageAttack != nullptr);
        CHECK(zeroDamageAttack->damage == 0.0F);
        CHECK(zeroDamageAttack->permitsSpecialAnimationSuccessor);

        std::vector<std::byte> actionBytes;
        appendS16(actionBytes, 6);
        appendSpecialAction(
            actionBytes, 1, "sandman-contact", 16, "ground_attack1",
            0, 50, 69, {42}, "ground_attack1_to_idle");
        appendSpecialAction(
            actionBytes, 2, "sandman-sense", 16, "ground_attack1",
            2, 2, 69, {43}, "action_type_2_is_not_a_successor");
        appendSpecialAction(
            actionBytes, 3, "blocked-contact", 16, "ground_attack1",
            0, 75, 70, {}, "blocked_successor");
        appendSpecialAction(
            actionBytes, 4, "oversized-attack-id", 16, "ground_attack1",
            0, 90, 40000, {}, "overflow_successor");
        appendSpecialAction(
            actionBytes, 5, "blocked-only", 16, "ground_attack_blocked",
            0, 50, 70, {}, "blocked_only_successor");
        appendSpecialAction(
            actionBytes, 6, "zero-damage-successor", 16,
            "ground_attack_recovery", 0, 50, 71, {},
            "ground_attack_recovery_done");

        usm::game::EnemySpecialActionConfigDatabase specialActions;
        CHECK(specialActions.load(actionBytes));
        CHECK(specialActions.actions().size() == 6U);

        const auto events =
            specialActions.findEvents(16, "ground_attack1");
        CHECK(events.size() == 4U);
        CHECK(events.front()->soundMapIds.size() == 1U);
        CHECK(events.front()->soundMapIds.front() == 42);
        CHECK(events.front()->nextAnimationName ==
              "ground_attack1_to_idle");

        const auto attackEvents =
            specialActions.findAttackEvents(16, "ground_attack1");
        CHECK(attackEvents.size() == 3U);
        CHECK(attackEvents.front()->recordId == 1);
        CHECK(attackEvents[1]->recordId == 3);
        CHECK(attackEvents[2]->recordId == 4);

        // SpecialAnimActionCheck (0x003a8c60) puts the authored +0x38
        // animation into IBehaviorBase+0x6c. UpdateAttackMelee_DoAttack
        // (0x003b9e44) may follow it only when EnemyAttackInfo+0x46 permits.
        CHECK(usm::game::specialAnimationSuccessor(
                  specialActions, attacks, 16, "ground_attack1") ==
              "ground_attack1_to_idle");
        CHECK(usm::game::specialAnimationSuccessor(
                  specialActions, attacks, 16, "ground_attack_blocked")
              .empty());
        // The native +0x46 continuation gate is independent of damage.
        // A zero-damage/sound-only authored clip can still own the next
        // animation in the chain.
        CHECK(usm::game::specialAnimationSuccessor(
                  specialActions, attacks, 16, "ground_attack_recovery") ==
              "ground_attack_recovery_done");
        CHECK(usm::game::specialAnimationSuccessor(
                  specialActions, attacks, 15, "ground_attack1")
              .empty());

        std::cout << checks
                  << " special-animation successor checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: "
                  << error.what() << '\n';
        return 1;
    }
}
