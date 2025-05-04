


# modi


I usually have small c++ modules to build. Setting up cmake is an overkill and 
brings unnecessary complexity. With this tool, I want to streamline my c++ module 
development.

This projects provides a utility `modi` that could be used to setup other modules.
Running `modi` at c++ project root (containing build.json)
emits a `build.ninja` file in build/ dir.
`ninja` can be invoked then to generate BMI, static/shared-library and
native executable.

## format for build.json

```
{
  "b": "build_dir",      // [optional] use this to set the build folder (by default, it is `build/`)

  "src": ["foo.cpp", ...], // list of source file names (relative paths)

  "imp": ["bar", ...], // list of c++ modules that need to be imported 

  "e": "binary_name"    // use this to build executable `binary_name`
  // or
  "a": "some_lib_name"        // use this to build static lib `libsome_lib_name.a`
}
```

## module registry

The tool relies on a module registry on file-system at `/modules/`.
To get such a dev environment for free, it is recommended to use 
`cpp-modules-base` docker as provided in `dockers/cpp-modules-base/` folder.

