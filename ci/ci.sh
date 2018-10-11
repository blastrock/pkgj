#!/bin/bash

set -xe

rm -fr ci/build ci/buildhost

cd ci

./setup_conan.sh

export VITASDK=$(pipenv run conan info vitasdk-toolchain/07@blastrock/pkgj --paths -pr vita | grep -Po '(?<=package_folder: ).*$')
export CC=gcc-7
export CXX=g++-7

mkdir buildhost
cd buildhost
pipenv run conan install ../.. --build missing -s build_type=RelWithDebInfo -s compiler=gcc -s compiler.version=7 -s compiler.libcxx=libstdc++11
cmake -GNinja ../.. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHOST_BUILD=ON -DPKGI_ENABLE_LOGGING=ON
ninja
cd ..

mkdir build
cd build
pipenv run conan install ../.. --build missing -pr vita -s build_type=RelWithDebInfo
cmake -GNinja ../.. -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja
cp pkgj pkgj.elf
cd ..

cd ..

if [[ -n "$TRAVIS_TAG" ]]; then
    SSH_FILE="$(mktemp -u $HOME/.ssh/XXXXX)"

    # Decrypt SSH key
    openssl aes-256-cbc \
        -K $encrypted_593a42f38d5b_key\
        -iv $encrypted_593a42f38d5b_iv\
        -in ".travis_deploy_key.enc" \
        -out "$SSH_FILE" -d

    # Enable SSH authentication
    chmod 600 "$SSH_FILE"
    printf "%s\n" \
        "Host github.com" \
        "  IdentityFile $SSH_FILE" \
        "  LogLevel ERROR" >> ~/.ssh/config

    git config --global user.name "Travis"
    git config --global user.email "travis"

    git remote set-url origin git@github.com:blastrock/pkgj.git

    git fetch origin last:refs/remotes/origin/last --depth 1
    git checkout -b last origin/last

    # skip the 'v' in the tag
    ./release.sh ${TRAVIS_TAG:1}
fi
