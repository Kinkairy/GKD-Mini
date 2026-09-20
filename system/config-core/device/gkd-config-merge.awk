BEGIN { FS = "|"; schema_count = 0 }

function fail(message) {
    print "GKD_CONFIG_REJECTED reason=" message > "/dev/stderr"
    failed = 1
}

FILENAME == schema_file {
    if ($0 ~ /^#/ || $0 == "") next
    if (NF != 8 || $1 == "" || known[$1]) {
        fail("bad_schema line=" FNR)
        next
    }
    known[$1] = 1
    order[++schema_count] = $1
    defaults[$1] = $3
    next
}

FILENAME == override_file {
    line = $0
    sub(/\r$/, "", line)
    if (line == "" || line ~ /^#/) next
    equals = index(line, "=")
    if (!equals) {
        fail("syntax file=override line=" FNR)
        next
    }
    key = substr(line, 1, equals - 1)
    value = substr(line, equals + 1)
    if (!(key in known)) {
        fail("unknown_key key=" key)
        next
    }
    if (seen[key]++) {
        fail("duplicate_key key=" key " file=override")
        next
    }
    override[key] = value
}

END {
    if (failed) exit 2
    for (i = 1; i <= schema_count; ++i) {
        key = order[i]
        value = (key in override) ? override[key] : defaults[key]
        print key "=" value
    }
}
