#include "assets/IrrScene.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string_view>

namespace usm::assets {
namespace {

pugi::xml_node findNamedAttribute(const pugi::xml_node& collection,
                                  std::string_view name) {
    for (const pugi::xml_node child : collection.children()) {
        if (name == child.attribute("name").value()) {
            return child;
        }
    }
    return {};
}

std::string valueOf(const pugi::xml_node& collection, std::string_view name) {
    const pugi::xml_node value = findNamedAttribute(collection, name);
    return value ? value.attribute("value").value() : std::string{};
}

template <std::size_t Count>
std::array<float, Count> parseFloatList(std::string value) {
    std::replace(value.begin(), value.end(), ',', ' ');
    std::array<float, Count> result{};
    const char* cursor = value.c_str();
    char* end = nullptr;
    for (float& component : result) {
        component = std::strtof(cursor, &end);
        if (end == cursor) {
            break;
        }
        cursor = end;
    }
    return result;
}

Vector3 parseVector3(const std::string& value) {
    const auto components = parseFloatList<3>(value);
    return {components[0], components[1], components[2]};
}

Quaternion parseQuaternion(const std::string& value) {
    const auto components = parseFloatList<4>(value);
    return {components[0], components[1], components[2], components[3]};
}

std::string normalizeResourcePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.starts_with("./")) {
        path.erase(0, 2);
    }
    return path;
}

void collectSceneNodes(const pugi::xml_node& parent,
                       std::vector<IrrSceneNode>& output) {
    for (const pugi::xml_node xmlNode : parent.children()) {
        if (std::string_view(xmlNode.name()) == "node") {
            const pugi::xml_node attributes = xmlNode.child("attributes");
            const pugi::xml_node userAttributes =
                xmlNode.child("userData").child("attributes");

            IrrSceneNode node;
            node.sceneType = xmlNode.attribute("type").value();
            node.id = findNamedAttribute(attributes, "Id")
                          .attribute("value")
                          .as_int(-1);
            node.position = parseVector3(valueOf(attributes, "Position"));
            node.rotation = parseQuaternion(valueOf(attributes, "Rotation"));
            node.scale = parseVector3(valueOf(attributes, "Scale"));
            node.absoluteTransform =
                parseFloatList<16>(valueOf(attributes, "AbsoluteTransformation"));
            const pugi::xml_node visible =
                findNamedAttribute(attributes, "Visible");
            node.visible = !visible || visible.attribute("value").as_bool(true);
            node.name = valueOf(userAttributes, "Name");
            node.gameType = valueOf(userAttributes, "!GameType");
            node.meshFile =
                normalizeResourcePath(valueOf(userAttributes, "MeshFile"));
            node.parentId = findNamedAttribute(userAttributes, "#ParentID")
                                .attribute("value")
                                .as_int(-1);
            output.push_back(std::move(node));
        }
        collectSceneNodes(xmlNode, output);
    }
}

} // namespace

Result IrrScene::load(std::span<const std::byte> bytes) {
    nodes_.clear();
    if (bytes.empty()) {
        return Result::failure("Irrlicht scene is empty");
    }

    pugi::xml_document document;
    const pugi::xml_parse_result parseResult = document.load_buffer(
        bytes.data(), bytes.size(), pugi::parse_default | pugi::parse_fragment,
        pugi::encoding_auto);
    if (!parseResult) {
        return Result::failure(std::string("Irrlicht scene XML error: ") +
                               parseResult.description());
    }

    collectSceneNodes(document, nodes_);
    if (nodes_.empty()) {
        return Result::failure("Irrlicht scene contains no nodes");
    }
    return Result::success();
}

const IrrSceneNode* IrrScene::findNode(std::int32_t id) const noexcept {
    const auto match = std::find_if(
        nodes_.begin(), nodes_.end(),
        [id](const IrrSceneNode& node) { return node.id == id; });
    return match == nodes_.end() ? nullptr : &*match;
}

} // namespace usm::assets
