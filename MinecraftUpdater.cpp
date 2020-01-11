#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "nlohmann-json/json.hpp"
#include "curl/curl.h"
#include "cryptopp/sha.h"
#include "cryptopp/filters.h"
#include "cryptopp/hex.h"


struct CurlInstance
{
	CURL* session;
	CurlInstance()
	{
		if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
			throw;
		session = curl_easy_init();
		if (session == nullptr)
			throw;
	}
	~CurlInstance()
	{
		curl_easy_cleanup(session);
		curl_global_cleanup();
	}
};

static size_t write_to_string(void* ptr, size_t, size_t nmemb, void* stream)
{
	std::string& out = *reinterpret_cast<std::string*>(stream);
	out.append(reinterpret_cast<const char*>(ptr), nmemb);

	return nmemb;
}

int main(int argc, char** argv)
{
	const constexpr auto versionManifestPath = "https://launchermeta.mojang.com/mc/game/version_manifest.json";
	const constexpr auto localServerPath = "./server.jar";

	std::string serverFilePath;

	if (argc > 1)
	{
		serverFilePath = argv[2];
	}
	else
	{
		serverFilePath = localServerPath;
	}

	char errorBuff[CURL_ERROR_SIZE];
	memset(errorBuff, 0, sizeof(errorBuff));

	CurlInstance instance;

	std::string jsonData;
	jsonData.reserve(1024);

	///Establish curl instance for accessing the Minecraft Version Manifest file from the web

	curl_easy_setopt(instance.session, CURLOPT_URL, versionManifestPath);
	//curl_easy_setopt(instance.session, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(instance.session, CURLOPT_ERRORBUFFER, errorBuff);
	curl_easy_setopt(instance.session, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(instance.session, CURLOPT_WRITEFUNCTION, write_to_string);
	curl_easy_setopt(instance.session, CURLOPT_WRITEDATA, &jsonData);

	auto perfCode = curl_easy_perform(instance.session);

	if (perfCode != CURLE_OK)
	{
		std::cerr << errorBuff << std::endl;
		return -1;
	}

	auto manifest = nlohmann::json::parse(jsonData, nullptr, false);

	if (manifest == nlohmann::json::value_t::discarded)
	{
		std::cerr << "Parse of version manifest failed." << std::endl;
		return -1;
	}

	std::string latestBuildURL;
	manifest["versions"][0]["url"].get_to(latestBuildURL);

	jsonData.clear();

	///Establish url for accessing the information for the latest version of Minecraft from the web

	curl_easy_setopt(instance.session, CURLOPT_URL, latestBuildURL.c_str());

	perfCode = curl_easy_perform(instance.session);
	if (perfCode != CURLE_OK)
	{
		std::cerr << errorBuff << std::endl;
		return -1;
	}

	auto latestBuildInfo = nlohmann::json::parse(jsonData, nullptr, false);
	if (latestBuildInfo == nlohmann::json::value_t::discarded)
	{
		std::cerr << "Parse of latest build info failed" << std::endl;
		return -1;
	}

	///Extracting the details of the latest build of the server, including the sha1 hash and download URL

	std::string latestServerHash;
	std::string latestServerURL;
	auto& serverDetails = latestBuildInfo["downloads"]["server"];
	serverDetails["sha1"].get_to(latestServerHash);
	serverDetails["url"].get_to(latestServerURL);


	std::cout << "Latest Server SHA1: " << latestServerHash << std::endl;

	///Accessing the current server file and comparing it against the latest version

	auto serverSize = std::filesystem::file_size(serverFilePath);
	std::string serverRaw(unsigned int(serverSize), char(0));
	{
		FILE* curServer = nullptr;
		fopen_s(&curServer, serverFilePath.c_str(), "rb");
		if (curServer == nullptr)
		{
			std::cerr << "Server file could not be opened for reading." << std::endl;
			return -1;
		}
		fread_s(serverRaw.data(), serverRaw.size(), 1, size_t(serverSize), curServer);
		fclose(curServer);
	}

	///Computing a SHA1 hash of the current server file to compare against the latest build from the web

	CryptoPP::SHA1 sha1;
	sha1.Update(reinterpret_cast<const CryptoPP::byte*>(serverRaw.data()), serverRaw.size());
	std::string digest(sha1.DigestSize(), 0);
	sha1.Final(reinterpret_cast<CryptoPP::byte*>(digest.data()));
	std::string encodedDigest;
	CryptoPP::StringSource ss(reinterpret_cast<const CryptoPP::byte*>(digest.data()), digest.size(), true, new CryptoPP::HexEncoder(new CryptoPP::StringSink(encodedDigest), false));

	std::cout << "Current Server SHA1: " << encodedDigest << std::endl;

	if (encodedDigest == latestServerHash)
	{
		std::cout << "Server is up-to-date!" << std::endl;
		return 0;
	}
	
	std::cout << "Server requires update." << std::endl;

	///The current server file is out of date. Ask if it should be updated.

	std::string serverData;
	serverData.reserve(32767);
	curl_easy_setopt(instance.session, CURLOPT_URL, latestServerURL.c_str());
	curl_easy_setopt(instance.session, CURLOPT_WRITEDATA, &serverData);
	curl_easy_setopt(instance.session, CURLOPT_NOPROGRESS, 0L);

	perfCode = curl_easy_perform(instance.session);
	if (perfCode != CURLE_OK)
	{
		std::cerr << errorBuff << std::endl;
		return -1;
	}

	{
		FILE* serverFile = nullptr;
		fopen_s(&serverFile, serverFilePath.c_str(), "wb+");
		if (serverFile == nullptr)
		{
			std::cerr << "Server file could not be opened for updating." << std::endl;
			return -1;
		}

		auto bytesWritten = fwrite(serverData.c_str(), 1, serverData.size(), serverFile);

		if (bytesWritten < serverData.size())
		{
			std::cerr << "Not enough of the server file was written to." << std::endl;
			fclose(serverFile);
			return -1;
		}

		fclose(serverFile);

		std::cout << "Server successfully updated to latest version!" << std::endl;
	}

}

