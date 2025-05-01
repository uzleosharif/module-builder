

/// - The purpose of this program is to allow building (and installing) cpp
/// modules conveniently.
/// - It tries to replace cmake usage. Uses json as source file for
/// build configuration.
/// - It can generate BMI, static/shared-library and native
/// executable.
/// - It can generate compile_commands.json.

// TODO(): generate compile_commands.json

import uzleo.json;
import std;
import fmt;

namespace rng = std::ranges;

namespace {

struct ModuleInfo {
  std::string_view bmi_path;
  std::string_view lib_name;
  std::vector<std::string_view> deps;
};

} // namespace

auto main() -> int {
  auto constexpr compiler{"clang++ -std=c++26 -stdlib=libc++ -O3"};

  std::array<std::string_view, 2> constexpr lib_search_paths{
      "/stuff/c++-packages/clang++-with-libc++/lib/",
      "/stuff/c++-packages/clang++-with-libc++/lib/uzleo/"};

  // package registry
  std::unordered_map<std::string_view, ModuleInfo> const module_info_map{
      {"uzleo.json",
       {.bmi_path =
            "/stuff/c++-packages/clang++-with-libc++/bmi/uzleo/json.pcm",
        .lib_name = "json",
        .deps = {"fmt", "std"}}},
      {"fmt",
       {.bmi_path = "/stuff/c++-packages/clang++-with-libc++/bmi/fmt.pcm",
        .lib_name = "fmt",
        .deps = {}}},
      {"std",
       {.bmi_path = "/stuff/c++-packages/clang++-with-libc++/bmi/std.pcm",
        .lib_name = "",
        .deps = {}}},
      {"std.compat",
       {.bmi_path =
            "/stuff/c++-packages/clang++-with-libc++/bmi/std.compat.pcm",
        .lib_name = "",
        .deps = {}}}

  };

  auto const build_json{uzleo::json::Parse("build.json")};
  auto const& outdir_json{build_json.GetValue("outdir")};
  std::unordered_set<std::string_view> modules_to_import{};
  for (auto const& j : build_json.GetValue("imported_modules").GetArray()) {
    modules_to_import.emplace(j.GetStringView());
    for (auto const dep : module_info_map.at(j.GetStringView()).deps) {
      modules_to_import.emplace(dep);
    }
    // TODO(): the newly added deps in previous step need to be scanned to their
    // deps too and so on ...
  }

  auto const make_needed_dirs{[&build_json, &outdir_json] {
    std::string dirs{""};
    dirs.reserve(100);

    dirs += "build/ ";
    for (auto const& j : build_json.GetValue("src").GetArray()) {
      if (j.GetStringView().contains('/')) {
        dirs += fmt::format(
            "build/{} ",
            j.GetStringView().substr(0, j.GetStringView().find_last_of('/')));
      }
    }

    if (build_json.Contains("e")) {
      dirs +=
          fmt::format("{}/bin/ ", outdir_json.GetValue("path").GetStringView());
    } else {
      if (outdir_json.Contains("namespace")) {
        dirs += fmt::format("{0}/lib/{1} {0}/bmi/{1} ",
                            outdir_json.GetValue("path").GetStringView(),
                            outdir_json.GetValue("namespace").GetStringView());
      } else {
        dirs += fmt::format("{0}/lib {0}/bmi ",
                            outdir_json.GetValue("path").GetStringView());
      }
    }

    std::string cmd;
    cmd.reserve(rng::size(dirs) + 10);
    cmd = "mkdir -p " + dirs + '\n';
    return cmd;
  }};

  auto const install_executable{[&build_json, &outdir_json, &module_info_map,
                                 &modules_to_import,
                                 &lib_search_paths]() -> std::string {
    std::string cmd{};
    cmd.reserve(100);
    // add output format info to command
    cmd +=
        fmt::format(" -o build/{}", build_json.GetValue("e").GetStringView());
    // add source-files to command
    for (auto const& j : build_json.GetValue("src").GetArray()) {
      cmd += fmt::format(" {}", j.GetStringView());
    }

    // add library info (linkage) to command
    for (auto const m : modules_to_import) {
      auto const lib_name{module_info_map.at(m).lib_name};
      if (not lib_name.empty()) {
        cmd += fmt::format(" -l{}", lib_name);
      }
    }
    for (auto const p : lib_search_paths) {
      cmd += fmt::format(" -L {}", p);
    }

    // install the generated executable/binary
    cmd += '\n';
    cmd += fmt::format("cp build/{} {}/bin/",
                       build_json.GetValue("e").GetStringView(),
                       outdir_json.GetValue("path").GetStringView());

    return cmd;
  }};

  auto const install_archive{[&build_json, &outdir_json]() -> std::string {
    /// this code block compiles individual source files (*.cppm)
    /// ony-by-one into *.o and *.pcm
    /// *.pcm are installed to outdir/bmi/{namespace}
    /// finally all *.o are assembled together to make a static
    /// library lib*.a the lib is installed to
    /// outdir/lib/{namespace}
    std::string cmd{};
    cmd.reserve(100);

    auto const& outdir_json{build_json.GetValue("outdir")};

    std::string overall_artifacts{};
    for (auto const& j : build_json.GetValue("src").GetArray()) {
      std::string_view output_artifact_sv{rng::begin(j.GetStringView()),
                                          j.GetStringView().find_last_of('.')};
      overall_artifacts += fmt::format("build/{}.o ", output_artifact_sv);

      cmd += fmt::format(" -fmodule-output -o build/{}.o -c {}\n",
                         output_artifact_sv, j.GetStringView());

      // install the generated bmi
      cmd += fmt::format("cp build/{}.pcm {}/bmi/", output_artifact_sv,
                         outdir_json.GetValue("path").GetStringView());
      if (outdir_json.Contains("namespace")) {
        cmd += outdir_json.GetValue("namespace").GetStringView();
      }
      cmd += "/\n";
    }
    // create archive
    cmd += fmt::format("ar r build/lib{}.a {}\n",
                       build_json.GetValue("a").GetStringView(),
                       overall_artifacts);

    // install the generated lib
    cmd += fmt::format("cp build/lib{}.a {}/lib/",
                       build_json.GetValue("a").GetStringView(),
                       outdir_json.GetValue("path").GetStringView());
    if (outdir_json.Contains("namespace")) {
      cmd +=
          build_json.GetValue("outdir").GetValue("namespace").GetStringView();
    }
    cmd += "/\n";

    return cmd;
  }};

  // setup initial part of clang compiler command
  std::string shell_commands{};
  shell_commands.reserve(512);
  shell_commands += make_needed_dirs();

  shell_commands += compiler;
  for (auto const m : modules_to_import) {
    shell_commands +=
        fmt::format(" -fmodule-file={}={}", m, module_info_map.at(m).bmi_path);
  }

  if (build_json.Contains("e")) {
    shell_commands += install_executable();
  } else if (build_json.Contains("a")) {
    shell_commands += install_archive();
  }

  // finally write to output medium
  std::ofstream file_stream{"build.sh"};
  file_stream << shell_commands;
}
