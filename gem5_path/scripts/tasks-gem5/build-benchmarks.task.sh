
declare_task "build-benchmarks" "Build benchmarks in the host system. Options:
        --architecture X: Build only architecture X
        BUG: Due to the way that becnhmarks are built, only one architecture can be built each time.
"

# TODO: Add options to choose what benchmarks should be built.
BENCHMARKS=(
    "bayes"
    "genome"
    "intruder"
    "kmeans"
    "labyrinth"
    "ssca2"
    "vacation"
    "yada"
)

# TODO: Add options to choose what sufixxes should be built.
SUFIXES=(
    "htm.fallbacklock"
)

task_build-benchmarks() {
    local -a archs=("${ENABLED_ARCHITECTURES[@]}")
    options="$(simpler_getopt "architecture:" "$@")"
    eval set -- "$options"
    while [[ $# -gt 0 ]] ; do
        if [[ "--architecture" == "$1" ]] ; then
            shift
            archs=("$1")
        elif [[ "--" == "$1" ]] ; then
            true # ignore
        else 
            error_and_exit "Unknown option '$1'"
        fi
        shift
    done
    for a in "${archs[@]}" ; do
        build_benchmarks "$a"
    done
}

build_benchmarks() {
    local arch="$1"

    if [[ "$arch" == "x86_64" ]] ; then 
        build_benchmarks_sumarray "$arch"
    else
        # TODO
        echo "$(color yellow "Skipping build of test benchmark (sumarray) because it is not yet supported for '$arch'. TODO: fix this")"
    fi

    build_benchmarks_stamp "$arch"
}

build_benchmarks_sumarray() {
    local arch="$1"

    echo "$(color green "Building test benchmark (sumarray) for $arch")"
    
    if [[ "$arch" == "x86_64" ]] ; then
        local makefile="Makefile.x86"
        export X86_CROSS_GCC_PREFIX="${BENCHMARKS_ARCH_COMPILER_PREFIX[$arch]}"
    else
        error_and_exit "Architecture $arch not supported for benchmark sumarray"
    fi
    pushd "$GEM5_ROOT/tests/test-progs/caps/sumarray" > /dev/null
    make -f "$makefile"
    popd > /dev/null
}

build_benchmarks_stamp() {
    local arch="$1"

    echo "$(color green "Building stamp benchmarks for $arch")"
    
    if [[ "$arch" == "x86_64" ]] ; then
        local build_arch="x86"
        export X86_CROSS_GCC_PREFIX="${BENCHMARKS_ARCH_COMPILER_PREFIX[$arch]}"
    elif [[ "$arch" == "aarch64" ]] ; then
        local build_arch="aarch64"
        export AARCH64_CROSS_GCC_PREFIX="${BENCHMARKS_ARCH_COMPILER_PREFIX[$arch]}"
    else
        error_and_exit "Architecture $arch not supported for stamp"
    fi

    check_stamp_gem5_directory_links
    
    for b in "${BENCHMARKS[@]}" ; do
        for s in "${SUFIXES[@]}" ; do
            if [[ "$arch" == "aarch64" && "$s" == "htm.fallbacklock2phase" ]] ; then
                echo "$(color yellow "Skipping build of $b.$a.$s (TODO)")"
            else
                (
                    echo "$(color green "Build $b.$a.$s")"
                    cd "$(absolute_path "$BENCHMARKS_HTM_STAMP/$b")"
                    make -j $(get_num_threads_for_building) -f "Makefile.$s" "ARCH=$arch"
                )
            fi
        done
    done
}

check_stamp_gem5_directory_links() {
    if [[ ! -d "$(absolute_path "$BENCHMARKS_HTM_STAMP")" || ! -L "${GEM5_ROOT}/${BENCHMARKS_HTM_STAMP}" ]] ; then
        error_and_exit "Stamp directory symlink '$(absolute_path "$BENCHMARKS_HTM_STAMP")' not found. Clone the repository in a directory out of ${GEM5_ROOT} and create a symbolic link to it in '$(dirname "$(absolute_path "$BENCHMARKS_HTM_STAMP")")'."
    fi

    if [[ ! -d "$(absolute_path "$BENCHMARKS_HTM_STAMP")/gem5" ]] ; then
        ln -s "$GEM5_ROOT" "$(absolute_path "$BENCHMARKS_HTM_STAMP")/gem5"
    fi
}
