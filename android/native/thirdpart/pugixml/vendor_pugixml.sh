#!/usr/bin/env bash
# Fetches official pugixml 1.14 sources (MIT). Run from repo root or this directory.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
TAG=v1.14
BASE="https://raw.githubusercontent.com/zeux/pugixml/${TAG}/src"
curl -fsSL "${BASE}/pugixml.hpp" -o "${DIR}/pugixml.hpp"
curl -fsSL "${BASE}/pugixml.cpp" -o "${DIR}/pugixml.cpp"
wc -c "${DIR}/pugixml.hpp" "${DIR}/pugixml.cpp"
