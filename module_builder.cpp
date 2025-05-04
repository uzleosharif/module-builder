

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
  command = $cxx $cxx_flags $module_flags -fmodule-output -c $in -o $out
  description = Compiling module $in)"};

  constexpr std::string_view rule_cxx_regular{R"(
rule cxx_regular
  command = $cxx $cxx_flags $module_flags -c $in -o $out
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

  // package registry
  std::unordered_map<std::string_view, ModuleInfo> const module_info_map{
      {"uzleo.json",
       {.bmi_path = "/modules/bmi/uzleo/json.pcm",
        .lib_name = "json",
        .deps = {"fmt", "std"}}},
      {"fmt",
       {.bmi_path = "/modules/bmi/fmt.pcm", .lib_name = "fmt", .deps = {}}},
      {"std", {.bmi_path = "/modules/bmi/std.pcm", .lib_name = "", .deps = {}}},
      {"std.compat",
       {.bmi_path = "/modules/bmi/std.compat.pcm", .lib_name = "", .deps = {}}},

  };

  auto const build_json{uzleo::json::Parse("build.json")};

  std::string builddir =
      build_json.Contains("b")
          ? build_json.GetValue("b").GetStringView() | rng::to<std::string>()
          : "build/";
  std::filesystem::create_directory(builddir);

  std::unordered_set<std::string_view> modules_to_import{};
  for (auto const& j : build_json.GetValue("imp").GetArray()) {
    modules_to_import.emplace(j.GetStringView());
    for (auto const dep : module_info_map.at(j.GetStringView()).deps) {
      modules_to_import.emplace(dep);
    }
    // TODO(): the newly added deps in previous step need to be scanned to their
    // deps too and so on ...
  }

  auto module_flags =
      modules_to_import |
      std::views::transform([&module_info_map](auto const m) -> std::string {
        return fmt::format("-fmodule-file={}={} ", m,
                           module_info_map.at(m).bmi_path);
      }) |
      std::views::join | rng::to<std::string>();

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
  file_stream << "cxx_flags = " << cxx_flags << '\n';
  file_stream << "module_flags = " << module_flags << '\n';
  file_stream << "ld_flags = " << ld_flags;
  file_stream << rule_cxx_module;
  file_stream << rule_cxx_regular;

  if (build_json.Contains("a")) {
    file_stream << rule_archive << '\n';
  } else if (build_json.Contains("e")) {
    file_stream << rule_link << '\n';
  }

  auto const& source_array{build_json.GetValue("src").GetArray()};
  for (auto const& j : source_array) {
    auto const cxx_rule{j.GetStringView().ends_with("cppm") ? "cxx_module"
                                                            : "cxx_regular"};
    file_stream << "build " << builddir
                << j.GetStringView().substr(0,
                                            j.GetStringView().find_last_of('.'))
                << ".o: " << cxx_rule << ' ' << j.GetStringView() << '\n';
  }

  if (build_json.Contains("a")) {
    file_stream << "build " << builddir << "lib"
                << build_json.GetValue("a").GetStringView() << ".a: archive ";
  } else if (build_json.Contains("e")) {
    file_stream << "build " << builddir
                << build_json.GetValue("e").GetStringView() << ": link ";
  }

  for (auto const& j : source_array) {
    file_stream << builddir
                << j.GetStringView().substr(0,
                                            j.GetStringView().find_last_of('.'))
                << ".o ";
  }
  file_stream << '\n';
}
