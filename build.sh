mkdir -p build/ /home/uzleo//bin/
clang++ -std=c++26 -stdlib=libc++ -O3 -fmodule-file=uzleo.json=/stuff/c++-packages/clang++-with-libc++/bmi/uzleo/json.pcm -fmodule-file=fmt=/stuff/c++-packages/clang++-with-libc++/bmi/fmt.pcm -fmodule-file=std=/stuff/c++-packages/clang++-with-libc++/bmi/std.pcm -o build/modi module_builder.cpp -ljson -lfmt -L /stuff/c++-packages/clang++-with-libc++/lib/ -L /stuff/c++-packages/clang++-with-libc++/lib/uzleo/

