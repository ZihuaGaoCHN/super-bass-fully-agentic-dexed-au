#include "BaseUrlPolicy.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace agentic_dexed::agent::http
{
namespace
{
BaseUrlResult reject(std::string code, std::string message)
{
    return BaseUrlResult::failure(makeProtocolError(
        std::move(code), std::move(message)));
}

std::string lowercase(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

bool isValidDnsOrIpv4Host(std::string_view host)
{
    if (host.empty() || host.front() == '.' || host.back() == '.'
        || host.front() == '-' || host.back() == '-')
        return false;
    return std::all_of(host.begin(), host.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '.' || character == '-';
    });
}

bool isValidIpv6Host(std::string_view host)
{
    if (host.empty() || host.find(':') == std::string_view::npos)
        return false;
    return std::all_of(host.begin(), host.end(), [](unsigned char character) {
        return std::isxdigit(character) != 0 || character == ':' || character == '.';
    });
}

bool isLoopback(std::string_view host)
{
    if (host == "localhost" || host == "::1")
        return true;
    if (host.rfind("127.", 0) != 0)
        return false;

    int components = 0;
    std::size_t begin = 0;
    while (begin <= host.size())
    {
        const auto end = host.find('.', begin);
        const auto part = host.substr(begin,
            end == std::string_view::npos ? host.size() - begin : end - begin);
        int value = -1;
        const auto conversion = std::from_chars(part.data(), part.data() + part.size(), value);
        if (part.empty() || conversion.ec != std::errc()
            || conversion.ptr != part.data() + part.size() || value < 0 || value > 255)
            return false;
        ++components;
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    return components == 4;
}
}

BaseUrlResult validateBaseUrl(std::string_view candidate)
{
    if (candidate.empty() || candidate.size() > 4096
        || candidate.find_first_of("\r\n\t ") != std::string_view::npos)
        return reject("malformed_url", "Provider URL is malformed");
    if (candidate.find('?') != std::string_view::npos
        || candidate.find('#') != std::string_view::npos)
        return reject("url_components_not_allowed",
                      "Provider URL must not contain a query or fragment");

    const auto separator = candidate.find("://");
    if (separator == std::string_view::npos)
        return reject("malformed_url", "Provider URL must include a scheme and host");

    const auto scheme = lowercase(candidate.substr(0, separator));
    if (scheme != "http" && scheme != "https")
        return reject("unsupported_scheme", "Provider URL must use HTTP or HTTPS");

    const auto authorityBegin = separator + 3;
    const auto authorityEnd = candidate.find('/', authorityBegin);
    const auto authority = candidate.substr(
        authorityBegin,
        authorityEnd == std::string_view::npos
            ? candidate.size() - authorityBegin
            : authorityEnd - authorityBegin);
    if (authority.empty())
        return reject("missing_host", "Provider URL must include a host");
    if (authority.find('@') != std::string_view::npos)
        return reject("userinfo_not_allowed", "Provider URL must not contain user information");
    if (authority.find('%') != std::string_view::npos)
        return reject("malformed_url", "Provider URL host is malformed");

    std::string host;
    std::string_view portText;
    bool ipv6 = false;
    if (authority.front() == '[')
    {
        const auto close = authority.find(']');
        if (close == std::string_view::npos)
            return reject("malformed_url", "Provider IPv6 host is malformed");
        host = lowercase(authority.substr(1, close - 1));
        ipv6 = true;
        const auto remainder = authority.substr(close + 1);
        if (!remainder.empty())
        {
            if (remainder.front() != ':')
                return reject("malformed_url", "Provider URL authority is malformed");
            portText = remainder.substr(1);
        }
    }
    else
    {
        const auto colon = authority.find(':');
        if (colon != std::string_view::npos)
        {
            if (authority.find(':', colon + 1) != std::string_view::npos)
                return reject("malformed_url", "IPv6 hosts must use brackets");
            host = lowercase(authority.substr(0, colon));
            portText = authority.substr(colon + 1);
        }
        else
        {
            host = lowercase(authority);
        }
    }

    if ((ipv6 && !isValidIpv6Host(host)) || (!ipv6 && !isValidDnsOrIpv4Host(host)))
        return reject("malformed_url", "Provider URL host is malformed");

    int port = scheme == "https" ? 443 : 80;
    if (!portText.empty())
    {
        int parsedPort = 0;
        const auto conversion = std::from_chars(
            portText.data(), portText.data() + portText.size(), parsedPort);
        if (conversion.ec != std::errc()
            || conversion.ptr != portText.data() + portText.size()
            || parsedPort < 1 || parsedPort > 65535)
            return reject("malformed_url", "Provider URL port is invalid");
        port = parsedPort;
    }
    else if (!authority.empty() && authority.back() == ':')
    {
        return reject("malformed_url", "Provider URL port is invalid");
    }

    if (scheme == "http" && !isLoopback(host))
        return reject("insecure_url", "Remote provider URLs must use HTTPS");

    return BaseUrlResult::success({ std::string(candidate), scheme, host, port });
}

bool hasSameOrigin(const ValidatedBaseUrl& lhs, const ValidatedBaseUrl& rhs) noexcept
{
    return lhs.scheme == rhs.scheme && lhs.host == rhs.host && lhs.port == rhs.port;
}
}

