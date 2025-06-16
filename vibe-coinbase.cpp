#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/NameValueCollection.h>
#include <Poco/Net/Context.h>
#include <Poco/Net/SSLManager.h>
#include <Poco/Net/PrivateKeyPassphraseHandler.h>
#include <Poco/HMACEngine.h>
#include <Poco/SHA2Engine.h>
#include <Poco/Base64Encoder.h>
#include <Poco/StreamCopier.h>
#include <Poco/Buffer.h>
#include <Poco/Util/ServerApplication.h>
#include <Poco/URI.h>

#include <ctime>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace Poco::Net;
using namespace Poco;
using namespace std;

// Replace with your actual credentials
const std::string API_KEY = "your_api_key";
const std::string API_SECRET = "your_api_secret";
const std::string API_PASSPHRASE = "your_api_passphrase";

// HMAC SHA256 signer
std::string createSignaturePoco(
        const std::string& secret,
        const std::string& timestamp,
        const std::string& method,
        const std::string& requestPath,
        const std::string& body) {
    const std::string prehash = timestamp + method + requestPath + body;

    HMACEngine<SHA2Engine256> hmac(secret);
    hmac.update(prehash);
    const std::vector<unsigned char>& digest = hmac.digest();
    std::ostringstream oss;
    Base64Encoder encoder(oss);
    encoder.write(reinterpret_cast<const char*>(digest.data()), digest.size());
    encoder.close();

    return oss.str();
}

std::string base64Encode(const unsigned char* buffer, size_t length) {
    BIO* bio, * b64;
    BUF_MEM* bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());

    // Do not use newlines to flush buffer
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);

    BIO_write(bio, buffer, length);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);

    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    return result;
}

std::string createSignatureSsl(
        const std::string& secret,      // base64-decoded secret key
        const std::string& timestamp,
        const std::string& method,
        const std::string& requestPath,
        const std::string& body) {
    const std::string prehash = timestamp + method + requestPath + body;

    unsigned char* digest;
    digest = HMAC(
        EVP_sha256(),
        secret.data(), secret.length(),
        reinterpret_cast<const unsigned char*>(prehash.data()), prehash.length(),
        nullptr, nullptr);

    return base64Encode(digest, 32);  // SHA256 outputs 32 bytes
}

std::string createSignature(
        const std::string& secret,      // base64-decoded secret key
        const std::string& timestamp,
        const std::string& method,
        const std::string& requestPath,
        const std::string& body) {
    auto pocoSignature = createSignaturePoco(secret, timestamp, method, requestPath, body);
    auto sslSignature = createSignatureSsl(secret, timestamp, method, requestPath, body);

    if (pocoSignature != sslSignature) {
        const std::string msg = "Signatures do not match, poco: " + pocoSignature + " ssl: " + sslSignature;
        throw std::runtime_error(msg);
    }
    return sslSignature;
}

int main() {
    Poco::Util::ServerApplication app;
    Poco::Net::initializeSSL();

    try {
        URI uri("wss://advanced-trade-ws.coinbase.com");
        HTTPSClientSession session(uri.getHost(), uri.getPort());

        std::string path = uri.getPathAndQuery();
        if (path.empty()) path = "/";

        std::string timestamp = std::to_string(std::time(nullptr));
        std::string method = "GET";
        std::string requestPath = "/"; // no REST endpoint here, but still required
        std::string body = "";         // empty string for GET

        std::string signature = createSignature(API_SECRET, timestamp, method, requestPath, body);

        HTTPRequest request(HTTPRequest::HTTP_GET, path, HTTPMessage::HTTP_1_1);
        request.set("CB-ACCESS-KEY", API_KEY);
        request.set("CB-ACCESS-SIGN", signature);
        request.set("CB-ACCESS-TIMESTAMP", timestamp);
        request.set("CB-ACCESS-PASSPHRASE", API_PASSPHRASE);
        request.set("User-Agent", "CoinbasePrimeClient/1.0");

        HTTPResponse response;
        WebSocket ws(session, request, response);

        // Send a subscription message for BTC ticker
        std::string subscribeMsg = R"({
            "type": "subscribe",
            "channel": "market_trades",
            "product_ids": ["BTC-USD"]
        })";

        ws.sendFrame(subscribeMsg.c_str(), subscribeMsg.length(), WebSocket::FRAME_TEXT);

        constexpr size_t bufferSize = 32*1024;
        auto buffer = std::make_unique<char[]>(bufferSize);
        int flags;
        int n;

        while (true) {
            n = ws.receiveFrame(buffer.get(), bufferSize, flags);
            std::string msg(buffer.get(), n);
            std::cout << "Received: " << msg << std::endl;
        }
    } catch (Exception& e) {
        std::cerr << "Exception: " << e.displayText() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "std::exception: " << e.what() << std::endl;
    }

    Poco::Net::uninitializeSSL();
}
