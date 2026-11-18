#include "repsp/platform/post_shader_catalog.h"
#include "repsp/platform/post_shader_preset.h"
#include "repsp/platform/post_shader_source.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#define PROBE_ASSERT(cond)                                                              \
    do                                                                                  \
    {                                                                                   \
        if (!(cond))                                                                    \
        {                                                                               \
            std::fprintf(stderr, "PROBE_ASSERT failed at %s:%d: %s\n",                  \
                         __FILE__, __LINE__, #cond);                                    \
            std::fflush(stderr);                                                        \
            std::exit(1);                                                               \
        }                                                                               \
    } while (0)

namespace
{

void TestSourceAssemblyAndParameters()
{
    const char* raw =
        "#version 120\n"
        "#pragma parameter MY_GAIN \"Gain\" 1.25 0.0 4.0 0.05\n"
        "#pragma parameter NO_STEP \"No step\" 0.5 0.0 1.0\n"
        "#if defined(VERTEX)\n"
        "attribute vec4 VertexCoord;\n"
        "varying vec2 uv;\n"
        "void main() { gl_Position = VertexCoord; uv = vec2(0.0); }\n"
        "#elif defined(FRAGMENT)\n"
        "uniform sampler2D Texture;\n"
        "varying vec2 uv;\n"
        "void main() {\n"
        "    vec4 my_gl_FragColor = texture2D(Texture, uv);\n"
        "    gl_FragColor = my_gl_FragColor;\n"
        "}\n"
        "#endif\n";

    repsp::platform::PostShaderProgramSource src;
    std::string err;
    PROBE_ASSERT(repsp::platform::BuildPostShaderProgramSource(raw, &src, &err, false));
    PROBE_ASSERT(src.parameters.size() == 2);
    PROBE_ASSERT(src.parameters[0].name == "MY_GAIN");
    PROBE_ASSERT(src.parameters[0].description == "Gain");
    PROBE_ASSERT(std::fabs(src.parameters[0].initial - 1.25f) < 0.001f);
    PROBE_ASSERT(std::fabs(src.parameters[0].value - 1.25f) < 0.001f);
    PROBE_ASSERT(src.parameters[1].name == "NO_STEP");
    PROBE_ASSERT(src.parameters[1].step == 0.0f);

    // Core 3.30 replacement checks
    PROBE_ASSERT(src.vertex.find("#version 330 core\n") == 0);
    PROBE_ASSERT(src.vertex.find("#version 120") == std::string::npos);
    PROBE_ASSERT(src.vertex.find("#define VERTEX\n") != std::string::npos);
    PROBE_ASSERT(src.vertex.find("#define PARAMETER_UNIFORM\n") != std::string::npos);
    PROBE_ASSERT(src.vertex.find("#define attribute in\n") != std::string::npos);
    PROBE_ASSERT(src.vertex.find("#define varying out\n") != std::string::npos);

    PROBE_ASSERT(src.fragment.find("#version 330 core\n") == 0);
    PROBE_ASSERT(src.fragment.find("#define FRAGMENT\n") != std::string::npos);
    PROBE_ASSERT(src.fragment.find("#define varying in\n") != std::string::npos);
    PROBE_ASSERT(src.fragment.find("#define texture2D texture\n") != std::string::npos);
    PROBE_ASSERT(src.fragment.find("out vec4 repsp_FragColor;\n") != std::string::npos);
    PROBE_ASSERT(src.fragment.find(" gl_FragColor") == std::string::npos);
    PROBE_ASSERT(src.fragment.find("vec4 my_gl_FragColor") != std::string::npos);
    PROBE_ASSERT(src.fragment.find("repsp_FragColor = my_gl_FragColor;") != std::string::npos);

    // GLES 3.00 check
    repsp::platform::PostShaderProgramSource es_src;
    PROBE_ASSERT(repsp::platform::BuildPostShaderProgramSource(raw, &es_src, &err, true));
    PROBE_ASSERT(es_src.fragment.find("#version 300 es\nprecision mediump float;\n") == 0);

    // Reject malformed parameter / missing stages
    PROBE_ASSERT(!repsp::platform::BuildPostShaderProgramSource("", &src, &err));
    PROBE_ASSERT(!repsp::platform::BuildPostShaderProgramSource("#if defined(VERTEX)\n#endif\n", &src, &err));
    const char* bad_param =
        "#pragma parameter BAD \"Desc\" 1.0 2.0 0.5\n"
        "#if defined(VERTEX)\n#elif defined(FRAGMENT)\n#endif\n";
    PROBE_ASSERT(!repsp::platform::BuildPostShaderProgramSource(bad_param, &src, &err));
}

void TestPresetParsing()
{
    const char* preset_text =
        "\xEF\xBB\xBF"
        "# comment\n"
        "shaders = 2\n"
        "shader0 = pass0.glsl\n"
        "scale_type0 = source\n"
        "scale0 = 2.0\n"
        "filter_linear0 = false\n"
        "float_framebuffer0 = true\n"
        "shader1 = pass1.glsl\n";

    repsp::platform::PostShaderPreset preset;
    std::string err;
    const auto reader = [](const std::string& rel, std::string* out, std::string* rerr) -> bool
    {
        (void)rerr;
        *out = "// " + rel + "\n#if defined(VERTEX)\n#elif defined(FRAGMENT)\n#endif\n";
        return true;
    };
    PROBE_ASSERT(repsp::platform::ParsePostShaderPreset(preset_text, reader, &preset, &err));
    PROBE_ASSERT(preset.passes.size() == 2);
    PROBE_ASSERT(preset.passes[0].scale_type_x == repsp::platform::PostShaderScaleType::kSource);
    PROBE_ASSERT(std::fabs(preset.passes[0].scale_x - 2.0f) < 0.001f);
    PROBE_ASSERT(preset.passes[0].filter_specified && !preset.passes[0].filter_linear);
    PROBE_ASSERT(preset.passes[0].float_framebuffer);
    PROBE_ASSERT(!preset.passes[0].to_window);
    PROBE_ASSERT(preset.passes[1].to_window);

    std::uint32_t w = 0;
    std::uint32_t h = 0;
    repsp::platform::PostShaderPassSize(preset.passes[0], 480, 272, 1440, 816, &w, &h);
    PROBE_ASSERT(w == 960 && h == 544);
    repsp::platform::PostShaderPassSize(preset.passes[1], 960, 544, 1440, 816, &w, &h);
    PROBE_ASSERT(w == 1440 && h == 816);

    PROBE_ASSERT(!repsp::platform::ParsePostShaderPreset("shaders = 0\n", reader, &preset, &err));
}

void TestCatalogAndPrecedence()
{
    std::error_code ec;
    const auto base = std::filesystem::temp_directory_path(ec) / "repsp_post_shader_probe";
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base, ec);

    // Missing directory lists built-ins only in fixed order
    const auto builtins = repsp::platform::ListPostShaders(base / "nonexistent");
    PROBE_ASSERT(builtins.size() == 5);
    PROBE_ASSERT(builtins[0].id == "crt" && builtins[0].builtin);
    PROBE_ASSERT(builtins[1].id == "scanline" && builtins[1].builtin);
    PROBE_ASSERT(builtins[2].id == "fsr" && builtins[2].builtin);
    PROBE_ASSERT(builtins[3].id == "scalefx" && builtins[3].builtin);
    PROBE_ASSERT(builtins[4].id == "anime4k" && builtins[4].builtin);

    // All 5 built-ins load and assemble cleanly
    for (const auto& entry : builtins)
    {
        repsp::platform::PostShaderPreset preset;
        std::string err;
        PROBE_ASSERT(repsp::platform::LoadPostShaderPreset(entry, &preset, &err));
        PROBE_ASSERT(!preset.passes.empty());
        for (const auto& pass : preset.passes)
        {
            repsp::platform::PostShaderProgramSource src;
            PROBE_ASSERT(repsp::platform::BuildPostShaderProgramSource(pass.text, &src, &err));
        }
    }

    // User files sorted after built-ins
    {
        std::ofstream(base / "b.glsl") << "// b";
        std::ofstream(base / "a.glslp") << "// a";
        std::ofstream(base / "ignore.txt") << "// x";
    }
    const auto listed = repsp::platform::ListPostShaders(base);
    PROBE_ASSERT(listed.size() == 7);
    PROBE_ASSERT(listed[5].id == "a.glslp" && !listed[5].builtin);
    PROBE_ASSERT(listed[6].id == "b.glsl" && !listed[6].builtin);

    // Precedence: CLI > Env > INI > none
    const std::string cli = "crt";
    const std::string ini = "fsr";
    PROBE_ASSERT(repsp::platform::ChoosePostShader(&cli, "scanline", &ini) == "crt");
    PROBE_ASSERT(repsp::platform::ChoosePostShader(nullptr, "scanline", &ini) == "scanline");
    PROBE_ASSERT(repsp::platform::ChoosePostShader(nullptr, "", &ini) == "fsr");
    PROBE_ASSERT(repsp::platform::ChoosePostShader(nullptr, nullptr, nullptr) == "none");

    std::filesystem::remove_all(base, ec);
}

} // namespace

int main()
{
    TestSourceAssemblyAndParameters();
    TestPresetParsing();
    TestCatalogAndPrecedence();
    std::printf("post_shader_probe: PASS\n");
    return 0;
}
