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
    {{build_dir}}/toyc-ir-tests
    {{build_dir}}/toyc-codegen-tests

test-frontend: build
    {{build_dir}}/toyc-frontend-tests

test-ir: build
    {{build_dir}}/toyc-ir-tests

test-codegen: build
    {{build_dir}}/toyc-codegen-tests

test-filter filter: build
    {{build_dir}}/toyc-frontend-tests --gtest_filter='{{filter}}'
    {{build_dir}}/toyc-ir-tests --gtest_filter='{{filter}}'
    {{build_dir}}/toyc-codegen-tests --gtest_filter='{{filter}}'

emit-asm file=sample: build
    {{build_dir}}/toyc-compiler < {{file}}

oracle-codegen: build
    test/codegen/run_codegen_oracle.sh

oracle-codegen-docker:
    test/codegen/run_codegen_oracle_docker.sh

coverage: configure-coverage
    cmake --build {{build_dir}} --target coverage

clean:
    cmake --build {{build_dir}} --target clean
