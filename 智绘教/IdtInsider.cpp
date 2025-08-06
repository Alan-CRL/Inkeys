#pragma once

#include "IdtInsider.h"

#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rand.h>

#pragma comment(lib, "libssl_static.lib")
#pragma comment(lib, "libcrypto_static.lib")

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "cpphttplib/httplib.h"

#include <windows.h>
#include <filesystem>

std::string aes_decrypt(const std::string& ciphertext)
{
	if (ciphertext.size() <= AES_BLOCK_SIZE)
	{
		return ""; // 密钥长度不正确或密文长度过短
	}

	unsigned char iv[AES_BLOCK_SIZE];
	memcpy(iv, ciphertext.data(), AES_BLOCK_SIZE);

	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (!ctx) {
		return ""; // 创建解密上下文失败
	}

	int len = 0;
	int plaintext_len = 0;
	std::string plaintext;
	plaintext.resize(ciphertext.size());

	if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL,
		reinterpret_cast<const unsigned char*>("m9r]bf@y0HPW;R/)e-C'90/O9CEVxgsM"), iv)) {
		EVP_CIPHER_CTX_free(ctx);
		return ""; // 初始化解密失败
	}

	if (1 != EVP_DecryptUpdate(ctx,
		reinterpret_cast<unsigned char*>(&plaintext[0]), &len,
		reinterpret_cast<const unsigned char*>(ciphertext.data() + AES_BLOCK_SIZE),
		ciphertext.size() - AES_BLOCK_SIZE)) {
		EVP_CIPHER_CTX_free(ctx);
		return ""; // 解密更新失败
	}
	plaintext_len = len;

	if (1 != EVP_DecryptFinal_ex(ctx,
		reinterpret_cast<unsigned char*>(&plaintext[0]) + len, &len)) {
		EVP_CIPHER_CTX_free(ctx);
		return ""; // 解密结束失败
	}
	plaintext_len += len;

	EVP_CIPHER_CTX_free(ctx);

	plaintext.resize(plaintext_len);
	return plaintext;
}

extern struct SetListStruct;
extern SetListStruct setlist;

bool isInsider = false;
std::string GetInsiderList()
{
	httplib::Result res;
	httplib::Headers headers = { {"Cache-Control", "no-cache"}, {"Pragma", "no-cache"} };

	// 尝试主地址
	{
		httplib::SSLClient scli("vip.123pan.cn");
		scli.set_follow_location(true);
		scli.set_connection_timeout(5);
		scli.set_read_timeout(10);

		// 尝试 Https 连接
		res = scli.Get("/1709404/version_identification/insider_list_out.idtc", headers);
		if (!res || res->status != 200)
		{
			// 失败后尝试使用 Http 连接
			httplib::Client cli("vip.123pan.cn");
			scli.set_follow_location(true);
			scli.set_connection_timeout(5);
			scli.set_read_timeout(10);

			res = cli.Get("/1709404/version_identification/insider_list_out.idtc", headers);
		}

		if (res && res->status == 200)
		{
			std::string ret = res->body;
			if (ret.compare(0, 3, "\xEF\xBB\xBF") == 0) ret = ret.substr(3);
			return ret;
		}
	}
	// 尝试备用地址
	{
		httplib::SSLClient scli("home.alan-crl.top");
		scli.set_follow_location(true);
		scli.set_connection_timeout(5);
		scli.set_read_timeout(10);

		// 尝试 Https 连接
		res = scli.Get("/version_identification/insider_list_out.idtc", headers);
		if (!res || res->status != 200)
		{
			// 失败后尝试使用 Http 连接
			httplib::Client cli("home.alan-crl.top");
			scli.set_follow_location(true);
			scli.set_connection_timeout(5);
			scli.set_read_timeout(10);

			res = cli.Get("/version_identification/insider_list_out.idtc", headers);
		}

		if (res && res->status == 200)
		{
			std::string ret = res->body;
			if (ret.compare(0, 3, "\xEF\xBB\xBF") == 0) ret = ret.substr(3);
			return ret;
		}
	}

	return "Error";
}
bool InsiderInitialization(std::string userId, std::string& updateChannelExtra)
{
	std::string insiderList = aes_decrypt(GetInsiderList());
	if (!insiderList.empty() && insiderList != "Error")
	{
		if (insiderList.find(userId) != insiderList.npos)
		{
			isInsider = true;
			updateChannelExtra = insiderList.substr(0, insiderList.find('\n'));
		}
		return true;
	}
	return false;
}