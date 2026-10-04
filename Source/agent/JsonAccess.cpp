#include "JsonAccess.h"

#include <cmath>
#include <limits>

namespace agentic_dexed::agent
{
namespace
{
std::string boundedFieldName(std::string_view name)
{
    constexpr std::size_t maximum = 64;
    return std::string(name.substr(0, maximum));
}

ProtocolError fieldError(
    std::string code, std::string_view name, std::string_view explanation)
{
    return makeProtocolError(
        std::move(code), "JSON field '" + boundedFieldName(name) + "' " + std::string(explanation));
}
}

ProtocolResult<juce::var> parseJson(std::string_view bytes)
{
    if (bytes.size() > limits::maxResponseBytes)
        return ProtocolResult<juce::var>::failure(makeProtocolError(
            "response_too_large", "JSON response exceeds the configured limit"));

    juce::var result;
    const auto text = juce::String::fromUTF8(bytes.data(), static_cast<int>(bytes.size()));
    const auto status = juce::JSON::parse(text, result);
    if (status.failed())
        return ProtocolResult<juce::var>::failure(makeProtocolError(
            "invalid_json", "Model response is not valid JSON"));
    return ProtocolResult<juce::var>::success(std::move(result));
}

ProtocolResult<juce::var> parseJsonObject(std::string_view bytes)
{
    auto result = parseJson(bytes);
    if (!result.ok())
        return result;
    if (result.value->getDynamicObject() == nullptr)
        return ProtocolResult<juce::var>::failure(makeProtocolError(
            "wrong_type", "JSON root must be an object"));
    return result;
}

ProtocolResult<juce::var> requireProperty(
    const juce::var& object, std::string_view name)
{
    const auto* dynamicObject = object.getDynamicObject();
    if (dynamicObject == nullptr)
        return ProtocolResult<juce::var>::failure(makeProtocolError(
            "wrong_type", "JSON value must be an object"));

    const juce::Identifier propertyName(
        juce::String::fromUTF8(name.data(), static_cast<int>(name.size())));
    if (!dynamicObject->hasProperty(propertyName))
        return ProtocolResult<juce::var>::failure(
            fieldError("missing_field", name, "is required"));
    return ProtocolResult<juce::var>::success(dynamicObject->getProperty(propertyName));
}

ProtocolResult<std::string> requireString(
    const juce::var& object, std::string_view name)
{
    auto property = requireProperty(object, name);
    if (!property.ok())
        return ProtocolResult<std::string>::failure(std::move(*property.error));
    if (!property.value->isString())
        return ProtocolResult<std::string>::failure(
            fieldError("wrong_type", name, "must be a string"));
    return ProtocolResult<std::string>::success(property.value->toString().toStdString());
}

ProtocolResult<double> requireNumber(
    const juce::var& object, std::string_view name)
{
    auto property = requireProperty(object, name);
    if (!property.ok())
        return ProtocolResult<double>::failure(std::move(*property.error));
    if (!property.value->isInt() && !property.value->isInt64() && !property.value->isDouble())
        return ProtocolResult<double>::failure(
            fieldError("wrong_type", name, "must be a number"));
    const auto number = static_cast<double>(*property.value);
    if (!std::isfinite(number))
        return ProtocolResult<double>::failure(
            fieldError("invalid_number", name, "must be finite"));
    return ProtocolResult<double>::success(number);
}

ProtocolResult<int64_t> requireInteger(
    const juce::var& object, std::string_view name)
{
    auto property = requireProperty(object, name);
    if (!property.ok())
        return ProtocolResult<int64_t>::failure(std::move(*property.error));
    if (!property.value->isInt() && !property.value->isInt64())
        return ProtocolResult<int64_t>::failure(
            fieldError("wrong_type", name, "must be an integer"));
    return ProtocolResult<int64_t>::success(static_cast<int64_t>(*property.value));
}

ProtocolResult<bool> requireBool(
    const juce::var& object, std::string_view name)
{
    auto property = requireProperty(object, name);
    if (!property.ok())
        return ProtocolResult<bool>::failure(std::move(*property.error));
    if (!property.value->isBool())
        return ProtocolResult<bool>::failure(
            fieldError("wrong_type", name, "must be a boolean"));
    return ProtocolResult<bool>::success(static_cast<bool>(*property.value));
}

ProtocolResult<const juce::Array<juce::var>*> requireArray(
    const juce::var& object, std::string_view name)
{
    auto property = requireProperty(object, name);
    if (!property.ok())
        return ProtocolResult<const juce::Array<juce::var>*>::failure(
            std::move(*property.error));
    const auto* array = property.value->getArray();
    if (array == nullptr)
        return ProtocolResult<const juce::Array<juce::var>*>::failure(
            fieldError("wrong_type", name, "must be an array"));
    return ProtocolResult<const juce::Array<juce::var>*>::success(array);
}

ProtocolResult<juce::var> requireObject(
    const juce::var& object, std::string_view name)
{
    auto property = requireProperty(object, name);
    if (!property.ok())
        return property;
    if (property.value->getDynamicObject() == nullptr)
        return ProtocolResult<juce::var>::failure(
            fieldError("wrong_type", name, "must be an object"));
    return property;
}
}
