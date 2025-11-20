



rm -rf bin
mkdir -p bin

release="2025.22.1"

platforms=(
  "linux-x86_64"
  "macos-aarch64"
  "windows-x86_64"
)

for platform in "${platforms[@]}"; do


  TAR="slang-${release}-${platform}.tar.gz"
  curl -L -O "https://github.com/shader-slang/slang/releases/download/v${release}/${TAR}"
  mkdir -p "bin/${platform}/"
  tar -xzf "${TAR}" -C "bin/${platform}/"

  rm -rf "bin/${platform}/share" # we don't need these.
  rm "${TAR}"
done