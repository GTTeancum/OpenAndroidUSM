#include "game/LevelOneBootstrap.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace usm::game {
namespace {

std::string normalizeArchivePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    // Level 2 Room 18 object 40039 serializes
    // ../entities/meshes_bin/car_mesh.bdae' as its AnimationFile. No archive
    // contains the apostrophe-suffixed path; the mesh path immediately above
    // names the intended bind-pose resource. Portable archive resolution
    // treats this malformed terminal quote as editor residue.
    if (path.ends_with('\'')) {
        path.pop_back();
    }
    while (path.starts_with("./")) {
        path.erase(0, 2);
    }
    constexpr std::string_view entityPrefix = "../entities/";
    if (path.starts_with(entityPrefix)) {
        path.erase(0, entityPrefix.size());
    }
    // The original constructors pass the authored MeshFile string directly
    // to Irrlicht's mounted virtual filesystem. Later room exports retain
    // editor paths such as ../../../data/game/levelnew_09/meshes_bin/..., but
    // the corresponding GBMP stores the resource beneath meshes_bin/. Rebase
    // that virtual mount path instead of treating the editor workstation path
    // as an archive entry name. CLevel::LoadNextObject does this for Geometry,
    // Collisions and NavMesh at 0x003853fc.
    constexpr std::string_view meshDirectory = "meshes_bin/";
    if (const std::size_t offset = path.rfind(meshDirectory);
        offset != std::string::npos) {
        path.erase(0, offset);
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

std::int32_t destroyableHitVoxSoundId(std::string meshFile) noexcept {
    meshFile = asciiLower(std::move(meshFile));
    struct Mapping {
        std::string_view fragment;
        std::int32_t voxSoundId;
    };
    // CDestroyableObject::Hit (0x00307fb8), keyed by the mesh categories
    // assigned in its constructor at 0x003072c8.
    constexpr std::array mappings{
        Mapping{"mailbox", 0x69},          Mapping{"garbagecan", 0x6d},
        Mapping{"shoppingcart", 0x6a},     Mapping{"lamppost", 0x6b},
        Mapping{"trafficlight", 0x6b},     Mapping{"cone", 0x6c},
        Mapping{"hidrant", 0x67},          Mapping{"pot", 0x66},
        Mapping{"crate", 0x65},            Mapping{"bankchair", 0x7d},
        Mapping{"bankoffice", 0x7a},       Mapping{"barber", 0x70},
        Mapping{"barrier", 0x71},          Mapping{"bigofficetable", 0x78},
        Mapping{"bookstand", 0x73},        Mapping{"vent_fan_box", 0x7e},
        Mapping{"vendingmachine", 0x7b},   Mapping{"steam_piping", 0x7c},
        Mapping{"officetable", 0x79},      Mapping{"officechair", 0x77},
        Mapping{"hotdog", 0x76},           Mapping{"gumdisp", 0x75},
        Mapping{"fire_extinguisher", 0x74},
        Mapping{"battery_cell", 0x72},     Mapping{"antenna", 0x6f},
        Mapping{"air_con", 0x6e},          Mapping{"busstop", 0x7f},
        Mapping{"newspaper", 0x80},        Mapping{"bench", 0x81},
        Mapping{"boxbroken", 0x155},
    };
    for (const Mapping& mapping : mappings) {
        if (meshFile.find(mapping.fragment) != std::string::npos) {
            return mapping.voxSoundId;
        }
    }
    return -1;
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
    // CLevel::LoadNextObject (0x003853fc) selects CSlideCar for the complete
    // SlideCar prefix, not just the bus variant used by an earlier probe.
    if (gameType.starts_with("SlideCar")) {
        return LevelObjectKind::SlideCar;
    }
    if (gameType == "PlatForm") {
        return LevelObjectKind::Platform;
    }
    if (gameType == "ElectricPlatForm") {
        return LevelObjectKind::ElectricPlatform;
    }
    if (gameType == "Train") {
        return LevelObjectKind::Train;
    }
    // CLevel::LoadNextObject (0x003853fc) uses a prefix factory branch.
    if (gameType.starts_with("BrokenBridge")) {
        return LevelObjectKind::BrokenBridge;
    }
    if (gameType == "AreaDamage") {
        return LevelObjectKind::AreaDamage;
    }
    return std::nullopt;
}

bool isBossGameType(std::string_view gameType) noexcept {
    // CLevel::LoadNextObject (0x003853fc) constructs the same CBoss class for
    // this complete authored set. Keep the factory grouping here instead of
    // promoting individual bosses only when a later level happens to need
    // them. Robot_Phantom is intentionally included: it is in the native
    // CBoss branch despite not using the Boss_ editor prefix.
    constexpr std::array bossTypes{
        std::string_view{"Boss_Rhino"},
        std::string_view{"Boss_Electro"},
        std::string_view{"Boss_Venom"},
        std::string_view{"Robot_Phantom"},
        std::string_view{"Boss_Sandman"},
        std::string_view{"Boss_Venom_On_Train"},
        std::string_view{"Boss_Green_Goblin"},
        std::string_view{"Boss_Dr_Octopus"},
    };
    return std::find(bossTypes.begin(), bossTypes.end(), gameType) !=
           bossTypes.end();
}

bool isEnemyGameType(std::string_view gameType) noexcept {
    // CLevel::LoadNextObject (0x003853fc, shared CEnemy branch at
    // 0x003863c4) routes this complete editor-type set through CEnemy.
    // These are not render-only scene actors: cinematics address their IDs
    // and ordinary room progression waits on their destroyed state.
    constexpr std::array enemyTypes{
        std::string_view{"MeleeThugEnemy_knife"},
        std::string_view{"MeleeThugEnemy_bat"},
        std::string_view{"MeleeThugEnemy_electrodes"},
        std::string_view{"RangeThug_molotov"},
        std::string_view{"MeleeThug_gun"},
        std::string_view{"RangeThug_big"},
        std::string_view{"RangeThug_hammer"},
        std::string_view{"SymbioteZombie_Boy"},
        std::string_view{"SymbioteZombie_Girl"},
        std::string_view{"SymbioteZombie_Boy_Toxin"},
        std::string_view{"SymbioteZombie_Girl_Common"},
        std::string_view{"Robot_Shield"},
        std::string_view{"Goblinite"},
        std::string_view{"Goblin_Range"},
        std::string_view{"Aircraft_blue"},
        std::string_view{"Aircraft_red"},
        std::string_view{"Robot_Rocket"},
    };
    return std::find(enemyTypes.begin(), enemyTypes.end(), gameType) !=
           enemyTypes.end();
}

Result loadTextures(filesystem::GbmpArchive& primaryArchive,
                    filesystem::GbmpArchive* fallbackArchive,
                    const assets::ColladaMeshFile& mesh,
                    std::vector<assets::BtexTexture>& output,
                    std::string_view assetName,
                    filesystem::GbmpArchive* secondaryFallbackArchive =
                        nullptr,
                    filesystem::GbmpArchive* inheritedLevelArchive =
                        nullptr,
                    filesystem::GbmpArchive* previousLevelArchive =
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
                                  secondaryFallbackArchive,
                                  previousLevelArchive,
                                  inheritedLevelArchive};
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
                mesh.geometries().begin(), mesh.geometries().end(),
                [&mesh, imageIndex](const assets::ColladaGeometry& geometry) {
                    return std::any_of(
                        geometry.meshBuffers.begin(),
                        geometry.meshBuffers.end(),
                        [&mesh, imageIndex](
                            const assets::ColladaMeshBuffer& buffer) {
                            const assets::ColladaMaterial* material =
                                mesh.findMaterial(buffer.materialName);
                            return material != nullptr &&
                                (material->diffuseImageIndex == imageIndex ||
                                 material->secondaryImageIndex == imageIndex ||
                                 material->lightmapImageIndex == imageIndex);
                        });
                });
            if (!referenced) {
                // Some shipped BDAE libraries retain complete editor material
                // catalogs even though the exported geometry binds only a
                // small subset. Level 2's break_wall_anim.bdae, for example,
                // declares 51 images while every debris buffer binds
                // Material__76 / 25_atlas.tga. Defer stale entries so a
                // missing leading image can reuse the first valid decoded
                // texture while preserving every material's image index.
                missingUnusedImageIndices.push_back(imageIndex);
                output.emplace_back();
                continue;
            }
            // Irrlicht's getTexture path leaves a missing texture slot null;
            // it does not reject the containing scene. Several shipped later
            // levels deliberately retain referenced editor texture names for
            // files absent from every GBMP (for example Level 5's
            // levelnew_05_04.tga). Keep the image index stable and let the
            // renderer bind its neutral texture for this null slot.
            output.emplace_back();
            continue;
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
                          filesystem::GbmpArchive* inheritedLevelArchive,
                          filesystem::GbmpArchive* previousLevelArchive,
                          const assets::IrrSceneNode& sceneNode,
                          const CinematicCommand& command,
                          std::int32_t objectId,
                          CinematicActorAsset& actor) {
    const CinematicAttribute* animationFile =
        command.findAttribute("AnimFile");
    const CinematicAttribute* clipIdAttribute =
        command.findAttribute("clipID");
    std::int32_t clipId = 0;
    if (animationFile == nullptr || sceneNode.meshFile.empty()) {
        return Result::failure(
            "Cinematic actor command has no mesh or animation file");
    }
    if (clipIdAttribute != nullptr &&
        !parseIntegerText(clipIdAttribute->value, clipId)) {
        return Result::failure("Cinematic actor command has invalid clipID");
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
                          actor.textures, actor.sceneNodeName, nullptr,
                          inheritedLevelArchive, previousLevelArchive);
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
    if (clipId < 0 ||
        static_cast<std::size_t>(clipId) >= actor.animation.clips().size()) {
        return Result::failure("Cinematic actor command references invalid "
                               "animation clip " +
                               std::to_string(clipId));
    }
    const assets::ColladaAnimationClip& clip =
        actor.animation.clips()[static_cast<std::size_t>(clipId)];
    // CCinematicThread::PlayDAEAnim (0x003709ec) pushes the authored BDAE
    // animator and selects clipID through IAnimatedObject. Playback therefore
    // begins at the selected clip's start, not at timestamp zero in the file.
    actor.animationClipId = clipId;
    actor.animationClipStartMilliseconds = clip.startMilliseconds;
    actor.animationClipEndMilliseconds = clip.endMilliseconds;
    return Result::success();
}

} // namespace

Result LevelOneBootstrap::load(const std::filesystem::path& gameDataRoot,
                               std::uint32_t levelNumber) {
    if (levelNumber == 0 || levelNumber > 99) {
        return Result::failure("Level number must be between 1 and 99");
    }
    const std::string levelStem =
        "levelnew_" + std::string(levelNumber < 10 ? "0" : "") +
        std::to_string(levelNumber);
    levelNumber_ = levelNumber;
    rooms_.clear();
    introSky_ = {};
    introStartScript_ = {};
    introScript_ = {};
    introEndScript_ = {};
    introCameraAnimation_ = {};
    introCamera_ = {};
    introColladaDurationMilliseconds_ = 0;
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
    molotovProjectile_ = {};
    boomerangProjectile_ = {};
    objectArchetypes_.clear();
    objects_.clear();
    environmentEffects_.clear();
    bonuses_.clear();
    hints_.clear();
    damageVolumes_.clear();
    restoreTriggers_.clear();
    restorePoints_.clear();
    checkPoints_.clear();
    dropAreas_.clear();
    dropObjects_.clear();
    triggerSounds_.clear();
    hud_ = {};
    effects_ = {};
    playerHitEffects_.clear();
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
    result = playerHitEffectConfigs_.load(gameDataRoot);
    if (!result) {
        return Result::failure("Could not load player hit effects: " +
                               result.message());
    }
    result = quickTimeActionConfigs_.load(gameDataRoot);
    if (!result) {
        return Result::failure("Could not load quick-time action configs: " +
                               result.message());
    }
    result = textCatalog_.load(gameDataRoot, "EN", levelStem);
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
    result = levelArchive.open(gameDataRoot / (levelStem + ".pack"));
    if (!result) {
        return result;
    }
    // The shipped Level 2 animated bank geometry still references
    // 24_build.tga from Level 1. The original level transition leaves the
    // preceding level archive available to the virtual filesystem, so a
    // direct Level 2 reconstruction mounts it explicitly as an inherited
    // texture source.
    filesystem::GbmpArchive inheritedLevelArchive;
    filesystem::GbmpArchive* inheritedLevelTextures = nullptr;
    if (levelNumber != 1) {
        result = inheritedLevelArchive.open(gameDataRoot /
                                            "levelnew_01.pack");
        if (!result) {
            return Result::failure(
                "Could not mount inherited Level 1 textures: " +
                result.message());
        }
        inheritedLevelTextures = &inheritedLevelArchive;
    }
    // Direct autoplay bootstraps do not execute the preceding level
    // transition, so mount the immediately preceding pack explicitly. The
    // original virtual filesystem still has it available when later assets
    // reference an earlier level (Level 12's sky uses Level 11's texture).
    filesystem::GbmpArchive previousLevelArchive;
    filesystem::GbmpArchive* previousLevelTextures = nullptr;
    if (levelNumber > 2) {
        const std::string previousStem =
            "levelnew_" +
            std::string(levelNumber - 1 < 10 ? "0" : "") +
            std::to_string(levelNumber - 1);
        result = previousLevelArchive.open(gameDataRoot /
                                           (previousStem + ".pack"));
        if (!result) {
            return Result::failure(
                "Could not mount preceding level textures: " +
                result.message());
        }
        previousLevelTextures = &previousLevelArchive;
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

    const auto loadAtcaSprite =
        [&spriteArchive, &resource](std::string_view stem,
                                    assets::SpriteAtlas& atlas,
                                    assets::DdsAtcTexture& texture) {
            Result loadResult = spriteArchive.read(
                std::string(stem) + ".bsprite", resource);
            if (loadResult) {
                loadResult = atlas.load(resource);
            }
            if (loadResult) {
                loadResult = spriteArchive.read(
                    std::string(stem) + ".tga", resource);
            }
            return loadResult ? texture.load(resource) : loadResult;
        };
    result = loadAtcaSprite("mainmenu", hud_.mainMenuAtlas,
                            hud_.mainMenuTexture);
    if (!result) {
        return Result::failure("Could not load main-menu sprite: " +
                               result.message());
    }
    result = loadAtcaSprite("tutorial", hud_.tutorialAtlas,
                            hud_.tutorialTexture);
    if (!result) {
        return Result::failure("Could not load tutorial sprite: " +
                               result.message());
    }
    result = loadAtcaSprite("transport", hud_.transportAtlas,
                            hud_.transportTexture);
    if (!result) {
        return Result::failure("Could not load transport sprite: " +
                               result.message());
    }
    result = loadAtcaSprite("font_normal_white", hud_.normalWhiteFontAtlas,
                            hud_.normalWhiteFontTexture);
    if (!result) {
        return Result::failure("Could not load normal white font: " +
                               result.message());
    }
    result = loadAtcaSprite("font_outline_small", hud_.outlineSmallFontAtlas,
                            hud_.outlineSmallFontTexture);
    if (!result) {
        return Result::failure("Could not load outline small font: " +
                               result.message());
    }
    result = loadAtcaSprite("font_outline_big", hud_.outlineBigFontAtlas,
                            hud_.outlineBigFontTexture);
    if (!result) {
        return Result::failure("Could not load outline big font: " +
                               result.message());
    }
    result = spriteArchive.read("bg_suit.bsprite", resource);
    if (result) {
        result = hud_.backgroundSuitAtlas.load(resource);
    }
    if (result) {
        result = spriteArchive.read("bg_suit.tga", resource);
    }
    if (result) {
        result = hud_.backgroundSuitTexture.load(resource);
    }
    if (!result) {
        return Result::failure("Could not load suit background sprite: " +
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

    // Player::LoadHitEffects (0x00344ab4) registers every BDAE named by the
    // 31-entry MCHitEffects.bin table up front. MC_STATE selects these exact
    // models at authored attack frames through auxiliaryIdLists[1].
    playerHitEffects_.reserve(playerHitEffectConfigs_.definitions().size());
    for (const PlayerHitEffectDefinition& definition :
         playerHitEffectConfigs_.definitions()) {
        PlayerHitEffectAsset effect;
        effect.definition = definition;
        const std::string meshPath =
            "meshes_bin/" + definition.meshFile;
        result = entityArchive.read(meshPath, resource);
        if (!result || !(result = effect.mesh.load(resource))) {
            return Result::failure("Could not load player hit effect " +
                                   definition.name + ": " +
                                   result.message());
        }
        if (definition.renderingParameter >= 0) {
            result = effect.animation.load(resource);
            if (!result) {
                return Result::failure(
                    "Could not load player hit-effect animation " +
                    definition.name + ": " + result.message());
            }
        }
        result = loadTextures(entityArchive, nullptr, effect.mesh,
                              effect.textures, definition.name);
        if (!result) {
            return result;
        }
        playerHitEffects_.push_back(std::move(effect));
    }

    // CBullet::setType(0) at 0x0035c444 passes this basename to
    // IAnimatedObject. The virtual filesystem resolves it inside
    // entities.pack's meshes_bin directory and hides the `bbox` child.
    webPelletProjectile_.meshFile = "meshes_bin/w_sm_webpellet.bdae";
    result = entityArchive.read(webPelletProjectile_.meshFile, resource);
    if (!result || !(result = webPelletProjectile_.mesh.load(resource))) {
        return Result::failure("Could not load player web pellet mesh: " +
                               result.message());
    }
    result = loadTextures(entityArchive, nullptr, webPelletProjectile_.mesh,
                          webPelletProjectile_.textures,
                          "player web pellet");
    if (!result) {
        return result;
    }

    // CLevel::InitAllWebLines (0x0037faa4-0x0037fb1e) requests this exact
    // resource from the video driver and assigns it to material layer zero.
    webLine_.textureFile = "textures_bin/web_rope.tga";
    result = entityArchive.read(webLine_.textureFile, resource);
    if (!result || !(result = webLine_.texture.load(resource))) {
        return Result::failure("Could not load native web-line texture: " +
                               result.message());
    }

    molotovProjectile_.meshFile = "meshes_bin/w_flamegrenade.bdae";
    result = entityArchive.read(molotovProjectile_.meshFile, resource);
    if (!result || !(result = molotovProjectile_.mesh.load(resource))) {
        return Result::failure("Could not load molotov projectile mesh: " +
                               result.message());
    }
    result = loadTextures(entityArchive, nullptr, molotovProjectile_.mesh,
                          molotovProjectile_.textures,
                          "molotov projectile");
    if (!result) {
        return result;
    }
    result = molotovProjectile_.animationBank.load(resource);
    if (!result) {
        return Result::failure(
            "Could not load molotov projectile animation bank: " +
            result.message());
    }

    // Both preserved CBoomerang constructors (0x0035b3fc and 0x0035b604)
    // pass `phantom_unit_weapons.bdae` to IAnimatedObject. Their instruction
    // sequences at 0x0035b560/0x0035b720 then load the `weapons` string from
    // 0x004e538a and call SetAnim(name, true, 0). The virtual filesystem
    // resolves that native basename in entities.pack's meshes_bin directory.
    boomerangProjectile_.meshFile =
        "meshes_bin/phantom_unit_weapons.bdae";
    result = entityArchive.read(boomerangProjectile_.meshFile, resource);
    if (!result || !(result = boomerangProjectile_.mesh.load(resource))) {
        return Result::failure("Could not load boomerang projectile mesh: " +
                               result.message());
    }
    result = loadTextures(entityArchive, nullptr,
                          boomerangProjectile_.mesh,
                          boomerangProjectile_.textures,
                          "boomerang projectile");
    if (!result) {
        return result;
    }
    result = boomerangProjectile_.animationBank.load(resource);
    if (!result) {
        return Result::failure(
            "Could not load boomerang projectile animation bank: " +
            result.message());
    }

    // CBehaviorRotate's constructor (0x003c2bfc) builds electro_wave.bdae;
    // CElectricPost's constructor (0x00354e94) builds electro_beam.bdae; and
    // CBehaviorWeak/CBehaviorElectroDush reference the billboard companion at
    // 0x0032afdc and 0x003b2b74. Load the exact packaged assets once so every
    // native Electro behavior shares their authored geometry and clips.
    const auto loadAnimatedEffectModel =
        [&entityArchive, &resource](ElectroEffectModelAsset& model,
                                    std::string_view meshFile,
                                    std::string_view label,
                                    bool requireAnimation) -> Result {
            model.meshFile = meshFile;
            Result loadResult = entityArchive.read(model.meshFile, resource);
            if (!loadResult || !(loadResult = model.mesh.load(resource))) {
                return Result::failure(
                    "Could not load " + std::string(label) + " mesh: " +
                    loadResult.message());
            }
            loadResult = loadTextures(entityArchive, nullptr, model.mesh,
                                      model.textures, label);
            if (!loadResult) {
                return loadResult;
            }
            loadResult = model.animationBank.load(resource);
            if (!loadResult) {
                if (!requireAnimation) {
                    model.animationBank = {};
                    return Result::success();
                }
                return Result::failure(
                    "Could not load " + std::string(label) +
                    " animation bank: " + loadResult.message());
            }
            return Result::success();
        };
    result = loadAnimatedEffectModel(
        electroEffects_.wave, "meshes_bin/electro_wave.bdae",
        "Electro wave", true);
    if (!result) {
        return result;
    }
    result = loadAnimatedEffectModel(
        electroEffects_.waveBillboard,
        "meshes_bin/electro_wave_billboard.bdae",
        "Electro wave billboard", true);
    if (!result) {
        return result;
    }
    result = loadAnimatedEffectModel(
        electroEffects_.beam, "meshes_bin/electro_beam.bdae",
        "Electro beam", true);
    if (!result) {
        return result;
    }

    // CBehaviorHurt::BehaviorUpdate (0x003b8dca/0x003b8e38) names both
    // resources directly. They are shared EffectManager assets rather than
    // level-authored scene objects, so load them from entities.pack here.
    result = loadAnimatedEffectModel(
        enemyLandingEffects_.shockwave,
        "meshes_bin/fx_shockwave.bdae", "enemy landing shockwave", false);
    if (!result) {
        return result;
    }
    result = loadAnimatedEffectModel(
        enemyLandingEffects_.crashWall,
        "meshes_bin/crashwall.bdae", "enemy landing crash wall", false);
    if (!result) {
        return result;
    }

    result = levelArchive.read(levelStem + ".irr", resource);
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
            levelStem + " scene has no complete Spider-Man player node");
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
    const std::filesystem::path playerAnimationPath{
        normalizeArchivePath(playerNode->animationFile)};
    const std::string displacementStem =
        playerAnimationPath.stem().generic_string();
    std::vector<std::byte> dummyDisplacement;
    std::vector<std::byte> pelvisDisplacement;
    result = entityArchive.read(
        "exported_meshes/" + displacementStem + "_dummy.bin",
        dummyDisplacement);
    if (!result) {
        return Result::failure("Could not load player dummy displacement: " +
                               result.message());
    }
    result = entityArchive.read(
        "exported_meshes/" + displacementStem + "_pelvis.bin",
        pelvisDisplacement);
    if (!result) {
        return Result::failure("Could not load player pelvis displacement: " +
                               result.message());
    }
    result = player_.animationDisplacement.load(dummyDisplacement,
                                                pelvisDisplacement);
    if (!result) {
        return Result::failure("Could not parse player displacement: " +
                               result.message());
    }

    if (mainScene_.linkedSceneFiles().empty()) {
        return Result::failure(levelStem + " scene has no linked rooms");
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
        room.objectId = geometryNode->id;
        room.position = geometryNode->position;
        room.motionInitiallyActive =
            booleanAttribute(*geometryNode, "Active", false);
        room.lineSpeedCentimetersPerMillisecond =
            floatAttribute(*geometryNode, "Line_Speed");
        room.linkedWaypointId =
            integerAttribute(*geometryNode, "^Link^WayPoint");
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
                              room.textures, room.name, nullptr,
                              inheritedLevelTextures,
                              previousLevelTextures);
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
        // CLevel::LoadNextObject (0x003853fc) initializes physics and builds a
        // navmesh only when the corresponding authored nodes are encountered.
        // It does not require both nodes. Levels 7, 8 and 11 intentionally
        // contain visual-only rooms or collision-only rooms.
        if (collisionNode != room.scene.nodes().end()) {
            const std::string collisionPath =
                normalizeArchivePath(collisionNode->meshFile);
            result = levelArchive.read(collisionPath, resource);
            if (!result) {
                return Result::failure("Could not read collision mesh " +
                                       collisionPath + " for " + room.name +
                                       ": " + result.message());
            }
            result = room.collision.load(resource);
            if (!result) {
                return Result::failure("Could not parse collision mesh " +
                                       collisionPath + " for " + room.name +
                                       ": " + result.message());
            }
        }
        if (navigationNode != room.scene.nodes().end()) {
            const std::string navigationPath =
                normalizeArchivePath(navigationNode->meshFile);
            result = levelArchive.read(navigationPath, resource);
            if (!result) {
                return Result::failure("Could not read navigation mesh " +
                                       navigationPath + " for " + room.name +
                                       ": " + result.message());
            }
            result = room.navigationMesh.load(resource);
            if (!result) {
                return Result::failure("Could not parse navigation mesh " +
                                       navigationPath + " for " + room.name +
                                       ": " + result.message());
            }
        }
        // Several later shipped levels contain valid, intentionally empty
        // collision or navigation BDAE resources for presentation-only room
        // shells. The native scene graph accepts those meshes; absence of a
        // geometry record is not a parse failure and must not block loading
        // unrelated rooms.
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
                    // The native CLevel::LoadNextObject factory explicitly
                    // accepts !GameType=Effect without constructing a game
                    // object (0x003853fc). Preserve known static emitters used
                    // by the reconstruction, but editor typos such as Level
                    // 4's big_firesomke placeholder cannot abort room loading.
                    continue;
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
            if (node.gameType == "Hint") {
                LevelHintAsset hint;
                hint.objectId = node.id;
                hint.linkedObjectId = integerAttribute(node, "^LinkID");
                hint.roomId = static_cast<std::int32_t>(roomIndex + 1);
                hint.animationIndex = integerAttribute(node, "AnimIndex", 0);
                hint.spriteFile =
                    std::string(userAttribute(node, "SpritesFile"));
                hint.position = worldPosition(node);
                hint.visible = booleanAttribute(node, "Visible", node.visible);
                if (hint.linkedObjectId < 0 || hint.spriteFile.empty()) {
                    return Result::failure(
                        "Hint " + std::to_string(node.id) +
                        " has invalid link or sprite attributes");
                }
                result = spriteArchive.read(hint.spriteFile, resource);
                if (!result || !(result = hint.atlas.load(resource))) {
                    return Result::failure("Could not load Hint sprite " +
                                           hint.spriteFile + ": " +
                                           result.message());
                }
                std::filesystem::path texturePath(hint.spriteFile);
                texturePath.replace_extension(".tga");
                result = spriteArchive.read(texturePath.generic_string(),
                                            resource);
                if (!result || !(result = hint.texture.load(resource))) {
                    return Result::failure("Could not load Hint texture " +
                                           texturePath.generic_string() +
                                           ": " + result.message());
                }
                hints_.push_back(std::move(hint));
                continue;
            }
            if (node.gameType == "EffectDamage") {
                LevelDamageAsset damage;
                damage.objectId = node.id;
                damage.roomId = static_cast<std::int32_t>(roomIndex + 1);
                damage.position = worldPosition(node);
                damage.rotation = node.rotation;
                damage.scale = node.scale;
                damage.sizes = vectorAttribute(node, "Sizes");
                damage.damage = floatAttribute(node, "Damage", 30.0F);
                if (damage.damage <= 0.1F) {
                    damage.damage = 30.0F;
                }
                damage.damageType = integerAttribute(node, "$DamageType", 0);
                damage.enabled = booleanAttribute(node, "Enable", true);
                if (damage.sizes.x == 0.0F || damage.sizes.y == 0.0F ||
                    damage.sizes.z == 0.0F || damage.damageType < 0 ||
                    damage.damageType > 1) {
                    return Result::failure("EffectDamage " +
                                           std::to_string(node.id) +
                                           " has invalid attributes");
                }
                damageVolumes_.push_back(std::move(damage));
                continue;
            }
            if (node.gameType == "CheckPoint") {
                LevelCheckPointAsset checkpoint;
                checkpoint.objectId = node.id;
                checkpoint.roomId =
                    static_cast<std::int32_t>(roomIndex + 1);
                checkpoint.position = worldPosition(node);
                checkpoint.rotation = node.rotation;
                checkpoint.scale = node.scale;
                checkpoint.worldTransform = node.absoluteTransform;
                checkpoint.sizes = vectorAttribute(node, "Sizes");
                checkpoint.savePosition =
                    booleanAttribute(node, "SavePosition", true);
                checkpoint.enabled =
                    booleanAttribute(node, "Enabled", true);
                checkpoint.orientedBox =
                    booleanAttribute(node, "IsOBBox", false);
                checkpoint.linkedWaypointId =
                    integerAttribute(node, "^Link^WayPoint");
                if (checkpoint.sizes.x <= 0.0F ||
                    checkpoint.sizes.y <= 0.0F ||
                    checkpoint.sizes.z <= 0.0F) {
                    return Result::failure("CheckPoint " +
                                           std::to_string(node.id) +
                                           " has invalid Sizes");
                }
                checkPoints_.push_back(std::move(checkpoint));
                continue;
            }
            if (node.gameType == "RestorePoint") {
                LevelRestorePointAsset point;
                point.objectId = node.id;
                point.roomId = static_cast<std::int32_t>(roomIndex + 1);
                point.position = worldPosition(node);
                const float facingLength = std::sqrt(
                    node.absoluteTransform[4] * node.absoluteTransform[4] +
                    node.absoluteTransform[5] * node.absoluteTransform[5]);
                if (facingLength > 1e-6F) {
                    point.facing = {
                        -node.absoluteTransform[4] / facingLength,
                        -node.absoluteTransform[5] / facingLength, 0.0F};
                }
                restorePoints_.push_back(std::move(point));
                continue;
            }
            if (node.gameType == "TriggerRestore") {
                LevelRestoreTriggerAsset trigger;
                trigger.objectId = node.id;
                trigger.roomId = static_cast<std::int32_t>(roomIndex + 1);
                trigger.restorePointId =
                    integerAttribute(node, "^Link^RestorePoint");
                trigger.position = worldPosition(node);
                trigger.rotation = node.rotation;
                trigger.scale = node.scale;
                trigger.worldTransform = node.absoluteTransform;
                trigger.sizes = vectorAttribute(node, "Sizes");
                trigger.damage = floatAttribute(node, "Damage", 0.0F);
                // CTriggerRestore::ProcessAttr (0x0036c170) retains these
                // three authored controls alongside Damage and RestorePoint.
                trigger.cinematicId =
                    integerAttribute(node, "^Link^Cinematic");
                trigger.fallAfterRestore =
                    booleanAttribute(node, "FallAfterRestore", false);
                trigger.useLastCheckpoint =
                    booleanAttribute(node, "UseLastCheckPoint", false);
                if (trigger.restorePointId < 0 ||
                    trigger.sizes.x == 0.0F ||
                    trigger.sizes.y == 0.0F ||
                    trigger.sizes.z == 0.0F || trigger.damage < 0.0F) {
                    return Result::failure("TriggerRestore " +
                                           std::to_string(node.id) +
                                           " has invalid attributes");
                }
                restoreTriggers_.push_back(std::move(trigger));
                continue;
            }
            if (node.gameType == "DropArea") {
                LevelDropAreaAsset area;
                area.objectId = node.id;
                area.roomId = static_cast<std::int32_t>(roomIndex + 1);
                area.position = worldPosition(node);
                area.sizes = vectorAttribute(node, "Sizes");
                area.effectType =
                    std::string(userAttribute(node, "$EffectType"));
                if (area.sizes.x == 0.0F || area.sizes.y == 0.0F ||
                    area.sizes.z == 0.0F || area.effectType.empty() ||
                    effects_.presets.find(area.effectType) == nullptr) {
                    return Result::failure("DropArea " +
                                           std::to_string(node.id) +
                                           " has invalid attributes");
                }
                dropAreas_.push_back(std::move(area));
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

    for (const LevelRestoreTriggerAsset& trigger : restoreTriggers_) {
        if (std::none_of(
                restorePoints_.begin(), restorePoints_.end(),
                [&trigger](const LevelRestorePointAsset& point) {
                    return point.objectId == trigger.restorePointId;
                })) {
            return Result::failure(
                "TriggerRestore references a missing restore point");
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
            std::string animationPath =
                normalizeArchivePath(node.animationFile);
            if (!animationPath.empty() &&
                entityArchive.find(animationPath) == nullptr &&
                levelArchive.find(animationPath) == nullptr) {
                std::int32_t authoredAnimationIndex = -1;
                if (parseIntegerText(node.initialAnimation,
                                     authoredAnimationIndex) &&
                    authoredAnimationIndex >= 0) {
                    // Level 2's end-cinematic actors name node-level
                    // *_anim.bdae files that are not shipped. Their actual
                    // PlayDAEAnim commands supply car_480_lv2_end.bdae and
                    // the other actor streams; the numeric @Anim otherwise
                    // leaves these objects in bind pose.
                    animationPath.clear();
                }
            }
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
                if (*kind == LevelObjectKind::AreaDamage &&
                    animationPath.empty()) {
                    // IAnimatedObject receives both paths at CAreaDamage's
                    // constructors (0x00302be0/0x00302e0c). When the second
                    // path is empty it can still resolve clips embedded in
                    // the mesh BDAE, as authored by the tentacle objects.
                    const Result embeddedAnimation =
                        archetype.animationBank.load(resource);
                    if (!embeddedAnimation &&
                        embeddedAnimation.message() !=
                            "BDAE animation library is invalid") {
                        return Result::failure(
                            "Could not load embedded AreaDamage animation " +
                            node.name + ": " +
                            embeddedAnimation.message());
                    }
                }
                filesystem::GbmpArchive* textureFallback =
                    meshArchive == &entityArchive ? &levelArchive
                                                   : &entityArchive;
                result = loadTextures(*meshArchive, textureFallback,
                                      archetype.mesh, archetype.textures,
                                      node.name, &spriteArchive,
                                      inheritedLevelTextures,
                                      previousLevelTextures);
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
                    // Several Level 2 Room 18 car instances point their
                    // AnimationFile at the static car_mesh.bdae while using
                    // a different visible car mesh. CAnimatedObject accepts
                    // the unresolved numeric @Anim and remains in bind pose;
                    // the resource is not evidence of a missing archive.
                    const bool authoredMeshWithoutAnimationLibrary =
                        !result && result.message() ==
                            "BDAE animation library is invalid";
                    if (!result && !authoredMeshWithoutAnimationLibrary) {
                        return Result::failure(
                            "Could not load animation for level object " +
                            node.name + " (ID " + std::to_string(node.id) +
                            ", " + node.animationFile + "): " +
                            result.message());
                    }
                }
                if (*kind == LevelObjectKind::Platform ||
                    *kind == LevelObjectKind::ElectricPlatform) {
                    // CPlatForm::Init (0x00318964) always constructs the
                    // independent transmission body from this fixed mesh,
                    // including when the authored room node says
                    // Collision=false. CElectricPlatForm derives from the
                    // same class and inherits that body.
                    archetype.physicsMeshFile = "meshes_bin/platform_phy.bdae";
                    result = entityArchive.read(archetype.physicsMeshFile,
                                                resource);
                    if (!result ||
                        !(result = archetype.physicsMesh.load(resource))) {
                        return Result::failure(
                            "Could not load CPlatForm physics mesh: " +
                            result.message());
                    }
                }
                if (*kind == LevelObjectKind::ElectricPlatform) {
                    // The ElectricPlatForm branch in CLevel::LoadNextObject
                    // (0x003853fc) calls SetMaterialAdditiveByTexName
                    // (0x00373838) with this exact texture-name fragment.
                    // This is a runtime override; the serialized BDAE
                    // material additive flag is correctly clear.
                    archetype.additiveTextureNameFragment = "electric_wr";
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
            object.initialAnimationLoops =
                booleanAttribute(node, "Loop", true);
            if (*kind == LevelObjectKind::Hostage &&
                !animationPath.empty()) {
                // CHostage::ProcessUserAttr (0x00338724) does not consume the
                // generic @Anim field. State 0 resolves @Anim1 by name and
                // starts it looping; CHostage::Init (0x003385e8) immediately
                // enters that state. This is the authored kneeling/captive
                // pose used by Level 3 hostage 30472.
                object.initialAnimation =
                    std::string(userAttribute(node, "@Anim1"));
                object.initialAnimationLoops = true;
                object.hostageAnimations = {
                    object.initialAnimation,
                    std::string(userAttribute(node, "@Anim2")),
                    std::string(userAttribute(node, "@Anim3")),
                    std::string(userAttribute(node, "@Anim4"))};
                object.hostageEnableRadius =
                    floatAttribute(node, "EnableRadius");
                object.hostageHealthOrbCount =
                    integerAttribute(node, "HPCount", 0);
                object.hostageSkillPointOrbCount =
                    integerAttribute(node, "SkillPointCount", 0);
                object.hostageHintHeight =
                    floatAttribute(node, "HeightAbove", 180.0F);
                const float buttonHeight =
                    floatAttribute(node, "ButtonHeightAbove", 0.0F);
                // The native constructor starts at 85. A negative authored
                // override is shifted down by another 100 before storage;
                // positive/zero values leave the constructor default intact.
                if (buttonHeight < 0.0F) {
                    object.hostageButtonHeight = buttonHeight - 100.0F;
                }
                object.hostageIsWoman =
                    asciiLower(objectArchetypes_[archetypeIndex].meshFile)
                        .find("woman") !=
                    std::string::npos;
            }
            object.archetypeIndex = archetypeIndex;
            object.roomId = static_cast<std::int32_t>(roomIndex + 1);
            object.position = worldPosition(node);
            object.rotation = node.rotation;
            object.scale = node.scale;
            object.worldTransform = node.absoluteTransform;
            object.visible =
                node.visible && *kind != LevelObjectKind::DropObject;
            object.hasCollision = node.hasCollision;
            object.additiveBlend =
                booleanAttribute(node, "AddColor", false);
            if (*kind == LevelObjectKind::AreaDamage) {
                object.initialAnimation =
                    std::string(userAttribute(node, "@Anim"));
                if (object.initialAnimation.empty() &&
                    objectArchetypes_[archetypeIndex]
                        .animationBank.findClip("still") != nullptr) {
                    // CAreaDamage::Init (0x003026bc) explicitly prefers the
                    // cached `still` clip before falling back to SetAnim.
                    object.initialAnimation = "still";
                }
                const auto& clips =
                    objectArchetypes_[archetypeIndex].animationBank.clips();
                if ((object.initialAnimation.empty() ||
                     objectArchetypes_[archetypeIndex]
                             .animationBank.findClip(
                                 object.initialAnimation) == nullptr) &&
                    !clips.empty()) {
                    // CAreaDamage::SetAnim (0x00302688) resolves @Anim and
                    // selects clip zero when the authored name is absent or
                    // invalid (several scenes serialize the literal `0`).
                    object.initialAnimation = clips.front().name;
                }
                object.initialAnimationLoops = false;
                object.damage = floatAttribute(node, "Damage", 30.0F);
                if (object.damage <= 0.1F) {
                    object.damage = 30.0F;
                }
                object.areaDamageType =
                    integerAttribute(node, "$DamageType", 0);
                object.areaDamageBeginDelayMilliseconds =
                    floatAttribute(node, "BeginDelayTime");
                object.areaDamageRandomLowMilliseconds =
                    floatAttribute(node, "RandomLowTime");
                object.areaDamageRandomHighMilliseconds =
                    floatAttribute(node, "RandomHighTime");
                object.areaDamageIgnorePhysics =
                    booleanAttribute(node, "IsIgnorePhysics", false);
                object.areaDamageActiveForever =
                    booleanAttribute(node, "ActiveForever", false);
                const std::string areaMesh = asciiLower(meshPath);
                object.areaDamageAutomaticDetection =
                    areaMesh.find("symbiote_trap_anim.bdae") ==
                        std::string::npos &&
                    areaMesh.find("symbiote_trap_02_anim.bdae") ==
                        std::string::npos &&
                    areaMesh.find("symbiote_bomb.bdae") ==
                        std::string::npos;
            }
            const auto& objectGeometries =
                objectArchetypes_[archetypeIndex].mesh.sceneGeometries();
            assets::Vector3 minimum{
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
            assets::Vector3 maximum{
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()};
            for (const assets::ColladaGeometry& geometry : objectGeometries) {
                minimum.x = std::min(minimum.x, geometry.bounds.minimum.x);
                minimum.y = std::min(minimum.y, geometry.bounds.minimum.y);
                minimum.z = std::min(minimum.z, geometry.bounds.minimum.z);
                maximum.x = std::max(maximum.x, geometry.bounds.maximum.x);
                maximum.y = std::max(maximum.y, geometry.bounds.maximum.y);
                maximum.z = std::max(maximum.z, geometry.bounds.maximum.z);
            }
            if (!objectGeometries.empty()) {
                const float extentX =
                    (maximum.x - minimum.x) * std::abs(object.scale.x);
                const float extentY =
                    (maximum.y - minimum.y) * std::abs(object.scale.y);
                const float extentZ =
                    (maximum.z - minimum.z) * std::abs(object.scale.z);
                object.collisionRadius =
                    0.5F * std::sqrt(extentX * extentX + extentY * extentY +
                                     extentZ * extentZ);
                object.collisionHeight = extentZ;
                if (*kind == LevelObjectKind::Comic) {
                    assets::Vector3 worldMinimum{
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max()};
                    assets::Vector3 worldMaximum{
                        std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest()};
                    for (std::uint32_t corner = 0; corner < 8; ++corner) {
                        const assets::Vector3 local{
                            (corner & 1U) != 0 ? maximum.x : minimum.x,
                            (corner & 2U) != 0 ? maximum.y : minimum.y,
                            (corner & 4U) != 0 ? maximum.z : minimum.z};
                        const auto& matrix = object.worldTransform;
                        const assets::Vector3 world{
                            local.x * matrix[0] + local.y * matrix[4] +
                                local.z * matrix[8] + matrix[12],
                            local.x * matrix[1] + local.y * matrix[5] +
                                local.z * matrix[9] + matrix[13],
                            local.x * matrix[2] + local.y * matrix[6] +
                                local.z * matrix[10] + matrix[14]};
                        worldMinimum.x = std::min(worldMinimum.x, world.x);
                        worldMinimum.y = std::min(worldMinimum.y, world.y);
                        worldMinimum.z = std::min(worldMinimum.z, world.z);
                        worldMaximum.x = std::max(worldMaximum.x, world.x);
                        worldMaximum.y = std::max(worldMaximum.y, world.y);
                        worldMaximum.z = std::max(worldMaximum.z, world.z);
                    }
                    // CComicCover::Init (0x00304440) expands the absolute
                    // mesh box by one complete original size on each side.
                    const assets::Vector3 center{
                        (worldMinimum.x + worldMaximum.x) * 0.5F,
                        (worldMinimum.y + worldMaximum.y) * 0.5F,
                        (worldMinimum.z + worldMaximum.z) * 0.5F};
                    const assets::Vector3 size{
                        worldMaximum.x - worldMinimum.x,
                        worldMaximum.y - worldMinimum.y,
                        worldMaximum.z - worldMinimum.z};
                    object.comicCollectionMinimum = {
                        center.x - size.x, center.y - size.y,
                        center.z - size.z};
                    object.comicCollectionMaximum = {
                        center.x + size.x, center.y + size.y,
                        center.z + size.z};
                    object.comicIndex = integerAttribute(node, "Index");
                    object.comicLevelStringId =
                        std::string(userAttribute(node, "$LEVEL_STRINGID"));
                }
            }
            if (*kind == LevelObjectKind::SpiderWebWall ||
                *kind == LevelObjectKind::SlideCar ||
                *kind == LevelObjectKind::BrokenBridge ||
                *kind == LevelObjectKind::AreaDamage) {
                object.collisionLocalMinimum = {
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max()};
                object.collisionLocalMaximum = {
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest()};
                for (const assets::ColladaGeometry& geometry :
                     objectGeometries) {
                    if (geometry.name != "bbox" &&
                        geometry.name != "Dummy_bbox") {
                        continue;
                    }
                    object.collisionLocalMinimum.x = std::min(
                        object.collisionLocalMinimum.x,
                        geometry.bounds.minimum.x);
                    object.collisionLocalMinimum.y = std::min(
                        object.collisionLocalMinimum.y,
                        geometry.bounds.minimum.y);
                    object.collisionLocalMinimum.z = std::min(
                        object.collisionLocalMinimum.z,
                        geometry.bounds.minimum.z);
                    object.collisionLocalMaximum.x = std::max(
                        object.collisionLocalMaximum.x,
                        geometry.bounds.maximum.x);
                    object.collisionLocalMaximum.y = std::max(
                        object.collisionLocalMaximum.y,
                        geometry.bounds.maximum.y);
                    object.collisionLocalMaximum.z = std::max(
                        object.collisionLocalMaximum.z,
                        geometry.bounds.maximum.z);
                    object.hasCollisionBounds = true;
                }
                if (!object.hasCollisionBounds &&
                    *kind == LevelObjectKind::AreaDamage &&
                    !objectGeometries.empty()) {
                    // CAreaDamage::Init (0x003026bc) falls back from the
                    // optional `bbox` child to the complete visual scene
                    // when it constructs physics. symbiote_bomb.bdae uses
                    // exactly that path.
                    object.collisionLocalMinimum = minimum;
                    object.collisionLocalMaximum = maximum;
                    object.hasCollisionBounds = true;
                }
                if (!object.hasCollisionBounds &&
                    *kind != LevelObjectKind::AreaDamage) {
                    return Result::failure(
                        node.gameType + " " + std::to_string(node.id) +
                        " has no authored bbox scene node");
                }
                // CSpiderWebWall::Init (0x0031ee04) passes its `bbox` child
                // to createCollisionMeshPhysics (0x003d8830) and forces all
                // triangles to native flags 0x06 at 0x003d8cc8. Like
                // CSlideCar, this constructs a transmission body regardless
                // of the room node's serialized Collision flag.
                if (*kind != LevelObjectKind::AreaDamage) {
                    object.hasCollision = true;
                }
            }
            if (*kind == LevelObjectKind::BrokenBridge) {
                // CBrokenBridge::Init/SetState (0x0030209c/0x00300dec)
                // selects idle explicitly; the IRR @Anim is unused.
                object.initialAnimation = "idle";
                object.initialAnimationLoops = true;
                object.hitVoxSoundId = 0x86;
                object.bridgeType = integerAttribute(node, "$BrokenBridgeType", 0);
                object.bridgeIdleShakeSeconds = floatAttribute(node, "Idle_ShakeTime", 1.0F);
                object.bridgeDropShakeSeconds = floatAttribute(node, "Drop_ShakeTime", 1.0F);
                object.bridgeDropDistance = floatAttribute(node, "Drop_Distance");
                object.bridgeDropAngleDegrees = floatAttribute(node, "Drop_Angle");
                object.bridgeDropSeconds = floatAttribute(node, "Drop_UseTime");
                object.bridgeSecondShakeSeconds = floatAttribute(node, "Drop2_ShakeTime", 1.0F);
                object.bridgeSecondDropDistance = floatAttribute(node, "Drop2_Distance");
                object.bridgeSecondAngleDegrees = floatAttribute(node, "Drop2_Angle");
                object.bridgeSecondDropSeconds = floatAttribute(node, "Drop2_UseTime");
                object.bridgeActivationDistance = floatAttribute(node, "DisFromSMan2Drop", 10.0F);
                object.bridgeCarRunSpeed = floatAttribute(node, "CarRunSpeed", 500.0F);
                if (object.bridgeType != 2 && object.bridgeDropSeconds <= 0.0F) {
                    return Result::failure("BrokenBridge has invalid first-drop duration");
                }
                if (object.bridgeType == 1 && object.bridgeSecondDropSeconds <= 0.0F) {
                    return Result::failure("BrokenBridge has invalid second-drop duration");
                }
            }
            if (*kind == LevelObjectKind::Platform ||
                *kind == LevelObjectKind::ElectricPlatform) {
                // CWayPointMover::ProcessUserAttr (0x003269f0) owns Active
                // and Line_Speed. CPlatForm::ProcessUserAttr (0x0031874c)
                // adds ParkDuration and ActiveForever.
                object.platformParkDurationMilliseconds =
                    floatAttribute(node, "ParkDuration");
                object.platformLineSpeedCentimetersPerMillisecond =
                    floatAttribute(node, "Line_Speed");
                object.platformInitiallyActive =
                    booleanAttribute(node, "Active", false);
                object.platformActiveForever =
                    booleanAttribute(node, "ActiveForever", false);
                object.platformLinkedWaypointId =
                    integerAttribute(node, "^Link^WayPoint");
                const auto& physicsGeometries =
                    objectArchetypes_[archetypeIndex]
                        .physicsMesh.sceneGeometries();
                object.collisionLocalMinimum = {
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max()};
                object.collisionLocalMaximum = {
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest()};
                for (const assets::ColladaGeometry& geometry :
                     physicsGeometries) {
                    object.collisionLocalMinimum.x = std::min(
                        object.collisionLocalMinimum.x,
                        geometry.bounds.minimum.x);
                    object.collisionLocalMinimum.y = std::min(
                        object.collisionLocalMinimum.y,
                        geometry.bounds.minimum.y);
                    object.collisionLocalMinimum.z = std::min(
                        object.collisionLocalMinimum.z,
                        geometry.bounds.minimum.z);
                    object.collisionLocalMaximum.x = std::max(
                        object.collisionLocalMaximum.x,
                        geometry.bounds.maximum.x);
                    object.collisionLocalMaximum.y = std::max(
                        object.collisionLocalMaximum.y,
                        geometry.bounds.maximum.y);
                    object.collisionLocalMaximum.z = std::max(
                        object.collisionLocalMaximum.z,
                        geometry.bounds.maximum.z);
                }
                object.hasCollisionBounds = !physicsGeometries.empty();
                object.hasCollision = object.hasCollisionBounds;
                if (object.platformParkDurationMilliseconds < 0.0F ||
                    object.platformLineSpeedCentimetersPerMillisecond < 0.0F ||
                    !object.hasCollisionBounds) {
                    return Result::failure(
                        node.gameType + " " + std::to_string(node.id) +
                        " has invalid authored platform attributes: park=" +
                        std::to_string(
                            object.platformParkDurationMilliseconds) +
                        " line_speed=" +
                        std::to_string(
                            object.platformLineSpeedCentimetersPerMillisecond) +
                        " waypoint=" +
                        std::to_string(object.platformLinkedWaypointId) +
                        " collision_bounds=" +
                        std::to_string(object.hasCollisionBounds ? 1 : 0));
                }
            }
            if (*kind == LevelObjectKind::ElectricPlatform) {
                object.electricOffDurationMilliseconds =
                    floatAttribute(node, "OffDuration");
                object.electricOnDurationMilliseconds =
                    floatAttribute(node, "OnDuration");
                object.electricReadyDurationMilliseconds =
                    floatAttribute(node, "ReadyDuration");
                object.electricDelayMilliseconds =
                    floatAttribute(node, "Delay");
                object.electricDamage = floatAttribute(node, "Damage");
                object.electricInitiallyActive =
                    booleanAttribute(node, "Active", false);
                object.electricInitialState =
                    integerAttribute(node, "$ElectricBoardState", 1);
                if (object.electricOffDurationMilliseconds < 0.0F ||
                    object.electricOnDurationMilliseconds < 0.0F ||
                    object.electricReadyDurationMilliseconds < 0.0F ||
                    object.electricDelayMilliseconds < 0.0F ||
                    object.electricDamage < 0.0F ||
                    object.electricInitialState < 0 ||
                    object.electricInitialState > 2) {
                    return Result::failure(
                        "ElectricPlatForm " + std::to_string(node.id) +
                        " has invalid authored runtime attributes: off=" +
                        std::to_string(object.electricOffDurationMilliseconds) +
                        " on=" +
                        std::to_string(object.electricOnDurationMilliseconds) +
                        " ready=" +
                        std::to_string(object.electricReadyDurationMilliseconds) +
                        " delay=" +
                        std::to_string(object.electricDelayMilliseconds) +
                        " damage=" + std::to_string(object.electricDamage) +
                        " electric_state=" +
                        std::to_string(object.electricInitialState) +
                        " park=" +
                        std::to_string(object.platformParkDurationMilliseconds) +
                        " line_speed=" +
                        std::to_string(
                            object.platformLineSpeedCentimetersPerMillisecond) +
                        " waypoint=" +
                        std::to_string(object.platformLinkedWaypointId) +
                        " collision_bounds=" +
                        std::to_string(object.hasCollisionBounds ? 1 : 0));
                }
            }
            if (*kind == LevelObjectKind::Train) {
                // CWayPointMover::ProcessUserAttr (0x003269f0) reads speed
                // and Active. CTrain::ProcessUserAttr (0x00321174) then
                // retains its nominal speed while the current train speed is
                // initialized to zero. InitLinker (0x0032146c) resolves the
                // carriage chain through ^Pre^Train/^Next^Train.
                object.trainLineSpeedCentimetersPerMillisecond =
                    floatAttribute(node, "Line_Speed");
                object.trainInitiallyActive =
                    booleanAttribute(node, "Active", true);
                object.trainLinkedWaypointId =
                    integerAttribute(node, "^Link^WayPoint");
                object.trainPreviousObjectId =
                    integerAttribute(node, "^Pre^Train");
                object.trainNextObjectId =
                    integerAttribute(node, "^Next^Train");
                object.trainLifeDurationMilliseconds =
                    floatAttribute(node, "lifeDuration");
                object.trainCanTransport =
                    booleanAttribute(node, "CanTransport", false);
                object.trainKillsPlayer =
                    booleanAttribute(node, "KillPlayer", false);
                if (object.trainLineSpeedCentimetersPerMillisecond < 0.0F ||
                    object.trainLifeDurationMilliseconds < 0.0F) {
                    return Result::failure(
                        "Train " + std::to_string(node.id) +
                        " has invalid authored runtime attributes");
                }
            }
            if (*kind == LevelObjectKind::Destroyable) {
                object.health = floatAttribute(node, "Health");
                object.damageRadius = floatAttribute(node, "Damage Radius");
                object.damage = floatAttribute(node, "Damage");
                object.destructionEffectType =
                    std::string(userAttribute(node, "$EffectType"));
                object.deadSpawnObjectId =
                    integerAttribute(node, "DeadSpawnId");
                object.deadCinematicId =
                    integerAttribute(node, "^Dead^Cinematic");
                object.attackable =
                    booleanAttribute(node, "IsAttack", true);
                object.collisionAfterDestruction =
                    booleanAttribute(node, "Collision Dead", false);
                object.hitVoxSoundId = destroyableHitVoxSoundId(meshPath);
            }
            if (*kind == LevelObjectKind::Comic &&
                (objectGeometries.empty() || object.comicIndex < 0)) {
                return Result::failure("Comic " + std::to_string(node.id) +
                                       " has no collection bounds or index");
            }
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
            if (*kind == LevelObjectKind::DropObject) {
                LevelDropObjectAsset drop;
                drop.objectId = node.id;
                drop.ownerAreaId =
                    integerAttribute(node, "!^Owner^DropArea");
                drop.roomId = static_cast<std::int32_t>(roomIndex + 1);
                drop.delayMilliseconds =
                    integerAttribute(node, "DelayTime", 0);
                drop.damage = floatAttribute(node, "Damage", 1.0F);
                drop.position = object.position;
                drop.effectType =
                    std::string(userAttribute(node, "$EffectType"));
                const auto& geometries =
                    objectArchetypes_[archetypeIndex].mesh.sceneGeometries();
                assets::Vector3 extents{};
                for (const assets::ColladaGeometry& geometry : geometries) {
                    extents.x = std::max(
                        extents.x,
                        std::max(std::abs(geometry.bounds.minimum.x),
                                 std::abs(geometry.bounds.maximum.x)));
                    extents.y = std::max(
                        extents.y,
                        std::max(std::abs(geometry.bounds.minimum.y),
                                 std::abs(geometry.bounds.maximum.y)));
                    extents.z = std::max(
                        extents.z,
                        std::max(std::abs(geometry.bounds.minimum.z),
                                 std::abs(geometry.bounds.maximum.z)));
                }
                drop.halfExtents = {
                    std::max(extents.x * std::abs(node.scale.x), 1.0F),
                    std::max(extents.y * std::abs(node.scale.y), 1.0F),
                    std::max(extents.z * std::abs(node.scale.z), 1.0F)};
                if (drop.ownerAreaId < 0 || drop.delayMilliseconds < 0 ||
                    drop.damage < 0.0F || drop.effectType.empty() ||
                    effects_.presets.find(drop.effectType) == nullptr) {
                    return Result::failure("DropObject " +
                                           std::to_string(node.id) +
                                           " has invalid attributes");
                }
                dropObjects_.push_back(std::move(drop));
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

    const auto appendWaypoints = [this](const assets::IrrScene& scene,
                                        std::int32_t roomId) {
        for (const assets::IrrSceneNode& node : scene.nodes()) {
            if (node.gameType != "WayPoint") {
                continue;
            }
            LevelWayPointAsset waypoint;
            waypoint.objectId = node.id;
            waypoint.name = node.name;
            waypoint.roomId = roomId;
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
    appendWaypoints(mainScene_, 0);
    for (std::size_t roomIndex = 0; roomIndex < rooms_.size(); ++roomIndex) {
        appendWaypoints(rooms_[roomIndex].scene,
                        static_cast<std::int32_t>(roomIndex + 1));
    }

    const auto appendSlides = [this](const assets::IrrScene& scene,
                                     std::int32_t roomId) -> Result {
        for (const assets::IrrSceneNode& node : scene.nodes()) {
            if (node.gameType != "Slide") {
                continue;
            }
            LevelSlideAsset slide;
            slide.objectId = node.id;
            slide.name = node.name;
            slide.roomId = roomId;
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
            // CSlider::Init (0x0031e7cc) accepts an empty or single-point
            // chain. Such editor placeholders simply contribute no catchable
            // segment to CSlider::Update; they are not a level-load error.
            slides_.push_back(std::move(slide));
        }
        return Result::success();
    };
    result = appendSlides(mainScene_, 0);
    if (!result) {
        slides_.clear();
        return result;
    }
    for (std::size_t roomIndex = 0; roomIndex < rooms_.size(); ++roomIndex) {
        result = appendSlides(rooms_[roomIndex].scene,
                              static_cast<std::int32_t>(roomIndex + 1));
        if (!result) {
            slides_.clear();
            return result;
        }
    }

    const auto appendWebGrabPoints = [this](const assets::IrrScene& scene,
                                            std::int32_t roomId)
        -> Result {
        for (const assets::IrrSceneNode& node : scene.nodes()) {
            if (node.gameType != "WebGrabPoint") {
                continue;
            }
            LevelWebGrabPointAsset point;
            point.objectId = node.id;
            point.roomId = roomId;
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
    result = appendWebGrabPoints(mainScene_, 0);
    if (!result) {
        webGrabPoints_.clear();
        return result;
    }
    for (std::size_t roomIndex = 0; roomIndex < rooms_.size(); ++roomIndex) {
        result = appendWebGrabPoints(
            rooms_[roomIndex].scene,
            static_cast<std::int32_t>(roomIndex + 1));
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
                trigger.roomId = static_cast<std::int32_t>(roomIndex + 1);
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
            const bool gunThug = node.gameType == "MeleeThug_gun";
            const bool bigRangeThug = node.gameType == "RangeThug_big";
            const bool hammerThug = node.gameType == "RangeThug_hammer";
            const bool molotovThug =
                node.gameType == "RangeThug_molotov";
            const bool nativeBoss = isBossGameType(node.gameType);
            const bool nativeEnemy = isEnemyGameType(node.gameType);
            const bool sandman = node.gameType == "Boss_Sandman";
            // CLevel::LoadNextObject (0x003853fc, constructor branch at
            // 0x0038640c) constructs CBoss for the complete set recognized by
            // isBossGameType. Keep every one in the persistent enemy pool so
            // cinematic commands and later recovered task graphs address the
            // same native object rather than a render-only scene node.
            const bool boss = nativeBoss;
            if (!nativeEnemy && !boss) {
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
                if (!asset.animationFile.empty()) {
                    result = entityArchive.read(
                        normalizeArchivePath(asset.animationFile), resource);
                    if (!result ||
                        !(result = asset.animationBank.load(resource))) {
                        return Result::failure(
                            "Could not load enemy animation " +
                            asset.animationFile + ": " + result.message());
                    }
                    const std::filesystem::path animationPath{
                        normalizeArchivePath(asset.animationFile)};
                    const std::string enemyDisplacementStem =
                        animationPath.stem().generic_string();
                    std::vector<std::byte> enemyDummyDisplacement;
                    std::vector<std::byte> enemyPelvisDisplacement;
                    result = entityArchive.read(
                        "exported_meshes/" + enemyDisplacementStem +
                            "_dummy.bin",
                        enemyDummyDisplacement);
                    if (!result) {
                        return Result::failure(
                            "Could not load enemy dummy displacement " +
                            enemyDisplacementStem + ": " + result.message());
                    }
                    result = entityArchive.read(
                        "exported_meshes/" + enemyDisplacementStem +
                            "_pelvis.bin",
                        enemyPelvisDisplacement);
                    if (!result) {
                        return Result::failure(
                            "Could not load enemy pelvis displacement " +
                            enemyDisplacementStem + ": " + result.message());
                    }
                    result = asset.animationDisplacement.load(
                        enemyDummyDisplacement, enemyPelvisDisplacement);
                    if (!result) {
                        return Result::failure(
                            "Could not parse enemy displacement " +
                            enemyDisplacementStem + ": " + result.message());
                    }
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
            // Robot_Phantom is routed through CBoss by the factory but its
            // shipped IRR nodes retain Enemy_Type=15 instead of Boss_Type.
            // All Boss_* nodes carry Boss_Type. Preserve that authored split.
            const bool bossTypeAttribute =
                boss && node.gameType != "Robot_Phantom";
            enemy.enemyTypeId = static_cast<std::int16_t>(integerAttribute(
                node, bossTypeAttribute ? "Boss_Type" : "Enemy_Type",
                sandman         ? 16
                : gunThug       ? 3
                : bigRangeThug  ? 4
                : hammerThug    ? 5
                : molotovThug   ? 2
                : node.gameType == "MeleeThugEnemy_knife" ? 0
                                                           : 1));
            enemy.initialAnimation = node.initialAnimation;
            if (enemy.initialAnimation.empty()) {
                enemy.initialAnimation =
                    bigRangeThug
                        ? "idlebaz"
                    : gunThug || hammerThug || molotovThug || sandman
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
            enemy.onWall = booleanAttribute(node, "OnWall", false);
            enemy.inAir = enemy.enemyTypeId == 20 || enemy.enemyTypeId == 21 ||
                          booleanAttribute(node, "InAir", false);
            enemy.immobile = booleanAttribute(node, "Imobile", false);
            enemy.lineSpeedCentimetersPerMillisecond =
                floatAttribute(node, "Line_Speed", 0.3F);
            enemy.awarenessRadius = floatAttribute(node, "Aware_Radius");
            enemy.awarenessAngleDegrees =
                floatAttribute(node, "Aware_Angle");
            if (enemy.onWall) {
                enemy.awarenessRadius = 500.0F;
            }
            if (enemyArchetypes_[archetypeIndex].animationBank.findClip(
                    enemy.initialAnimation) == nullptr) {
                // IAnimatedObject::LoadMeshAndAnimator (0x00310b00) selects
                // animation index zero immediately after constructing the
                // authored bank. Scene metadata can name a nonexistent clip
                // (Level 2 Rhino 30014 uses punch_right_to_idle); native does
                // not resolve that string during CEnemy::ProcessUserAttr.
                const auto& clips =
                    enemyArchetypes_[archetypeIndex].animationBank.clips();
                if (clips.empty()) {
                    if (!enemyArchetypes_[archetypeIndex]
                             .animationFile.empty()) {
                        return Result::failure(
                            "Enemy " + std::to_string(enemy.objectId) +
                            " animation bank is empty");
                    }
                    // CLevel::LoadNextObject routes Aircraft_blue and
                    // Aircraft_red through the common CEnemy constructor even
                    // though their authored AnimationFile attribute is empty.
                    // They remain valid mesh-only CEnemy instances; SetAnim is
                    // not performed until a task supplies an animation bank.
                    enemy.initialAnimation.clear();
                } else {
                    enemy.initialAnimation = clips.front().name;
                }
            }
            enemies_.push_back(std::move(enemy));
        }
    }

    // Level-one cinematic 1265 is retained by the dedicated boot sequence
    // below. The
    // other PlayDAECamera streams are loaded here for native in-level
    // playback using the same scene-node actor bindings.
    for (LevelCinematicAsset& cinematic : cinematics_) {
        if (!cinematic.scriptAvailable ||
            (levelNumber == 1 && cinematic.objectId == 1265)) {
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
        const CinematicAttribute* clipIdAttribute =
            cameraCommand->findAttribute("clipID");
        const CinematicAttribute* nextCinematic =
            cameraCommand->findAttribute("^ID^Cinematic^Next");
        float farPlaneOverride = 0.0F;
        std::int32_t clipId = 0;
        cinematic.nextCinematicId = 0;
        const bool validFarPlane =
            farPlane == nullptr ||
            parseFloatText(farPlane->value, farPlaneOverride);
        const bool validNextCinematic =
            nextCinematic == nullptr ||
            parseIntegerText(nextCinematic->value,
                             cinematic.nextCinematicId);
        const bool validClipId =
            clipIdAttribute == nullptr ||
            parseIntegerText(clipIdAttribute->value, clipId);
        // CCinematicThread::PlayDAECamera (0x00370db0) uses Irrlicht's typed
        // getters directly. Missing farPlane and successor attributes read as
        // zero; Level 7 cinematic 327 intentionally omits farPlane and uses
        // the camera BDAE's authored value.
        if (cameraFile == nullptr || cameraFile->value.empty() ||
            !validFarPlane || !validNextCinematic || !validClipId) {
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
                  cinematic.cameraAnimation, farPlaneOverride, clipId))) {
            return Result::failure("Could not load animated camera for " +
                                   cinematic.name + ": " +
                                   result.message());
        }
        cinematic.cameraAnimationStartMilliseconds =
            cameraCommand->timestampMilliseconds;
        cinematic.colladaDurationMilliseconds =
            cinematic.cameraAnimationStartMilliseconds +
            cinematic.animatedCamera.clipDurationMilliseconds();

        for (const CinematicThread& thread : cinematic.script.threads()) {
            for (const CinematicCommand& command : thread.commands) {
                if (command.name != "PlayDAEAnim") {
                    continue;
                }
                const assets::IrrSceneNode* sceneNode =
                    findLevelNode(mainScene_, rooms_, thread.objectId);
                if (sceneNode == nullptr) {
                    // CCinematicThread::PlayDAEAnim (0x003709ec) checks its
                    // bound object pointer at +0x60 and returns without doing
                    // anything when the thread is unbound. Level 7 cinematic
                    // 327 deliberately carries a PlayDAEAnim command on
                    // object -1 alongside its valid animated camera.
                    continue;
                }
                CinematicActorAsset actor;
                result = loadCinematicActor(levelArchive, entityArchive,
                                            inheritedLevelTextures,
                                            previousLevelTextures,
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
                                 actor.animationClipDurationMilliseconds());
                cinematic.actors.push_back(std::move(actor));
            }
        }
        // PlayDAECamera-only sequences are valid; no actor stream is required.
    }

    const auto skyNode = std::find_if(
        mainScene_.nodes().begin(), mainScene_.nodes().end(),
        [](const assets::IrrSceneNode& node) {
            return node.gameType == "SkyPlane" && !node.meshFile.empty();
        });
    if (skyNode == mainScene_.nodes().end()) {
        return Result::failure(levelStem + " scene has no sky mesh node");
    }
    introSky_.name = levelStem + " sky";
    result = levelArchive.read(normalizeArchivePath(skyNode->meshFile),
                               resource);
    if (!result) {
        return result;
    }
    result = introSky_.geometry.load(resource);
    if (!result || introSky_.geometry.geometries().empty()) {
        return Result::failure("Could not parse " + levelStem + " sky: " +
                               result.message());
    }
    result = loadTextures(levelArchive, &entityArchive, introSky_.geometry,
                          introSky_.textures, introSky_.name, nullptr,
                          inheritedLevelTextures,
                          previousLevelTextures);
    if (!result) {
        return result;
    }
    // CLevel::LoadNextObject (0x003853fc) maps the authored
    // !GameType="SkyPlane" node to FpsSkyBoxSceneNode.  Its render method
    // (0x0039a998) applies the active-camera-relative transform; this is not
    // the separate CSkyBoxObject path.
    introSky_.cameraRelative = true;

    const auto isInLevelCinematic = [this](std::int32_t objectId) {
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
    if (levelNumber != 1) {
        std::erase_if(objects_, [&isInLevelCinematic](
                                    const LevelObjectAsset& object) {
            return object.kind == LevelObjectKind::Animated &&
                   isInLevelCinematic(object.objectId);
        });
        return Result::success();
    }

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
    const CinematicCommand* introCameraCommand = nullptr;
    for (const CinematicThread& thread : introScript_.threads()) {
        for (const CinematicCommand& command : thread.commands) {
            if (command.name == "PlayDAECamera") {
                if (introCameraCommand != nullptr) {
                    return Result::failure(
                        "Level-one intro has multiple PlayDAECamera commands");
                }
                introCameraCommand = &command;
            }
        }
    }
    if (introCameraCommand == nullptr) {
        return Result::failure(
            "Level-one intro has no PlayDAECamera command");
    }
    const CinematicAttribute* introCameraFile =
        introCameraCommand->findAttribute("CameraAnimFile");
    const CinematicAttribute* introFarPlane =
        introCameraCommand->findAttribute("farPlane");
    const CinematicAttribute* introClipIdAttribute =
        introCameraCommand->findAttribute("clipID");
    float introFarPlaneOverride = 0.0F;
    std::int32_t introClipId = 0;
    if (introCameraFile == nullptr || introCameraFile->value.empty() ||
        (introFarPlane != nullptr &&
         !parseFloatText(introFarPlane->value, introFarPlaneOverride)) ||
        (introClipIdAttribute != nullptr &&
         !parseIntegerText(introClipIdAttribute->value, introClipId))) {
        return Result::failure(
            "Level-one intro PlayDAECamera attributes are invalid");
    }
    result = levelArchive.read(
        normalizeArchivePath(introCameraFile->value), resource);
    if (!result) {
        return result;
    }
    result = introCameraAnimation_.load(resource);
    if (!result) {
        return result;
    }
    result = introCamera_.bind(introCameraAnimation_, introFarPlaneOverride,
                               introClipId);
    if (!result) {
        return result;
    }
    introColladaDurationMilliseconds_ =
        introCameraCommand->timestampMilliseconds +
        introCamera_.clipDurationMilliseconds();

    for (const CinematicThread& thread : introScript_.threads()) {
        for (const CinematicCommand& command : thread.commands) {
            if (command.name != "PlayDAEAnim") {
                continue;
            }
            const CinematicAttribute* animationFile =
                command.findAttribute("AnimFile");
            const CinematicAttribute* clipIdAttribute =
                command.findAttribute("clipID");
            std::int32_t clipId = 0;
            const assets::IrrSceneNode* sceneNode =
                findLevelNode(mainScene_, rooms_, thread.objectId);
            if (animationFile == nullptr || sceneNode == nullptr ||
                sceneNode->meshFile.empty()) {
                introActors_.clear();
                return Result::failure(
                    "Cinematic actor command has no matching scene node");
            }
            if (clipIdAttribute != nullptr &&
                !parseIntegerText(clipIdAttribute->value, clipId)) {
                introActors_.clear();
                return Result::failure(
                    "Level-one cinematic actor has invalid clipID");
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
            if (clipId < 0 || static_cast<std::size_t>(clipId) >=
                                  actor.animation.clips().size()) {
                introActors_.clear();
                return Result::failure(
                    "Level-one cinematic actor references invalid clipID");
            }
            const assets::ColladaAnimationClip& clip =
                actor.animation.clips()[static_cast<std::size_t>(clipId)];
            actor.animationClipId = clipId;
            actor.animationClipStartMilliseconds = clip.startMilliseconds;
            actor.animationClipEndMilliseconds = clip.endMilliseconds;
            introColladaDurationMilliseconds_ =
                std::max(introColladaDurationMilliseconds_,
                         actor.animationStartMilliseconds +
                             actor.animationClipDurationMilliseconds());
            introActors_.push_back(std::move(actor));
        }
    }
    if (introActors_.empty()) {
        return Result::failure("Level-one intro has no cinematic actors");
    }
    const auto isDedicatedCinematicActor =
        [this, &isInLevelCinematic](std::int32_t objectId) {
        if (std::any_of(
                introActors_.begin(), introActors_.end(),
                [objectId](const CinematicActorAsset& actor) {
                    return actor.objectId == objectId;
                })) {
            return true;
        }
        return isInLevelCinematic(objectId);
    };
    std::erase_if(objects_, [&isDedicatedCinematicActor](
                                const LevelObjectAsset& object) {
        return object.kind == LevelObjectKind::Animated &&
               isDedicatedCinematicActor(object.objectId);
    });
    return Result::success();
}

} // namespace usm::game
