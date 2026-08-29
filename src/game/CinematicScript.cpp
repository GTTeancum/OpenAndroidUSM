#include "game/CinematicScript.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <limits>

namespace usm::game {

const CinematicAttribute* CinematicCommand::findAttribute(
    std::string_view attributeName) const noexcept {
    const auto iterator = std::find_if(
        attributes.begin(), attributes.end(),
        [attributeName](const CinematicAttribute& attribute) {
            return attribute.name == attributeName;
        });
    return iterator == attributes.end() ? nullptr : &*iterator;
}

Result CinematicScript::load(std::span<const std::byte> bytes) {
    threads_.clear();
    if (bytes.empty()) {
        return Result::failure("CFF cinematic script is empty");
    }

    pugi::xml_document document;
    const pugi::xml_parse_result parseResult = document.load_buffer(
        bytes.data(), bytes.size(), pugi::parse_default | pugi::parse_fragment,
        pugi::encoding_auto);
    if (!parseResult) {
        return Result::failure(std::string("CFF cinematic XML error: ") +
                               parseResult.description());
    }

    for (const pugi::xml_node xmlThread : document.children("cinematicThread")) {
        CinematicThread thread;
        thread.type = xmlThread.attribute("type").as_int(-1);
        thread.name = xmlThread.attribute("name").value();
        thread.objectId = xmlThread.attribute("object").as_int(-1);

        for (const pugi::xml_node xmlTime : xmlThread.children("time")) {
            const unsigned long long timestamp =
                xmlTime.attribute("stamp").as_ullong(
                    std::numeric_limits<unsigned long long>::max());
            if (timestamp > std::numeric_limits<std::uint32_t>::max()) {
                threads_.clear();
                return Result::failure("CFF command timestamp is invalid");
            }
            for (const pugi::xml_node xmlCommand : xmlTime.children("command")) {
                CinematicCommand command;
                command.timestampMilliseconds =
                    static_cast<std::uint32_t>(timestamp);
                command.id = xmlCommand.attribute("id").as_int(-1);
                command.name = xmlCommand.attribute("name").value();

                const pugi::xml_node xmlAttributes =
                    xmlCommand.child("attributes");
                for (const pugi::xml_node xmlAttribute :
                     xmlAttributes.children()) {
                    command.attributes.push_back(
                        {xmlAttribute.name(),
                         xmlAttribute.attribute("name").value(),
                         xmlAttribute.attribute("value").value()});
                }
                thread.commands.push_back(std::move(command));
            }
        }
        threads_.push_back(std::move(thread));
    }
    if (threads_.empty()) {
        return Result::failure("CFF cinematic contains no command threads");
    }
    return Result::success();
}

std::size_t CinematicScript::commandCount() const noexcept {
    std::size_t count = 0;
    for (const CinematicThread& thread : threads_) {
        count += thread.commands.size();
    }
    return count;
}

} // namespace usm::game
