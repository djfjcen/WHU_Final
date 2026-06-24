set shell := ["zsh", "-cu"]

build_dir := "build"
sample := "test/sample.tc"

default:
    just --list

configure:
    cmake -S . -B {{build_dir}} -DCMAKE_BUILD_TYPE=Debug

configure-coverage:
    cmake -S . -B {{build_dir}} -DTOYC_ENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug

build:
    cmake --build {{build_dir}}

build-coverage: configure-coverage
    cmake --build {{build_dir}}

run file=sample flags="-dump-ast":
    {{build_dir}}/toyc-compiler {{flags}} < {{file}}

build-run file=sample flags="-dump-ast": build
    {{build_dir}}/toyc-compiler {{flags}} < {{file}}

test: build
    {{build_dir}}/toyc-frontend-tests

test-filter filter: build
    {{build_dir}}/toyc-frontend-tests --gtest_filter='{{filter}}'

coverage: configure-coverage
    cmake --build {{build_dir}} --target coverage

clean:
    cmake --build {{build_dir}} --target clean
