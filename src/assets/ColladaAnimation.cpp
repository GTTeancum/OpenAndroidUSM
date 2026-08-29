#include "assets/ColladaAnimation.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>

namespace usm::assets {
namespace {

constexpr std::uint32_t kColladaRootPointerField = 0x1c;
constexpr std::uint32_t kAnimationCountOffset = 0x10;
constexpr std::uint32_t kAnimationArrayOffset = 0x14;
constexpr std::uint32_t kAnimationSize = 0x24;
constexpr std::uint32_t kSourceSize = 0x0c;
constexpr std::uint32_t kTimestampSourceType = 4;
constexpr std::uint32_t kFloatingPointSourceType = 6;
constexpr std::uint32_t kCameraCountOffset = 0x24;
constexpr std::uint32_t kCameraArrayOffset = 0x28;
constexpr std::uint32_t kCameraSize = 0x1c;

class BinaryView final {
public:
    explicit BinaryView(std::span<const std::byte> bytes) : bytes_(bytes) {}

    template <typename Integer>
    [[nodiscard]] std::optional<Integer> integer(std::uint32_t offset) const {
        if (offset > bytes_.size() || sizeof(Integer) > bytes_.size() - offset) {
            return std::nullopt;
        }
        Integer result{};
        for (std::size_t index = 0; index < sizeof(Integer); ++index) {
            result |= static_cast<Integer>(std::to_integer<unsigned char>(
                          bytes_[offset + index]))
                      << (index * 8);
        }
        return result;
    }

    [[nodiscard]] std::optional<float> floating(std::uint32_t offset) const {
        const auto bits = integer<std::uint32_t>(offset);
        return bits ? std::optional(std::bit_cast<float>(*bits)) : std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> string(
        std::uint32_t offset) const {
        if (offset >= bytes_.size()) {
            return std::nullopt;
        }
        const char* begin = reinterpret_cast<const char*>(bytes_.data() + offset);
        const char* end = reinterpret_cast<const char*>(bytes_.data() + bytes_.size());
        const char* terminator = std::find(begin, end, '\0');
        return terminator == end ? std::nullopt
                                 : std::optional(std::string(begin, terminator));
    }

    [[nodiscard]] bool contains(std::uint64_t offset,
                                std::uint64_t size) const noexcept {
        return offset <= bytes_.size() && size <= bytes_.size() - offset;
    }

private:
    std::span<const std::byte> bytes_;
};

ColladaAnimationProperty propertyFromId(std::string_view id) noexcept {
    if (id.ends_with("-translation")) {
        return ColladaAnimationProperty::Translation;
    }
    if (id.ends_with("-rotation")) {
        return ColladaAnimationProperty::Rotation;
    }
    return ColladaAnimationProperty::Unknown;
}

Result parseTrack(const BinaryView& view, std::uint32_t animationOffset,
                  ColladaAnimationTrack& output) {
    const auto idOffset = view.integer<std::uint32_t>(animationOffset);
    const auto sourceCount = view.integer<std::uint32_t>(animationOffset + 4);
    const auto sourcesOffset = view.integer<std::uint32_t>(animationOffset + 8);
    const auto channelCount = view.integer<std::uint32_t>(animationOffset + 20);
    const auto channelsOffset = view.integer<std::uint32_t>(animationOffset + 24);
    if (!idOffset || !sourceCount || !sourcesOffset || !channelCount ||
        !channelsOffset || *sourceCount < 2 || *channelCount == 0 ||
        !view.contains(*sourcesOffset,
                       static_cast<std::uint64_t>(*sourceCount) * kSourceSize)) {
        return Result::failure("BDAE animation record is invalid");
    }
    const auto id = view.string(*idOffset);
    const auto targetOffset = view.integer<std::uint32_t>(*channelsOffset + 4);
    if (!id || !targetOffset) {
        return Result::failure("BDAE animation names are invalid");
    }
    const auto target = view.string(*targetOffset);
    if (!target) {
        return Result::failure("BDAE animation target is invalid");
    }

    const std::uint32_t timeSource = *sourcesOffset;
    const std::uint32_t valueSource = *sourcesOffset + kSourceSize;
    const auto timeType = view.integer<std::uint32_t>(timeSource);
    const auto timeCount = view.integer<std::uint32_t>(timeSource + 4);
    const auto timesOffset = view.integer<std::uint32_t>(timeSource + 8);
    const auto valueType = view.integer<std::uint32_t>(valueSource);
    const auto valueCount = view.integer<std::uint32_t>(valueSource + 4);
    const auto valuesOffset = view.integer<std::uint32_t>(valueSource + 8);
    if (!timeType || !timeCount || !timesOffset || !valueType || !valueCount ||
        !valuesOffset || *timeType != kTimestampSourceType ||
        *valueType != kFloatingPointSourceType || *timeCount == 0 ||
        *valueCount % *timeCount != 0 ||
        !view.contains(*timesOffset,
                       static_cast<std::uint64_t>(*timeCount) * 4) ||
        !view.contains(*valuesOffset,
                       static_cast<std::uint64_t>(*valueCount) * 4)) {
        return Result::failure("BDAE animation source streams are invalid");
    }
    const std::uint32_t componentCount = *valueCount / *timeCount;
    if (componentCount == 0 || componentCount > 4) {
        return Result::failure("BDAE animation component count is unsupported");
    }

    output.id = *id;
    output.targetNode = *target;
    output.property = propertyFromId(output.id);
    output.componentCount = componentCount;
    output.timestampsMilliseconds.reserve(*timeCount);
    for (std::uint32_t index = 0; index < *timeCount; ++index) {
        const auto timestamp =
            view.integer<std::uint32_t>(*timesOffset + index * 4);
        if (!timestamp ||
            (!output.timestampsMilliseconds.empty() &&
             *timestamp < output.timestampsMilliseconds.back())) {
            return Result::failure("BDAE animation timestamps are invalid");
        }
        output.timestampsMilliseconds.push_back(*timestamp);
    }
    output.values.reserve(*valueCount);
    for (std::uint32_t index = 0; index < *valueCount; ++index) {
        const auto value = view.floating(*valuesOffset + index * 4);
        if (!value || !std::isfinite(*value)) {
            return Result::failure("BDAE animation values are invalid");
        }
        output.values.push_back(*value);
    }
    return Result::success();
}

Result parseCamera(const BinaryView& view, std::uint32_t rootOffset,
                   std::optional<ColladaCamera>& output) {
    const auto count = view.integer<std::uint32_t>(rootOffset + kCameraCountOffset);
    const auto array = view.integer<std::uint32_t>(rootOffset + kCameraArrayOffset);
    if (!count || !array ||
        (*count != 0 &&
         !view.contains(*array,
                        static_cast<std::uint64_t>(*count) * kCameraSize))) {
        return Result::failure("BDAE camera library is invalid");
    }
    if (*count == 0) {
        output.reset();
        return Result::success();
    }
    if (*count != 1) {
        return Result::failure("BDAE contains multiple cinematic cameras");
    }
    const auto idOffset = view.integer<std::uint32_t>(*array);
    const auto projection = view.integer<std::uint32_t>(*array + 4);
    const auto fieldOfView = view.floating(*array + 8);
    const auto aspectRatio = view.floating(*array + 12);
    const auto nearPlane = view.floating(*array + 16);
    const auto farPlane = view.floating(*array + 20);
    const auto targetOffset = view.integer<std::uint32_t>(*array + 24);
    if (!idOffset || !projection || !fieldOfView || !aspectRatio ||
        !nearPlane || !farPlane || !targetOffset) {
        return Result::failure("BDAE camera record is truncated");
    }
    const auto id = view.string(*idOffset);
    const auto target = view.string(*targetOffset);
    if (!id || !target || !std::isfinite(*fieldOfView) ||
        !std::isfinite(*aspectRatio) || !std::isfinite(*nearPlane) ||
        !std::isfinite(*farPlane) || *fieldOfView <= 0.0F ||
        *aspectRatio <= 0.0F || *nearPlane <= 0.0F ||
        *farPlane <= *nearPlane) {
        return Result::failure("BDAE camera values are invalid");
    }
    output = ColladaCamera{*id, *target, *projection != 0, *fieldOfView,
                           *aspectRatio, *nearPlane, *farPlane};
    return Result::success();
}

} // namespace

ColladaAnimationSample ColladaAnimationTrack::sample(
    std::uint32_t timestampMilliseconds) const noexcept {
    ColladaAnimationSample result{};
    result.componentCount = componentCount;
    if (timestampsMilliseconds.empty() || componentCount == 0) {
        return result;
    }
    const auto upper = std::upper_bound(timestampsMilliseconds.begin(),
                                        timestampsMilliseconds.end(),
                                        timestampMilliseconds);
    const std::size_t right = upper == timestampsMilliseconds.end()
                                  ? timestampsMilliseconds.size() - 1
                                  : static_cast<std::size_t>(
                                        upper - timestampsMilliseconds.begin());
    const std::size_t left = right == 0 ? 0 : right - 1;
    float factor = 0.0F;
    if (right != left) {
        const auto interval = timestampsMilliseconds[right] -
                              timestampsMilliseconds[left];
        factor = interval == 0
                     ? 0.0F
                     : static_cast<float>(timestampMilliseconds -
                                          timestampsMilliseconds[left]) /
                           static_cast<float>(interval);
    }
    float rotationSign = 1.0F;
    if (property == ColladaAnimationProperty::Rotation &&
        componentCount == 4 && left != right) {
        float dot = 0.0F;
        for (std::uint32_t component = 0; component < componentCount;
             ++component) {
            dot += values[left * componentCount + component] *
                   values[right * componentCount + component];
        }
        rotationSign = dot < 0.0F ? -1.0F : 1.0F;
    }
    for (std::uint32_t component = 0; component < componentCount; ++component) {
        const float first = values[left * componentCount + component];
        const float second =
            values[right * componentCount + component] * rotationSign;
        result.value[component] = first + (second - first) * factor;
    }
    if (property == ColladaAnimationProperty::Rotation && componentCount == 4) {
        const float length = std::sqrt(
            result.value[0] * result.value[0] +
            result.value[1] * result.value[1] +
            result.value[2] * result.value[2] +
            result.value[3] * result.value[3]);
        if (length > std::numeric_limits<float>::epsilon()) {
            for (float& component : result.value) {
                component /= length;
            }
        }
    }
    return result;
}

Result ColladaAnimationFile::load(std::span<const std::byte> bytes) {
    tracks_.clear();
    camera_.reset();
    Result result = resource_.load(bytes);
    if (!result) {
        return result;
    }
    const auto rootOffset = resource_.resolvePointer(kColladaRootPointerField);
    if (!rootOffset) {
        return Result::failure("BDAE file has no SCollada root pointer");
    }
    const BinaryView view(resource_.bytes());
    const auto count =
        view.integer<std::uint32_t>(*rootOffset + kAnimationCountOffset);
    const auto array =
        view.integer<std::uint32_t>(*rootOffset + kAnimationArrayOffset);
    if (!count || !array || *count == 0 ||
        !view.contains(*array,
                       static_cast<std::uint64_t>(*count) * kAnimationSize)) {
        return Result::failure("BDAE animation library is invalid");
    }
    tracks_.reserve(*count);
    for (std::uint32_t index = 0; index < *count; ++index) {
        ColladaAnimationTrack track;
        result = parseTrack(view, *array + index * kAnimationSize, track);
        if (!result) {
            tracks_.clear();
            return result;
        }
        tracks_.push_back(std::move(track));
    }
    result = parseCamera(view, *rootOffset, camera_);
    if (!result) {
        tracks_.clear();
        return result;
    }
    return Result::success();
}

std::uint32_t ColladaAnimationFile::durationMilliseconds() const noexcept {
    std::uint32_t duration = 0;
    for (const ColladaAnimationTrack& track : tracks_) {
        if (!track.timestampsMilliseconds.empty()) {
            duration = std::max(duration, track.timestampsMilliseconds.back());
        }
    }
    return duration;
}

} // namespace usm::assets
