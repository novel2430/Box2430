#!/bin/sh
set -eu

result=${BOX2430_CLOEXEC_RESULT:?}
for descriptor in /proc/$$/fd/*; do
    target=$(readlink "$descriptor" 2>/dev/null || true)
    case $target in
        socket:*) echo inherited >"$result"; exit 1 ;;
    esac
done
echo clean >"$result"
