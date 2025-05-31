

import uzleo.json;
import std;
import fmt;

namespace rng = std::ranges;

namespace {

std::string_view constexpr compiler{"clang++"};
std::string_view constexpr cxx_flags{"-std=c++26 -stdlib=libc++ -O3"};
std::array<std::string_view, 2> constexpr kLibSearchPaths{
    "/modules/lib/", "/modules/lib/uzleo/"};
std::string_view constexpr rule_cxx_module{R"(
rule cxx_module
  command = $cxx $cxx_flags $module_flags -fmodule-output -MJ $out.json -c $in -o $out
  description = Compiling module $in)"};

std::string_view constexpr rule_cxx_regular{R"(
rule cxx_regular
  command = $cxx $cxx_flags $module_flags -MJ $out.json -c $in -o $out
  description = Compiling source $in)"};

std::string_view constexpr rule_archive{R"(
rule archive
  command = ar rcs $out $in
  description = Archiving $out)"};

std::string_view constexpr rule_link{R"(
rule link
  command = $cxx $cxx_flags $module_flags @link.rsp $ld_flags -o $out
  rspfile = link.rsp
  rspfile_content = $in
  description = Linking $out)"};

constexpr std::string_view rule_sharedlib{R"(
rule sharedlib
  command = $cxx -shared -o $out $in $ld_flags
  description = Linking shared library $out)"};

struct ModuleInfo {
  std::string_view bmi_path;
  std::string_view lib_name;
  std::vector<std::string_view> deps;
};

using ModuleInfoMap = std::unordered_map<std::string_view, ModuleInfo>;

auto GetBuildDirPath(uzleo::json::Json const& build_json) {
  std::string build_dir =
      build_json.Contains("b")
          ? build_json.GetJson("b").GetStringView() | rng::to<std::string>()
          : "build/";
  return build_dir;
}

auto ResolveDeps(ModuleInfoMap const& module_info_map,
                 uzleo::json::Json const& build_json) {
  // stack based DFS to resolve transitive deps
  std::stack<std::string_view> to_process{};
  for (auto const& j : build_json.GetJson("imp").GetArray()) {
    to_process.push(j.GetStringView());
  }

  std::unordered_set<std::string_view> modules_to_import{};
  while (not to_process.empty()) {
    auto current{to_process.top()};
    to_process.pop();
    if (not modules_to_import.contains(current)) {
      modules_to_import.insert(current);
      for (auto const m : module_info_map.at(current).deps) {
        to_process.push(m);
      }
    }
  }

  return modules_to_import | rng::to<std::vector>();
}

auto FillLdFlags(std::vector<std::string_view> const& modules_to_import,
                 ModuleInfoMap const& module_info_map) -> std::string {
  // TODO(uzleo): ld_flags should also check what is `l` in json
  std::string ld_flags{};
  ld_flags.reserve(100);
  rng::copy(modules_to_import |
                std::views::filter([&module_info_map](auto const m) {
                  return not module_info_map.at(m).lib_name.empty();
                }) |
                std::views::transform([&module_info_map](
                                          auto const m) -> std::string {
                  return fmt::format("-l{} ", module_info_map.at(m).lib_name);
                }) |
                std::views::join,
            std::back_inserter(ld_flags));
  rng::copy(
      kLibSearchPaths | std::views::transform([](auto const l) -> std::string {
        return fmt::format("-L{} ", l);
      }) | std::views::join,
      std::back_inserter(ld_flags));

  return ld_flags;
}

auto FillModuleFlags(std::vector<std::string_view> const& modules_to_import,
                     ModuleInfoMap const& module_info_map,
                     std::string_view build_dir) -> std::string {
  std::string module_flags{};

  module_flags.reserve(100);
  rng::copy(modules_to_import |
                std::views::transform(
                    [&module_info_map](auto const m) -> std::string {
                      return fmt::format("-fmodule-file={}={} ", m,
                                         module_info_map.at(m).bmi_path);
                    }) |
                std::views::join,
            std::back_inserter(module_flags));
  module_flags += fmt::format(" -fprebuilt-module-path={}", build_dir);

  return module_flags;
}

auto WriteNinjaFile(ModuleInfoMap const& module_info_map,
                    uzleo::json::Json const& build_json) {
  auto const build_dir{GetBuildDirPath(build_json)};
  std::filesystem::create_directory(build_dir);

  auto const modules_to_import{ResolveDeps(module_info_map, build_json)};

  std::ofstream file_stream{fmt::format("{}/build.ninja", build_dir)};
  file_stream << "cxx = " << compiler << '\n';

  file_stream << "cxx_flags = " << cxx_flags << ' ';
  if (build_json.Contains("so")) {
    file_stream << "-fPIC";
  }
  file_stream << '\n';

  file_stream << "module_flags = "
              << FillModuleFlags(modules_to_import, module_info_map, build_dir)
              << '\n';
  file_stream << "ld_flags = "
              << FillLdFlags(modules_to_import, module_info_map);
  file_stream << rule_cxx_module;
  file_stream << rule_cxx_regular;

  if (build_json.Contains("a")) {
    file_stream << rule_archive << '\n';
  } else if (build_json.Contains("e")) {
    file_stream << rule_link << '\n';
  } else if (build_json.Contains("so")) {
    file_stream << rule_sharedlib << '\n';
  } else {
    std::filesystem::remove_all(build_dir);
    throw std::invalid_argument{
        "Do not know what to generate. Is build.json ok?"};
  }

  // doing build rule for each individual source file
  auto const& sources_map{build_json.GetJson("src").GetMap()};
  for (auto const& [src_name, src_deps_json] : sources_map) {
    std::string_view const src_name_sv{src_name};
    auto const cxx_rule{src_name_sv.ends_with("cppm") ? "cxx_module"
                                                      : "cxx_regular"};
    file_stream << "build " << build_dir
                << src_name_sv.substr(0, src_name_sv.find_last_of('.'))
                << ".o: " << cxx_rule << ' ' << src_name_sv;

    const auto& deps = src_deps_json.GetArray();
    if (not deps.empty()) {
      file_stream << " | ";
      for (auto const& j : deps) {
        file_stream << build_dir
                    << j.GetStringView().substr(
                           0, j.GetStringView().find_last_of('.'))
                    << ".o ";
      }
    }

    file_stream << '\n';
  }

  if (build_json.Contains("a")) {
    file_stream << "build " << build_dir << "lib"
                << build_json.GetJson("a").GetStringView() << ".a: archive ";
  } else if (build_json.Contains("e")) {
    file_stream << "build " << build_dir
                << build_json.GetJson("e").GetStringView() << ": link ";
  } else if (build_json.Contains("so")) {
    file_stream << "build " << build_dir << "lib"
                << build_json.GetJson("so").GetStringView()
                << ".so: sharedlib ";
  }
  // append all compiled object files (*.o) to this build rule
  for (auto const& src_name : sources_map | std::views::keys) {
    std::string_view const src_name_sv{src_name};
    file_stream << build_dir
                << src_name_sv.substr(0, src_name_sv.find_last_of('.'))
                << ".o ";
  }
  file_stream << '\n';
}

} // namespace

auto main() -> int {
  // package registry
  ModuleInfoMap const module_info_map{
      {"uzleo.json",
       {.bmi_path = "/modules/bmi/uzleo/json.pcm",
        .lib_name = "json",
        .deps = {"fmt", "std"}}},
      {"fmt",
       {.bmi_path = "/modules/bmi/fmt.pcm", .lib_name = "fmt", .deps = {}}},
      {"std", {.bmi_path = "/modules/bmi/std.pcm", .lib_name = "", .deps = {}}},
      {"foo",
       {.bmi_path = "/modules/bmi/foo.pcm",
        .lib_name = "foo",
        .deps = {"std"}}},
      {"std.compat",
       {.bmi_path = "/modules/bmi/std.compat.pcm",
        .lib_name = "",
        .deps = {}}}};

  auto const build_json{uzleo::json::Parse("build.json")};

  WriteNinjaFile(module_info_map, build_json);
}
