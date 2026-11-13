# SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
# SPDX-License-Identifier: MIT
#
# Runs the built CLI tools (btoon, btoon-convert, btoon-schema) on the fixtures in this
# directory. Registered as the CTest "tools_cli" (see CMakeLists.txt):
#
#   cmake -DBTOON=<btoon> -DCONVERT=<btoon-convert> -DSCHEMA=<btoon-schema>
#         -DFIXTURES=<this directory> -DWORK=<scratch directory> -P run_tools_test.cmake
#
# all-types.btoon, all-types.json and deep-128.btoon come from make_fixtures.py (an
# independent Python encoder and JSON mapping).
cmake_minimum_required(VERSION 3.20)

foreach(var IN ITEMS BTOON CONVERT SCHEMA FIXTURES WORK)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "run_tools_test.cmake: pass -D${var}=...")
    endif()
endforeach()

set(F "${FIXTURES}")
set(W "${WORK}")
file(REMOVE_RECURSE "${W}")
file(MAKE_DIRECTORY "${W}")

# check(<name> <expected exit status> [STDIN file] [STDOUT file] [STDERR regex] [DIR dir]
#       COMMAND <command> <args>...)
# Runs the command and fails the test if the exit status differs, if standard error
# doesn't match STDERR, or if a sanitizer or crash report appears. A crash can't pass
# as an ordinary failure: execute_process reports a signal as text, not as a number.
function(check name expected)
    cmake_parse_arguments(PARSE_ARGV 2 A "" "STDIN;STDOUT;STDERR;DIR" "COMMAND")
    set(cmd COMMAND ${A_COMMAND} RESULT_VARIABLE rc ERROR_VARIABLE err)
    if(A_STDIN)
        list(APPEND cmd INPUT_FILE "${A_STDIN}")
    endif()
    if(A_STDOUT)
        list(APPEND cmd OUTPUT_FILE "${A_STDOUT}")
    else()
        list(APPEND cmd OUTPUT_VARIABLE out)
    endif()
    if(A_DIR)
        list(APPEND cmd WORKING_DIRECTORY "${A_DIR}")
    else()
        list(APPEND cmd WORKING_DIRECTORY "${W}")
    endif()
    execute_process(${cmd})
    if(NOT "${rc}" STREQUAL "${expected}")
        message(SEND_ERROR "${name}: exit status ${rc}, expected ${expected}\n${err}")
    elseif(A_STDERR AND NOT err MATCHES "${A_STDERR}")
        message(SEND_ERROR "${name}: standard error doesn't match '${A_STDERR}':\n${err}")
    endif()
    if(err MATCHES "Sanitizer|runtime error:|uncaught exception|Segmentation fault|stack overflow")
        message(SEND_ERROR "${name}: crash or sanitizer report:\n${err}")
    endif()
endfunction()

# same(<name> <file> <file>): byte-identical files.
function(same name a b)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${a}" "${b}" RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(SEND_ERROR "${name}: ${a} and ${b} differ")
    endif()
endfunction()

# contains(<name> <file> <regex>) / lacks(...)
function(contains name file regex)
    if(NOT EXISTS "${file}")
        message(SEND_ERROR "${name}: ${file} is missing")
        return()
    endif()
    file(READ "${file}" text)
    if(NOT text MATCHES "${regex}")
        message(SEND_ERROR "${name}: ${file} doesn't match '${regex}':\n${text}")
    endif()
endfunction()

# expect_text(<name> <file> <text>): the file holds exactly these bytes. Compared in hex:
# file(WRITE) writes in text mode on Windows ("\n" becomes "\r\n"), and file(READ) can drop
# carriage returns, so neither can produce or check exact bytes on every platform.
function(expect_text name file expected)
    if(NOT EXISTS "${file}")
        message(SEND_ERROR "${name}: ${file} is missing")
        return()
    endif()
    file(READ "${file}" actual_hex HEX)
    string(HEX "${expected}" expected_hex)
    if(NOT actual_hex STREQUAL expected_hex)
        file(READ "${file}" text)
        message(SEND_ERROR "${name}: ${file} is\n${text}\nexpected\n${expected}\n(hex ${actual_hex}\n vs ${expected_hex})")
    endif()
endfunction()

# --- every type round-trips: BTOON -> JSON -> BTOON, byte for byte ----------------------

check("btoon decode" 0 COMMAND "${BTOON}" decode "${F}/all-types.btoon" a.json)
same("btoon decode writes the mapping" "${W}/a.json" "${F}/all-types.json")
check("btoon encode" 0 COMMAND "${BTOON}" encode a.json a.btoon)
same("btoon round trip" "${W}/a.btoon" "${F}/all-types.btoon")
check("btoon decode --compact" 0 COMMAND "${BTOON}" decode "${F}/all-types.btoon" a-compact.json --compact)
check("btoon encode compact" 0 COMMAND "${BTOON}" encode a-compact.json a-compact.btoon)
same("btoon compact round trip" "${W}/a-compact.btoon" "${F}/all-types.btoon")
check("btoon decode via stdio" 0 STDIN "${F}/all-types.btoon" STDOUT "${W}/a-stdio.json"
      COMMAND "${BTOON}" decode - -)
same("btoon decode via stdio" "${W}/a-stdio.json" "${F}/all-types.json")
check("btoon encode via stdio" 0 STDIN "${F}/all-types.json" STDOUT "${W}/a-stdio.btoon"
      COMMAND "${BTOON}" encode)
same("btoon encode via stdio" "${W}/a-stdio.btoon" "${F}/all-types.btoon")
check("btoon info" 0 COMMAND "${BTOON}" info "${F}/all-types.btoon")

check("btoon-convert to JSON" 0 COMMAND "${CONVERT}" -o json -p "${F}/all-types.btoon" c.json)
same("btoon-convert writes the mapping" "${W}/c.json" "${F}/all-types.json")
check("btoon-convert from JSON" 0 COMMAND "${CONVERT}" c.json c.btoon)
same("btoon-convert JSON round trip" "${W}/c.btoon" "${F}/all-types.btoon")
check("btoon-convert to compact JSON" 0 COMMAND "${CONVERT}" -o json "${F}/all-types.btoon" c-compact.json)
check("btoon-convert from compact JSON" 0 COMMAND "${CONVERT}" -i json -o btoon c-compact.json c-compact.btoon)
same("btoon-convert compact round trip" "${W}/c-compact.btoon" "${F}/all-types.btoon")
check("btoon-convert to YAML" 0 COMMAND "${CONVERT}" -o yaml "${F}/all-types.btoon" c.yaml)
check("btoon-convert from YAML" 0 COMMAND "${CONVERT}" c.yaml c-yaml.btoon)
same("btoon-convert YAML round trip" "${W}/c-yaml.btoon" "${F}/all-types.btoon")
check("btoon-convert to MessagePack" 0 COMMAND "${CONVERT}" -o msgpack "${F}/all-types.btoon" c.msgpack)
check("btoon-convert from MessagePack" 0 COMMAND "${CONVERT}" c.msgpack c-mp.btoon)
same("btoon-convert MessagePack round trip" "${W}/c-mp.btoon" "${F}/all-types.btoon")
check("btoon-convert via stdio" 0 STDIN "${F}/all-types.btoon" STDOUT "${W}/c-stdio.json"
      COMMAND "${CONVERT}" -i btoon -o json -p - -)
same("btoon-convert via stdio" "${W}/c-stdio.json" "${F}/all-types.json")

# The review's examples: uint64 above INT64_MAX, BTOON-only types.
file(WRITE "${W}/big.json" "{\"id\": 18446744073709551615, \"n\": 9223372036854775808}")
check("btoon-convert uint64" 0 COMMAND "${CONVERT}" big.json big.btoon)
check("decode uint64" 0 COMMAND "${BTOON}" decode big.btoon big-out.json --compact)
expect_text("uint64 survives btoon-convert" "${W}/big-out.json"
            "{\"id\":18446744073709551615,\"n\":9223372036854775808}\n")

# Values nested exactly 128 deep (the limit), with tags at the bottom.
check("decode depth 128" 0 COMMAND "${BTOON}" decode "${F}/deep-128.btoon" deep-128.json)
check("encode depth 128" 0 COMMAND "${BTOON}" encode deep-128.json deep-128.btoon)
same("depth-128 round trip" "${W}/deep-128.btoon" "${F}/deep-128.btoon")
check("btoon-convert depth 128" 0 COMMAND "${CONVERT}" -o json "${F}/deep-128.btoon" deep-128c.json)
check("btoon-convert depth 128 back" 0 COMMAND "${CONVERT}" deep-128c.json deep-128c.btoon)
same("btoon-convert depth-128 round trip" "${W}/deep-128c.btoon" "${F}/deep-128.btoon")

# Bigints above 40,000 bytes are written in hex.
string(REPEAT "7f" 40001 hexdigits)
file(WRITE "${W}/hexbig.json" "{\"$bigint\": \"-0x${hexdigits}\"}")
check("encode hex bigint" 0 COMMAND "${BTOON}" encode hexbig.json hexbig.btoon)
check("decode hex bigint" 0 COMMAND "${BTOON}" decode hexbig.btoon hexbig-out.json)
contains("large bigints are hex" "${W}/hexbig-out.json" "\"-0x7f7f")
check("re-encode hex bigint" 0 COMMAND "${BTOON}" encode hexbig-out.json hexbig2.btoon)
same("hex bigint round trip" "${W}/hexbig2.btoon" "${W}/hexbig.btoon")

# --- deep JSON is a clean error in every tool -------------------------------------------

string(REPEAT "[" 100000 open)
string(REPEAT "]" 100000 close)
file(WRITE "${W}/deep.json" "${open}${close}")
check("btoon encode deep JSON" 1 STDERR "max_depth" COMMAND "${BTOON}" encode deep.json deep.btoon)
check("btoon-convert deep JSON" 1 STDERR "max_depth" COMMAND "${CONVERT}" deep.json deep.btoon)
check("btoon-convert deep YAML" 1 STDERR "max_depth" COMMAND "${CONVERT}" -i yaml deep.json deep.btoon)
check("btoon-schema infer deep JSON" 1 STDERR "max_depth" COMMAND "${SCHEMA}" infer deep.json)
string(REPEAT "{\"a\":" 50000 open)
string(REPEAT "}" 50000 close)
file(WRITE "${W}/deep-object.json" "${open}1${close}")
check("btoon-convert deep schema" 1 STDERR "max_depth"
      COMMAND "${CONVERT}" -s deep-object.json "${F}/all-types.json" x.btoon)
string(REPEAT "[" 128 open)
string(REPEAT "]" 128 close)
file(WRITE "${W}/d128.json" "${open}${close}")
check("128 levels are fine" 0 COMMAND "${BTOON}" encode d128.json d128.btoon)
file(WRITE "${W}/d129.json" "[${open}${close}]")
check("129 levels are too deep" 1 STDERR "max_depth" COMMAND "${BTOON}" encode d129.json d129.btoon)

# --- HMAC keys -------------------------------------------------------------------------------

file(WRITE "${W}/empty.key" "")
file(WRITE "${W}/real.key" "0123456789abcdef0123456789abcdef")
check("encode with an empty key file" 2 STDERR "empty"
      COMMAND "${BTOON}" encode "${F}/all-types.json" unsigned.btoon --hmac-key-file empty.key)
if(EXISTS "${W}/unsigned.btoon")
    message(SEND_ERROR "encode with an empty key file still wrote its output")
endif()
check("decode with an empty key file" 2 STDERR "empty"
      COMMAND "${BTOON}" decode "${F}/all-types.btoon" - --hmac-key-file empty.key)
check("info with an empty key file" 2 STDERR "empty"
      COMMAND "${BTOON}" info "${F}/all-types.btoon" --hmac-key-file empty.key)
check("sign" 0 COMMAND "${BTOON}" encode "${F}/all-types.json" signed.btoon --hmac-key-file real.key)
check("verify" 0 COMMAND "${BTOON}" decode signed.btoon signed.json --hmac-key-file real.key)
same("signed round trip" "${W}/signed.json" "${F}/all-types.json")
check("unsigned input with a key" 1 STDERR "authentication_failed"
      COMMAND "${BTOON}" decode "${F}/all-types.btoon" - --hmac-key-file real.key)

# --- failed writes exit nonzero ------------------------------------------------------------

file(WRITE "${W}/rows.json" "[{\"a\": 1}, {\"a\": 2}]")
check("btoon-schema infer" 0 COMMAND "${SCHEMA}" infer rows.json)

if(EXISTS "/dev/full")
    check("btoon decode to /dev/full" 1 COMMAND "${BTOON}" decode "${F}/all-types.btoon" /dev/full)
    check("btoon decode stdout on /dev/full" 1 STDOUT /dev/full COMMAND "${BTOON}" decode "${F}/all-types.btoon" -)
    check("btoon encode to /dev/full" 1 COMMAND "${BTOON}" encode "${F}/all-types.json" /dev/full)
    check("btoon info on /dev/full" 1 STDOUT /dev/full COMMAND "${BTOON}" info "${F}/all-types.btoon")
    check("btoon-convert to /dev/full" 1 COMMAND "${CONVERT}" -o json "${F}/all-types.btoon" /dev/full)
    check("btoon-convert stdout on /dev/full" 1 STDOUT /dev/full
          COMMAND "${CONVERT}" -o json "${F}/all-types.btoon" -)
    check("btoon-schema infer on /dev/full" 1 STDOUT /dev/full COMMAND "${SCHEMA}" infer rows.json)
    check("btoon-schema infer -o /dev/full" 1 COMMAND "${SCHEMA}" infer rows.json -o /dev/full)
else()
    message(STATUS "no /dev/full here; skipping the /dev/full checks")
endif()
if(UNIX)
    # Standard output closed (>&-).
    foreach(tool IN ITEMS BTOON CONVERT SCHEMA)
        if(tool STREQUAL "BTOON")
            set(args decode "${F}/all-types.btoon" -)
        elseif(tool STREQUAL "CONVERT")
            set(args -o json "${F}/all-types.btoon" -)
        else()
            set(args infer rows.json)
        endif()
        check("${tool} with stdout closed" 1 COMMAND sh -c "exec \"$@\" >&-" sh "${${tool}}" ${args})
    endforeach()
endif()

# --- btoon-convert: CSV, XML, arguments, batch mode -----------------------------------------

# A committed fixture, byte for byte: it mixes LF and CRLF line ends and has a line break
# inside a quoted field, which file(WRITE) can't reproduce on Windows.
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${F}/people.csv" "${W}/people.csv")
check("CSV in" 0 COMMAND "${CONVERT}" people.csv people.btoon)
check("CSV decode" 0 COMMAND "${BTOON}" decode people.btoon people.json --compact)
expect_text("CSV reading follows RFC 4180" "${W}/people.json"
    "[{\"code\":\"0x10\",\"count\":{\"$bigint\":\"99999999999999999999\"},\"empty\":null,\"flag\":true,\"name\":\"Nan\",\"note\":\"a,b\",\"quoted\":\"42\"},{\"code\":1000.0,\"count\":-7,\"empty\":null,\"flag\":false,\"name\":\"inf\",\"note\":\"say \\\"hi\\\"\",\"quoted\":\"\"},{\"code\":\"1e\",\"count\":0.0015,\"empty\":null,\"flag\":\"TRUE\",\"name\":\"multi\\nline\",\"note\":\"01\",\"quoted\":\"x\"}]\n")
check("CSV out" 0 COMMAND "${CONVERT}" -o csv people.btoon people-out.csv)
check("CSV back in" 0 COMMAND "${CONVERT}" people-out.csv people2.btoon)
same("CSV round trip" "${W}/people2.btoon" "${W}/people.btoon")
expect_text("CSV writing follows RFC 4180" "${W}/people-out.csv"
    "code,count,empty,flag,name,note,quoted\r\n\"0x10\",99999999999999999999,,true,\"Nan\",\"a,b\",\"42\"\r\n1000.0,-7,,false,\"inf\",\"say \"\"hi\"\"\",\"\"\r\n\"1e\",0.0015,,\"TRUE\",\"multi\nline\",\"01\",\"x\"\r\n")
check("CSV with a tab delimiter" 0 COMMAND "${CONVERT}" -o csv --csv-delimiter tab people.btoon people.tsv)
check("TSV back in" 0 COMMAND "${CONVERT}" -i csv --csv-delimiter tab people.tsv people3.btoon)
same("TSV round trip" "${W}/people3.btoon" "${W}/people.btoon")
file(WRITE "${W}/unterminated.csv" "a,b\n\"x,1\n")
check("unterminated quote" 1 STDERR "unterminated" COMMAND "${CONVERT}" unterminated.csv x.btoon)
file(WRITE "${W}/ragged.csv" "a,b\n1,2,3\n")
check("ragged CSV" 1 STDERR "3 fields" COMMAND "${CONVERT}" ragged.csv x.btoon)
check("CSV needs an array of maps" 1 STDERR "array of maps" COMMAND "${CONVERT}" -o csv big.btoon x.csv)

file(WRITE "${W}/x.json" "{\"k\\\"><x a=\\\"\":\"</string><injected/>&\", \"t\":\"tab\\there\\r\"}")
check("XML out" 0 COMMAND "${CONVERT}" -o xml x.json x.xml)
expect_text("XML output is escaped" "${W}/x.xml"
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n  <map>\n    <entry key=\"k&quot;&gt;&lt;x a=&quot;\">\n      <string>&lt;/string&gt;&lt;injected/&gt;&amp;</string>\n    </entry>\n    <entry key=\"t\">\n      <string>tab\there&#13;</string>\n    </entry>\n  </map>\n</root>\n")
file(WRITE "${W}/ctl.json" "[\"bell\\u0007\"]")
check("XML can't hold U+0007" 1 STDERR "U\\+0007" COMMAND "${CONVERT}" -o xml ctl.json ctl.xml)
check("XML can't hold U+0000" 1 STDERR "U\\+0000" COMMAND "${CONVERT}" -o xml "${F}/all-types.btoon" all.xml)
file(WRITE "${W}/types.json" "{\"a\":[true,-1,18446744073709551615,0.5,null],\"b\":{\"$binary\":\"AAE=\"},\"big\":{\"$bigint\":\"18446744073709551616\"},\"d\":{\"$date\":1},\"dt\":{\"$datetime\":\"5\"},\"e\":{\"$ext\":[5,\"AQ==\"]},\"f\":{\"$float\":\"-Infinity\"},\"t\":{\"$timestamp\":\"2024-01-01T00:00:00.5+02:00\"},\"v\":{\"$f32\":[1.5,{\"$float\":\"NaN\"}]},\"vr\":{\"$f64\":\"AQAAAAAA+H8=\"}}")
check("XML of every type" 0 COMMAND "${CONVERT}" -o xml types.json types.xml)
expect_text("XML of every type" "${W}/types.xml" "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<root>
  <map>
    <entry key=\"a\">
      <array>
        <bool>true</bool>
        <int>-1</int>
        <uint>18446744073709551615</uint>
        <float>0.5</float>
        <null/>
      </array>
    </entry>
    <entry key=\"b\">
      <binary>AAE=</binary>
    </entry>
    <entry key=\"big\">
      <bigint>18446744073709551616</bigint>
    </entry>
    <entry key=\"d\">
      <date>1</date>
    </entry>
    <entry key=\"dt\">
      <datetime>5</datetime>
    </entry>
    <entry key=\"e\">
      <ext type=\"5\">AQ==</ext>
    </entry>
    <entry key=\"f\">
      <float>-Infinity</float>
    </entry>
    <entry key=\"t\">
      <timestamp>2024-01-01T00:00:00.5+02:00</timestamp>
    </entry>
    <entry key=\"v\">
      <f32>
        <float>1.5</float>
        <float>NaN</float>
      </f32>
    </entry>
    <entry key=\"vr\">
      <f64 encoding=\"base64\">AQAAAAAA+H8=</f64>
    </entry>
  </map>
</root>
")

check("bad --batch-size" 2 STDERR "positive integer" COMMAND "${CONVERT}" --batch-size abc big.json x.btoon)
check("bad --batch-size 0" 2 COMMAND "${CONVERT}" --batch-size 0 big.json x.btoon)
check("missing option value" 2 COMMAND "${CONVERT}" big.json x.btoon --csv-delimiter)
check("bad delimiter" 2 COMMAND "${CONVERT}" --csv-delimiter ab big.json x.btoon)
check("unknown option" 2 COMMAND "${CONVERT}" --bogus big.json x.btoon)
check("unknown format" 2 COMMAND "${CONVERT}" -o nope big.json x.btoon)
check("too many arguments" 2 COMMAND "${CONVERT}" big.json x.btoon y.btoon)
check("no arguments" 2 COMMAND "${CONVERT}")
check("btoon unknown option" 2 COMMAND "${BTOON}" encode --bogus big.json x.btoon)
check("btoon unknown command" 2 COMMAND "${BTOON}" frobnicate)
check("btoon too many arguments" 2 COMMAND "${BTOON}" encode big.json x.btoon y.btoon)
check("btoon missing key file name" 2 COMMAND "${BTOON}" encode big.json x.btoon --hmac-key-file)
file(WRITE "${W}/ts.json" "{\"$timestamp\": \"2024-02-30T00:00:00Z\"}")
check("btoon strict $timestamp" 1 STDERR "no such day"
      STDIN "${W}/ts.json" COMMAND "${BTOON}" encode - x.btoon)

# --batch: the last argument is the output directory (the review's example).
file(MAKE_DIRECTORY "${W}/batch/in" "${W}/batch/out")
file(WRITE "${W}/batch/in/one.json" "{\"a\":1}")
file(WRITE "${W}/batch/in/two.json" "{\"b\":2}")
file(WRITE "${W}/batch/out/stale.json" "{\"old\":true}")
check("batch" 0 DIR "${W}/batch" COMMAND "${CONVERT}" --batch -o btoon in/one.json in/two.json out/)
foreach(f IN ITEMS out/one.btoon out/two.btoon)
    if(NOT EXISTS "${W}/batch/${f}")
        message(SEND_ERROR "batch: ${f} is missing")
    endif()
endforeach()
foreach(f IN ITEMS one.btoon two.btoon stale.btoon out/stale.btoon)
    if(EXISTS "${W}/batch/${f}")
        message(SEND_ERROR "batch: ${f} shouldn't exist")
    endif()
endforeach()
check("batch from a directory" 0 DIR "${W}/batch" COMMAND "${CONVERT}" --batch -o json in out2)
foreach(f IN ITEMS out2/one.json out2/two.json)
    if(NOT EXISTS "${W}/batch/${f}")
        message(SEND_ERROR "batch from a directory: ${f} is missing")
    endif()
endforeach()
file(MAKE_DIRECTORY "${W}/batch/in2")
file(WRITE "${W}/batch/in2/one.json" "{\"c\":3}")
check("batch output collision" 2 STDERR "both" DIR "${W}/batch"
      COMMAND "${CONVERT}" --batch in/one.json in2/one.json out3)
file(WRITE "${W}/batch/in/bad.json" "{")
check("batch with a bad file" 1 DIR "${W}/batch" COMMAND "${CONVERT}" --batch in out4)
if(NOT EXISTS "${W}/batch/out4/one.btoon")
    message(SEND_ERROR "batch with a bad file: the good files weren't converted")
endif()
