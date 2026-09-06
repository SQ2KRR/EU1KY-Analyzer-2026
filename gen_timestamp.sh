#!/usr/bin/env bash
set -euo pipefail
OUT="Src/Inc/build_timestamp.h"
UTC_US="$(date -u '+%Y-%m-%d %H:%M UT')"
UTC_D="$(date -u '+%d-%m-%Y %H:%M UT')"
cat > "$OUT" <<EOF
#ifndef BUILD_TIMESTAMP_US
#define HGREV "N/A"
#define BUILD_TIMESTAMP_US "$UTC_US"
#define BUILD_TIMESTAMP_D "$UTC_D"
#define HGREVSTR(s) stringify_(s)
#define stringify_(s) #s
#endif
EOF
echo "Repository Git not found; using N/A revision"
echo "$OUT file created at $UTC_US"
