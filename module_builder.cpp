

import uzleo.json;
import std;
import fmt;

namespace {

namespace rng = std::ranges;
namespace fs = std::filesystem;

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
                 uzleo::json::Json const& build_json)
    -> std::unordered_set<std::string_view> {
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

  return modules_to_import;
}

auto FillLdFlags(std::unordered_set<std::string_view> const& modules_to_import,
                 ModuleInfoMap const& module_info_map,
                 uzleo::json::Json const& build_json) -> std::string {
  std::string ld_flags{};
  ld_flags.reserve(100);

  if (build_json.Contains("l")) {
    rng::copy(build_json.GetJson("l").GetArray() |
                  rng::views::transform([](uzleo::json::Json const& lib_json) {
                    return fmt::format(" -l{} ", lib_json.GetStringView());
                  }) |
                  rng::views::join,
              std::back_inserter(ld_flags));
  }

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

auto FillModuleFlags(
    std::unordered_set<std::string_view> const& modules_to_import,
    ModuleInfoMap const& module_info_map, std::string_view build_dir)
    -> std::string {
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

auto ExtractModuleName(std::string_view src_path) -> std::string {
  static std::regex const module_re{
      R"(^\s*(?:export\s+)?module\s+([a-zA-Z0-9_.:]+))"};

  std::ifstream file{fs::path{src_path}};
  std::string line{};
  while (std::getline(file, line)) {
    std::smatch m{};
    if (std::regex_search(line, m, module_re)) {
      return m[1].str();
    }
  }

  // fallback: use the filename (wihtout extension) as the module name
  return fs::path{src_path}.stem().string();
}

auto MakeOSubstring(std::string_view src_name_sv) -> std::string {
  if (src_name_sv.ends_with("cppm")) {
    // For module interface units use the exported module name as the base name
    // for the output artifacts.
    return ExtractModuleName(src_name_sv);
  }

  // Keep the relative directory structure to avoid name collisions. Replace
  // directory separators with underscores so the result can be used directly
  // as a file name under the build directory.
  fs::path p{src_name_sv};
  p.replace_extension("");
  std::string sanitized{p.string()};
  rng::replace(sanitized, '/', '_');
  // "./foo.cpp" and "foo.cpp" should map to the same object file name.
  if (sanitized.starts_with("./")) {
    sanitized.erase(0, 2);
  }
  return sanitized;
}

using SrcDepsMap =
    std::unordered_map<std::string_view, std::unordered_set<std::string>>;

/// Remove all '//' and '/*…*/' comments from `text`.
auto StripComments(std::string const& text) -> std::string {
  std::string out;
  out.reserve(text.size());
  bool in_block = false;

  for (std::size_t i = 0; i < text.size(); ++i) {
    if (!in_block && i + 1 < text.size() && text[i] == '/' &&
        text[i + 1] == '*') {
      in_block = true;
      ++i; // skip '*'
    } else if (in_block && i + 1 < text.size() && text[i] == '*' &&
               text[i + 1] == '/') {
      in_block = false;
      ++i; // skip '/'
    } else if (!in_block) {
      // line‐comment
      if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
        // skip until end of line
        i += 2;
        while (i < text.size() && text[i] != '\n')
          ++i;
        if (i < text.size())
          out += '\n';
      } else {
        out += text[i];
      }
    }
  }
  return out;
}

auto ParseImports(std::string_view src_path,
                  std::unordered_map<std::string, std::string_view> const&
                      module_to_source_path_map)
    -> std::unordered_set<std::string> {
  static std::regex const import_re{R"(\bimport\s+([a-zA-Z0-9_.:]+))"};
  static std::regex const module_decl_re{
      R"(^\s*(?:export\s+)?module\s+([a-zA-Z0-9_.:]+))"};

  static std::unordered_map<std::string_view, std::unordered_set<std::string>>
      source_path_to_deps_cache{};
  if (source_path_to_deps_cache.contains(src_path)) {
    return source_path_to_deps_cache[src_path];
  }

  std::ifstream file{fs::path{src_path}, std::ios::binary bitor std::ios::ate};
  std::string file_content{};
  file_content.resize(file.tellg());

  file.seekg(0);
  file.read(file_content.data(), rng::size(file_content));

  auto cleaned_file_contents{StripComments(file_content)};
  auto deps =
      rng::subrange{std::regex_iterator{rng::cbegin(cleaned_file_contents),
                                        rng::cend(cleaned_file_contents),
                                        import_re},
                    std::sregex_iterator{}} |
      rng::views::transform(
          [](std::smatch const& m) -> std::string { return m[1].str(); }) |
      rng::views::filter(
          [&module_to_source_path_map](std::string const& mod_name) -> bool {
            return module_to_source_path_map.contains(mod_name);
          }) |
      rng::views::transform(
          [&module_to_source_path_map](std::string const& mod_name) {
            return module_to_source_path_map.at(mod_name);
          }) |
      rng::to<std::unordered_set<std::string>>();

  // If this is a module implementation unit, make it depend on its interface
  std::smatch module_match{};
  if (std::regex_search(cleaned_file_contents, module_match, module_decl_re)) {
    auto const module_interface_unit_name{module_match[1].str()};
    if (rng::contains(module_to_source_path_map |
                          std::views::filter([src_path](auto const& kvp) {
                            return kvp.second != src_path;
                          }) |
                          std::views::keys,
                      module_interface_unit_name)) {
      deps.emplace(module_to_source_path_map.at(module_interface_unit_name));
    }
  }

  source_path_to_deps_cache.emplace(src_path, std::move(deps));
  return source_path_to_deps_cache[src_path];
}

auto DetermineSrcDeps(uzleo::json::Json const& build_json) -> SrcDepsMap {
  // TODO(uzleo): parallel scanning of source-files e.g. with a thread pool
  // check out senders feature to describe cooperative tasks

  SrcDepsMap src_deps{};
  auto const& sources_arr{build_json.GetJson("src").GetArray()};

  // first pass: build a map from module-name -> source-paths
  std::unordered_map<std::string, std::string_view> module_to_source_path_map{};
  for (auto const& j : sources_arr | rng::views::filter([](auto const& j) {
                         return j.GetStringView().ends_with("cppm");
                       })) {
    auto const src_name{j.GetStringView()};
    module_to_source_path_map[ExtractModuleName(src_name)] = src_name;
  }

  // second pass: for every source (cpp, cppm), parse its imports
  for (auto const& j : sources_arr) {
    auto const src_name{j.GetStringView()};
    src_deps[src_name] = ParseImports(src_name, module_to_source_path_map);
  }

  return src_deps;
}

auto WriteNinjaFile(
    ModuleInfoMap const& module_info_map, uzleo::json::Json const& build_json,
    std::unordered_set<std::string_view> const& modules_to_import,
    SrcDepsMap const& src_deps) {
  auto const build_dir{GetBuildDirPath(build_json)};
  fs::create_directory(build_dir);

  std::ofstream file_stream{fmt::format("{}/build.ninja", build_dir)};
  file_stream << "cxx = " << compiler << '\n';

  file_stream << "cxx_flags = " << cxx_flags << ' ';
  if (build_json.Contains("e")) {
    file_stream << "-fsanitize=address,leak ";
  }
  if (build_json.Contains("so")) {
    file_stream << "-fPIC";
  }
  file_stream << '\n';

  file_stream << "module_flags = "
              << FillModuleFlags(modules_to_import, module_info_map, build_dir)
              << '\n';
  file_stream << "ld_flags = "
              << FillLdFlags(modules_to_import, module_info_map, build_json);
  file_stream << rule_cxx_module;
  file_stream << rule_cxx_regular;

  if (build_json.Contains("a")) {
    file_stream << rule_archive << '\n';
  } else if (build_json.Contains("e")) {
    file_stream << rule_link << '\n';
  } else if (build_json.Contains("so")) {
    file_stream << rule_sharedlib << '\n';
  } else {
    fs::remove_all(build_dir);
    throw std::invalid_argument{
        "Do not know what to generate. Is build.json ok?"};
  }

  // doing build rule for each individual source file
  for (auto const& [src_name_sv, deps] : src_deps) {
    auto const cxx_rule{src_name_sv.ends_with("cppm") ? "cxx_module"
                                                      : "cxx_regular"};
    file_stream << "build " << build_dir << MakeOSubstring(src_name_sv)
                << ".o: " << cxx_rule << ' ' << src_name_sv;
    if (not deps.empty()) {
      file_stream << " | ";
      for (auto const& d : deps) {
        file_stream << build_dir << MakeOSubstring(d) << ".o ";
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
  for (auto const& src_name_sv : src_deps | std::views::keys) {
    file_stream << build_dir << MakeOSubstring(src_name_sv) << ".o ";
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

  auto const build_json{uzleo::json::Parse(fs::path{"build.json"})};

  WriteNinjaFile(module_info_map, build_json,
                 ResolveDeps(module_info_map, build_json),
                 DetermineSrcDeps(build_json));
}
