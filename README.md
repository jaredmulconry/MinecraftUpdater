# MinecraftUpdater
C++ project intended to track down the latest version of Minecraft server, checks against a local server and downloads and replaces the local server if out-of-date.

This project depends on being linked to the following projects:
libcurl for accessing the https-hosted json databases,
cryptopp for comparing SHA1 hashes of local and remote server files

This project also depends on the nlohmann JSON library for parsing json databases.

Building this project depends on C++17 for the standard filesystem library. This requirement could be weakened to C++11 with some work.

It would be possible to create an interactive application that exposes the most recent versions and allow a user to choose which version to update to.