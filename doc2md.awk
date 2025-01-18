#!/usr/bin/awk -f
# Usage: doc2md,awk FILE.c FILE.h
/^\/\*\*$/{
    inside_doc = 1
}

inside_doc {
    line = substr($0, 4)

    if (match(line, /^Arguments?:/)) {
        line = "**" substr(line, RSTART, RLENGTH) "**"
    } else if (match(line, /^Returns?:/)) {
        line = ( \
            "**" substr(line, RSTART, RLENGTH) "** " \
            substr(line, RSTART + RLENGTH + 1) \
        )
    } else if (match(line, /- [a-zA-Z_][a-zA-Z0-9_]*:/)) {
        line = ( \
            "- **" substr(line, RSTART + 2, RLENGTH - 2) "** " \
            substr(line, RSTART + RLENGTH + 1) \
        )
    }

    if (buffer) {
        buffer = buffer "\n" line
    } else {
        buffer = line
    }
}

inside_doc && /^ \*\/$/ {
    inside_doc = 0
}

NF == 0 || $1 == "{" {
    buffer = ""
}

FILENAME ~ /\.c$/ && /^[a-z].*bigint_[a-z0-9_]+\(/ {
    if (!match($0, /bigint_[a-z0-9_]+\(/)) {
        print "regex error" > "/dev/fd/2"
        close("/dev/fd/2")
        exit 1
    }

    name = substr($0, RSTART, RLENGTH - 1)
    entries[name] = sprintf( \
        "### %s ###\n\n**Signature:** `%s`\n\n**Description:**\n%s", \
        name, $0, buffer \
    )

    buffer = ""
}

FILENAME ~ /\.h/ {
    if (/^\/\//) {
        print "##", substr($0, 4), "##\n"
    } else if (match($0, /bigint_[a-z0-9_]+\(/)) {
        name = substr($0, RSTART, RLENGTH - 1)

        if (name in entries) {
            print entries[name]
        } else {
            print "missing documentation:", name > "/dev/fd/2"
            close("/dev/fd/2")
            exit 1
        }
    }
}
