# Streaming file-export contract, including interruption across separate processes.
foreach(required IN ITEMS EXE SAMPLE WORK)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "file_cli.cmake requires ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${WORK}")

function(run_expect expected)
    execute_process(COMMAND "${EXE}" ${ARGN}
        RESULT_VARIABLE rc OUTPUT_VARIABLE msg ERROR_VARIABLE err TIMEOUT 120)
    if(NOT rc STREQUAL "${expected}")
        message(FATAL_ERROR "Expected exit ${expected}, got ${rc}: ${ARGN}\n${msg}\n${err}")
    endif()
    set(last_out "${msg}" PARENT_SCOPE)
    set(last_err "${err}" PARENT_SCOPE)
endfunction()

set(common --flame "${SAMPLE}" --size 48 32 --oversample 2 --filter-radius 0.35
    --samples 131073 --seed 424242 --threads 2 --memory-mib 64 --disk-mib 32 --strip-rows 5)
set(output "${WORK}/resumed render.png")
set(checkpoint "${WORK}/resume checkpoint")
set(fresh "${WORK}/fresh render.png")
set(estimated "${WORK}/estimated render.png")
set(estimate_checkpoint "${WORK}/estimate checkpoint")
file(REMOVE "${estimated}")
file(REMOVE_RECURSE "${checkpoint}" "${estimate_checkpoint}")

# Estimation validates the same budget and format inputs but creates no files.
run_expect(0 ${common} --output "${estimated}" --bit-depth 16 --alpha
    --checkpoint "${estimate_checkpoint}" --estimate)
if(EXISTS "${estimated}" OR EXISTS "${estimate_checkpoint}" OR
   NOT last_out MATCHES "width=48 height=32" OR NOT last_out MATCHES "format=png16 alpha=1" OR
   NOT last_out MATCHES "strip_rows=5 strips=7" OR
   NOT last_out MATCHES "host_bytes=([0-9]+)")
    message(FATAL_ERROR "Estimate mutated storage or omitted the selected plan: ${last_out}")
endif()
set(host_bytes "${CMAKE_MATCH_1}")
if(NOT last_out MATCHES "disk_bytes=([0-9]+)")
    message(FATAL_ERROR "Estimate omitted disk storage: ${last_out}")
endif()
set(disk_bytes "${CMAKE_MATCH_1}")
if(host_bytes GREATER 67108864 OR disk_bytes GREATER 33554432)
    message(FATAL_ERROR "The accepted plan exceeds its selected budgets")
endif()
run_expect(0 --flame "${SAMPLE}" --size 24 16 --samples 1000 --estimate)
if(NOT last_out MATCHES "width=24 height=16" OR NOT last_out MATCHES "format=png8")
    message(FATAL_ERROR "Output-free estimation did not use the default PNG plan")
endif()

# Pause publishes durable sampling data, preserves a previous destination and
# reports a distinct status. A different process resumes with another worker count.
file(WRITE "${output}" "previous completed destination\n")
file(SHA256 "${output}" previous)
run_expect(75 ${common} --output "${output}" --bit-depth 16 --alpha
    --checkpoint "${checkpoint}" --pause-after 1)
file(SHA256 "${output}" paused_destination)
if(NOT paused_destination STREQUAL previous OR
   NOT EXISTS "${checkpoint}/manifest.apocheckpoint" OR
   NOT EXISTS "${checkpoint}/chunk-0.apohits" OR
   NOT last_out MATCHES "completed_samples=65536 total_samples=131073")
    message(FATAL_ERROR "Pause did not preserve the destination and commit exactly one chunk")
endif()

# Reject incompatible identities, strip choices, and unrequested checkpoint reuse.
foreach(bad IN ITEMS "--seed;424243" "--bit-depth;8" "--size;49;32"
        "--samples;131074" "--strip-rows;6")
    run_expect(1 ${common} --output "${output}" --bit-depth 16 --alpha
        --checkpoint "${checkpoint}" --resume ${bad})
    file(SHA256 "${output}" after)
    if(NOT after STREQUAL previous)
        message(FATAL_ERROR "A rejected checkpoint identity replaced the destination: ${bad}")
    endif()
endforeach()
run_expect(1 ${common} --output "${output}" --bit-depth 16 --alpha --checkpoint "${checkpoint}")

# Corrupt a copy, keeping the resumable source untouched. Both the manifest and
# a committed chunk are independently checked before any destination replacement.
set(corrupt_chunk "${WORK}/corrupt chunk checkpoint")
set(corrupt_manifest "${WORK}/corrupt manifest checkpoint")
file(REMOVE_RECURSE "${corrupt_chunk}" "${corrupt_manifest}")
file(MAKE_DIRECTORY "${corrupt_chunk}" "${corrupt_manifest}")
file(COPY "${checkpoint}/" DESTINATION "${corrupt_chunk}")
file(COPY "${checkpoint}/" DESTINATION "${corrupt_manifest}")
file(APPEND "${corrupt_chunk}/chunk-0.apohits" "invalid extra chunk byte")
file(APPEND "${corrupt_manifest}/manifest.apocheckpoint" "invalid extra manifest byte")
foreach(corrupt IN ITEMS "${corrupt_chunk}" "${corrupt_manifest}")
    run_expect(1 ${common} --output "${output}" --bit-depth 16 --alpha
        --checkpoint "${corrupt}" --resume)
    file(SHA256 "${output}" after)
    if(NOT after STREQUAL previous)
        message(FATAL_ERROR "Corrupted checkpoint replaced the destination")
    endif()
endforeach()

run_expect(0 ${common} --output "${output}" --bit-depth 16 --alpha
    --checkpoint "${checkpoint}" --resume --threads 3)
if(NOT last_out MATCHES "backend=cpu" OR NOT last_out MATCHES "format=png16 alpha=1 resumed=1" OR
   NOT last_out MATCHES "samples=131073" OR NOT last_out MATCHES "plotted=[1-9][0-9]*" OR
   NOT last_out MATCHES "sampling_ms=" OR NOT last_out MATCHES "reconstruction_ms=")
    message(FATAL_ERROR "Completion did not report actual backend, counts, format and timings")
endif()
file(READ "${output}" signature LIMIT 8 HEX)
file(READ "${output}" ihdr OFFSET 16 LIMIT 10 HEX)
if(NOT signature STREQUAL "89504e470d0a1a0a" OR NOT ihdr STREQUAL "00000030000000201006")
    message(FATAL_ERROR "PNG is not the requested 48x32 16-bit RGBA image")
endif()
run_expect(0 ${common} --output "${fresh}" --format png16 --alpha --threads 1)
file(SHA256 "${output}" resumed_hash)
file(SHA256 "${fresh}" fresh_hash)
if(NOT resumed_hash STREQUAL fresh_hash)
    message(FATAL_ERROR "Resumed and uninterrupted PNG16 exports differ")
endif()

# A completed sampling checkpoint can reconstruct the same image again.
run_expect(0 ${common} --output "${output}" --bit-depth 16 --alpha
    --checkpoint "${checkpoint}" --resume)
file(SHA256 "${output}" repeated_hash)
if(NOT repeated_hash STREQUAL fresh_hash)
    message(FATAL_ERROR "Reconstruction from a completed checkpoint changed pixels")
endif()

# The extension and explicit format both select the high-depth writers.
set(ppm "${WORK}/high depth.ppm")
run_expect(0 ${common} --output "${ppm}" --bit-depth 16)
file(READ "${ppm}" ppm_header LIMIT 15 HEX)
if(NOT ppm_header MATCHES "^50360a34382033320a36353533350a")
    message(FATAL_ERROR "PPM16 did not declare maximum sample value 65535")
endif()
set(tiff "${WORK}/high depth.tif")
run_expect(0 ${common} --output "${tiff}" --format TIFF16 --alpha)
file(READ "${tiff}" tiff_header LIMIT 4 HEX)
if(NOT tiff_header STREQUAL "49492a00")
    message(FATAL_ERROR "TIFF extension alias did not select the streaming TIFF writer")
endif()

# All validation failures happen before replacing an existing complete output.
foreach(bad IN ITEMS "--memory-mib;1" "--disk-mib;1" "--strip-rows;0"
        "--strip-rows;16385" "--bit-depth;12" "--format;bmp"
        "--format;png8;--bit-depth;16" "--size;0;32" "--size;16385;32"
        "--oversample;0" "--filter-radius;nan" "--filter-radius;-1"
        "--pause-after;1" "--resume")
    run_expect(1 ${common} --output "${output}" --bit-depth 16 --alpha ${bad})
    file(SHA256 "${output}" after)
    if(NOT after STREQUAL fresh_hash)
        message(FATAL_ERROR "Invalid option replaced a complete PNG: ${bad}")
    endif()
endforeach()
run_expect(1 ${common} --output "${WORK}/rejected.bmp" --alpha)
run_expect(1 ${common} --output "${WORK}/rejected.ppm" --alpha)
run_expect(1 ${common} --output "${WORK}/rejected.unknown")
message(STATUS "Streaming CLI: estimation, PNG16/RGBA, TIFF16, PPM16, budget rejection, checkpoint pause/resume and corruption checks passed")
