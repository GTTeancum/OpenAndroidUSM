#include "game/LevelOneBootstrap.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace usm::game {
namespace {

std::string normalizeArchivePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.starts_with("./")) {
        path.erase(0, 2);
    }
    constexpr std::string_view entityPrefix = "../entities/";
    if (path.starts_with(entityPrefix)) {
        path.erase(0, entityPrefix.size());
    }
    return path;
}

std::string asciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

std::string_view userAttribute(const assets::IrrSceneNode& node,
                               std::string_view name) noexcept {
    const auto match = node.userAttributes.find(std::string(name));
    return match == node.userAttributes.end() ? std::string_view{}
                                               : match->second;
}

std::int32_t integerAttribute(const assets::IrrSceneNode& node,
                              std::string_view name,
                              std::int32_t fallback = -1) noexcept {
    const std::string_view value = userAttribute(node, name);
    std::int32_t parsed = fallback;
    const auto result = std::from_chars(value.data(), value.data() + value.size(),
                                        parsed);
    return result.ec == std::errc{} ? parsed : fallback;
}

float floatAttribute(const assets::IrrSceneNode& node, std::string_view name,
                     float fallback = 0.0F) noexcept {
    const std::string_view value = userAttribute(node, name);
    if (value.empty()) {
        return fallback;
    }
    std::string storage(value);
    char* end = nullptr;
    const float parsed = std::strtof(storage.c_str(), &end);
    return end == storage.c_str() ? fallback : parsed;
}

bool booleanAttribute(const assets::IrrSceneNode& node, std::string_view name,
                      bool fallback = false) noexcept {
    const std::string_view value = userAttribute(node, name);
    if (value == "true" || value == "1") {
        return true;
    }
    if (value == "false" || value == "0") {
        return false;
    }
    return fallback;
}

std::array<bool, 16> roomMaskAttribute(const assets::IrrSceneNode& node,
                                       std::string_view name) noexcept {
    std::array<bool, 16> rooms{};
    std::string_view text = userAttribute(node, name);
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view item = text.substr(0, comma);
        std::int32_t roomId = 0;
        const auto parsed = std::from_chars(
            item.data(), item.data() + item.size(), roomId);
        if (parsed.ec == std::errc{} &&
            parsed.ptr == item.data() + item.size() && roomId >= 1 &&
            roomId <= static_cast<std::int32_t>(rooms.size())) {
            rooms[static_cast<std::size_t>(roomId - 1)] = true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        text.remove_prefix(comma + 1);
    }
    return rooms;
}

assets::Vector3 vectorAttribute(const assets::IrrSceneNode& node,
                                std::string_view name) noexcept {
    std::string storage(userAttribute(node, name));
    std::replace(storage.begin(), storage.end(), ',', ' ');
    const char* cursor = storage.c_str();
    char* end = nullptr;
    assets::Vector3 result;
    for (float* component : {&result.x, &result.y, &result.z}) {
        *component = std::strtof(cursor, &end);
        if (end == cursor) {
            return {};
        }
        cursor = end;
    }
    return result;
}

assets::Vector3 worldPosition(const assets::IrrSceneNode& node) noexcept {
    return node.absoluteTransform[15] == 0.0F
               ? node.position
               : assets::Vector3{node.absoluteTransform[12],
                                 node.absoluteTransform[13],
                                 node.absoluteTransform[14]};
}

const assets::IrrSceneNode* findLevelNode(
    const assets::IrrScene& mainScene,
    std::span<const LevelRoomAsset> rooms, std::int32_t id) noexcept {
    const assets::IrrSceneNode* node = mainScene.findNode(id);
    if (node != nullptr) {
        return node;
    }
    for (const LevelRoomAsset& room : rooms) {
        node = room.scene.findNode(id);
        if (node != nullptr) {
            return node;
        }
    }
    return nullptr;
}

std::optional<LevelObjectKind> levelObjectKind(
    std::string_view gameType) noexcept {
    if (gameType == "AnimatedObject") {
        return LevelObjectKind::Animated;
    }
    if (gameType == "DestroyableObject") {
        return LevelObjectKind::Destroyable;
    }
    if (gameType == "Comic") {
        return LevelObjectKind::Comic;
    }
    if (gameType == "Car") {
        return LevelObjectKind::Car;
    }
    if (gameType == "DropObject") {
        return LevelObjectKind::DropObject;
    }
    if (gameType == "SpiderWebWall") {
        return LevelObjectKind::SpiderWebWall;
    }
    if (gameType == "StaticObject") {
        return LevelObjectKind::StaticObject;
    }
    if (gameType == "Hostage") {
        return LevelObjectKind::Hostage;
    }
    if (gameType == "StreamPiping") {
        return LevelObjectKind::StreamPiping;
    }
    if (gameType == "SlideCar_bus") {
        return LevelObjectKind::SlideCar;
    }
    return std::nullopt;
}

Result loadTextures(filesystem::GbmpArchive& primaryArchive,
                    filesystem::GbmpArchive* fallbackArchive,
                    const assets::ColladaMeshFile& mesh,
                    std::vector<assets::BtexTexture>& output,
                    std::string_view assetName,
                    filesystem::GbmpArchive* secondaryFallbackArchive =
                        nullptr) {
    output.clear();
    output.reserve(mesh.images().size());
    std::vector<std::size_t> missingUnusedImageIndices;
    std::vector<std::byte> resource;
    for (std::size_t imageIndex = 0; imageIndex < mesh.images().size();
         ++imageIndex) {
        const assets::ColladaImage& image = mesh.images()[imageIndex];
        std::string source = image.sourcePath;
        std::replace(source.begin(), source.end(), '\\', '/');
        const std::string filename =
            std::filesystem::path(source).filename().string();
        while (source.starts_with("../")) {
            source.erase(0, 3);
        }
        std::vector<std::string> candidates{
            "textures_bin/" + image.sourcePath,
            "textures_bin/" + filename,
            "textures/" + filename,
            source,
        };
        // comic_cover.bdae names its reflection layer envmap_ringx.tga, but
        // the shipped entity archive stores that resource as envmap_ring.tga.
        // Keep this data-level alias next to archive resolution rather than
        // changing the material or substituting an unrelated texture.
        if (asciiLower(filename) == "envmap_ringx.tga") {
            candidates.push_back("textures_bin/envmap_ring.tga");
            candidates.push_back("textures/envmap_ring.tga");
            candidates.push_back("envmap_ring.tga");
        }
        const std::string lowerFilename = asciiLower(filename);
        filesystem::GbmpArchive* selectedArchive = nullptr;
        std::string selectedPath;
        const std::array archives{&primaryArchive, fallbackArchive,
                                  secondaryFallbackArchive};
        for (filesystem::GbmpArchive* archive : archives) {
            if (archive == nullptr) {
                continue;
            }
            const auto direct = std::find_if(
                candidates.begin(), candidates.end(),
                [archive](const std::string& path) {
                    return archive->find(path) != nullptr;
                });
            if (direct != candidates.end()) {
                selectedArchive = archive;
                selectedPath = *direct;
                break;
            }
            const auto byFilename = std::find_if(
                archive->entries().begin(), archive->entries().end(),
                [&lowerFilename](const filesystem::GbmpArchiveEntry& entry) {
                    return asciiLower(std::filesystem::path(entry.path)
                                          .filename()
                                          .string()) == lowerFilename;
                });
            if (byFilename != archive->entries().end()) {
                selectedArchive = archive;
                selectedPath = byFilename->path;
                break;
            }
        }
        if (selectedArchive == nullptr) {
            const bool referenced = std::any_of(
                mesh.materials().begin(), mesh.materials().end(),
                [imageIndex](const assets::ColladaMaterial& material) {
                    return material.diffuseImageIndex == imageIndex ||
                           material.secondaryImageIndex == imageIndex;
                });
            if (!referenced) {
                // Some shipped BDAE image libraries retain editor-only image
                // names that no material references (vat.bdae includes
                // levelnew_01_01.tga and comic_cover.bdae includes book.tga
                // this way). Defer the placeholder so a missing leading image
                // can reuse the first valid decoded texture while preserving
                // every material's image index.
                missingUnusedImageIndices.push_back(imageIndex);
                output.emplace_back();
                continue;
            }
            output.clear();
            return Result::failure("Could not locate " +
                                   std::string(assetName) + " texture " +
                                   image.sourcePath);
        }
        Result result = selectedArchive->read(selectedPath, resource);
        if (!result) {
            output.clear();
            return Result::failure("Could not load " +
                                   std::string(assetName) + " texture " +
                                   image.sourcePath + ": " + result.message());
        }
        assets::BtexTexture texture;
        result = texture.load(resource);
        if (!result) {
            output.clear();
            return Result::failure("Could not decode " +
                                   std::string(assetName) + " texture " +
                                   image.sourcePath + ": " + result.message());
        }
        output.push_back(std::move(texture));
    }
    if (!missingUnusedImageIndices.empty()) {
        const auto validTexture = std::find_if(
            output.begin(), output.end(), [](const assets::BtexTexture& texture) {
                return !texture.mipLevels().empty();
            });
        if (validTexture == output.end()) {
            output.clear();
            return Result::failure("Could not locate any referenced " +
                                   std::string(assetName) + " texture");
        }
        for (const std::size_t imageIndex : missingUnusedImageIndices) {
            output[imageIndex] = *validTexture;
        }
    }
    return Result::success();
}

bool parseFloatText(std::string_view text, float& value) noexcept {
    if (text.empty()) {
        return false;
    }
    const std::string storage(text);
    char* end = nullptr;
    value = std::strtof(storage.c_str(), &end);
    return end != storage.c_str() && *end == '\0';
}

bool parseIntegerText(std::string_view text, std::int32_t& value) noexcept {
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} &&
           parsed.ptr == text.data() + text.size();
}

Result loadCinematicActor(filesystem::GbmpArchive& levelArchive,
                          filesystem::GbmpArchive& entityArchive,
                          const assets::IrrSceneNode& sceneNode,
                          const CinematicCommand& command,
                          std::int32_t objectId,
                          CinematicActorAsset& actor) {
    const CinematicAttribute* animationFile =
        command.findAttribute("AnimFile");
    if (animationFile == nullptr || sceneNode.meshFile.empty()) {
        return Result::failure(
            "Cinematic actor command has no mesh or animation file");
    }

    actor = {};
    actor.objectId = objectId;
    actor.sceneNodeName = sceneNode.name;
    actor.animationStartMilliseconds = command.timestampMilliseconds;
    actor.position = sceneNode.position;
    actor.rotation = sceneNode.rotation;
    actor.scale = sceneNode.scale;
    actor.worldTransform = sceneNode.absoluteTransform;

    const std::string meshPath = normalizeArchivePath(sceneNode.meshFile);
    filesystem::GbmpArchive* meshArchive =
        entityArchive.find(meshPath) != nullptr ? &entityArchive
                                                : &levelArchive;
    if (meshArchive->find(meshPath) == nullptr) {
        return Result::failure("Could not locate cinematic actor mesh " +
                               sceneNode.meshFile);
    }
    std::vector<std::byte> resource;
    Result result = meshArchive->read(meshPath, resource);
    if (!result || !(result = actor.mesh.load(resource))) {
        return Result::failure("Could not load cinematic actor " +
                               actor.sceneNodeName + ": " +
                               result.message());
    }
    filesystem::GbmpArchive* textureFallback =
        meshArchive == &entityArchive ? &levelArchive : &entityArchive;
    result = loadTextures(*meshArchive, textureFallback, actor.mesh,
                          actor.textures, actor.sceneNodeName);
    if (!result) {
        return result;
    }
    const std::string animationPath =
        normalizeArchivePath(animationFile->value);
    result = levelArchive.read(animationPath, resource);
    if (!result || !(result = actor.animation.load(resource))) {
        return Result::failure("Could not load animation for " +
                               actor.sceneNodeName + ": " +
                               result.message());
    }
    return Result::success();
}

} // namespace

Result LevelOneBootstrap::load(const std::filesystem::path& gameDataRoot) {
    rooms_.clear();
    introSky_ = {};
    introActors_.clear();
    player_ = {};
    cameraAreas_.clear();
    triggers_.clear();
    waypoints_.clear();
    webGrabPoints_.clear();
    slides_.clear();
    cinematics_.clear();
    enemyArchetypes_.clear();
    enemies_.clear();
    objectArchetypes_.clear();
    objects_.clear();
    environmentEffects_.clear();
    bonuses_.clear();
    triggerSounds_.clear();
    hud_ = {};
    effects_ = {};
    Result result = attackConfigs_.load(gameDataRoot);
    if (!result) {
        return Result::failure("Could not load attack configs: " +
                               result.message());
    }
    result = buttonConfigs_.load(gameDataRoot);
    if (!result) {
        return Result::failure("Could not load button configs: " +
                               result.message());
    }
    result = textCatalog_.load(gameDataRoot);
    if (!result) {
        return Result::failure("Could not load localized strings: " +
                               result.message());
    }
    result = enemySpecialActions_.load(gameDataRoot);
    if (!result) {
        return Result::failure("Could not load enemy special actions: " +
                               result.message());
    }
    result = enemyBehaviorConfigs_.load(gameDataRoot);
    if (!result) {
        return Result::failure("Could not load enemy behavior configs: " +
                               result.message());
    }
    result = enemyAttributeConfigs_.load(gameDataRoot);
    if (!result) {
        return Result::failure("Could not load enemy attribute configs: " +
                               result.message());
    }
    result = enemyAttackIntervalConfigs_.load(gameDataRoot);
    if (!result) {
        return Result::failure("Could not load enemy attack intervals: " +
                               result.message());
    }
    result = enemyRangeAttackConfigs_.load(gameDataRoot);
    if (!result) {
        return Result::failure("Could not load enemy range-attack configs: " +
                               result.message());
    }
    filesystem::GbmpArchive levelArchive;
    result = levelArchive.open(gameDataRoot / "levelnew_01.pack");
    if (!result) {
        return result;
    }
    filesystem::GbmpArchive entityArchive;
    result = entityArchive.open(gameDataRoot / "entities.pack");
    if (!result) {
        return result;
    }

    filesystem::GbmpArchive spriteArchive;
    result = spriteArchive.open(gameDataRoot / "sprites.pack");
    if (!result) {
        return result;
    }

    std::vector<std::byte> resource;
    result = spriteArchive.read("interface.bsprite", resource);
    if (!result) {
        return Result::failure("Could not load interface sprite metadata: " +
                               result.message());
    }
    result = hud_.interfaceAtlas.load(resource);
    if (!result) {
        return Result::failure("Could not parse interface sprite metadata: " +
                               result.message());
    }
    result = spriteArchive.read("interface.tga", resource);
    if (!result) {
        return Result::failure("Could not load interface sprite texture: " +
                               result.message());
    }
    result = hud_.interfaceTexture.load(resource);
    if (!result) {
        return Result::failure("Could not decode interface sprite texture: " +
                               result.message());
    }

    result = entityArchive.read("effects.xml", resource);
    if (!result) {
        return Result::failure("Could not load effect presets: " +
                               result.message());
    }
    result = effects_.presets.load(resource);
    if (!result) {
        return Result::failure("Could not parse effect presets: " +
                               result.message());
    }
    result = spriteArchive.read("effects.bsprite", resource);
    if (!result) {
        return Result::failure("Could not load effect sprite metadata: " +
                               result.message());
    }
    result = effects_.atlas.load(resource);
    if (!result) {
        return Result::failure("Could not parse effect sprite metadata: " +
                               result.message());
    }
    result = spriteArchive.read("effects.tga", resource);
    if (!result) {
        return Result::failure("Could not load effect sprite texture: " +
                               result.message());
    }
    result = effects_.texture.load(resource);
    if (!result) {
        return Result::failure("Could not decode effect sprite texture: " +
                               result.message());
    }

    result = levelArchive.read("levelnew_01.irr", resource);
    if (!result) {
        return result;
    }
    result = mainScene_.load(resource);
    if (!result) {
        return result;
    }

    const auto playerNode = std::find_if(
        mainScene_.nodes().begin(), mainScene_.nodes().end(),
        [](const assets::IrrSceneNode& node) {
            return node.gameType == "SpiderMan";
        });
    if (playerNode == mainScene_.nodes().end() ||
        playerNode->meshFile.empty() || playerNode->animationFile.empty()) {
        return Result::failure(
            "Level 1 scene has no complete Spider-Man player node");
    }
    player_.objectId = playerNode->id;
    player_.sceneNodeName = playerNode->name;
    player_.initialAnimation = playerNode->initialAnimation;
    player_.initialCameraAreaId = playerNode->initialCameraAreaId;
    player_.linkedCinematicId = playerNode->linkedCinematicId;
    player_.endGameCinematicId = playerNode->endGameCinematicId;
    player_.hasCollision = playerNode->hasCollision;
    player_.health = floatAttribute(*playerNode, "Health", 1000.0F);
    player_.position = playerNode->position;
    player_.rotation = playerNode->rotation;
    player_.scale = playerNode->scale;
    player_.worldTransform = playerNode->absoluteTransform;

    result = entityArchive.read(normalizeArchivePath(playerNode->meshFile),
                                resource);
    if (!result) {
        return Result::failure("Could not load player mesh: " +
                               result.message());
    }
    result = player_.mesh.load(resource);
    if (!result) {
        return Result::failure("Could not parse player mesh: " +
                               result.message());
    }
    result = loadTextures(entityArchive, nullptr, player_.mesh,
                          player_.textures, player_.sceneNodeName);
    if (!result) {
        return result;
    }
    result = entityArchive.read(
        normalizeArchivePath(playerNode->animationFile), resource);
    if (!result) {
        return Result::failure("Could not load player animation bank: " +
                               result.message());
    }
    result = player_.animationBank.load(resource);
    if (!result) {
        return Result::failure("Could not parse player animation bank: " +
                               result.message());
    }
    if (player_.animationBank.findClip(player_.initialAnimation) == nullptr) {
        return Result::failure("Player initial animation is not in its bank");
    }

    if (mainScene_.linkedSceneFiles().empty()) {
        return Result::failure("Level 1 scene has no linked rooms");
    }
    rooms_.reserve(mainScene_.linkedSceneFiles().size());
    for (const std::string& roomFile : mainScene_.linkedSceneFiles()) {
        LevelRoomAsset room;
        room.sceneFile = roomFile;
        result = levelArchive.read(roomFile, resource);
        if (!result) {
            return result;
        }
        result = room.scene.load(resource);
        if (!result) {
            return Result::failure("Could not parse " + roomFile + ": " +
                                   result.message());
        }
        room.name = room.scene.nodes().front().name;
        if (room.name.empty()) {
            room.name = std::filesystem::path(roomFile).stem().string();
        }
        const auto geometryNode = std::find_if(
            room.scene.nodes().begin(), room.scene.nodes().end(),
            [](const assets::IrrSceneNode& node) {
                return node.gameType == "Geometry" && !node.meshFile.empty();
            });
        if (geometryNode == room.scene.nodes().end()) {
            return Result::failure(room.name +
                                   " has no Geometry scene node");
        }
        result = levelArchive.read(
            normalizeArchivePath(geometryNode->meshFile), resource);
        if (!result) {
            return result;
        }
        result = room.geometry.load(resource);
        if (!result || room.geometry.geometries().empty()) {
            return Result::failure("Could not parse geometry for " +
                                   room.name + ": " + result.message());
        }
        result = loadTextures(levelArchive, &entityArchive, room.geometry,
                              room.textures, room.name);
        if (!result || room.textures.empty()) {
            return !result ? result
                           : Result::failure(room.name +
                                             " has no diffuse textures");
        }
        const auto collisionNode = std::find_if(
            room.scene.nodes().begin(), room.scene.nodes().end(),
            [](const assets::IrrSceneNode& node) {
                return node.gameType == "Collisions" &&
                       !node.meshFile.empty();
            });
        const auto navigationNode = std::find_if(
            room.scene.nodes().begin(), room.scene.nodes().end(),
            [](const assets::IrrSceneNode& node) {
                return node.gameType == "NavMesh" &&
                       !node.meshFile.empty();
            });
        if (collisionNode == room.scene.nodes().end() ||
            navigationNode == room.scene.nodes().end()) {
            return Result::failure(room.name +
                                   " has no collision or navigation mesh");
        }
        result = levelArchive.read(
            normalizeArchivePath(collisionNode->meshFile), resource);
        if (!result || !(result = room.collision.load(resource)) ||
            room.collision.geometries().empty()) {
            return Result::failure("Could not load collision mesh for " +
                                   room.name + ": " + result.message());
        }
        result = levelArchive.read(
            normalizeArchivePath(navigationNode->meshFile), resource);
        if (!result || !(result = room.navigationMesh.load(resource)) ||
            room.navigationMesh.geometries().empty()) {
            return Result::failure("Could not load navigation mesh for " +
                                   room.name + ": " + result.message());
        }
        rooms_.push_back(std::move(room));
    }

    // CRoom creates these concrete object classes from their authored
    // !GameType values. Keep mesh/animation payloads deduplicated while
    // preserving an independently addressable instance for every scene ID.
    for (std::size_t roomIndex = 0; roomIndex < rooms_.size(); ++roomIndex) {
        const LevelRoomAsset& room = rooms_[roomIndex];
        for (const assets::IrrSceneNode& node : room.scene.nodes()) {
            if (node.gameType == "Effect") {
                const std::string effectType(
                    userAttribute(node, "$EffectType"));
                if (effectType.empty() ||
                    effects_.presets.find(effectType) == nullptr) {
                    return Result::failure("Room effect " + node.name +
                                           " has an unknown preset");
                }
                environmentEffects_.push_back(
                    {node.id, effectType,
                     static_cast<std::int32_t>(roomIndex + 1),
                     worldPosition(node), node.visible});
                continue;
            }
            if (node.gameType == "Bonus") {
                LevelBonusType type = LevelBonusType::Health;
                if (booleanAttribute(node, "WebPower", false)) {
                    type = LevelBonusType::WebPower;
                } else if (booleanAttribute(node, "SkillPoint", false)) {
                    type = LevelBonusType::SkillPoint;
                } else if (!booleanAttribute(node, "HP", false)) {
                    return Result::failure("Bonus " +
                                           std::to_string(node.id) +
                                           " has no enabled type flag");
                }
                bonuses_.push_back(
                    {node.id, type,
                     static_cast<std::int32_t>(roomIndex + 1),
                     worldPosition(node), node.visible});
                continue;
            }
            if (node.gameType == "TriggerSound") {
                const std::string eventName(
                    userAttribute(node, "$VoxSounds"));
                const assets::Vector3 sizes = vectorAttribute(node, "Sizes");
                if (eventName.empty() || sizes.x <= 0.0F ||
                    sizes.y <= 0.0F || sizes.z <= 0.0F) {
                    return Result::failure("TriggerSound " + node.name +
                                           " has invalid attributes");
                }
                LevelTriggerSoundAsset sound;
                sound.objectId = node.id;
                sound.eventName = eventName;
                sound.roomId = static_cast<std::int32_t>(roomIndex + 1);
                sound.position = worldPosition(node);
                sound.rotation = node.rotation;
                sound.scale = node.scale;
                sound.worldTransform = node.absoluteTransform;
                sound.sizes = sizes;
                sound.axisAlignedBox =
                    booleanAttribute(node, "IsAABBox", false);
                triggerSounds_.push_back(std::move(sound));
            }
        }
    }

    for (std::size_t roomIndex = 0; roomIndex < rooms_.size(); ++roomIndex) {
        const LevelRoomAsset& room = rooms_[roomIndex];
        for (const assets::IrrSceneNode& node : room.scene.nodes()) {
            const std::optional<LevelObjectKind> kind =
                levelObjectKind(node.gameType);
            if (!kind || node.meshFile.empty()) {
                continue;
            }
            const std::string meshPath =
                normalizeArchivePath(node.meshFile);
            const std::string animationPath =
                normalizeArchivePath(node.animationFile);
            const auto existing = std::find_if(
                objectArchetypes_.begin(), objectArchetypes_.end(),
                [&meshPath, &animationPath](
                    const LevelObjectArchetypeAsset& candidate) {
                    return candidate.meshFile == meshPath &&
                           candidate.animationFile == animationPath;
                });
            std::size_t archetypeIndex = 0;
            if (existing == objectArchetypes_.end()) {
                LevelObjectArchetypeAsset archetype;
                archetype.meshFile = meshPath;
                archetype.animationFile = animationPath;
                filesystem::GbmpArchive* meshArchive =
                    entityArchive.find(meshPath) != nullptr ? &entityArchive
                                                            : &levelArchive;
                if (meshArchive->find(meshPath) == nullptr) {
                    return Result::failure("Could not locate level object mesh " +
                                           node.meshFile);
                }
                result = meshArchive->read(meshPath, resource);
                if (!result || !(result = archetype.mesh.load(resource))) {
                    return Result::failure("Could not load level object " +
                                           node.name + ": " +
                                           result.message());
                }
                filesystem::GbmpArchive* textureFallback =
                    meshArchive == &entityArchive ? &levelArchive
                                                   : &entityArchive;
                result = loadTextures(*meshArchive, textureFallback,
                                      archetype.mesh, archetype.textures,
                                      node.name, &spriteArchive);
                if (!result) {
                    return result;
                }
                if (!animationPath.empty()) {
                    filesystem::GbmpArchive* animationArchive =
                        entityArchive.find(animationPath) != nullptr
                            ? &entityArchive
                            : &levelArchive;
                    if (animationArchive->find(animationPath) == nullptr) {
                        return Result::failure(
                            "Could not locate level object animation " +
                            node.animationFile);
                    }
                    result = animationArchive->read(animationPath, resource);
                    if (!result) {
                        return Result::failure(
                            "Could not load animation for level object " +
                            node.name + " (ID " + std::to_string(node.id) +
                            ", " + node.animationFile + "): " +
                            result.message());
                    }
                    result = archetype.animationBank.load(resource);
                    const bool authoredMeshWithoutAnimationLibrary =
                        !result && animationPath == meshPath &&
                        result.message() ==
                            "BDAE animation library is invalid";
                    if (!result && !authoredMeshWithoutAnimationLibrary) {
                        return Result::failure(
                            "Could not load animation for level object " +
                            node.name + " (ID " + std::to_string(node.id) +
                            ", " + node.animationFile + "): " +
                            result.message());
                    }
                }
                objectArchetypes_.push_back(std::move(archetype));
                archetypeIndex = objectArchetypes_.size() - 1;
            } else {
                archetypeIndex = static_cast<std::size_t>(
                    std::distance(objectArchetypes_.begin(), existing));
            }

            LevelObjectAsset object;
            object.objectId = node.id;
            object.name = node.name;
            object.gameType = node.gameType;
            object.kind = *kind;
            object.initialAnimation =
                animationPath.empty() ? std::string{} : node.initialAnimation;
            object.archetypeIndex = archetypeIndex;
            object.roomId = static_cast<std::int32_t>(roomIndex + 1);
            object.position = worldPosition(node);
            object.rotation = node.rotation;
            object.scale = node.scale;
            object.worldTransform = node.absoluteTransform;
            object.visible = node.visible;
            object.hasCollision = node.hasCollision;
            if (!animationPath.empty() && !object.initialAnimation.empty()) {
                const auto& animationBank =
                    objectArchetypes_[archetypeIndex].animationBank;
                if (animationBank.findClip(object.initialAnimation) == nullptr) {
                    std::int32_t clipIndex = -1;
                    if (parseIntegerText(object.initialAnimation, clipIndex) &&
                        clipIndex >= 0 &&
                        static_cast<std::size_t>(clipIndex) <
                            animationBank.clips().size()) {
                        object.initialAnimation =
                            animationBank.clips()[clipIndex].name;
                    } else {
                        // CAnimatedObject::ProcessUserAttr (0x002fd560)
                        // silently keeps the bind pose when @Anim cannot be
                        // resolved by name.
                        object.initialAnimation.clear();
                    }
                }
            }
            objects_.push_back(std::move(object));
        }
    }

    const auto appendCameraAreas = [this](const assets::IrrScene& scene)
        -> Result {
        for (const assets::IrrSceneNode& areaNode : scene.nodes()) {
            if (areaNode.gameType != "CameraArea") {
                continue;
            }
            CameraArea area;
            area.objectId = areaNode.id;
            area.nextAreaIds = areaNode.nextCameraAreaIds;
            area.switchTimeUnits = areaNode.cameraAreaSwitchTimeUnits;
            area.inverseNormal = areaNode.cameraAreaInverseNormal;
            area.height = areaNode.cameraAreaHeight;
            area.zFollowRate = areaNode.cameraAreaZFollowRate;
            area.disabled = areaNode.cameraAreaDisabled;
            area.farPlaneOffset = areaNode.cameraFarPlaneOffset;
            area.mustInvisibleRooms =
                roomMaskAttribute(areaNode, "mustInVisibleRoom");
            area.mustVisibleRooms =
                roomMaskAttribute(areaNode, "mustVisibleRoom");
            for (std::size_t index = 0; index < area.controlPoints.size();
                 ++index) {
                const assets::IrrSceneNode* controlNode = findLevelNode(
                    mainScene_, rooms_,
                    areaNode.cameraControlPointIds[index]);
                if (controlNode == nullptr ||
                    controlNode->gameType != "CamCtrlPoint" ||
                    controlNode->cameraDistance <= 0.0F) {
                    return Result::failure("Camera area " + areaNode.name +
                                           " has an invalid control point");
                }
                area.controlPoints[index] = {
                    controlNode->id,
                    worldPosition(*controlNode),
                    controlNode->cameraDirection,
                    controlNode->cameraDistance,
                    controlNode->cameraTargetOffset,
                    controlNode->cameraTargetHeightOffset,
                };
            }
            cameraAreas_.push_back(std::move(area));
        }
        return Result::success();
    };
    result = appendCameraAreas(mainScene_);
    if (!result) {
        cameraAreas_.clear();
        return result;
    }
    for (const LevelRoomAsset& room : rooms_) {
        result = appendCameraAreas(room.scene);
        if (!result) {
            cameraAreas_.clear();
            return result;
        }
    }
    GameplayCamera gameplayCamera;
    result = gameplayCamera.bind(cameraAreas_, player_.initialCameraAreaId);
    if (!result) {
        cameraAreas_.clear();
        return result;
    }

    const auto appendWaypoints = [this](const assets::IrrScene& scene) {
        for (const assets::IrrSceneNode& node : scene.nodes()) {
            if (node.gameType != "WayPoint") {
                continue;
            }
            LevelWayPointAsset waypoint;
            waypoint.objectId = node.id;
            waypoint.name = node.name;
            waypoint.position = worldPosition(node);
            waypoint.enabled = booleanAttribute(node, "Enabled", true);
            waypoint.electricShock =
                booleanAttribute(node, "ElectricShock", false);
            waypoint.nextWaypointIds = {
                integerAttribute(node, "^Next1^WayPoint"),
                integerAttribute(node, "^Next2^WayPoint"),
            };
            waypoint.useGravityWhenEnd =
                booleanAttribute(node, "UseGravityWhenEnd", true);
            waypoint.unstandable =
                booleanAttribute(node, "UnStandable", false);
            waypoint.jumpDirection = integerAttribute(node, "$JumpDir", 0);
            waypoint.timeToMe = floatAttribute(node, "TimeToMe");
            waypoint.linkedCameraAreaId =
                integerAttribute(node, "^Linked^CameraArea");
            waypoints_.push_back(std::move(waypoint));
        }
    };
    appendWaypoints(mainScene_);
    for (const LevelRoomAsset& room : rooms_) {
        appendWaypoints(room.scene);
    }

    const auto appendSlides = [this](const assets::IrrScene& scene) -> Result {
        for (const assets::IrrSceneNode& node : scene.nodes()) {
            if (node.gameType != "Slide") {
                continue;
            }
            LevelSlideAsset slide;
            slide.objectId = node.id;
            slide.name = node.name;
            slide.position = worldPosition(node);
            slide.linkedWaypointId =
                integerAttribute(node, "^Link^WayPoint");
            slide.enabled = booleanAttribute(node, "Enabled", true);
            slide.electricShock =
                booleanAttribute(node, "ElectricShock", false);
            std::unordered_set<std::int32_t> visited;
            std::int32_t waypointId = slide.linkedWaypointId;
            while (waypointId >= 0) {
                if (!visited.insert(waypointId).second) {
                    return Result::failure("Slide " +
                                           std::to_string(slide.objectId) +
                                           " has a cyclic waypoint graph");
                }
                const auto waypoint = std::find_if(
                    waypoints_.begin(), waypoints_.end(),
                    [waypointId](const LevelWayPointAsset& candidate) {
                        return candidate.objectId == waypointId;
                    });
                if (waypoint == waypoints_.end()) {
                    return Result::failure("Slide " +
                                           std::to_string(slide.objectId) +
                                           " has an invalid waypoint");
                }
                slide.waypointIds.push_back(waypointId);
                waypointId = waypoint->nextWaypointIds[0];
            }
            if (slide.waypointIds.size() < 2) {
                return Result::failure("Slide " +
                                       std::to_string(slide.objectId) +
                                       " has no traversable segment");
            }
            slides_.push_back(std::move(slide));
        }
        return Result::success();
    };
    result = appendSlides(mainScene_);
    if (!result) {
        slides_.clear();
        return result;
    }
    for (const LevelRoomAsset& room : rooms_) {
        result = appendSlides(room.scene);
        if (!result) {
            slides_.clear();
            return result;
        }
    }

    const auto appendWebGrabPoints = [this](const assets::IrrScene& scene)
        -> Result {
        for (const assets::IrrSceneNode& node : scene.nodes()) {
            if (node.gameType != "WebGrabPoint") {
                continue;
            }
            LevelWebGrabPointAsset point;
            point.objectId = node.id;
            point.position = worldPosition(node);
            point.directionControlPointId =
                integerAttribute(node, "^Dir^CamCtrlPoint");
            const assets::IrrSceneNode* directionNode = findLevelNode(
                mainScene_, rooms_, point.directionControlPointId);
            if (directionNode == nullptr ||
                directionNode->gameType != "CamCtrlPoint") {
                return Result::failure("Web grab point " +
                                       std::to_string(point.objectId) +
                                       " has no valid direction control point");
            }
            point.direction = directionNode->cameraDirection;
            point.length = floatAttribute(node, "Length");
            point.visibleLength =
                floatAttribute(node, "VisiableLength", -1.0F);
            point.verticalAngleDegrees = floatAttribute(node, "AngleV");
            point.horizontalAngleDegrees = floatAttribute(node, "AngleH");
            point.exitSpeed = floatAttribute(node, "OutSpeed");
            point.cannotControl =
                booleanAttribute(node, "CanNotControl", false);
            point.targetWaypointId =
                integerAttribute(node, "^Target^WayPoint");
            point.targetSlideId = integerAttribute(node, "^Target^Slide");
            if (point.targetWaypointId >= 0) {
                const assets::IrrSceneNode* targetNode = findLevelNode(
                    mainScene_, rooms_, point.targetWaypointId);
                if (targetNode == nullptr ||
                    targetNode->gameType != "WayPoint") {
                    return Result::failure("Web grab point " +
                                           std::to_string(point.objectId) +
                                           " has no valid target waypoint");
                }
                point.hasTargetWaypoint = true;
                point.targetWaypointPosition = worldPosition(*targetNode);
            }
            webGrabPoints_.push_back(std::move(point));
        }
        return Result::success();
    };
    result = appendWebGrabPoints(mainScene_);
    if (!result) {
        webGrabPoints_.clear();
        return result;
    }
    for (const LevelRoomAsset& room : rooms_) {
        result = appendWebGrabPoints(room.scene);
        if (!result) {
            webGrabPoints_.clear();
            return result;
        }
    }

    for (std::size_t roomIndex = 0; roomIndex < rooms_.size(); ++roomIndex) {
        const LevelRoomAsset& room = rooms_[roomIndex];
        for (const assets::IrrSceneNode& node : room.scene.nodes()) {
            if (node.gameType == "Trigger") {
                LevelTriggerAsset trigger;
                trigger.objectId = node.id;
                trigger.name = node.name;
                trigger.position = worldPosition(node);
                trigger.rotation = node.rotation;
                trigger.scale = node.scale;
                trigger.worldTransform = node.absoluteTransform;
                trigger.sizes = vectorAttribute(node, "Sizes");
                trigger.orientedBox =
                    booleanAttribute(node, "IsOBBox", false);
                trigger.enabled = booleanAttribute(node, "Enabled", true);
                trigger.autoDisabled =
                    booleanAttribute(node, "AutoDisabled", false);
                trigger.outToInCinematicId =
                    integerAttribute(node, "^OutToIn^Cinematic");
                trigger.inToOutCinematicId =
                    integerAttribute(node, "^InToOut^Cinematic");
                trigger.whileInsideCinematicId =
                    integerAttribute(node, "^WhileIn^Cinematic");
                trigger.whileOutsideCinematicId =
                    integerAttribute(node, "^WhileOut^Cinematic");
                triggers_.push_back(std::move(trigger));
                continue;
            }
            if (node.gameType == "Cinematic") {
                const std::string_view scriptFile =
                    userAttribute(node, "!ScriptFile");
                if (scriptFile.empty()) {
                    return Result::failure("Cinematic " + node.name +
                                           " has no script file");
                }
                LevelCinematicAsset cinematic;
                cinematic.objectId = node.id;
                cinematic.name = node.name;
                cinematic.scriptFile = normalizeArchivePath(
                    std::string(scriptFile));
                if (levelArchive.find(cinematic.scriptFile) == nullptr) {
                    // Room 13 contains an orphaned editor cinematic 1239 but
                    // the shipped archive has no matching CFF. Preserve the
                    // authored object while leaving it non-runnable.
                    cinematics_.push_back(std::move(cinematic));
                    continue;
                }
                result = levelArchive.read(cinematic.scriptFile, resource);
                if (!result || !(result = cinematic.script.load(resource))) {
                    return Result::failure("Could not load cinematic " +
                                           cinematic.name + ": " +
                                           result.message());
                }
                result = cinematic.cameraTrack.load(cinematic.script);
                if (!result) {
                    return Result::failure("Could not load camera track for " +
                                           cinematic.name + ": " +
                                           result.message());
                }
                cinematic.scriptAvailable = true;
                cinematics_.push_back(std::move(cinematic));
                continue;
            }
            const bool meleeThug =
                node.gameType.starts_with("MeleeThugEnemy_");
            const bool gunThug = node.gameType == "MeleeThug_gun";
            const bool bigRangeThug = node.gameType == "RangeThug_big";
            const bool hammerThug = node.gameType == "RangeThug_hammer";
            const bool sandman = node.gameType == "Boss_Sandman";
            if (!meleeThug && !gunThug && !bigRangeThug && !hammerThug &&
                !sandman) {
                continue;
            }

            std::string resolvedAnimationFile = node.animationFile;
            if (entityArchive.find(
                    normalizeArchivePath(resolvedAnimationFile)) == nullptr &&
                node.gameType == "MeleeThugEnemy_knife") {
                // Level 1 authors knife enemies with a nonexistent
                // thug_knife_anim.bdae. The shipped archive and cinematic
                // knife actor both use the shared bat/knife animation bank.
                resolvedAnimationFile =
                    "../entities/meshes_bin/thug_bat_anim.bdae";
            }
            const auto archetype = std::find_if(
                enemyArchetypes_.begin(), enemyArchetypes_.end(),
                [&node, &resolvedAnimationFile](
                    const EnemyArchetypeAsset& candidate) {
                    return candidate.meshFile == node.meshFile &&
                           candidate.animationFile == resolvedAnimationFile;
                });
            std::size_t archetypeIndex = 0;
            if (archetype == enemyArchetypes_.end()) {
                EnemyArchetypeAsset asset;
                asset.gameType = node.gameType;
                asset.meshFile = node.meshFile;
                asset.animationFile = resolvedAnimationFile;
                result = entityArchive.read(
                    normalizeArchivePath(asset.meshFile), resource);
                if (!result || !(result = asset.mesh.load(resource))) {
                    return Result::failure("Could not load enemy mesh " +
                                           asset.meshFile + ": " +
                                           result.message());
                }
                result = loadTextures(entityArchive, nullptr, asset.mesh,
                                      asset.textures, asset.gameType);
                if (!result) {
                    return result;
                }
                result = entityArchive.read(
                    normalizeArchivePath(asset.animationFile), resource);
                if (!result || !(result = asset.animationBank.load(resource))) {
                    return Result::failure("Could not load enemy animation " +
                                           asset.animationFile + ": " +
                                           result.message());
                }
                enemyArchetypes_.push_back(std::move(asset));
                archetypeIndex = enemyArchetypes_.size() - 1;
            } else {
                archetypeIndex = static_cast<std::size_t>(
                    std::distance(enemyArchetypes_.begin(), archetype));
            }

            LevelEnemyAsset enemy;
            enemy.objectId = node.id;
            enemy.name = node.name;
            enemy.gameType = node.gameType;
            enemy.enemyTypeId = static_cast<std::int16_t>(integerAttribute(
                node, sandman ? "Boss_Type" : "Enemy_Type",
                sandman         ? 16
                : gunThug       ? 3
                : bigRangeThug  ? 4
                : hammerThug    ? 5
                : node.gameType == "MeleeThugEnemy_knife" ? 0
                                                           : 1));
            enemy.initialAnimation = node.initialAnimation;
            if (enemy.initialAnimation.empty()) {
                enemy.initialAnimation =
                    bigRangeThug
                        ? "idlebaz"
                    : gunThug || hammerThug || sandman
                        ? "idle"
                    : node.gameType == "MeleeThugEnemy_knife"
                        ? "idle_knife_at_idle"
                        : "idle_at1_idle";
            }
            enemy.archetypeIndex = archetypeIndex;
            enemy.roomId = static_cast<std::int32_t>(roomIndex + 1);
            enemy.position = worldPosition(node);
            enemy.rotation = node.rotation;
            enemy.scale = node.scale;
            enemy.worldTransform = node.absoluteTransform;
            enemy.health = floatAttribute(node, "Health");
            enemy.visible = node.visible;
            enemy.aiEnabled = booleanAttribute(node, "AI_Enable", true);
            enemy.waitSpawn = booleanAttribute(node, "WaitSpawn", false);
            enemy.lineSpeedCentimetersPerMillisecond =
                floatAttribute(node, "Line_Speed", 0.3F);
            enemy.awarenessRadius = floatAttribute(node, "Aware_Radius");
            enemy.awarenessAngleDegrees =
                floatAttribute(node, "Aware_Angle");
            if (enemyArchetypes_[archetypeIndex].animationBank.findClip(
                    enemy.initialAnimation) == nullptr) {
                return Result::failure("Enemy " + std::to_string(enemy.objectId) +
                                       " initial animation " +
                                       enemy.initialAnimation + " is missing");
            }
            enemies_.push_back(std::move(enemy));
        }
    }

    // Cinematic 1265 is retained by the dedicated boot sequence below. The
    // other PlayDAECamera streams are loaded here for native in-level
    // playback using the same scene-node actor bindings.
    for (LevelCinematicAsset& cinematic : cinematics_) {
        if (!cinematic.scriptAvailable || cinematic.objectId == 1265) {
            continue;
        }
        const CinematicCommand* cameraCommand = nullptr;
        for (const CinematicThread& thread : cinematic.script.threads()) {
            for (const CinematicCommand& command : thread.commands) {
                if (command.name == "PlayDAECamera") {
                    if (cameraCommand != nullptr) {
                        return Result::failure(
                            "Cinematic has multiple PlayDAECamera commands");
                    }
                    cameraCommand = &command;
                }
            }
        }
        if (cameraCommand == nullptr) {
            continue;
        }
        const CinematicAttribute* cameraFile =
            cameraCommand->findAttribute("CameraAnimFile");
        const CinematicAttribute* farPlane =
            cameraCommand->findAttribute("farPlane");
        const CinematicAttribute* nextCinematic =
            cameraCommand->findAttribute("^ID^Cinematic^Next");
        float farPlaneOverride = 0.0F;
        if (cameraFile == nullptr || farPlane == nullptr ||
            nextCinematic == nullptr ||
            !parseFloatText(farPlane->value, farPlaneOverride) ||
            !parseIntegerText(nextCinematic->value,
                              cinematic.nextCinematicId)) {
            return Result::failure(
                "PlayDAECamera has invalid camera attributes");
        }
        if (const CinematicAttribute* levelEnd =
                cameraCommand->findAttribute("level end")) {
            cinematic.levelEndAfterPlayback = levelEnd->value == "true" ||
                                              levelEnd->value == "1";
        }
        if (const CinematicAttribute* gameEnd =
                cameraCommand->findAttribute("game end")) {
            cinematic.gameEndAfterPlayback = gameEnd->value == "true" ||
                                             gameEnd->value == "1";
        }
        cinematic.cameraAnimationFile =
            normalizeArchivePath(cameraFile->value);
        result = levelArchive.read(cinematic.cameraAnimationFile, resource);
        if (!result ||
            !(result = cinematic.cameraAnimation.load(resource)) ||
            !(result = cinematic.animatedCamera.bind(
                  cinematic.cameraAnimation, farPlaneOverride))) {
            return Result::failure("Could not load animated camera for " +
                                   cinematic.name + ": " +
                                   result.message());
        }
        cinematic.colladaDurationMilliseconds =
            cinematic.cameraAnimation.durationMilliseconds();

        for (const CinematicThread& thread : cinematic.script.threads()) {
            for (const CinematicCommand& command : thread.commands) {
                if (command.name != "PlayDAEAnim") {
                    continue;
                }
                const assets::IrrSceneNode* sceneNode =
                    findLevelNode(mainScene_, rooms_, thread.objectId);
                if (sceneNode == nullptr) {
                    return Result::failure(
                        "Cinematic actor command has no matching scene node");
                }
                CinematicActorAsset actor;
                result = loadCinematicActor(levelArchive, entityArchive,
                                            *sceneNode, command,
                                            thread.objectId, actor);
                if (!result) {
                    return Result::failure("Could not load actors for " +
                                           cinematic.name + ": " +
                                           result.message());
                }
                cinematic.colladaDurationMilliseconds =
                    std::max(cinematic.colladaDurationMilliseconds,
                             actor.animationStartMilliseconds +
                                 actor.animation.durationMilliseconds());
                cinematic.actors.push_back(std::move(actor));
            }
        }
        if (cinematic.actors.empty()) {
            return Result::failure(
                "Animated cinematic has no actor animations");
        }
    }

    introSky_.name = "Level 1 sky";
    result = levelArchive.read("meshes_bin/lvl01_sky.bdae", resource);
    if (!result) {
        return result;
    }
    result = introSky_.geometry.load(resource);
    if (!result || introSky_.geometry.geometries().empty()) {
        return Result::failure("Could not parse Level 1 sky: " +
                               result.message());
    }
    result = loadTextures(levelArchive, &entityArchive, introSky_.geometry,
                          introSky_.textures, introSky_.name);
    if (!result) {
        return result;
    }
    const auto skyNode = std::find_if(
        mainScene_.nodes().begin(), mainScene_.nodes().end(),
        [](const assets::IrrSceneNode& node) {
            return node.meshFile.find("lvl01_sky.bdae") != std::string::npos;
        });
    if (skyNode == mainScene_.nodes().end()) {
        return Result::failure("Level 1 scene has no sky mesh node");
    }
    // CSkyBoxObject::Update (0x0031b6f4) replaces the serialized node
    // position with the active camera position every frame.
    introSky_.cameraRelative = true;

    result = levelArchive.read(
        "cinematics/levelnew_01_1264_cinematic.cff", resource);
    if (!result) {
        return result;
    }
    result = introStartScript_.load(resource);
    if (!result) {
        return result;
    }
    result = levelArchive.read(
        "cinematics/levelnew_01_1265_cinematic.cff", resource);
    if (!result) {
        return result;
    }
    result = introScript_.load(resource);
    if (!result) {
        return result;
    }
    result = levelArchive.read(
        "cinematics/levelnew_01_1266_cinematic.cff", resource);
    if (!result) {
        return result;
    }
    result = introEndScript_.load(resource);
    if (!result) {
        return result;
    }
    result = levelArchive.read("meshes_bin/camera_lv1_start.bdae", resource);
    if (!result) {
        return result;
    }
    result = introCameraAnimation_.load(resource);
    if (!result) {
        return result;
    }
    // PlayDAECamera in levelnew_01_1265_cinematic.cff overrides the BDAE's
    // 1000-unit far plane with 10000 units.
    result = introCamera_.bind(introCameraAnimation_, 10000.0F);
    if (!result) {
        return result;
    }

    for (const CinematicThread& thread : introScript_.threads()) {
        for (const CinematicCommand& command : thread.commands) {
            if (command.name != "PlayDAEAnim") {
                continue;
            }
            const CinematicAttribute* animationFile =
                command.findAttribute("AnimFile");
            const assets::IrrSceneNode* sceneNode =
                findLevelNode(mainScene_, rooms_, thread.objectId);
            if (animationFile == nullptr || sceneNode == nullptr ||
                sceneNode->meshFile.empty()) {
                introActors_.clear();
                return Result::failure(
                    "Cinematic actor command has no matching scene node");
            }

            CinematicActorAsset actor;
            actor.objectId = thread.objectId;
            actor.sceneNodeName = sceneNode->name;
            actor.animationStartMilliseconds = command.timestampMilliseconds;
            actor.position = sceneNode->position;
            actor.rotation = sceneNode->rotation;
            actor.scale = sceneNode->scale;
            actor.worldTransform = sceneNode->absoluteTransform;

            result = entityArchive.read(
                normalizeArchivePath(sceneNode->meshFile), resource);
            if (!result) {
                introActors_.clear();
                return Result::failure("Could not load cinematic actor " +
                                       actor.sceneNodeName + ": " +
                                       result.message());
            }
            result = actor.mesh.load(resource);
            if (!result) {
                introActors_.clear();
                return Result::failure("Could not parse cinematic actor " +
                                       actor.sceneNodeName + ": " +
                                       result.message());
            }
            result = loadTextures(entityArchive, nullptr, actor.mesh,
                                  actor.textures, actor.sceneNodeName);
            if (!result) {
                introActors_.clear();
                return result;
            }
            result = levelArchive.read(
                normalizeArchivePath(animationFile->value), resource);
            if (!result) {
                introActors_.clear();
                return Result::failure("Could not load animation for " +
                                       actor.sceneNodeName + ": " +
                                       result.message());
            }
            result = actor.animation.load(resource);
            if (!result) {
                introActors_.clear();
                return Result::failure("Could not parse animation for " +
                                       actor.sceneNodeName + ": " +
                                       result.message());
            }
            introActors_.push_back(std::move(actor));
        }
    }
    if (introActors_.empty()) {
        return Result::failure("Level-one intro has no cinematic actors");
    }
    const auto isDedicatedCinematicActor = [this](std::int32_t objectId) {
        if (std::any_of(
                introActors_.begin(), introActors_.end(),
                [objectId](const CinematicActorAsset& actor) {
                    return actor.objectId == objectId;
                })) {
            return true;
        }
        return std::any_of(
            cinematics_.begin(), cinematics_.end(),
            [objectId](const LevelCinematicAsset& cinematic) {
                return std::any_of(
                    cinematic.actors.begin(), cinematic.actors.end(),
                    [objectId](const CinematicActorAsset& actor) {
                        return actor.objectId == objectId;
                    });
            });
    };
    std::erase_if(objects_, [&isDedicatedCinematicActor](
                                const LevelObjectAsset& object) {
        return object.kind == LevelObjectKind::Animated &&
               isDedicatedCinematicActor(object.objectId);
    });
    return Result::success();
}

} // namespace usm::game
