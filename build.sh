set -e

cd "$(dirname "$0")"

git pull

cmake -S . -B build

cmake --build build

./build/test