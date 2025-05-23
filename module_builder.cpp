

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

  std::array<std::string_view, 2> constexpr lib_search_paths{
      "/modules/lib/", "/modules/lib/uzleo/"};

  auto constexpr compiler{"clang++"};
  auto constexpr cxx_flags{"-std=c++26 -stdlib=libc++ -O3"};

  constexpr std::string_view rule_cxx_module{R"(
rule cxx_module
  command = $cxx $cxx_flags $module_flags -fmodule-output -MJ $out.json -c $in -o $out
  description = Compiling module $in)"};

  constexpr std::string_view rule_cxx_regular{R"(
rule cxx_regular
  command = $cxx $cxx_flags $module_flags -MJ $out.json -c $in -o $out
  description = Compiling source $in)"};

  constexpr std::string_view rule_archive{R"(
rule archive
  command = ar rcs $out $in
  description = Archiving $out)"};

  constexpr std::string_view rule_link{R"(
rule link
  command = $cxx $cxx_flags $module_flags @link.rsp $ld_flags -o $out
  rspfile = link.rsp
  rspfile_content = $in
  description = Linking $out)"};

  constexpr std::string_view rule_sharedlib{R"(
rule sharedlib
  command = $cxx -shared -o $out $in $ld_flags
  description = Linking shared library $out)"};

  // package registry
  std::unordered_map<std::string_view, ModuleInfo> const module_info_map{
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

  std::string builddir =
      build_json.Contains("b")
          ? build_json.GetJson("b").GetStringView() | rng::to<std::string>()
          : "build/";
  std::filesystem::create_directory(builddir);

  auto const resolve_deps{
      [&build_json, &module_info_map]() -> std::vector<std::string_view> {
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
      }};

  auto modules_to_import{resolve_deps()};

  auto module_flags =
      modules_to_import |
      std::views::transform([&module_info_map](auto const m) -> std::string {
        return fmt::format("-fmodule-file={}={} ", m,
                           module_info_map.at(m).bmi_path);
      }) |
      std::views::join | rng::to<std::string>();
  module_flags += fmt::format(" -fprebuilt-module-path={}", builddir);

  auto ld_flags =
      modules_to_import | std::views::filter([&module_info_map](auto const m) {
        return not module_info_map.at(m).lib_name.empty();
      }) |
      std::views::transform([&module_info_map](auto const m) -> std::string {
        return fmt::format("-l{} ", module_info_map.at(m).lib_name);
      }) |
      std::views::join | rng::to<std::string>();
  rng::copy(
      lib_search_paths | std::views::transform([](auto const l) -> std::string {
        return fmt::format("-L{} ", l);
      }) | std::views::join,
      std::back_inserter(ld_flags));

  std::ofstream file_stream{fmt::format("{}/build.ninja", builddir)};
  file_stream << "cxx = " << compiler << '\n';

  file_stream << "cxx_flags = " << cxx_flags << ' ';
  if (build_json.Contains("so")) {
    file_stream << "-fPIC";
  }
  file_stream << '\n';

  file_stream << "module_flags = " << module_flags << '\n';
  file_stream << "ld_flags = " << ld_flags;
  file_stream << rule_cxx_module;
  file_stream << rule_cxx_regular;

  if (build_json.Contains("a")) {
    file_stream << rule_archive << '\n';
  } else if (build_json.Contains("e")) {
    file_stream << rule_link << '\n';
  } else if (build_json.Contains("so")) {
    file_stream << rule_sharedlib << '\n';
  } else {
    std::filesystem::remove_all(builddir);
    throw std::invalid_argument{
        "Do not know what to generate. Is build.json ok?"};
  }

  // doing build rule for each individual source file
  auto const& sources_map{build_json.GetJson("src").GetMap()};
  for (auto const& [src_name, src_deps_json] : sources_map) {
    std::string_view const src_name_sv{src_name};
    auto const cxx_rule{src_name_sv.ends_with("cppm") ? "cxx_module"
                                                      : "cxx_regular"};
    file_stream << "build " << builddir
                << src_name_sv.substr(0, src_name_sv.find_last_of('.'))
                << ".o: " << cxx_rule << ' ' << src_name_sv;

    const auto& deps = src_deps_json.GetArray();
    if (not deps.empty()) {
      file_stream << " | ";
      for (auto const& j : deps) {
        file_stream << builddir
                    << j.GetStringView().substr(
                           0, j.GetStringView().find_last_of('.'))
                    << ".o ";
      }
    }

    file_stream << '\n';
  }

  if (build_json.Contains("a")) {
    file_stream << "build " << builddir << "lib"
                << build_json.GetJson("a").GetStringView() << ".a: archive ";
  } else if (build_json.Contains("e")) {
    file_stream << "build " << builddir
                << build_json.GetJson("e").GetStringView() << ": link ";
  } else if (build_json.Contains("so")) {
    file_stream << "build " << builddir << "lib"
                << build_json.GetJson("so").GetStringView()
                << ".so: sharedlib ";
  }
  // append all compiled object files (*.o) to this build rule
  for (auto const& src_name : sources_map | std::views::keys) {
    std::string_view const src_name_sv{src_name};
    file_stream << builddir
                << src_name_sv.substr(0, src_name_sv.find_last_of('.'))
                << ".o ";
  }
  file_stream << '\n';
}
