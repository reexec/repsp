// Adapted from re2DJ (BSD 3-Clause License; see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, re2DJ contributors

#include "repsp/platform/post_shader_source.h"

#include <cctype>
#include <locale>
#include <sstream>

namespace repsp::platform
{
namespace
{

std::string_view TrimLeft(std::string_view text)
{
    std::size_t index = 0;
    while (index < text.size() && (text[index] == ' ' || text[index] == '\t'))
    {
        ++index;
    }
    return text.substr(index);
}

bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool IsIdentChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool IsIdentifier(std::string_view text)
{
    if (text.empty() ||
        !(std::isalpha(static_cast<unsigned char>(text.front())) != 0 || text.front() == '_'))
    {
        return false;
    }
    for (const char character : text)
    {
        if (!IsIdentChar(character))
        {
            return false;
        }
    }
    return true;
}

// `#pragma parameter NAME "Description" initial minimum maximum [step]`.
bool ParseParameterLine(std::string_view line, PostShaderParameter* parameter, std::string* error)
{
    std::istringstream stream{std::string(line)};
    stream.imbue(std::locale::classic());
    std::string pragma;
    std::string keyword;
    stream >> pragma >> keyword >> parameter->name;
    if (!IsIdentifier(parameter->name))
    {
        *error = "parameter name is not an identifier: " + std::string(line);
        return false;
    }
    stream >> std::ws;
    if (stream.peek() != '"')
    {
        *error = "parameter description is not quoted: " + std::string(line);
        return false;
    }
    stream.get();
    if (!std::getline(stream, parameter->description, '"'))
    {
        *error = "parameter description is not closed: " + std::string(line);
        return false;
    }
    if (!(stream >> parameter->initial >> parameter->minimum >> parameter->maximum))
    {
        *error = "parameter needs initial, minimum and maximum values: " + std::string(line);
        return false;
    }
    if (!(stream >> parameter->step))
    {
        parameter->step = 0.0f;
    }
    if (parameter->minimum > parameter->maximum)
    {
        *error = "parameter minimum exceeds its maximum: " + std::string(line);
        return false;
    }
    parameter->value = parameter->initial;
    return true;
}

std::string ReplaceToken(std::string_view text,
                         std::string_view token,
                         std::string_view replacement,
                         bool* replaced_any)
{
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    *replaced_any = false;
    while (pos < text.size())
    {
        const std::size_t found = text.find(token, pos);
        if (found == std::string_view::npos)
        {
            out.append(text.substr(pos));
            break;
        }
        const bool left_ok = (found == 0) || !IsIdentChar(text[found - 1U]);
        const std::size_t after = found + token.size();
        const bool right_ok = (after >= text.size()) || !IsIdentChar(text[after]);
        if (left_ok && right_ok)
        {
            out.append(text.substr(pos, found - pos));
            out.append(replacement);
            *replaced_any = true;
            pos = after;
        }
        else
        {
            out.append(text.substr(pos, found - pos + 1U));
            pos = found + 1U;
        }
    }
    return out;
}

std::string_view StripLeadingVersion(std::string_view text)
{
    const std::size_t nl = text.find('\n');
    const std::string_view first_line = nl == std::string_view::npos ? text : text.substr(0, nl);
    if (StartsWith(TrimLeft(first_line), "#version"))
    {
        return nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1U);
    }
    return text;
}

std::string AssembleStage(std::string_view text, bool is_vertex, bool gles)
{
    const std::string_view body = StripLeadingVersion(text);
    bool uses_frag_color = false;
    std::string transformed_body;
    if (!is_vertex)
    {
        transformed_body = ReplaceToken(body, "gl_FragColor", "repsp_FragColor", &uses_frag_color);
    }

    std::string result;
    result.reserve(body.size() + 256U);
    if (gles)
    {
        result.append("#version 300 es\nprecision mediump float;\n");
    }
    else
    {
        result.append("#version 330 core\n");
    }
    result.append(is_vertex ? "#define VERTEX\n" : "#define FRAGMENT\n");
    result.append("#define PARAMETER_UNIFORM\n");
    if (is_vertex)
    {
        result.append("#define attribute in\n#define varying out\n");
        result.append(body);
    }
    else
    {
        result.append("#define varying in\n#define texture2D texture\n");
        if (uses_frag_color)
        {
            result.append("out vec4 repsp_FragColor;\n");
        }
        result.append(transformed_body);
    }
    return result;
}

} // namespace

bool BuildPostShaderProgramSource(std::string_view text,
                                  PostShaderProgramSource* source,
                                  std::string* error,
                                  bool gles)
{
    std::string scratch;
    if (error == nullptr)
    {
        error = &scratch;
    }
    if (source == nullptr)
    {
        *error = "no destination for the shader source";
        return false;
    }
    if (text.empty())
    {
        *error = "shader text is empty";
        return false;
    }
    if (text.find("VERTEX") == std::string_view::npos ||
        text.find("FRAGMENT") == std::string_view::npos)
    {
        *error = "shader must guard a VERTEX and a FRAGMENT stage in one file";
        return false;
    }

    std::vector<PostShaderParameter> parameters;
    std::size_t line_start = 0;
    while (line_start < text.size())
    {
        std::size_t line_end = text.find('\n', line_start);
        if (line_end == std::string_view::npos)
        {
            line_end = text.size();
        }
        std::string_view line = text.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        const std::string_view trimmed = TrimLeft(line);
        if (StartsWith(trimmed, "#pragma") &&
            StartsWith(TrimLeft(trimmed.substr(7)), "parameter"))
        {
            PostShaderParameter parameter;
            if (!ParseParameterLine(trimmed, &parameter, error))
            {
                return false;
            }
            bool duplicate = false;
            for (const PostShaderParameter& existing : parameters)
            {
                duplicate = duplicate || existing.name == parameter.name;
            }
            if (!duplicate)
            {
                parameters.push_back(std::move(parameter));
            }
        }
        line_start = line_end + 1U;
    }

    source->vertex = AssembleStage(text, true, gles);
    source->fragment = AssembleStage(text, false, gles);
    source->parameters = std::move(parameters);
    return true;
}

} // namespace repsp::platform
